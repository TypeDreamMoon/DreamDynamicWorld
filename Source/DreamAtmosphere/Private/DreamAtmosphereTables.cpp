// Copyright DreamMoon. All Rights Reserved.

#include "DreamAtmosphereTables.h"
#include "DreamAtmosphereHlslShim.h"
#include "DreamAtmosphereSettings.h"

#include "DataDrivenShaderPlatformInfo.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Engine/TextureRenderTargetVolume.h"
#include "Engine/VolumeTexture.h"
#include "GlobalShader.h"
#include "RenderGraphBuilder.h"
#include "RenderGraphUtils.h"
#include "RenderingThread.h"
#include "RHIStaticStates.h"
#include "ShaderParameterStruct.h"
#include "SystemTextures.h"
#include "TextureResource.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(DreamAtmosphereTables)

namespace DreamAtmosphere::TablesRender
{
	constexpr int32 GroupSize = 8;

	static bool ShouldCompile(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
	}

	/** What one build needs, copied to the render thread. */
	struct FBuild
	{
		FVector4f Atmosphere[5];
		FIntPoint TransmittanceSize;
		FIntPoint MultiScatteringSize;
		FIntVector SkySize;
		float SkyAltitude = 0.0f;
		uint32 SkySteps = 32;
	};
}

BEGIN_SHADER_PARAMETER_STRUCT(FDreamAtmosphereLutParameters, )
	SHADER_PARAMETER_ARRAY(FVector4f, Atmosphere, [5])
	SHADER_PARAMETER(FIntPoint, TransmittanceSize)
	SHADER_PARAMETER(FIntPoint, MultiScatteringSize)
	SHADER_PARAMETER(FIntVector, SkySize)
	SHADER_PARAMETER(float, SkyAltitude)
	SHADER_PARAMETER(uint32, SkySteps)
	SHADER_PARAMETER(uint32, SkySlice)
	SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float4>, TransmittanceLut)
	SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float4>, MultiScatteringLut)
	SHADER_PARAMETER_RDG_TEXTURE(Texture3D<float4>, SkyLut)
	SHADER_PARAMETER_SAMPLER(SamplerState, LutSampler)
	SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float4>, OutTransmittance)
	SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float4>, OutMultiScattering)
	SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture3D<float4>, OutSky)
	SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float4>, OutSlice)
END_SHADER_PARAMETER_STRUCT()

#define DREAM_ATMOSPHERE_LUT_SHADER(Name)                                                                                   \
	class Name : public FGlobalShader                                                                                       \
	{                                                                                                                       \
		DECLARE_GLOBAL_SHADER(Name);                                                                                        \
		SHADER_USE_PARAMETER_STRUCT(Name, FGlobalShader);                                                                   \
		BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )                                                                        \
			SHADER_PARAMETER_STRUCT_INCLUDE(FDreamAtmosphereLutParameters, Lut)                                             \
		END_SHADER_PARAMETER_STRUCT()                                                                                       \
		static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)                          \
		{                                                                                                                   \
			return DreamAtmosphere::TablesRender::ShouldCompile(Parameters);                                                \
		}                                                                                                                   \
		static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment) \
		{                                                                                                                   \
			FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);                                        \
			OutEnvironment.SetDefine(TEXT("THREADGROUP_SIZE"), DreamAtmosphere::TablesRender::GroupSize);                   \
		}                                                                                                                   \
	};

DREAM_ATMOSPHERE_LUT_SHADER(FDreamAtmosphereTransmittanceCS)
DREAM_ATMOSPHERE_LUT_SHADER(FDreamAtmosphereMultiScatteringCS)
DREAM_ATMOSPHERE_LUT_SHADER(FDreamAtmosphereSkyCS)
DREAM_ATMOSPHERE_LUT_SHADER(FDreamAtmosphereSkySliceCS)
DREAM_ATMOSPHERE_LUT_SHADER(FDreamAtmosphereSkyAtlasCS)
#undef DREAM_ATMOSPHERE_LUT_SHADER

