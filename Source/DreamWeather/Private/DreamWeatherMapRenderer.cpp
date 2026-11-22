// Copyright DreamMoon. All Rights Reserved.

#include "DreamWeatherMapRenderer.h"
#include "DreamWeatherModule.h"

#include "DataDrivenShaderPlatformInfo.h"
#include "GlobalShader.h"
#include "RenderGraphBuilder.h"
#include "RenderGraphUtils.h"
#include "ShaderParameterStruct.h"
#include "TextureResource.h"

class FDreamWeatherMapCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FDreamWeatherMapCS);
	SHADER_USE_PARAMETER_STRUCT(FDreamWeatherMapCS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER(FVector4f, GlobalWeather)
		SHADER_PARAMETER(FVector2f, PrecipitationGate)
		SHADER_PARAMETER(FVector2f, MapCenter)
		SHADER_PARAMETER(float, MapExtent)
		SHADER_PARAMETER(uint32, NumZones)
		SHADER_PARAMETER(FIntPoint, Resolution)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<float4>, Zones)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float4>, OutMap)
	END_SHADER_PARAMETER_STRUCT()

	static constexpr int32 ThreadGroupSize = 8;

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.SetDefine(TEXT("THREADGROUP_SIZE"), ThreadGroupSize);
	}
};

IMPLEMENT_GLOBAL_SHADER(FDreamWeatherMapCS, "/Plugin/DreamDynamicWorld/Private/DreamWeatherMap.usf", "MainCS", SF_Compute);

void FDreamWeatherMapRenderer::EnqueueUpdate(FUpdateArgs&& Args)
{
	check(IsInGameThread());
	TSharedRef<FDreamWeatherMapRenderer, ESPMode::ThreadSafe> Self = AsShared();
	ENQUEUE_RENDER_COMMAND(DreamWeatherMap)(
		[Self, Args = MoveTemp(Args)](FRHICommandListImmediate& RHICmdList)
		{
			FRDGBuilder GraphBuilder(RHICmdList, RDG_EVENT_NAME("DreamWeatherMap"));
			Self->Update_RenderThread(GraphBuilder, Args);
			GraphBuilder.Execute();
		});
}

void FDreamWeatherMapRenderer::Update_RenderThread(FRDGBuilder& GraphBuilder, const FUpdateArgs& Args)
{
	check(IsInRenderingThread());
	if (!Args.Target)
	{
		return;
	}
	FRHITexture* TargetRHI = Args.Target->GetRenderTargetTexture();
	if (!TargetRHI || !EnumHasAnyFlags(TargetRHI->GetFlags(), TexCreate_UAV))
	{
		return;
	}
	const FIntVector Size = TargetRHI->GetSizeXYZ();
	const FIntPoint Resolution(Size.X, Size.Y);

	FRDGTextureRef Target = RegisterExternalTexture(GraphBuilder, TargetRHI, TEXT("DreamWeather.Map"));

	// A structured buffer may not be empty: one weightless zone stands in when there are none.
	TArray<FVector4f> ZoneData = Args.Zones;
	const uint32 NumZones = uint32(FMath::Clamp(Args.NumZones, 0, ZoneData.Num() / 3));
	if (ZoneData.Num() == 0)
	{
		ZoneData.Add(FVector4f(0.0f, 0.0f, 1.0f, 0.0f));
		ZoneData.Add(FVector4f(1.0f, 1.0f, 1.0f, 0.0f));
		ZoneData.Add(FVector4f(-1.0f, -1.0f, -1.0f, 0.0f));
	}
	FRDGBufferRef ZoneBuffer = CreateStructuredBuffer(GraphBuilder, TEXT("DreamWeather.MapZones"), ZoneData);

	FDreamWeatherMapCS::FParameters* Params = GraphBuilder.AllocParameters<FDreamWeatherMapCS::FParameters>();
	Params->GlobalWeather = Args.Global;
	Params->PrecipitationGate = Args.PrecipitationGate;
	Params->MapCenter = Args.Center;
	Params->MapExtent = Args.Extent;
	Params->NumZones = NumZones;
	Params->Resolution = Resolution;
	Params->Zones = GraphBuilder.CreateSRV(ZoneBuffer);
	Params->OutMap = GraphBuilder.CreateUAV(Target);

	TShaderMapRef<FDreamWeatherMapCS> Shader(GetGlobalShaderMap(GMaxRHIFeatureLevel));
	FComputeShaderUtils::AddPass(GraphBuilder, RDG_EVENT_NAME("DreamWeatherMap %dx%d, %u zones", Resolution.X, Resolution.Y, NumZones), Shader, Params,
		FComputeShaderUtils::GetGroupCount(Resolution, FDreamWeatherMapCS::ThreadGroupSize));
	GraphBuilder.SetTextureAccessFinal(Target, ERHIAccess::SRVMask);
	DrawCount.fetch_add(1);
}
