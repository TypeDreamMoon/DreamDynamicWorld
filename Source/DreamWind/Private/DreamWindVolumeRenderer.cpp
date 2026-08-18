// Copyright DreamMoon. All Rights Reserved.

#include "DreamWindVolumeRenderer.h"
#include "DreamWindModule.h"

#include "GlobalShader.h"
#include "ShaderParameterStruct.h"
#include "RenderGraphBuilder.h"
#include "RenderGraphUtils.h"
#include "RenderGraphResources.h"
#include "RenderTargetPool.h"
#include "RHIStaticStates.h"
#include "SystemTextures.h"
#include "TextureResource.h"
#include "DataDrivenShaderPlatformInfo.h"

// -------------------------------------------------------------------------------------------------
// Compute shader
// -------------------------------------------------------------------------------------------------

class FDreamWindVolumeUpdateCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FDreamWindVolumeUpdateCS);
	SHADER_USE_PARAMETER_STRUCT(FDreamWindVolumeUpdateCS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER(FVector4f, P0)
		SHADER_PARAMETER(FVector4f, P1)
		SHADER_PARAMETER(FVector4f, P2)
		SHADER_PARAMETER(FVector4f, T0)
		SHADER_PARAMETER(FVector4f, T1)
		SHADER_PARAMETER(FVector3f, PrevCenter)
		SHADER_PARAMETER(float, PrevValid)
		SHADER_PARAMETER(FVector3f, NewCenter)
		SHADER_PARAMETER(float, VoxelSize)
		SHADER_PARAMETER(FVector3f, InvExtent)
		SHADER_PARAMETER(float, DeltaTime)
		SHADER_PARAMETER(FIntVector, Resolution)
		SHADER_PARAMETER(float, DecayFactor)
		SHADER_PARAMETER(uint32, NumSources)
		SHADER_PARAMETER(float, AdvectScale)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<float4>, Sources)
		SHADER_PARAMETER_RDG_TEXTURE(Texture3D<float4>, PrevVolume)
		SHADER_PARAMETER_SAMPLER(SamplerState, PrevSampler)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float4>, TerrainTexture)
		SHADER_PARAMETER_SAMPLER(SamplerState, TerrainSampler)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture3D<float4>, OutVolume)
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.SetDefine(TEXT("THREADGROUP_SIZE"), 4);
	}
};

IMPLEMENT_GLOBAL_SHADER(FDreamWindVolumeUpdateCS, "/Plugin/DreamWind/Private/DreamWindVolumeUpdate.usf", "MainCS", SF_Compute);

// -------------------------------------------------------------------------------------------------

FDreamWindVolumeRenderer::~FDreamWindVolumeRenderer()
{
	// History is released on the render thread through EnqueueReset before the owner lets go.
}

void FDreamWindVolumeRenderer::EnqueueUpdate(FUpdateArgs&& Args)
{
	check(IsInGameThread());
	TSharedRef<FDreamWindVolumeRenderer, ESPMode::ThreadSafe> Self = AsShared();
	ENQUEUE_RENDER_COMMAND(DreamWindVolumeUpdate)(
		[Self, Args = MoveTemp(Args)](FRHICommandListImmediate& RHICmdList)
		{
			FRDGBuilder GraphBuilder(RHICmdList, RDG_EVENT_NAME("DreamWindVolume"));
			Self->Update_RenderThread(GraphBuilder, Args);
			GraphBuilder.Execute();
		});
}

void FDreamWindVolumeRenderer::EnqueueReset()
{
	TSharedRef<FDreamWindVolumeRenderer, ESPMode::ThreadSafe> Self = AsShared();
	ENQUEUE_RENDER_COMMAND(DreamWindVolumeReset)(
		[Self](FRHICommandListImmediate&)
		{
			Self->Release_RenderThread();
		});
}

void FDreamWindVolumeRenderer::Release_RenderThread()
{
	History.SafeRelease();
	HistoryResolution = FIntVector::ZeroValue;
	bHistoryValid = false;
}