IMPLEMENT_GLOBAL_SHADER(FDreamAtmosphereTransmittanceCS, "/Plugin/DreamDynamicWorld/Private/DreamAtmosphereLuts.usf", "TransmittanceCS", SF_Compute);
IMPLEMENT_GLOBAL_SHADER(FDreamAtmosphereMultiScatteringCS, "/Plugin/DreamDynamicWorld/Private/DreamAtmosphereLuts.usf", "MultiScatteringCS", SF_Compute);
IMPLEMENT_GLOBAL_SHADER(FDreamAtmosphereSkyCS, "/Plugin/DreamDynamicWorld/Private/DreamAtmosphereLuts.usf", "SkyCS", SF_Compute);
IMPLEMENT_GLOBAL_SHADER(FDreamAtmosphereSkySliceCS, "/Plugin/DreamDynamicWorld/Private/DreamAtmosphereLuts.usf", "SkySliceCS", SF_Compute);
IMPLEMENT_GLOBAL_SHADER(FDreamAtmosphereSkyAtlasCS, "/Plugin/DreamDynamicWorld/Private/DreamAtmosphereLuts.usf", "SkyAtlasCS", SF_Compute);

namespace DreamAtmosphere::TablesRender
{
	static void Fill(FRDGBuilder& GraphBuilder, const FBuild& B, FDreamAtmosphereLutParameters& P)
	{
		for (int32 i = 0; i < 5; ++i)
		{
			P.Atmosphere[i] = B.Atmosphere[i];
		}
		P.TransmittanceSize = B.TransmittanceSize;
		P.MultiScatteringSize = B.MultiScatteringSize;
		P.SkySize = B.SkySize;
		P.SkyAltitude = B.SkyAltitude;
		P.SkySteps = B.SkySteps;
		P.SkySlice = 0;
		P.LutSampler = TStaticSamplerState<SF_Bilinear, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();
		P.TransmittanceLut = GSystemTextures.GetBlackDummy(GraphBuilder);
		P.MultiScatteringLut = GSystemTextures.GetBlackDummy(GraphBuilder);
		P.SkyLut = GSystemTextures.GetVolumetricBlackDummy(GraphBuilder);
	}

	static void AddBuildPasses(FRDGBuilder& GraphBuilder, const FBuild& B, FRHITexture* TransmittanceRHI, FRHITexture* MultiRHI, FRHITexture* SkyRHI)
	{
		FGlobalShaderMap* ShaderMap = GetGlobalShaderMap(GMaxRHIFeatureLevel);
		FRDGTextureRef Transmittance = RegisterExternalTexture(GraphBuilder, TransmittanceRHI, TEXT("DreamAtmosphere.Transmittance"));
		FRDGTextureRef Multi = RegisterExternalTexture(GraphBuilder, MultiRHI, TEXT("DreamAtmosphere.MultiScattering"));
		FRDGTextureRef Sky = RegisterExternalTexture(GraphBuilder, SkyRHI, TEXT("DreamAtmosphere.Sky"));

		{
			auto* Params = GraphBuilder.AllocParameters<FDreamAtmosphereTransmittanceCS::FParameters>();
			Fill(GraphBuilder, B, Params->Lut);
			Params->Lut.OutTransmittance = GraphBuilder.CreateUAV(Transmittance);
			FComputeShaderUtils::AddPass(GraphBuilder, RDG_EVENT_NAME("Transmittance %dx%d", B.TransmittanceSize.X, B.TransmittanceSize.Y),
				TShaderMapRef<FDreamAtmosphereTransmittanceCS>(ShaderMap), Params, FComputeShaderUtils::GetGroupCount(B.TransmittanceSize, GroupSize));
		}
		{
			auto* Params = GraphBuilder.AllocParameters<FDreamAtmosphereMultiScatteringCS::FParameters>();
			Fill(GraphBuilder, B, Params->Lut);
			Params->Lut.TransmittanceLut = Transmittance;
			Params->Lut.OutMultiScattering = GraphBuilder.CreateUAV(Multi);
			FComputeShaderUtils::AddPass(GraphBuilder, RDG_EVENT_NAME("MultiScattering %dx%d", B.MultiScatteringSize.X, B.MultiScatteringSize.Y),
				TShaderMapRef<FDreamAtmosphereMultiScatteringCS>(ShaderMap), Params, FComputeShaderUtils::GetGroupCount(B.MultiScatteringSize, GroupSize));
		}
		{
			auto* Params = GraphBuilder.AllocParameters<FDreamAtmosphereSkyCS::FParameters>();
			Fill(GraphBuilder, B, Params->Lut);
			Params->Lut.TransmittanceLut = Transmittance;
			Params->Lut.MultiScatteringLut = Multi;
			Params->Lut.OutSky = GraphBuilder.CreateUAV(Sky);
			FComputeShaderUtils::AddPass(GraphBuilder, RDG_EVENT_NAME("Sky %dx%dx%d", B.SkySize.X, B.SkySize.Y, B.SkySize.Z),
				TShaderMapRef<FDreamAtmosphereSkyCS>(ShaderMap), Params, FComputeShaderUtils::GetGroupCount(B.SkySize, FIntVector(4, 4, 4)));
		}
		GraphBuilder.SetTextureAccessFinal(Transmittance, ERHIAccess::SRVMask);
		GraphBuilder.SetTextureAccessFinal(Multi, ERHIAccess::SRVMask);
		GraphBuilder.SetTextureAccessFinal(Sky, ERHIAccess::SRVMask);
	}
}

