// Copyright DreamMoon. All Rights Reserved.

#include "DreamAtmosphereAerial.h"
#include "DreamAtmosphereSettings.h"
#include "DreamAtmosphereTables.h"

#include "DataDrivenShaderPlatformInfo.h"
#include "Engine/TextureRenderTarget2D.h"
#include "GlobalShader.h"
#include "PixelShaderUtils.h"
#include "PostProcess/PostProcessMaterialInputs.h"
#include "RenderGraphBuilder.h"
#include "RenderGraphUtils.h"
#include "RenderingThread.h"
#include "RHIStaticStates.h"
#include "SceneView.h"
#include "ScreenPass.h"
#include "ShaderParameterStruct.h"
#include "SystemTextures.h"
#include "TextureResource.h"
#include "UObject/Package.h"

namespace DreamAtmosphere::AerialRender
{
	static bool ShouldCompile(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
	}
}

BEGIN_SHADER_PARAMETER_STRUCT(FDreamAtmosphereAerialCommon, )
	SHADER_PARAMETER_ARRAY(FVector4f, Atmosphere, [5])
	SHADER_PARAMETER(FIntPoint, TransmittanceSize)
	SHADER_PARAMETER(FIntPoint, MultiScatteringSize)
	SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float4>, TransmittanceLut)
	SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float4>, MultiScatteringLut)
	SHADER_PARAMETER_SAMPLER(SamplerState, LutSampler)
	SHADER_PARAMETER(FMatrix44f, ClipToTranslatedWorld)
	SHADER_PARAMETER(FVector3f, TranslatedCamera)
	SHADER_PARAMETER(FVector3f, CameraKm)
	SHADER_PARAMETER(FVector3f, SunDirection)
	SHADER_PARAMETER(FVector3f, SunLight)
	SHADER_PARAMETER(FVector3f, MoonDirection)
	SHADER_PARAMETER(FVector3f, MoonLight)
	SHADER_PARAMETER(float, MaxDistance)
	SHADER_PARAMETER(float, DistanceScale)
	SHADER_PARAMETER(float, SkyDistance)
	SHADER_PARAMETER(FIntVector, VolumeSize)
	SHADER_PARAMETER(uint32, Steps)
END_SHADER_PARAMETER_STRUCT()

class FDreamAtmosphereAerialVolumeCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FDreamAtmosphereAerialVolumeCS);
	SHADER_USE_PARAMETER_STRUCT(FDreamAtmosphereAerialVolumeCS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_STRUCT_INCLUDE(FDreamAtmosphereAerialCommon, Common)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture3D<float4>, OutVolume)
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters) { return DreamAtmosphere::AerialRender::ShouldCompile(Parameters); }
};

class FDreamAtmosphereAerialCompositePS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FDreamAtmosphereAerialCompositePS);
	SHADER_USE_PARAMETER_STRUCT(FDreamAtmosphereAerialCompositePS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_STRUCT_INCLUDE(FDreamAtmosphereAerialCommon, Common)
		SHADER_PARAMETER_STRUCT_REF(FViewUniformShaderParameters, View)
		SHADER_PARAMETER_RDG_TEXTURE(Texture3D<float4>, AerialVolume)
		SHADER_PARAMETER_SAMPLER(SamplerState, AerialSampler)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float>, SceneDepth)
		SHADER_PARAMETER(FVector4f, ViewRect)
		RENDER_TARGET_BINDING_SLOTS()
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters) { return DreamAtmosphere::AerialRender::ShouldCompile(Parameters); }
};

class FDreamAtmosphereAerialSliceCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FDreamAtmosphereAerialSliceCS);
	SHADER_USE_PARAMETER_STRUCT(FDreamAtmosphereAerialSliceCS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER(FIntVector, SkySize)
		SHADER_PARAMETER(uint32, SkySlice)
		SHADER_PARAMETER_RDG_TEXTURE(Texture3D<float4>, SkyLut)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float4>, OutSlice)
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters) { return DreamAtmosphere::AerialRender::ShouldCompile(Parameters); }
	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.SetDefine(TEXT("THREADGROUP_SIZE"), 8);
	}
};

