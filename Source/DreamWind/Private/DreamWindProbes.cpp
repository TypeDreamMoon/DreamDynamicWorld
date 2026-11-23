// Copyright DreamMoon. All Rights Reserved.

#include "DreamWindProbes.h"

#include "DataDrivenShaderPlatformInfo.h"
#include "GlobalShader.h"
#include "RenderGraphBuilder.h"
#include "RenderGraphUtils.h"
#include "RHIGPUReadback.h"
#include "RHIStaticStates.h"
#include "ShaderParameterStruct.h"
#include "SystemTextures.h"
#include "TextureResource.h"

class FDreamWindProbeCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FDreamWindProbeCS);
	SHADER_USE_PARAMETER_STRUCT(FDreamWindProbeCS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER(FVector4f, V0)
		SHADER_PARAMETER(FVector4f, V1)
		SHADER_PARAMETER(FVector4f, V2)
		SHADER_PARAMETER(FVector4f, V3)
		SHADER_PARAMETER(uint32, NumProbes)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<float4>, Points)
		SHADER_PARAMETER_RDG_TEXTURE(Texture3D<float4>, Near)
		SHADER_PARAMETER_RDG_TEXTURE(Texture3D<float4>, Far)
		SHADER_PARAMETER_SAMPLER(SamplerState, VolumeSampler)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<float4>, OutProbes)
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
	}
};
IMPLEMENT_GLOBAL_SHADER(FDreamWindProbeCS, "/Plugin/DreamDynamicWorld/Private/DreamWindProbes.usf", "ProbeCS", SF_Compute);

FDreamWindFluidProbes::~FDreamWindFluidProbes()
{
	// (the owner flushes rendering before it lets go: nothing reads these any more)
	for (FSlot& Slot : Slots)
	{
		delete Slot.Readback;
		Slot.Readback = nullptr;
	}
}

void FDreamWindFluidProbes::Enqueue(FRequest&& Request)
{
	check(IsInGameThread());
	TSharedRef<FDreamWindFluidProbes, ESPMode::ThreadSafe> Self = AsShared();
	ENQUEUE_RENDER_COMMAND(DreamWindFluidProbes)([Self, Request = MoveTemp(Request)](FRHICommandListImmediate& RHICmdList)
	{
		Self->Poll_RenderThread();
		Self->Dispatch_RenderThread(RHICmdList, Request);
	});
}

bool FDreamWindFluidProbes::Get(FName Key, FVector4f& Out) const
{
	FScopeLock Guard(&Lock);
	if (const FVector4f* Found = Results.Find(Key))
	{
		Out = *Found;
		return true;
	}
	return false;
}

void FDreamWindFluidProbes::Clear()
{
	FScopeLock Guard(&Lock);
	Results.Reset();
}

void FDreamWindFluidProbes::Poll_RenderThread()
{
	// The oldest pending readbacks first; whatever has arrived replaces the answers it carries.
	for (int32 i = 0; i < UE_ARRAY_COUNT(Slots); ++i)
	{
		FSlot& Slot = Slots[(NextSlot + i) % UE_ARRAY_COUNT(Slots)];
		if (!Slot.bPending || !Slot.Readback || !Slot.Readback->IsReady())
		{
			continue;
		}
		const int32 Count = Slot.Keys.Num();
		if (const FVector4f* Data = static_cast<const FVector4f*>(Slot.Readback->Lock(Count * sizeof(FVector4f))))
		{
			FScopeLock Guard(&Lock);
			for (int32 k = 0; k < Count; ++k)
			{
				Results.Add(Slot.Keys[k], Data[k]);
			}
		}
		Slot.Readback->Unlock();
		Slot.bPending = false;
	}
}

void FDreamWindFluidProbes::Dispatch_RenderThread(FRHICommandListImmediate& RHICmdList, const FRequest& Request)
{
	const int32 Count = FMath::Min(Request.Points.Num(), MaxProbes);
	FSlot& Slot = Slots[NextSlot];
	if (Count == 0 || Slot.bPending)
	{
		return;   // nothing asked, or the readbacks are all still in flight (the GPU is far behind): skip a frame
	}
	auto Resolve = [](FTextureRenderTargetResource* Resource) -> FRHITexture*
	{
		return Resource ? Resource->GetRenderTargetTexture() : nullptr;
	};
	FRHITexture* NearRHI = Resolve(Request.Near);
	FRHITexture* FarRHI = Resolve(Request.Far);

	FRDGBuilder GraphBuilder(RHICmdList, RDG_EVENT_NAME("DreamWind.FluidProbes"));
	TArray<FVector4f> Points(Request.Points.GetData(), Count);
	FRDGBufferRef PointBuffer = CreateStructuredBuffer(GraphBuilder, TEXT("DreamWind.ProbePoints"), Points);
	FRDGBufferRef Out = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateStructuredDesc(sizeof(FVector4f), MaxProbes), TEXT("DreamWind.Probes"));
	FRDGTextureRef Dummy = GraphBuilder.RegisterExternalTexture(GSystemTextures.VolumetricBlackDummy);

	auto* P = GraphBuilder.AllocParameters<FDreamWindProbeCS::FParameters>();
	P->V0 = Request.V0;
	P->V1 = NearRHI ? Request.V1 : FVector4f(Request.V1.X, Request.V1.Y, Request.V1.Z, 0.0f);
	P->V2 = Request.V2;
	P->V3 = FarRHI ? Request.V3 : FVector4f(Request.V3.X, Request.V3.Y, Request.V3.Z, 0.0f);
	P->NumProbes = uint32(Count);
	P->Points = GraphBuilder.CreateSRV(PointBuffer);
	P->Near = NearRHI ? RegisterExternalTexture(GraphBuilder, NearRHI, TEXT("DreamWind.ProbeNear")) : Dummy;
	P->Far = FarRHI ? RegisterExternalTexture(GraphBuilder, FarRHI, TEXT("DreamWind.ProbeFar")) : Dummy;
	P->VolumeSampler = TStaticSamplerState<SF_Trilinear, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();
	P->OutProbes = GraphBuilder.CreateUAV(Out);
	FComputeShaderUtils::AddPass(GraphBuilder, RDG_EVENT_NAME("Probes %d", Count),
		TShaderMapRef<FDreamWindProbeCS>(GetGlobalShaderMap(GMaxRHIFeatureLevel)), P, FIntVector(1, 1, 1));

	if (!Slot.Readback)
	{
		Slot.Readback = new FRHIGPUBufferReadback(TEXT("DreamWind.ProbeReadback"));
	}
	AddEnqueueCopyPass(GraphBuilder, Slot.Readback, Out, Count * sizeof(FVector4f));
	GraphBuilder.Execute();
	Slot.Keys = TArray<FName>(Request.Keys.GetData(), Count);
	Slot.bPending = true;
	NextSlot = (NextSlot + 1) % UE_ARRAY_COUNT(Slots);
}