void UDreamAtmosphereTables::EnsureTargets()
{
	const UDreamAtmosphereSettings* S = UDreamAtmosphereSettings::Get();
	auto Make2D = [this](TObjectPtr<UTextureRenderTarget2D>& Target, FIntPoint Size, const TCHAR* Name)
	{
		Size = FIntPoint(FMath::Max(Size.X, 2), FMath::Max(Size.Y, 2));
		if (!Target)
		{
			Target = NewObject<UTextureRenderTarget2D>(this, Name, RF_Transient);
			Target->bSupportsUAV = true;
			Target->bCanCreateUAV = true;
			Target->ClearColor = FLinearColor::Black;
			Target->Filter = TF_Bilinear;
			Target->AddressX = TA_Clamp;
			Target->AddressY = TA_Clamp;
		}
		if (Target->SizeX != Size.X || Target->SizeY != Size.Y || Target->GetFormat() != PF_A32B32G32R32F || !Target->GetResource())
		{
			Target->InitCustomFormat(Size.X, Size.Y, PF_A32B32G32R32F, true);
			Target->UpdateResourceImmediate(true);
		}
	};
	Make2D(Transmittance, S->TransmittanceSize, TEXT("DreamAtmosphereTransmittance"));
	Make2D(MultiScattering, S->MultiScatteringSize, TEXT("DreamAtmosphereMultiScattering"));
	const FIntVector SkySize(FMath::Max(S->SkySize.X, 2), FMath::Max(S->SkySize.Y, 2), FMath::Max(S->SkySize.Z, 2));
	Make2D(SkySlice, FIntPoint(SkySize.X, SkySize.Y), TEXT("DreamAtmosphereSkySlice"));
	Make2D(SkyAtlas, FIntPoint(SkySize.X, SkySize.Y * SkySize.Z), TEXT("DreamAtmosphereSkyAtlas"));
	if (!Sky)
	{
		Sky = NewObject<UTextureRenderTargetVolume>(this, TEXT("DreamAtmosphereSky"), RF_Transient);
		Sky->bSupportsUAV = true;
		Sky->bCanCreateUAV = true;
		Sky->ClearColor = FLinearColor::Black;
		Sky->Filter = TF_Bilinear;
	}
	if (Sky->SizeX != SkySize.X || Sky->SizeY != SkySize.Y || Sky->SizeZ != SkySize.Z || Sky->OverrideFormat != PF_A32B32G32R32F || !Sky->GetResource())
	{
		Sky->Init(SkySize.X, SkySize.Y, SkySize.Z, PF_A32B32G32R32F);
		Sky->UpdateResourceImmediate(true);
	}
}

