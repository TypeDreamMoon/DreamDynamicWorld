// Copyright DreamMoon. All Rights Reserved.

#include "DreamSurfaceMapRenderer.h"
#include "DreamSurfaceSubsystem.h"

#include "DataDrivenShaderPlatformInfo.h"
#include "GlobalShader.h"
#include "RenderGraphBuilder.h"
#include "RenderGraphUtils.h"
#include "RenderTargetPool.h"
#include "RHIStaticStates.h"
#include "ShaderParameterStruct.h"
#include "SystemTextures.h"
#include "TextureResource.h"

namespace DreamSurface::Render
{
	constexpr int32 ThreadGroupSize = 8;

	static bool ShouldCompile(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
	}

	static void SetGroupSize(FShaderCompilerEnvironment& OutEnvironment)
	{
		OutEnvironment.SetDefine(TEXT("THREADGROUP_SIZE"), ThreadGroupSize);
	}

	/** A structured buffer may not be empty: a weightless record stands in. */
	static TArray<FVector4f> NonEmpty(const TArray<FVector4f>& Data, const TArray<FVector4f>& Filler)
	{
		return Data.Num() > 0 ? Data : Filler;
	}
}

class FDreamSurfaceStateCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FDreamSurfaceStateCS);
	SHADER_USE_PARAMETER_STRUCT(FDreamSurfaceStateCS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER(FVector4f, Rates0)
		SHADER_PARAMETER(FVector4f, Rates1)
		SHADER_PARAMETER(FVector4f, Rates2)
		SHADER_PARAMETER(FVector4f, Rates3)
		SHADER_PARAMETER(FVector4f, Rates4)
		SHADER_PARAMETER(FVector4f, Rates5)
		SHADER_PARAMETER(uint32, NumSteps)
		SHADER_PARAMETER(float, StepHours)
		SHADER_PARAMETER(FVector4f, PartialWeather)
		SHADER_PARAMETER(FVector4f, PartialChannels)
		SHADER_PARAMETER(FVector4f, PartialAir)
		SHADER_PARAMETER(FVector4f, PartialKinds)
		SHADER_PARAMETER(float, PartialHours)
		SHADER_PARAMETER(FVector2f, PrecipitationGate)
		SHADER_PARAMETER(FVector2f, MapCenter)
		SHADER_PARAMETER(float, MapExtent)
		SHADER_PARAMETER(uint32, NumZones)
		SHADER_PARAMETER(uint32, bIntegrate)
		SHADER_PARAMETER(FIntPoint, Resolution)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<float4>, Steps)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<float4>, Zones)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<float4>, ZoneTracks)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<float4>, Floods)
		SHADER_PARAMETER(uint32, NumFloodBodies)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float4>, StepStart)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float4>, IceStepStart)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float4>, HailStepStart)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float4>, OutMap)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float4>, OutIce)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float4>, OutHail)
		SHADER_PARAMETER(FVector4f, WindL1Near)
		SHADER_PARAMETER(FVector4f, WindL1Far)
		SHADER_PARAMETER(FVector4f, WindL1Params)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, WindL1NearDerived)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, WindL1FarDerived)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, WindL1NearHeight)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, WindL1FarHeight)
		SHADER_PARAMETER_SAMPLER(SamplerState, WindL1Sampler)
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters) { return DreamSurface::Render::ShouldCompile(Parameters); }
	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		DreamSurface::Render::SetGroupSize(OutEnvironment);
	}
};

IMPLEMENT_GLOBAL_SHADER(FDreamSurfaceStateCS, "/Plugin/DreamDynamicWorld/Private/DreamSurfaceMap.usf", "StateCS", SF_Compute);

class FDreamSurfaceCoverCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FDreamSurfaceCoverCS);
	SHADER_USE_PARAMETER_STRUCT(FDreamSurfaceCoverCS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER(FVector2f, MapCenter)
		SHADER_PARAMETER(float, MapExtent)
		SHADER_PARAMETER(FIntPoint, Resolution)
		SHADER_PARAMETER(FVector4f, Terrain0)
		SHADER_PARAMETER(FVector4f, Terrain1)
		SHADER_PARAMETER(FVector4f, Terrain2)
		SHADER_PARAMETER(uint32, NumShelters)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, TerrainTexture)
		SHADER_PARAMETER_SAMPLER(SamplerState, TerrainSampler)
		SHADER_PARAMETER(FVector4f, L1Level)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, L1HeightMap)
		SHADER_PARAMETER_SAMPLER(SamplerState, L1HeightSampler)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<float4>, Shelters)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float4>, OutCover)
		SHADER_PARAMETER(float, LowRadius)
		SHADER_PARAMETER(uint32, NumRoofs)
		SHADER_PARAMETER_ARRAY(FVector4f, Roofs, [12])
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, RoofDepth0)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, RoofDepth1)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, RoofDepth2)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, RoofDepth3)
		SHADER_PARAMETER_SAMPLER(SamplerState, RoofSampler)
		SHADER_PARAMETER(uint32, NumMasks)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<float4>, Masks)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float4>, OutMask)
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters) { return DreamSurface::Render::ShouldCompile(Parameters); }
	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		DreamSurface::Render::SetGroupSize(OutEnvironment);
	}
};

IMPLEMENT_GLOBAL_SHADER(FDreamSurfaceCoverCS, "/Plugin/DreamDynamicWorld/Private/DreamSurfaceMap.usf", "CoverCS", SF_Compute);

void FDreamSurfaceMapRenderer::EnqueueState(int32 Slot, FStateArgs&& Args)
{
	check(IsInGameThread());
	TSharedRef<FDreamSurfaceMapRenderer, ESPMode::ThreadSafe> Self = AsShared();
	ENQUEUE_RENDER_COMMAND(DreamSurfaceMap)(
		[Self, Slot, Args = MoveTemp(Args)](FRHICommandListImmediate& RHICmdList)
		{
			FRDGBuilder GraphBuilder(RHICmdList, RDG_EVENT_NAME("DreamSurfaceMap"));
			Self->State_RenderThread(GraphBuilder, Slot, Args);
			GraphBuilder.Execute();
		});
}

void FDreamSurfaceMapRenderer::EnqueueCover(FCoverArgs&& Args)
{
	check(IsInGameThread());
	TSharedRef<FDreamSurfaceMapRenderer, ESPMode::ThreadSafe> Self = AsShared();
	ENQUEUE_RENDER_COMMAND(DreamSurfaceCover)(
		[Self, Args = MoveTemp(Args)](FRHICommandListImmediate& RHICmdList)
		{
			FRDGBuilder GraphBuilder(RHICmdList, RDG_EVENT_NAME("DreamSurfaceCover"));
			Self->Cover_RenderThread(GraphBuilder, Args);
			GraphBuilder.Execute();
		});
}