IMPLEMENT_GLOBAL_SHADER(FDreamAtmosphereAerialVolumeCS, "/Plugin/DreamDynamicWorld/Private/DreamAtmosphereAerial.usf", "VolumeCS", SF_Compute);
IMPLEMENT_GLOBAL_SHADER(FDreamAtmosphereAerialCompositePS, "/Plugin/DreamDynamicWorld/Private/DreamAtmosphereAerial.usf", "CompositePS", SF_Pixel);
// (The sky table's slice copy reads any volume: the test reads the aerial volume with it.)
IMPLEMENT_GLOBAL_SHADER(FDreamAtmosphereAerialSliceCS, "/Plugin/DreamDynamicWorld/Private/DreamAtmosphereLuts.usf", "SkySliceCS", SF_Compute);

namespace DreamAtmosphere::Aerial
{
	static constexpr double CmPerKm = 1.0e5;

	static FCamera MakeCamera(const FMatrix& ClipToTranslatedWorld, const FVector& Location, const FVector& PreViewTranslation, const FFrame& Frame)
	{
		FCamera C;
		C.ClipToTranslatedWorld = FMatrix44f(ClipToTranslatedWorld);
		C.TranslatedCamera = FVector3f(Location + PreViewTranslation);
		const double PlanetRadius = double(Frame.Atmosphere[0].X);
		C.CameraKm = FVector3f(float(Location.X / CmPerKm), float(Location.Y / CmPerKm), float((Location.Z - double(Frame.SeaLevel)) / CmPerKm + PlanetRadius));
		return C;
	}

	FCamera FCamera::FromView(const FSceneView& View, const FFrame& Frame)
	{
		const FViewMatrices& M = View.ViewMatrices;
		return MakeCamera(M.GetClipToTranslatedWorld(), M.GetViewOrigin(), M.GetPreViewTranslation(), Frame);
	}

	FCamera FCamera::FromMatrices(const FMatrix& ViewProjection, const FVector& Location, const FFrame& Frame)
	{
		// Translated world = world - Location: the camera at the origin.
		const FMatrix Translated = FTranslationMatrix(Location) * ViewProjection;
		return MakeCamera(Translated.Inverse(), Location, -Location, Frame);
	}

	static void FillCommon(FRDGBuilder& GraphBuilder, const FFrame& Frame, const FCamera& Camera, FDreamAtmosphereAerialCommon& P)
	{
		for (int32 i = 0; i < 5; ++i)
		{
			P.Atmosphere[i] = Frame.Atmosphere[i];
		}
		P.TransmittanceSize = Frame.TransmittanceSize;
		P.MultiScatteringSize = Frame.MultiScatteringSize;
		FRHITexture* T = Frame.Transmittance ? Frame.Transmittance->TextureRHI.GetReference() : nullptr;
		FRHITexture* M = Frame.MultiScattering ? Frame.MultiScattering->TextureRHI.GetReference() : nullptr;
		P.TransmittanceLut = T ? RegisterExternalTexture(GraphBuilder, T, TEXT("DreamAtmosphere.Transmittance")) : GSystemTextures.GetBlackDummy(GraphBuilder);
		P.MultiScatteringLut = M ? RegisterExternalTexture(GraphBuilder, M, TEXT("DreamAtmosphere.MultiScattering")) : GSystemTextures.GetBlackDummy(GraphBuilder);
		P.LutSampler = TStaticSamplerState<SF_Bilinear, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();
		P.ClipToTranslatedWorld = Camera.ClipToTranslatedWorld;
		P.TranslatedCamera = Camera.TranslatedCamera;
		P.CameraKm = Camera.CameraKm;
		P.SunDirection = Frame.SunDirection;
		P.SunLight = Frame.SunLight;
		P.MoonDirection = Frame.MoonDirection;
		P.MoonLight = Frame.MoonLight;
		P.MaxDistance = FMath::Max(Frame.MaxDistanceKm, 0.01f);
		P.DistanceScale = FMath::Max(Frame.DistanceScale, 0.0f);
		P.SkyDistance = Frame.SkyDistanceKm;
		P.VolumeSize = Frame.VolumeSize;
		P.Steps = uint32(FMath::Clamp(Frame.Steps, 2, 32));
	}

