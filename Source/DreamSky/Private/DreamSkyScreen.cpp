// Copyright DreamMoon. All Rights Reserved.

#include "DreamSkyScreen.h"

#include "DataDrivenShaderPlatformInfo.h"
#include "GlobalShader.h"
#include "PixelShaderUtils.h"
#include "PostProcess/PostProcessMaterialInputs.h"
#include "RenderGraphBuilder.h"
#include "RenderGraphUtils.h"
#include "SceneView.h"
#include "SceneTexturesConfig.h"
#include "ShaderParameterStruct.h"
#include "SystemTextures.h"

class FDreamSkyScreenPS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FDreamSkyScreenPS);
	SHADER_USE_PARAMETER_STRUCT(FDreamSkyScreenPS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER(FVector4f, Screen)
		SHADER_PARAMETER(FVector4f, Frost)
		SHADER_PARAMETER(FVector4f, FrostTint)
		SHADER_PARAMETER(FVector4f, Haze)
		SHADER_PARAMETER(FVector4f, HazeMotion)
		SHADER_PARAMETER_STRUCT_REF(FViewUniformShaderParameters, View)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, SceneDepth)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, InputTexture)
		SHADER_PARAMETER_SAMPLER(SamplerState, InputSampler)
		SHADER_PARAMETER(FVector2f, InputInvExtent)
		SHADER_PARAMETER(FVector2f, InputRectMin)
		SHADER_PARAMETER(FVector2f, InputRectSize)
		SHADER_PARAMETER(FVector2f, OutputRectMin)
		SHADER_PARAMETER(FVector2f, OutputRectSize)
		SHADER_PARAMETER(float, Seconds)
		RENDER_TARGET_BINDING_SLOTS()
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
	}
};

IMPLEMENT_GLOBAL_SHADER(FDreamSkyScreenPS, "/Plugin/DreamDynamicWorld/Private/DreamSkyScreen.usf", "ScreenPS", SF_Pixel);

FScreenPassTexture DreamSky::Screen::AddPasses(FRDGBuilder& GraphBuilder, const FSceneView& View, const FPostProcessMaterialInputs& Inputs, const FSettings& Settings)
{
	FScreenPassTexture SceneColor = FScreenPassTexture::CopyFromSlice(GraphBuilder, Inputs.GetInput(EPostProcessMaterialInput::SceneColor));
	if (!Settings.bEnabled || !SceneColor.IsValid())
	{
		return SceneColor;
	}
	// (Into the frame's own output when this is the last pass, else a texture like the input.)
	FScreenPassRenderTarget Output = Inputs.OverrideOutput;
	if (!Output.IsValid())
	{
		Output = FScreenPassRenderTarget::CreateFromInput(GraphBuilder, SceneColor, ERenderTargetLoadAction::ENoAction, TEXT("DreamSky.Screen"));
	}
	RDG_EVENT_SCOPE(GraphBuilder, "DreamSky.Screen");
	FGlobalShaderMap* ShaderMap = GetGlobalShaderMap(View.GetFeatureLevel());
	FDreamSkyScreenPS::FParameters* Params = GraphBuilder.AllocParameters<FDreamSkyScreenPS::FParameters>();
	Params->Screen = Settings.Screen;
	Params->Frost = Settings.Frost;
	Params->FrostTint = Settings.FrostTint;
	Params->Haze = Settings.Haze;
	Params->HazeMotion = Settings.HazeMotion;
	Params->View = View.ViewUniformBuffer;
	// (The depth at the render's own resolution: the shimmer reads it through the view's rect.)
	FRDGTextureRef Depth = Inputs.SceneTextures.SceneTextures ? Inputs.SceneTextures.SceneTextures->GetParameters()->SceneDepthTexture : nullptr;
	if (!Depth || !View.IsPerspectiveProjection())
	{
		Depth = GSystemTextures.GetDepthDummy(GraphBuilder);
		Params->Haze.X = 0.0f;
	}
	Params->SceneDepth = Depth;
	Params->InputTexture = SceneColor.Texture;
	Params->InputSampler = TStaticSamplerState<SF_Bilinear, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();
	const FIntPoint Extent = SceneColor.Texture->Desc.Extent;
	Params->InputInvExtent = FVector2f(1.0f / Extent.X, 1.0f / Extent.Y);
	Params->InputRectMin = FVector2f(SceneColor.ViewRect.Min);
	Params->InputRectSize = FVector2f(SceneColor.ViewRect.Size());
	Params->OutputRectMin = FVector2f(Output.ViewRect.Min);
	Params->OutputRectSize = FVector2f(Output.ViewRect.Size());
	// (The world's time: the drops stand still with the game paused.)
	Params->Seconds = View.Family ? float(FMath::Fmod(View.Family->Time.GetWorldTimeSeconds(), 3600.0)) : 0.0f;
	Params->RenderTargets[0] = Output.GetRenderTargetBinding();
	TShaderMapRef<FDreamSkyScreenPS> Shader(ShaderMap);
	FPixelShaderUtils::AddFullscreenPass(GraphBuilder, ShaderMap, RDG_EVENT_NAME("Screen"), Shader, Params, Output.ViewRect);
	return FScreenPassTexture(Output);
}