void FDreamSurfaceMapRenderer::State_RenderThread(FRDGBuilder& GraphBuilder, int32 Slot, const FStateArgs& Args)
{
	using namespace DreamSurface::Render;
	check(IsInRenderingThread());
	FRHITexture* TargetRHI = Args.Target ? Args.Target->GetRenderTargetTexture() : nullptr;
	FRHITexture* IceRHI = Args.IceTarget ? Args.IceTarget->GetRenderTargetTexture() : nullptr;
	FRHITexture* HailRHI = Args.HailTarget ? Args.HailTarget->GetRenderTargetTexture() : nullptr;
	if (!TargetRHI || !EnumHasAnyFlags(TargetRHI->GetFlags(), TexCreate_UAV) || !IceRHI || !EnumHasAnyFlags(IceRHI->GetFlags(), TexCreate_UAV)
		|| !HailRHI || !EnumHasAnyFlags(HailRHI->GetFlags(), TexCreate_UAV))
	{
		return;
	}
	const FIntVector Size = TargetRHI->GetSizeXYZ();
	const FIntPoint Resolution(Size.X, Size.Y);
	if (IceRHI->GetSizeXYZ() != Size || HailRHI->GetSizeXYZ() != Size)
	{
		return;
	}

	// The step start, three textures of the map's size; a new one (first use, a resize) has to be integrated.
	bool bIntegrate = Args.bIntegrate;
	auto EnsureStart = [&GraphBuilder, &Resolution, &bIntegrate](TRefCountPtr<IPooledRenderTarget>& Start, const TCHAR* Name)
	{
		if (!Start.IsValid() || Start->GetDesc().Extent != Resolution)
		{
			const FPooledRenderTargetDesc Desc = FPooledRenderTargetDesc::Create2DDesc(Resolution, PF_A32B32G32R32F, FClearValueBinding::Black,
				TexCreate_None, TexCreate_ShaderResource | TexCreate_UAV, false);
			GRenderTargetPool.FindFreeElement(GraphBuilder.RHICmdList, Desc, Start, Name);
			bIntegrate = true;
		}
	};
	TRefCountPtr<IPooledRenderTarget>& StepStart = StepStarts.FindOrAdd(Slot);
	TRefCountPtr<IPooledRenderTarget>& IceStepStart = IceStepStarts.FindOrAdd(Slot);
	EnsureStart(StepStart, TEXT("DreamSurface.StepStart"));
	EnsureStart(IceStepStart, TEXT("DreamSurface.IceStepStart"));
	TRefCountPtr<IPooledRenderTarget>& HailStepStart = HailStepStarts.FindOrAdd(Slot);
	EnsureStart(HailStepStart, TEXT("DreamSurface.HailStepStart"));

	FRDGTextureRef Target = RegisterExternalTexture(GraphBuilder, TargetRHI, TEXT("DreamSurface.Map"));
	FRDGTextureRef IceTarget = RegisterExternalTexture(GraphBuilder, IceRHI, TEXT("DreamSurface.IceMap"));
	FRDGTextureRef Start = GraphBuilder.RegisterExternalTexture(StepStart, TEXT("DreamSurface.StepStart"));
	FRDGTextureRef IceStart = GraphBuilder.RegisterExternalTexture(IceStepStart, TEXT("DreamSurface.IceStepStart"));
	FRDGTextureRef HailTarget = RegisterExternalTexture(GraphBuilder, HailRHI, TEXT("DreamSurface.HailMap"));
	FRDGTextureRef HailStart = GraphBuilder.RegisterExternalTexture(HailStepStart, TEXT("DreamSurface.HailStepStart"));

	const TArray<FVector4f> NoStep = { FVector4f::Zero(), FVector4f::Zero(), FVector4f(15.0f, -1.0f, 0.0f, 0.0f), FVector4f::Zero() };
	const TArray<FVector4f> NoZone = { FVector4f(0.0f, 0.0f, 1.0f, 0.0f), FVector4f(1.0f, 1.0f, 1.0f, 0.0f), FVector4f(-1.0f, -1.0f, -1.0f, 0.0f), FVector4f(-1.0e4f), FVector4f(-1.0e4f) };
	FRDGBufferRef StepBuffer = CreateStructuredBuffer(GraphBuilder, TEXT("DreamSurface.Steps"), NonEmpty(Args.Steps, NoStep));
	FRDGBufferRef ZoneBuffer = CreateStructuredBuffer(GraphBuilder, TEXT("DreamSurface.Zones"), NonEmpty(Args.Zones, NoZone));
	const TArray<FVector4f> NoTrack = { FVector4f::Zero() };
	FRDGBufferRef TrackBuffer = CreateStructuredBuffer(GraphBuilder, TEXT("DreamSurface.ZoneTracks"), NonEmpty(Args.ZoneTracks, NoTrack));
	// The floods: every block (the whole steps and the one in progress) must be there, else none.
	const int32 FloodSteps = Args.Steps.Num() / FDreamSurfaceMapSteps::FloatsPerStep + 1;
	const bool bFloods = Args.NumFloodBodies > 0 && Args.Floods.Num() >= FloodSteps * Args.NumFloodBodies * 2;
	const TArray<FVector4f> NoFlood = { FVector4f::Zero(), FVector4f::Zero() };
	FRDGBufferRef FloodBuffer = CreateStructuredBuffer(GraphBuilder, TEXT("DreamSurface.Floods"), bFloods ? Args.Floods : NoFlood);

	FDreamSurfaceStateCS::FParameters* Params = GraphBuilder.AllocParameters<FDreamSurfaceStateCS::FParameters>();
	Params->Rates0 = Args.Rates[0];
	Params->Rates1 = Args.Rates[1];
	Params->Rates2 = Args.Rates[2];
	Params->Rates3 = Args.Rates[3];
	Params->Rates4 = Args.Rates[4];
	Params->Rates5 = Args.Rates[5];
	Params->NumSteps = uint32(Args.Steps.Num() / FDreamSurfaceMapSteps::FloatsPerStep);
	Params->StepHours = Args.StepHours;
	Params->PartialWeather = Args.PartialWeather;
	Params->PartialChannels = Args.PartialChannels;
	Params->PartialAir = Args.PartialAir;
	Params->PartialKinds = Args.PartialKinds;
	Params->PartialHours = Args.PartialHours;
	Params->PrecipitationGate = Args.PrecipitationGate;
	Params->MapCenter = Args.Center;
	Params->MapExtent = Args.Extent;
	Params->NumZones = uint32(FMath::Clamp(Args.NumZones, 0, Args.Zones.Num() / 5));
	Params->bIntegrate = bIntegrate ? 1u : 0u;
	Params->Resolution = Resolution;
	Params->Steps = GraphBuilder.CreateSRV(StepBuffer);
	Params->Zones = GraphBuilder.CreateSRV(ZoneBuffer);
	Params->ZoneTracks = GraphBuilder.CreateSRV(TrackBuffer);
	Params->Floods = GraphBuilder.CreateSRV(FloodBuffer);
	Params->NumFloodBodies = bFloods ? uint32(Args.NumFloodBodies) : 0u;
	Params->StepStart = GraphBuilder.CreateUAV(Start);
	Params->IceStepStart = GraphBuilder.CreateUAV(IceStart);
	Params->HailStepStart = GraphBuilder.CreateUAV(HailStart);
	Params->OutMap = GraphBuilder.CreateUAV(Target);
	Params->OutIce = GraphBuilder.CreateUAV(IceTarget);
	Params->OutHail = GraphBuilder.CreateUAV(HailTarget);
	// The wind's terrain map, a level only with both its maps (else it is off: the wind there as in the open).
	auto WindLevel = [&GraphBuilder](FVector4f Level, FTextureRenderTargetResource* Derived, FTextureRenderTargetResource* Height,
		FVector4f& OutLevel, FRDGTextureRef& OutDerived, FRDGTextureRef& OutHeight)
	{
		FRHITexture* DerivedRHI = Derived ? Derived->GetRenderTargetTexture() : nullptr;
		FRHITexture* HeightRHI = Height ? Height->GetRenderTargetTexture() : nullptr;
		const bool bOn = Level.W > 0.5f && DerivedRHI && HeightRHI;
		OutLevel = bOn ? Level : FVector4f::Zero();
		OutDerived = bOn ? RegisterExternalTexture(GraphBuilder, DerivedRHI, TEXT("DreamSurface.WindDerived")) : GSystemTextures.GetBlackDummy(GraphBuilder);
		OutHeight = bOn ? RegisterExternalTexture(GraphBuilder, HeightRHI, TEXT("DreamSurface.WindHeight")) : GSystemTextures.GetBlackDummy(GraphBuilder);
	};
	WindLevel(Args.WindL1Near, Args.WindL1NearDerived, Args.WindL1NearHeight, Params->WindL1Near, Params->WindL1NearDerived, Params->WindL1NearHeight);
	WindLevel(Args.WindL1Far, Args.WindL1FarDerived, Args.WindL1FarHeight, Params->WindL1Far, Params->WindL1FarDerived, Params->WindL1FarHeight);
	Params->WindL1Params = Args.WindL1Params;
	Params->WindL1Sampler = TStaticSamplerState<SF_Bilinear, AM_Wrap, AM_Wrap, AM_Wrap>::GetRHI();

	TShaderMapRef<FDreamSurfaceStateCS> Shader(GetGlobalShaderMap(GMaxRHIFeatureLevel));
	FComputeShaderUtils::AddPass(GraphBuilder,
		RDG_EVENT_NAME("DreamSurfaceMap %dx%d%s", Resolution.X, Resolution.Y, bIntegrate ? TEXT(", integrate") : TEXT("")),
		Shader, Params, FComputeShaderUtils::GetGroupCount(Resolution, ThreadGroupSize));
	GraphBuilder.SetTextureAccessFinal(Target, ERHIAccess::SRVMask);
	GraphBuilder.SetTextureAccessFinal(IceTarget, ERHIAccess::SRVMask);
	GraphBuilder.SetTextureAccessFinal(HailTarget, ERHIAccess::SRVMask);
	DrawCount.fetch_add(1);
	if (bIntegrate)
	{
		IntegrateCount.fetch_add(1);
	}
}