	FRDGTexture* AddVolumePass(FRDGBuilder& GraphBuilder, const FFrame& Frame, const FCamera& Camera)
	{
		const FIntVector Size(FMath::Max(Frame.VolumeSize.X, 1), FMath::Max(Frame.VolumeSize.Y, 1), FMath::Max(Frame.VolumeSize.Z, 1));
		FRDGTextureRef Volume = GraphBuilder.CreateTexture(FRDGTextureDesc::Create3D(Size, PF_FloatRGBA, FClearValueBinding::Black,
			TexCreate_ShaderResource | TexCreate_UAV), TEXT("DreamAtmosphere.AerialVolume"));
		auto* Params = GraphBuilder.AllocParameters<FDreamAtmosphereAerialVolumeCS::FParameters>();
		FillCommon(GraphBuilder, Frame, Camera, Params->Common);
		Params->Common.VolumeSize = Size;
		Params->OutVolume = GraphBuilder.CreateUAV(Volume);
		FComputeShaderUtils::AddPass(GraphBuilder, RDG_EVENT_NAME("AerialVolume %dx%dx%d", Size.X, Size.Y, Size.Z),
			TShaderMapRef<FDreamAtmosphereAerialVolumeCS>(GetGlobalShaderMap(GMaxRHIFeatureLevel)), Params,
			FComputeShaderUtils::GetGroupCount(FIntPoint(Size.X, Size.Y), 8));
		return Volume;
	}

	FScreenPassTexture AddCompositePass(FRDGBuilder& GraphBuilder, const FSceneView& View, const FPostProcessMaterialInputs& Inputs, const FFrame& Frame)
	{
		FScreenPassTexture SceneColor = FScreenPassTexture::CopyFromSlice(GraphBuilder, Inputs.GetInput(EPostProcessMaterialInput::SceneColor));
		if (!Frame.bEnabled || !SceneColor.IsValid() || !Frame.Transmittance || !Frame.MultiScattering || !Inputs.SceneTextures.SceneTextures)
		{
			return SceneColor;
		}
		FRDGTextureRef Depth = Inputs.SceneTextures.SceneTextures->GetParameters()->SceneDepthTexture;
		if (!Depth)
		{
			return SceneColor;
		}
		RDG_EVENT_SCOPE(GraphBuilder, "DreamAtmosphere.AerialPerspective");
		const FCamera Camera = FCamera::FromView(View, Frame);
		FRDGTextureRef Volume = AddVolumePass(GraphBuilder, Frame, Camera);

		auto* Params = GraphBuilder.AllocParameters<FDreamAtmosphereAerialCompositePS::FParameters>();
		FillCommon(GraphBuilder, Frame, Camera, Params->Common);
		Params->View = View.ViewUniformBuffer;
		Params->AerialVolume = Volume;
		Params->AerialSampler = TStaticSamplerState<SF_Bilinear, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();
		Params->SceneDepth = Depth;
		const FIntRect Rect = SceneColor.ViewRect;
		Params->ViewRect = FVector4f(float(Rect.Min.X), float(Rect.Min.Y), float(FMath::Max(Rect.Width(), 1)), float(FMath::Max(Rect.Height(), 1)));
		Params->RenderTargets[0] = FRenderTargetBinding(SceneColor.Texture, ERenderTargetLoadAction::ELoad);
		// Premultiplied: the scene times the air's transmittance plus its light.
		FRHIBlendState* Blend = TStaticBlendState<CW_RGB, BO_Add, BF_One, BF_SourceAlpha>::GetRHI();
		FPixelShaderUtils::AddFullscreenPass(GraphBuilder, GetGlobalShaderMap(View.GetFeatureLevel()), RDG_EVENT_NAME("Composite"),
			TShaderMapRef<FDreamAtmosphereAerialCompositePS>(GetGlobalShaderMap(View.GetFeatureLevel())), Params, Rect, Blend);
		return SceneColor;
	}

