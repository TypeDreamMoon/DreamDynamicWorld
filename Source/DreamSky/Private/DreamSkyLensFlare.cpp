// Copyright DreamMoon. All Rights Reserved.

#include "DreamSkyLensFlare.h"

#include "DataDrivenShaderPlatformInfo.h"
#include "GlobalShader.h"
#include "PixelShaderUtils.h"
#include "RenderGraphBuilder.h"
#include "RenderGraphUtils.h"
#include "SceneView.h"
#include "ShaderParameterStruct.h"

class FDreamSkyLensFlareMeasureCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FDreamSkyLensFlareMeasureCS);
	SHADER_USE_PARAMETER_STRUCT(FDreamSkyLensFlareMeasureCS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_STRUCT_REF(FViewUniformShaderParameters, View)
		SHADER_PARAMETER(FVector4f, SunDirection)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, SceneColorTexture)
		SHADER_PARAMETER_SAMPLER(SamplerState, SceneColorSampler)
		SHADER_PARAMETER(FVector2f, SceneColorInvExtent)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float4>, OutMeasure)
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
	}
};

class FDreamSkyLensFlarePS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FDreamSkyLensFlarePS);
	SHADER_USE_PARAMETER_STRUCT(FDreamSkyLensFlarePS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_STRUCT_REF(FViewUniformShaderParameters, View)
		SHADER_PARAMETER(FVector4f, SunDirection)
		SHADER_PARAMETER(FVector4f, Flare)
		SHADER_PARAMETER(FVector4f, FlareTint)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float4>, Measure)
		RENDER_TARGET_BINDING_SLOTS()
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
	}
};

IMPLEMENT_GLOBAL_SHADER(FDreamSkyLensFlareMeasureCS, "/Plugin/DreamDynamicWorld/Private/DreamSkyLensFlare.usf", "MeasureCS", SF_Compute);
IMPLEMENT_GLOBAL_SHADER(FDreamSkyLensFlarePS, "/Plugin/DreamDynamicWorld/Private/DreamSkyLensFlare.usf", "FlarePS", SF_Pixel);

void DreamSky::LensFlare::AddPasses(FRDGBuilder& GraphBuilder, const FSceneView& View, const FScreenPassTexture& SceneColor, const FSettings& Settings)
{
	if (!Settings.bEnabled || !SceneColor.IsValid())
	{
		return;
	}
	RDG_EVENT_SCOPE(GraphBuilder, "DreamSky.LensFlare");
	FGlobalShaderMap* ShaderMap = GetGlobalShaderMap(View.GetFeatureLevel());
	FRDGTextureRef Measure = GraphBuilder.CreateTexture(FRDGTextureDesc::Create2D(FIntPoint(1, 1), PF_A32B32G32R32F, FClearValueBinding::None,
		TexCreate_ShaderResource | TexCreate_UAV), TEXT("DreamSky.LensFlareMeasure"));
	{
		FDreamSkyLensFlareMeasureCS::FParameters* Params = GraphBuilder.AllocParameters<FDreamSkyLensFlareMeasureCS::FParameters>();
		Params->View = View.ViewUniformBuffer;
		Params->SunDirection = Settings.SunDirection;
		Params->SceneColorTexture = SceneColor.Texture;
		Params->SceneColorSampler = TStaticSamplerState<SF_Bilinear, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();
		const FIntPoint Extent = SceneColor.Texture->Desc.Extent;
		Params->SceneColorInvExtent = FVector2f(1.0f / Extent.X, 1.0f / Extent.Y);
		Params->OutMeasure = GraphBuilder.CreateUAV(Measure);
		TShaderMapRef<FDreamSkyLensFlareMeasureCS> Shader(ShaderMap);
		FComputeShaderUtils::AddPass(GraphBuilder, RDG_EVENT_NAME("Measure"), Shader, Params, FIntVector(1, 1, 1));
	}
	FDreamSkyLensFlarePS::FParameters* Params = GraphBuilder.AllocParameters<FDreamSkyLensFlarePS::FParameters>();
	Params->View = View.ViewUniformBuffer;
	Params->SunDirection = Settings.SunDirection;
	Params->Flare = Settings.Flare;
	Params->FlareTint = Settings.Tint;
	Params->Measure = Measure;
	Params->RenderTargets[0] = FRenderTargetBinding(SceneColor.Texture, ERenderTargetLoadAction::ELoad);
	TShaderMapRef<FDreamSkyLensFlarePS> Shader(ShaderMap);
	FPixelShaderUtils::AddFullscreenPass(GraphBuilder, ShaderMap, RDG_EVENT_NAME("Flare"), Shader, Params, SceneColor.ViewRect,
		TStaticBlendState<CW_RGB, BO_Add, BF_One, BF_One>::GetRHI());
}