void FDreamSurfaceMapRenderer::Cover_RenderThread(FRDGBuilder& GraphBuilder, const FCoverArgs& Args)
{
	using namespace DreamSurface::Render;
	check(IsInRenderingThread());
	FRHITexture* TargetRHI = Args.Target ? Args.Target->GetRenderTargetTexture() : nullptr;
	if (!TargetRHI || !EnumHasAnyFlags(TargetRHI->GetFlags(), TexCreate_UAV))
	{
		return;
	}
	const FIntVector Size = TargetRHI->GetSizeXYZ();
	const FIntPoint Resolution(Size.X, Size.Y);
	FRDGTextureRef Target = RegisterExternalTexture(GraphBuilder, TargetRHI, TEXT("DreamSurface.Cover"));

	FRHITexture* TerrainRHI = Args.Terrain ? Args.Terrain->GetRenderTargetTexture() : nullptr;
	FVector4f Terrain1 = Args.TerrainParams[1];
	FRDGTextureRef Terrain;
	if (TerrainRHI)
	{
		Terrain = RegisterExternalTexture(GraphBuilder, TerrainRHI, TEXT("DreamSurface.Terrain"));
	}
	else
	{
		Terrain = GSystemTextures.GetBlackDummy(GraphBuilder);
		Terrain1.X = 0.0f; // no map: disabled
	}

	const TArray<FVector4f> NoShelter = { FVector4f::Zero(), FVector4f::Zero(), FVector4f::Zero() };
	FRDGBufferRef ShelterBuffer = CreateStructuredBuffer(GraphBuilder, TEXT("DreamSurface.Shelters"), NonEmpty(Args.Shelters, NoShelter));

	FDreamSurfaceCoverCS::FParameters* Params = GraphBuilder.AllocParameters<FDreamSurfaceCoverCS::FParameters>();
	Params->MapCenter = Args.Center;
	Params->MapExtent = Args.Extent;
	Params->Resolution = Resolution;
	Params->Terrain0 = Args.TerrainParams[0];
	Params->Terrain1 = Terrain1;
	Params->Terrain2 = Args.TerrainParams[2];
	Params->NumShelters = uint32(Args.Shelters.Num() / 3);
	Params->TerrainTexture = Terrain;
	Params->TerrainSampler = TStaticSamplerState<SF_Bilinear, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();
	// The wind's runtime terrain map (near level), when it runs: the level's top surfaces as they stand now.
	FRHITexture* L1RHI = Args.L1Height && Args.L1Level.W > 0.5f ? Args.L1Height->GetRenderTargetTexture() : nullptr;
	Params->L1Level = L1RHI ? Args.L1Level : FVector4f::Zero();
	Params->L1HeightMap = L1RHI ? RegisterExternalTexture(GraphBuilder, L1RHI, TEXT("DreamSurface.WindHeight")) : GSystemTextures.GetBlackDummy(GraphBuilder);
	Params->L1HeightSampler = TStaticSamplerState<SF_Bilinear, AM_Wrap, AM_Wrap, AM_Wrap>::GetRHI();
	Params->Shelters = GraphBuilder.CreateSRV(ShelterBuffer);
	Params->OutCover = GraphBuilder.CreateUAV(Target);
	Params->LowRadius = FMath::Max(Args.LowRadius, 50.0f);

	// Roof captures: their depth targets in four slots (a black stand-in for the unused ones).
	const int32 NumRoofs = FMath::Min3(4, Args.RoofTargets.Num(), Args.Roofs.Num() / 3);
	FRDGTextureRef RoofTextures[4];
	for (int32 i = 0; i < 4; ++i)
	{
		FRHITexture* RoofRHI = i < NumRoofs && Args.RoofTargets[i] ? Args.RoofTargets[i]->GetRenderTargetTexture() : nullptr;
		RoofTextures[i] = RoofRHI ? RegisterExternalTexture(GraphBuilder, RoofRHI, TEXT("DreamSurface.RoofDepth")) : GSystemTextures.GetBlackDummy(GraphBuilder);
	}
	Params->NumRoofs = uint32(NumRoofs);
	for (int32 i = 0; i < NumRoofs * 3; ++i)
	{
		Params->Roofs[i] = Args.Roofs[i];
	}
	Params->RoofDepth0 = RoofTextures[0];
	Params->RoofDepth1 = RoofTextures[1];
	Params->RoofDepth2 = RoofTextures[2];
	Params->RoofDepth3 = RoofTextures[3];
	Params->RoofSampler = TStaticSamplerState<SF_Point, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();

	// Mask brushes, into the mask map beside the cover (a transient one when the map is missing).
	const TArray<FVector4f> NoMask = { FVector4f::Zero(), FVector4f(1.0f, 1.0f, 1.0f, 0.0f), FVector4f(1.0f) };
	FRDGBufferRef MaskBuffer = CreateStructuredBuffer(GraphBuilder, TEXT("DreamSurface.Masks"), NonEmpty(Args.Masks, NoMask));
	FRHITexture* MaskRHI = Args.MaskTarget ? Args.MaskTarget->GetRenderTargetTexture() : nullptr;
	FRDGTextureRef MaskMap = MaskRHI && EnumHasAnyFlags(MaskRHI->GetFlags(), TexCreate_UAV)
		? RegisterExternalTexture(GraphBuilder, MaskRHI, TEXT("DreamSurface.Mask"))
		: GraphBuilder.CreateTexture(FRDGTextureDesc::Create2D(Resolution, PF_FloatRGBA, FClearValueBinding::White, TexCreate_ShaderResource | TexCreate_UAV), TEXT("DreamSurface.MaskScratch"));
	Params->NumMasks = uint32(Args.Masks.Num() / 3);
	Params->Masks = GraphBuilder.CreateSRV(MaskBuffer);
	Params->OutMask = GraphBuilder.CreateUAV(MaskMap);

	TShaderMapRef<FDreamSurfaceCoverCS> Shader(GetGlobalShaderMap(GMaxRHIFeatureLevel));
	FComputeShaderUtils::AddPass(GraphBuilder, RDG_EVENT_NAME("DreamSurfaceCover %dx%d, %d shelters, %d roofs, %d masks", Resolution.X, Resolution.Y,
		Args.Shelters.Num() / 3, NumRoofs, Args.Masks.Num() / 3),
		Shader, Params, FComputeShaderUtils::GetGroupCount(Resolution, ThreadGroupSize));
	GraphBuilder.SetTextureAccessFinal(Target, ERHIAccess::SRVMask);
	if (MaskRHI && MaskMap->IsExternal())
	{
		GraphBuilder.SetTextureAccessFinal(MaskMap, ERHIAccess::SRVMask);
	}
	DrawCount.fetch_add(1);
}

class FDreamSurfaceTrailsCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FDreamSurfaceTrailsCS);
	SHADER_USE_PARAMETER_STRUCT(FDreamSurfaceTrailsCS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER(FVector2f, TrailCenter)
		SHADER_PARAMETER(FVector2f, PreviousCenter)
		SHADER_PARAMETER(float, TrailExtent)
		SHADER_PARAMETER(float, PrintKeep)
		SHADER_PARAMETER(float, RippleKeep)
		SHADER_PARAMETER(float, WaveStep)
		SHADER_PARAMETER(float, WaveKeep)
		SHADER_PARAMETER(float, WaveSettle)
		SHADER_PARAMETER(uint32, NumStamps)
		SHADER_PARAMETER(FIntPoint, TrailResolution)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, PreviousTrails)
		SHADER_PARAMETER_SAMPLER(SamplerState, PreviousSampler)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<float4>, Stamps)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float4>, OutTrails)
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters) { return DreamSurface::Render::ShouldCompile(Parameters); }
	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		DreamSurface::Render::SetGroupSize(OutEnvironment);
	}
};

IMPLEMENT_GLOBAL_SHADER(FDreamSurfaceTrailsCS, "/Plugin/DreamDynamicWorld/Private/DreamSurfaceMap.usf", "TrailsCS", SF_Compute);