	bool ReadVolumeSlice(const FFrame& Frame, const FCamera& Camera, int32 Z, TArray<FLinearColor>& OutPixels)
	{
		OutPixels.Reset();
		UTextureRenderTarget2D* Slice = NewObject<UTextureRenderTarget2D>(GetTransientPackage(), NAME_None, RF_Transient);
		Slice->bSupportsUAV = true;
		Slice->bCanCreateUAV = true;
		Slice->InitCustomFormat(FMath::Max(Frame.VolumeSize.X, 1), FMath::Max(Frame.VolumeSize.Y, 1), PF_FloatRGBA, true);
		Slice->UpdateResourceImmediate(true);
		FTextureRenderTargetResource* SliceResource = Slice->GameThread_GetRenderTargetResource();
		if (!SliceResource)
		{
			return false;
		}
		const uint32 SliceIndex = uint32(FMath::Clamp(Z, 0, FMath::Max(Frame.VolumeSize.Z, 1) - 1));
		ENQUEUE_RENDER_COMMAND(DreamAtmosphereAerialSlice)([Frame, Camera, SliceIndex, SliceResource](FRHICommandListImmediate& RHICmdList)
			{
				FRHITexture* SliceRHI = SliceResource->GetRenderTargetTexture();
				if (!SliceRHI)
				{
					return;
				}
				FRDGBuilder GraphBuilder(RHICmdList, RDG_EVENT_NAME("DreamAtmosphere.AerialSlice"));
				FRDGTextureRef Volume = AddVolumePass(GraphBuilder, Frame, Camera);
				auto* Params = GraphBuilder.AllocParameters<FDreamAtmosphereAerialSliceCS::FParameters>();
				Params->SkySize = Frame.VolumeSize;
				Params->SkySlice = SliceIndex;
				Params->SkyLut = Volume;
				Params->OutSlice = GraphBuilder.CreateUAV(RegisterExternalTexture(GraphBuilder, SliceRHI, TEXT("DreamAtmosphere.AerialSlice")));
				FComputeShaderUtils::AddPass(GraphBuilder, RDG_EVENT_NAME("Slice"), TShaderMapRef<FDreamAtmosphereAerialSliceCS>(GetGlobalShaderMap(GMaxRHIFeatureLevel)),
					Params, FComputeShaderUtils::GetGroupCount(FIntPoint(Frame.VolumeSize.X, Frame.VolumeSize.Y), 8));
				GraphBuilder.Execute();
			});
		FlushRenderingCommands();
		const bool bRead = SliceResource->ReadLinearColorPixels(OutPixels);
		Slice->ReleaseResource();
		return bRead;
	}

	FFrame MakeFrame(const UDreamAtmosphereTables* Tables, const FVector& SunDirection, const FVector& MoonDirection, float MoonIllumination)
	{
		const UDreamAtmosphereSettings* S = UDreamAtmosphereSettings::Get();
		FFrame F;
		if (!Tables || !Tables->IsBuilt() || !Tables->GetTransmittance() || !Tables->GetMultiScattering())
		{
			return F;
		}
		Tables->GetBuiltParameters().Pack(F.Atmosphere);
		F.Transmittance = Tables->GetTransmittance()->GetResource();
		F.MultiScattering = Tables->GetMultiScattering()->GetResource();
		F.TransmittanceSize = FIntPoint(Tables->GetTransmittance()->SizeX, Tables->GetTransmittance()->SizeY);
		F.MultiScatteringSize = FIntPoint(Tables->GetMultiScattering()->SizeX, Tables->GetMultiScattering()->SizeY);
		F.SunDirection = FVector3f(SunDirection.GetSafeNormal());
		const FLinearColor Sun = S->SunColor * FMath::Max(S->SunIlluminance, 0.0f);
		F.SunLight = FVector3f(Sun.R, Sun.G, Sun.B);
		F.MoonDirection = FVector3f(MoonDirection.GetSafeNormal());
		const FLinearColor Moon = S->MoonColor * (FMath::Max(S->MoonIlluminance, 0.0f) * FMath::Clamp(MoonIllumination, 0.0f, 1.0f));
		F.MoonLight = FVector3f(Moon.R, Moon.G, Moon.B);
		F.MaxDistanceKm = FMath::Max(S->AerialMaxDistance, 1.0f);
		F.DistanceScale = FMath::Max(S->AerialDistanceScale, 0.0f);
		F.SkyDistanceKm = FMath::Max(S->SkyDistance, 1.0f);
		F.SeaLevel = S->SeaLevel;
		F.VolumeSize = FIntVector(FMath::Clamp(S->AerialVolumeSize.X, 4, 128), FMath::Clamp(S->AerialVolumeSize.Y, 4, 128), FMath::Clamp(S->AerialVolumeSize.Z, 4, 128));
		F.Steps = FMath::Clamp(S->AerialSteps, 2, 32);
		F.bEnabled = S->bAerialPerspective;
		return F;
	}
}