void UDreamAtmosphereTables::Build(const FDreamAtmosphereParameters& Parameters)
{
	using namespace DreamAtmosphere::TablesRender;
	if (!FApp::CanEverRender())
	{
		return;
	}
	EnsureTargets();
	const UDreamAtmosphereSettings* S = UDreamAtmosphereSettings::Get();
	FBuild B;
	Parameters.Pack(B.Atmosphere);
	B.TransmittanceSize = FIntPoint(Transmittance->SizeX, Transmittance->SizeY);
	B.MultiScatteringSize = FIntPoint(MultiScattering->SizeX, MultiScattering->SizeY);
	B.SkySize = FIntVector(Sky->SizeX, Sky->SizeY, Sky->SizeZ);
	B.SkySteps = uint32(FMath::Clamp(S->SkySteps, 8, 128));
	FTextureRenderTargetResource* TransmittanceResource = Transmittance->GameThread_GetRenderTargetResource();
	FTextureRenderTargetResource* MultiResource = MultiScattering->GameThread_GetRenderTargetResource();
	FTextureResource* SkyResource = Sky->GetResource();
	if (!TransmittanceResource || !MultiResource || !SkyResource)
	{
		return;
	}
	ENQUEUE_RENDER_COMMAND(DreamAtmosphereTables)([B, TransmittanceResource, MultiResource, SkyResource](FRHICommandListImmediate& RHICmdList)
		{
			FRHITexture* T = TransmittanceResource->GetRenderTargetTexture();
			FRHITexture* M = MultiResource->GetRenderTargetTexture();
			FRHITexture* Sk = SkyResource->TextureRHI.GetReference();
			if (!T || !M || !Sk)
			{
				return;
			}
			FRDGBuilder GraphBuilder(RHICmdList, RDG_EVENT_NAME("DreamAtmosphere.Tables"));
			AddBuildPasses(GraphBuilder, B, T, M, Sk);
			GraphBuilder.Execute();
		});
	Built = Parameters;
	bBuilt = true;
	ReadCpuSky();
}

void UDreamAtmosphereTables::ReadCpuSky()
{
	using namespace DreamAtmosphere::TablesRender;
	CpuSky.Reset();
	CpuSkySize = FIntVector::ZeroValue;
	FTextureRenderTargetResource* AtlasResource = SkyAtlas ? SkyAtlas->GameThread_GetRenderTargetResource() : nullptr;
	FTextureResource* SkyResource = Sky ? Sky->GetResource() : nullptr;
	if (!AtlasResource || !SkyResource)
	{
		return;
	}
	FBuild B;
	Built.Pack(B.Atmosphere);
	B.SkySize = FIntVector(Sky->SizeX, Sky->SizeY, Sky->SizeZ);
	ENQUEUE_RENDER_COMMAND(DreamAtmosphereSkyAtlas)([B, AtlasResource, SkyResource](FRHICommandListImmediate& RHICmdList)
		{
			FRHITexture* AtlasRHI = AtlasResource->GetRenderTargetTexture();
			FRHITexture* SkyRHI = SkyResource->TextureRHI.GetReference();
			if (!AtlasRHI || !SkyRHI)
			{
				return;
			}
			FRDGBuilder GraphBuilder(RHICmdList, RDG_EVENT_NAME("DreamAtmosphere.SkyAtlas"));
			auto* Params = GraphBuilder.AllocParameters<FDreamAtmosphereSkyAtlasCS::FParameters>();
			Fill(GraphBuilder, B, Params->Lut);
			Params->Lut.SkyLut = RegisterExternalTexture(GraphBuilder, SkyRHI, TEXT("DreamAtmosphere.Sky"));
			Params->Lut.OutSlice = GraphBuilder.CreateUAV(RegisterExternalTexture(GraphBuilder, AtlasRHI, TEXT("DreamAtmosphere.SkyAtlas")));
			FComputeShaderUtils::AddPass(GraphBuilder, RDG_EVENT_NAME("SkyAtlas"), TShaderMapRef<FDreamAtmosphereSkyAtlasCS>(GetGlobalShaderMap(GMaxRHIFeatureLevel)),
				Params, FComputeShaderUtils::GetGroupCount(FIntPoint(B.SkySize.X, B.SkySize.Y * B.SkySize.Z), GroupSize));
			GraphBuilder.Execute();
		});
	// (Once per build of the air, which is rare: a style changed, a level opened. The wait is a few milliseconds.)
	FlushRenderingCommands();
	TArray<FLinearColor> Pixels;
	if (AtlasResource->ReadLinearColorPixels(Pixels) && Pixels.Num() == Sky->SizeX * Sky->SizeY * Sky->SizeZ)
	{
		CpuSky = MoveTemp(Pixels);
		CpuSkySize = FIntVector(Sky->SizeX, Sky->SizeY, Sky->SizeZ);
	}
}

