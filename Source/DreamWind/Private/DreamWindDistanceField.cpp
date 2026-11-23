// Copyright DreamMoon. All Rights Reserved.

#include "DreamWindDistanceField.h"
#include "DreamWindDistanceProbe.h"
#include "DreamWindModule.h"

#include "DataDrivenShaderPlatformInfo.h"
#include "GlobalShader.h"
#include "HAL/IConsoleManager.h"
#include "RenderGraphBuilder.h"
#include "RenderGraphUtils.h"
#include "RHIGlobals.h"
#include "SceneView.h"
#include "ShaderParameterStruct.h"
#include "TextureResource.h"

namespace DreamWindDistanceFieldImpl
{
	int32 RequestCount = 0;
	int32 SavedNecessityCheck = 1;
	int32 SavedNecessitySufficient = 0;

	// The renderer prepares the global distance field when its necessity check passes (r.AOGlobalDistanceField.DetailedNecessityCheck
	// 0 makes it pass always) AND either that alone is declared enough (r.GlobalDistanceField.DetailedNecessitySufficient 1)
	// or distance field AO is supported: with both set, neither Lumen (which with hardware ray tracing never asks for it) nor
	// distance field AO has to be on. See ShouldPrepareGlobalDistanceField in the renderer.
	IConsoleVariable* NecessityCheck()
	{
		return IConsoleManager::Get().FindConsoleVariable(TEXT("r.AOGlobalDistanceField.DetailedNecessityCheck"));
	}

	IConsoleVariable* NecessitySufficient()
	{
		return IConsoleManager::Get().FindConsoleVariable(TEXT("r.GlobalDistanceField.DetailedNecessitySufficient"));
	}
}

class FDreamWindDistanceProbeCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FDreamWindDistanceProbeCS);
	SHADER_USE_PARAMETER_STRUCT(FDreamWindDistanceProbeCS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_STRUCT_REF(FViewUniformShaderParameters, View)
		SHADER_PARAMETER(FVector3f, TranslatedCenter)
		SHADER_PARAMETER(float, Size)
		SHADER_PARAMETER(FIntPoint, Resolution)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float4>, OutSlice)
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5) && DoesPlatformSupportDistanceFields(Parameters.Platform);
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		// The global distance field's textures come with the view's uniform buffer.
		OutEnvironment.SetDefine(TEXT("DISTANCE_FIELD_IN_VIEW_UB"), 1);
	}
};

IMPLEMENT_GLOBAL_SHADER(FDreamWindDistanceProbeCS, "/Plugin/DreamDynamicWorld/Private/DreamWindDistanceProbe.usf", "MainCS", SF_Compute);

bool DreamWind::DistanceField::IsSupported()
{
	static const IConsoleVariable* Generate = IConsoleManager::Get().FindConsoleVariable(TEXT("r.GenerateMeshDistanceFields"));
	return Generate && Generate->GetInt() != 0 && DoesPlatformSupportDistanceFields(GMaxRHIShaderPlatform);
}

void DreamWind::DistanceField::Request(bool bRequest)
{
	using namespace DreamWindDistanceFieldImpl;
	IConsoleVariable* Check = NecessityCheck();
	IConsoleVariable* Sufficient = NecessitySufficient();
	if (bRequest)
	{
		if (RequestCount++ == 0 && Check && Sufficient)
		{
			SavedNecessityCheck = Check->GetInt();
			SavedNecessitySufficient = Sufficient->GetInt();
			Check->Set(0, ECVF_SetByCode);
			Sufficient->Set(1, ECVF_SetByCode);
			UE_LOG(LogDreamWind, Log, TEXT("Global distance field requested for the wind (r.AOGlobalDistanceField.DetailedNecessityCheck 0, r.GlobalDistanceField.DetailedNecessitySufficient 1)."));
		}
	}
	else if (RequestCount > 0 && --RequestCount == 0 && Check && Sufficient)
	{
		Check->Set(SavedNecessityCheck, ECVF_SetByCode);
		Sufficient->Set(SavedNecessitySufficient, ECVF_SetByCode);
	}
}

bool DreamWind::DistanceField::IsRequested()
{
	return DreamWindDistanceFieldImpl::RequestCount > 0;
}

static FAutoConsoleCommand GDreamWindDistanceFieldCommand(
	TEXT("DreamWind.DistanceField"),
	TEXT("DreamWind.DistanceField <1|0>: ask the renderer to keep the global distance field up to date for the wind (1), or stop asking (0)."),
	FConsoleCommandWithArgsDelegate::CreateLambda([](const TArray<FString>& Args)
	{
		const bool bOn = Args.Num() == 0 || FCString::Atoi(*Args[0]) != 0;
		if (bOn != DreamWind::DistanceField::IsRequested())
		{
			DreamWind::DistanceField::Request(bOn);
		}
		UE_LOG(LogDreamWind, Display, TEXT("Global distance field for the wind: %s (supported here: %s)"), bOn ? TEXT("requested") : TEXT("not requested"),
			DreamWind::DistanceField::IsSupported() ? TEXT("yes") : TEXT("no"));
	}));

void DreamWind::DistanceField::AddProbePass(FRDGBuilder& GraphBuilder, const FSceneView& View, const FProbeRequest& Request)
{
	FRHITexture* TargetRHI = Request.Target ? Request.Target->GetRenderTargetTexture() : nullptr;
	if (!TargetRHI || !EnumHasAnyFlags(TargetRHI->GetFlags(), TexCreate_UAV) || !DoesPlatformSupportDistanceFields(View.GetShaderPlatform()))
	{
		return;
	}
	FRDGTextureRef Target = RegisterExternalTexture(GraphBuilder, TargetRHI, TEXT("DreamWind.DistanceProbe"));
	auto* Params = GraphBuilder.AllocParameters<FDreamWindDistanceProbeCS::FParameters>();
	Params->View = View.ViewUniformBuffer;
	Params->TranslatedCenter = FVector3f(Request.Center + View.ViewMatrices.GetPreViewTranslation());
	Params->Size = Request.Size;
	Params->Resolution = Request.Resolution;
	Params->OutSlice = GraphBuilder.CreateUAV(Target);
	FComputeShaderUtils::AddPass(GraphBuilder, RDG_EVENT_NAME("DreamWind.DistanceProbe %dx%d", Request.Resolution.X, Request.Resolution.Y),
		TShaderMapRef<FDreamWindDistanceProbeCS>(GetGlobalShaderMap(View.GetFeatureLevel())), Params,
		FComputeShaderUtils::GetGroupCount(Request.Resolution, 8));
}