void FDreamSurfaceMapRenderer::EnqueueTrails(FTrailArgs&& Args)
{
	check(IsInGameThread());
	TSharedRef<FDreamSurfaceMapRenderer, ESPMode::ThreadSafe> Self = AsShared();
	ENQUEUE_RENDER_COMMAND(DreamSurfaceTrails)(
		[Self, Args = MoveTemp(Args)](FRHICommandListImmediate& RHICmdList)
		{
			FRDGBuilder GraphBuilder(RHICmdList, RDG_EVENT_NAME("DreamSurfaceTrails"));
			Self->Trails_RenderThread(GraphBuilder, Args);
			GraphBuilder.Execute();
		});
}

void FDreamSurfaceMapRenderer::Trails_RenderThread(FRDGBuilder& GraphBuilder, const FTrailArgs& Args)
{
	using namespace DreamSurface::Render;
	check(IsInRenderingThread());
	FRHITexture* TargetRHI = Args.Target ? Args.Target->GetRenderTargetTexture() : nullptr;
	if (!TargetRHI || !EnumHasAnyFlags(TargetRHI->GetFlags(), TexCreate_UAV))
	{
		return;
	}
	const FIntVector Size = TargetRHI->GetSizeXYZ();
	const FIntPoint Resolution(Size.X, Size.Y);
	FRDGTextureRef Target = RegisterExternalTexture(GraphBuilder, TargetRHI, TEXT("DreamSurface.Trails"));

	// The previous map, kept here: a new one (first use, a resize, a reset) starts from nothing.
	bool bReset = Args.bReset;
	if (!TrailHistory.IsValid() || TrailHistory->GetDesc().Extent != Resolution)
	{
		const FPooledRenderTargetDesc Desc = FPooledRenderTargetDesc::Create2DDesc(Resolution, PF_FloatRGBA, FClearValueBinding::Black,
			TexCreate_None, TexCreate_ShaderResource | TexCreate_RenderTargetable, false);
		GRenderTargetPool.FindFreeElement(GraphBuilder.RHICmdList, Desc, TrailHistory, TEXT("DreamSurface.TrailHistory"));
		bReset = true;
	}
	FRDGTextureRef History = GraphBuilder.RegisterExternalTexture(TrailHistory, TEXT("DreamSurface.TrailHistory"));

	const TArray<FVector4f> NoStamp = { FVector4f(0.0f, 0.0f, 1.0f, 0.0f), FVector4f::Zero() };
	FRDGBufferRef StampBuffer = CreateStructuredBuffer(GraphBuilder, TEXT("DreamSurface.Stamps"), NonEmpty(Args.Stamps, NoStamp));

	FDreamSurfaceTrailsCS::FParameters* Params = GraphBuilder.AllocParameters<FDreamSurfaceTrailsCS::FParameters>();
	Params->TrailCenter = Args.Center;
	// Starting over: the previous map lies nowhere near.
	Params->PreviousCenter = bReset ? Args.Center + FVector2f(Args.Extent * 4.0f) : Args.PreviousCenter;
	Params->TrailExtent = Args.Extent;
	Params->PrintKeep = Args.PrintKeep;
	Params->RippleKeep = Args.RippleKeep;
	Params->WaveStep = Args.WaveStep;
	Params->WaveKeep = Args.WaveKeep;
	Params->WaveSettle = Args.WaveSettle;
	Params->NumStamps = uint32(Args.Stamps.Num() / 2);
	Params->TrailResolution = Resolution;
	Params->PreviousTrails = History;
	Params->PreviousSampler = TStaticSamplerState<SF_Point, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();
	Params->Stamps = GraphBuilder.CreateSRV(StampBuffer);
	Params->OutTrails = GraphBuilder.CreateUAV(Target);

	TShaderMapRef<FDreamSurfaceTrailsCS> Shader(GetGlobalShaderMap(GMaxRHIFeatureLevel));
	FComputeShaderUtils::AddPass(GraphBuilder, RDG_EVENT_NAME("DreamSurfaceTrails %dx%d, %d prints", Resolution.X, Resolution.Y, Args.Stamps.Num() / 2),
		Shader, Params, FComputeShaderUtils::GetGroupCount(Resolution, ThreadGroupSize));
	// What this update made is what the next one carries over.
	AddCopyTexturePass(GraphBuilder, Target, History);
	GraphBuilder.SetTextureAccessFinal(Target, ERHIAccess::SRVMask);
	DrawCount.fetch_add(1);
}