FLinearColor UDreamAtmosphereTables::SampleSky(const FVector& View, const FVector& Light) const
{
	using namespace DreamWind::Hlsl;
	if (CpuSky.Num() == 0)
	{
		return FLinearColor::Black;
	}
	const FVector V = View.GetSafeNormal(), L = Light.GetSafeNormal();
	const FVector2D VH(V.X, V.Y), LH(L.X, L.Y);
	const double Lengths = VH.Size() * LH.Size();
	const float CosAround = Lengths > 1.0e-6 ? float(FVector2D::DotProduct(VH, LH) / Lengths) : 1.0f;
	const float3 Uvw = DSA_SkyUvw(float(V.Z), CosAround, float(L.Z));
	// Trilinear between the texels that store the coordinates edge to edge (DSA_TableParam).
	const FIntVector N = CpuSkySize;
	const float Fx = Uvw.x * (N.X - 1), Fy = Uvw.y * (N.Y - 1), Fz = Uvw.z * (N.Z - 1);
	const int32 X0 = FMath::Clamp(int32(Fx), 0, N.X - 1), Y0 = FMath::Clamp(int32(Fy), 0, N.Y - 1), Z0 = FMath::Clamp(int32(Fz), 0, N.Z - 1);
	const int32 X1 = FMath::Min(X0 + 1, N.X - 1), Y1 = FMath::Min(Y0 + 1, N.Y - 1), Z1 = FMath::Min(Z0 + 1, N.Z - 1);
	const float Ax = Fx - X0, Ay = Fy - Y0, Az = Fz - Z0;
	auto At = [&](int32 X, int32 Y, int32 Z) { return CpuSky[(Z * N.Y + Y) * N.X + X]; };
	auto Row = [&](int32 Y, int32 Z) { return FMath::Lerp(At(X0, Y, Z), At(X1, Y, Z), Ax); };
	auto Plane = [&](int32 Z) { return FMath::Lerp(Row(Y0, Z), Row(Y1, Z), Ay); };
	FLinearColor C = FMath::Lerp(Plane(Z0), Plane(Z1), Az);
	C.A = 1.0f;
	return C;
}

bool UDreamAtmosphereTables::MakeBakedSummary(FDreamAtmosphereBakedSky& Out) const
{
	using namespace DreamWind::Hlsl;
	if (CpuSky.Num() == 0)
	{
		return false;
	}
	const int32 D = CpuSkySize.Z;
	Out.Zenith.SetNum(D);
	Out.Horizon.SetNum(D);
	for (int32 k = 0; k < D; ++k)
	{
		// The light at the slice's elevation, as the table stores it.
		const float LightZ = DSA_SkyDirections(float3(0.0f, 0.0f, DSA_TableParam(float(k), float(D)))).z;
		const FVector Light(FMath::Sqrt(FMath::Max(1.0f - LightZ * LightZ, 0.0f)), 0.0, LightZ);
		Out.Zenith[k] = SampleSky(FVector::UpVector, Light);
		FLinearColor Round = FLinearColor::Black;
		for (int32 i = 0; i < 8; ++i)
		{
			const double Az = i * UE_PI / 4.0;
			Round += SampleSky(FVector(FMath::Cos(Az) * 0.99875, FMath::Sin(Az) * 0.99875, 0.05), Light);
		}
		Out.Horizon[k] = Round * 0.125f;
		Out.Horizon[k].A = 1.0f;
	}
	Out.BakedFor = Built;
	return true;
}

