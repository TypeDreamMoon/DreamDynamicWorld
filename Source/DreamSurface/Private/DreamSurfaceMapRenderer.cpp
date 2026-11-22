// Copyright DreamMoon. All Rights Reserved.

#include "DreamSurfaceMapRenderer.h"

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
		SHADER_PARAMETER(uint32, NumSteps)
		SHADER_PARAMETER(float, StepHours)
		SHADER_PARAMETER(FVector4f, PartialWeather)
		SHADER_PARAMETER(FVector4f, PartialChannels)
		SHADER_PARAMETER(FVector4f, PartialAir)
		SHADER_PARAMETER(float, PartialHours)
		SHADER_PARAMETER(FVector2f, PrecipitationGate)
		SHADER_PARAMETER(FVector2f, MapCenter)
		SHADER_PARAMETER(float, MapExtent)
		SHADER_PARAMETER(uint32, NumZones)
		SHADER_PARAMETER(uint32, bIntegrate)
		SHADER_PARAMETER(FIntPoint, Resolution)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<float4>, Steps)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<float4>, Zones)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float4>, StepStart)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float4>, OutMap)
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
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<float4>, Shelters)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float2>, OutCover)
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
	if (!TargetRHI || !EnumHasAnyFlags(TargetRHI->GetFlags(), TexCreate_UAV))
	{
		return;
	}
	const FIntVector Size = TargetRHI->GetSizeXYZ();
	const FIntPoint Resolution(Size.X, Size.Y);

	// The step start, a texture of the map's size; a new one (first use, a resize) has to be integrated.
	bool bIntegrate = Args.bIntegrate;
	TRefCountPtr<IPooledRenderTarget>& StepStart = StepStarts.FindOrAdd(Slot);
	if (!StepStart.IsValid() || StepStart->GetDesc().Extent != Resolution)
	{
		const FPooledRenderTargetDesc Desc = FPooledRenderTargetDesc::Create2DDesc(Resolution, PF_A32B32G32R32F, FClearValueBinding::Black,
			TexCreate_None, TexCreate_ShaderResource | TexCreate_UAV, false);
		GRenderTargetPool.FindFreeElement(GraphBuilder.RHICmdList, Desc, StepStart, TEXT("DreamSurface.StepStart"));
		bIntegrate = true;
	}

	FRDGTextureRef Target = RegisterExternalTexture(GraphBuilder, TargetRHI, TEXT("DreamSurface.Map"));
	FRDGTextureRef Start = GraphBuilder.RegisterExternalTexture(StepStart, TEXT("DreamSurface.StepStart"));

	const TArray<FVector4f> NoStep = { FVector4f::Zero(), FVector4f::Zero(), FVector4f(15.0f, -1.0f, 0.0f, 0.0f) };
	const TArray<FVector4f> NoZone = { FVector4f(0.0f, 0.0f, 1.0f, 0.0f), FVector4f(1.0f, 1.0f, 1.0f, 0.0f), FVector4f(-1.0f, -1.0f, -1.0f, 0.0f), FVector4f(-1.0e4f) };
	FRDGBufferRef StepBuffer = CreateStructuredBuffer(GraphBuilder, TEXT("DreamSurface.Steps"), NonEmpty(Args.Steps, NoStep));
	FRDGBufferRef ZoneBuffer = CreateStructuredBuffer(GraphBuilder, TEXT("DreamSurface.Zones"), NonEmpty(Args.Zones, NoZone));

	FDreamSurfaceStateCS::FParameters* Params = GraphBuilder.AllocParameters<FDreamSurfaceStateCS::FParameters>();
	Params->Rates0 = Args.Rates[0];
	Params->Rates1 = Args.Rates[1];
	Params->Rates2 = Args.Rates[2];
	Params->NumSteps = uint32(Args.Steps.Num() / 3);
	Params->StepHours = Args.StepHours;
	Params->PartialWeather = Args.PartialWeather;
	Params->PartialChannels = Args.PartialChannels;
	Params->PartialAir = Args.PartialAir;
	Params->PartialHours = Args.PartialHours;
	Params->PrecipitationGate = Args.PrecipitationGate;
	Params->MapCenter = Args.Center;
	Params->MapExtent = Args.Extent;
	Params->NumZones = uint32(Args.Zones.Num() / 4);
	Params->bIntegrate = bIntegrate ? 1u : 0u;
	Params->Resolution = Resolution;
	Params->Steps = GraphBuilder.CreateSRV(StepBuffer);
	Params->Zones = GraphBuilder.CreateSRV(ZoneBuffer);
	Params->StepStart = GraphBuilder.CreateUAV(Start);
	Params->OutMap = GraphBuilder.CreateUAV(Target);

	TShaderMapRef<FDreamSurfaceStateCS> Shader(GetGlobalShaderMap(GMaxRHIFeatureLevel));
	FComputeShaderUtils::AddPass(GraphBuilder,
		RDG_EVENT_NAME("DreamSurfaceMap %dx%d%s", Resolution.X, Resolution.Y, bIntegrate ? TEXT(", integrate") : TEXT("")),
		Shader, Params, FComputeShaderUtils::GetGroupCount(Resolution, ThreadGroupSize));
	GraphBuilder.SetTextureAccessFinal(Target, ERHIAccess::SRVMask);
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
	Params->Shelters = GraphBuilder.CreateSRV(ShelterBuffer);
	Params->OutCover = GraphBuilder.CreateUAV(Target);

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