void FDreamWindVolumeRenderer::Update_RenderThread(FRDGBuilder& GraphBuilder, const FUpdateArgs& Args)
{
	check(IsInRenderingThread());

	if (!Args.TargetResource)
	{
		return;
	}
	FRHITexture* TargetRHI = Args.TargetResource->GetRenderTargetTexture();
	if (!TargetRHI)
	{
		return;
	}

	const FIntVector Res = Args.Resolution;
	if (Res.X <= 0 || Res.Y <= 0 || Res.Z <= 0)
	{
		return;
	}
	const FIntVector TargetSize = TargetRHI->GetSizeXYZ();
	if (TargetSize != Res)
	{
		UE_LOG(LogDreamWind, Warning, TEXT("Wind volume render target is %dx%dx%d but the settings ask for %dx%dx%d; skipping update."),
			TargetSize.X, TargetSize.Y, TargetSize.Z, Res.X, Res.Y, Res.Z);
		return;
	}
	if (!EnumHasAnyFlags(TargetRHI->GetFlags(), TexCreate_UAV))
	{
		UE_LOG(LogDreamWind, Warning, TEXT("Wind volume render target was created without UAV support; enable 'Supports UAV' on the asset."));
		return;
	}

	FRDGTextureRef Target = RegisterExternalTexture(GraphBuilder, TargetRHI, TEXT("DreamWind.VolumeRT"));

	// (Re)create the history when it does not match; the first frame after that starts from zero.
	if (Args.bResetHistory || !History.IsValid() || HistoryResolution != Res)
	{
		History.SafeRelease();
		const FRDGTextureDesc Desc = FRDGTextureDesc::Create3D(Res, PF_FloatRGBA, FClearValueBinding::Black, TexCreate_ShaderResource | TexCreate_UAV);
		History = AllocatePooledTexture(Desc, TEXT("DreamWind.VolumeHistory"));
		HistoryResolution = Res;
		bHistoryValid = false;
	}
	FRDGTextureRef HistoryTexture = GraphBuilder.RegisterExternalTexture(History, TEXT("DreamWind.VolumeHistory"));

	// Sources: three float4 per record, at least one dummy element so the buffer is never empty.
	using namespace DreamWind::Hlsl;
	const int32 NumSources = Args.Frame.Sources.Num();
	TArray<FVector4f> SourceData;
	SourceData.Reserve(FMath::Max(NumSources, 1) * DW_SOURCE_FLOAT4S);
	for (const FDreamWindSourceData& S : Args.Frame.Sources)
	{
		SourceData.Add(ToVector4f(S.PositionRadius));
		SourceData.Add(ToVector4f(S.DirectionStrength));
		SourceData.Add(ToVector4f(S.Misc));
		SourceData.Add(ToVector4f(S.BoxExtent));
		SourceData.Add(ToVector4f(S.BoxRotation));
	}
	FRDGBufferRef SourceBuffer = CreateStructuredBuffer(GraphBuilder, TEXT("DreamWind.Sources"), SourceData);

	const FVector3f Extent = FVector3f(Res) * Args.VoxelSize;

	FDreamWindVolumeUpdateCS::FParameters* Params = GraphBuilder.AllocParameters<FDreamWindVolumeUpdateCS::FParameters>();
	Params->P0 = Args.Frame.P0;
	Params->P1 = Args.Frame.P1;
	Params->P2 = Args.Frame.P2;
	Params->T0 = Args.Frame.T0;
	Params->T1 = Args.Frame.T1;
	Params->PrevCenter = Args.PrevCenter;
	Params->PrevValid = bHistoryValid ? 1.0f : 0.0f;
	Params->NewCenter = Args.NewCenter;
	Params->VoxelSize = Args.VoxelSize;
	Params->InvExtent = FVector3f(1.0f / Extent.X, 1.0f / Extent.Y, 1.0f / Extent.Z);
	Params->DeltaTime = Args.DeltaTime;
	Params->Resolution = Res;
	Params->DecayFactor = Args.DecayFactor;
	Params->NumSources = uint32(NumSources);
	Params->AdvectScale = Args.bAdvect ? 1.0f : 0.0f;
	Params->Sources = GraphBuilder.CreateSRV(SourceBuffer);
	Params->PrevVolume = HistoryTexture;
	Params->PrevSampler = TStaticSamplerState<SF_Trilinear, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();
	Params->TerrainSampler = TStaticSamplerState<SF_Bilinear, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();
	{
		FRDGTextureRef TerrainRDG = nullptr;
		if (Args.Frame.T1.X > 0.5f && Args.TerrainResource)
		{
			if (FRHITexture* TerrainRHI = Args.TerrainResource->GetRenderTargetTexture())
			{
				TerrainRDG = RegisterExternalTexture(GraphBuilder, TerrainRHI, TEXT("DreamWind.TerrainRT"));
			}
		}
		if (!TerrainRDG)
		{
			Params->T1.X = 0.0f;
			TerrainRDG = GraphBuilder.RegisterExternalTexture(GSystemTextures.BlackDummy);
		}
		Params->TerrainTexture = TerrainRDG;
	}
	Params->OutVolume = GraphBuilder.CreateUAV(Target);

	TShaderMapRef<FDreamWindVolumeUpdateCS> ComputeShader(GetGlobalShaderMap(GMaxRHIFeatureLevel));
	const FIntVector GroupCount = FComputeShaderUtils::GetGroupCount(Res, 4);
	FComputeShaderUtils::AddPass(GraphBuilder, RDG_EVENT_NAME("DreamWindVolumeUpdate %dx%dx%d, %d sources", Res.X, Res.Y, Res.Z, NumSources), ComputeShader, Params, GroupCount);

	// The result is next frame's history and this frame's texture for materials / Niagara.
	AddCopyTexturePass(GraphBuilder, Target, HistoryTexture);
	GraphBuilder.SetTextureAccessFinal(Target, ERHIAccess::SRVMask);
	bHistoryValid = true;
	++FramesRendered;
}