#if WITH_EDITOR
UVolumeTexture* UDreamAtmosphereTables::BakeSky(UObject* Outer, FName Name, float Scale, FDreamAtmosphereBakedSky& OutBaked) const
{
	if (CpuSky.Num() == 0 || !Outer || !MakeBakedSummary(OutBaked))
	{
		return nullptr;
	}
	UVolumeTexture* Texture = FindObject<UVolumeTexture>(Outer, *Name.ToString());
	if (!Texture)
	{
		Texture = NewObject<UVolumeTexture>(Outer, Name, Outer == GetTransientPackage() ? RF_Transient : (RF_Public | RF_Standalone));
	}
	const FIntVector N = CpuSkySize;
	const TArray<FLinearColor>& Voxels = CpuSky;
	Texture->UpdateSourceFromFunction([&Voxels, N, Scale](int32 X, int32 Y, int32 Z, void* Out)
	{
		const FLinearColor C = Voxels[(Z * N.Y + Y) * N.X + X] * Scale;
		*static_cast<FFloat16Color*>(Out) = FFloat16Color(FLinearColor(C.R, C.G, C.B, 1.0f));
	}, N.X, N.Y, N.Z, TSF_RGBA16F);
	Texture->PreEditChange(nullptr);
	Texture->SRGB = false;
	Texture->CompressionSettings = TC_HDR;
	Texture->MipGenSettings = TMGS_NoMipmaps;
	Texture->LODGroup = TEXTUREGROUP_World;
	Texture->NeverStream = true;
	Texture->AddressMode = TA_Clamp;
	Texture->Filter = TF_Bilinear;
	Texture->PostEditChange();
	OutBaked.Table = Texture;
	OutBaked.Scale = Scale;
	return Texture;
}
#endif

FLinearColor UDreamAtmosphereTables::LightTransmittance(float LightZ) const
{
	using namespace DreamWind::Hlsl;
	FVector4f P[5];
	Built.Pack(P);
	const FDSA_Atmosphere A = DSA_Unpack(ToHlsl(P[0]), ToHlsl(P[1]), ToHlsl(P[2]), ToHlsl(P[3]), ToHlsl(P[4]));
	const float3 T = DSA_LightTransmittance(A, A.BottomRadius, FMath::Clamp(LightZ, -1.0f, 1.0f));
	return FLinearColor(T.x, T.y, T.z, 1.0f);
}

bool UDreamAtmosphereTables::ReadSkySlice(int32 Z, TArray<FLinearColor>& OutPixels)
{
	using namespace DreamAtmosphere::TablesRender;
	OutPixels.Reset();
	if (!bBuilt || !Sky || !SkySlice || !FApp::CanEverRender())
	{
		return false;
	}
	FTextureRenderTargetResource* SliceResource = SkySlice->GameThread_GetRenderTargetResource();
	FTextureResource* SkyResource = Sky->GetResource();
	if (!SliceResource || !SkyResource)
	{
		return false;
	}
	FBuild B;
	Built.Pack(B.Atmosphere);
	B.SkySize = FIntVector(Sky->SizeX, Sky->SizeY, Sky->SizeZ);
	const uint32 Slice = uint32(FMath::Clamp(Z, 0, Sky->SizeZ - 1));
	ENQUEUE_RENDER_COMMAND(DreamAtmosphereSkySlice)([B, Slice, SliceResource, SkyResource](FRHICommandListImmediate& RHICmdList)
		{
			FRHITexture* SliceRHI = SliceResource->GetRenderTargetTexture();
			FRHITexture* SkyRHI = SkyResource->TextureRHI.GetReference();
			if (!SliceRHI || !SkyRHI)
			{
				return;
			}
			FRDGBuilder GraphBuilder(RHICmdList, RDG_EVENT_NAME("DreamAtmosphere.SkySlice"));
			auto* Params = GraphBuilder.AllocParameters<FDreamAtmosphereSkySliceCS::FParameters>();
			Fill(GraphBuilder, B, Params->Lut);
			Params->Lut.SkySlice = Slice;
			Params->Lut.SkyLut = RegisterExternalTexture(GraphBuilder, SkyRHI, TEXT("DreamAtmosphere.Sky"));
			FRDGTextureRef Out = RegisterExternalTexture(GraphBuilder, SliceRHI, TEXT("DreamAtmosphere.SkySlice"));
			Params->Lut.OutSlice = GraphBuilder.CreateUAV(Out);
			FComputeShaderUtils::AddPass(GraphBuilder, RDG_EVENT_NAME("SkySlice"), TShaderMapRef<FDreamAtmosphereSkySliceCS>(GetGlobalShaderMap(GMaxRHIFeatureLevel)),
				Params, FComputeShaderUtils::GetGroupCount(FIntPoint(B.SkySize.X, B.SkySize.Y), GroupSize));
			GraphBuilder.Execute();
		});
	FlushRenderingCommands();
	return SliceResource->ReadLinearColorPixels(OutPixels);
}
