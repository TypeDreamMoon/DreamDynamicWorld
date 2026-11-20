// Copyright DreamMoon. All Rights Reserved.

#include "DreamSkyCloudRenderer.h"
#include "DreamSky.h"
#include "DreamSkySubsystem.h"

#include "DataDrivenShaderPlatformInfo.h"
#include "GlobalShader.h"
#include "PixelShaderUtils.h"
#include "RenderGraphBuilder.h"
#include "SceneView.h"
#include "ShaderParameterStruct.h"

static TAutoConsoleVariable<int32> CVarDreamSkyCloudRenderDebug(
	TEXT("DreamSky.CloudRender.Debug"), 0,
	TEXT("1 = multiply the sky pixels by a grid of view directions where the sky cloud renderer composites its clouds")
	TEXT(" (right after the base pass: lighting, fog and translucency land on top of it). 0 = off."),
	ECVF_RenderThreadSafe);

class FDreamSkyCloudCompositePS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FDreamSkyCloudCompositePS);
	SHADER_USE_PARAMETER_STRUCT(FDreamSkyCloudCompositePS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_STRUCT_REF(FViewUniformShaderParameters, View)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, SceneDepth)
		SHADER_PARAMETER(float, SkyDepth)
		SHADER_PARAMETER(uint32, DebugMode)
		RENDER_TARGET_BINDING_SLOTS()
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
	}
};

IMPLEMENT_GLOBAL_SHADER(FDreamSkyCloudCompositePS, "/Plugin/DreamDynamicWorld/Private/DreamSkyCloudComposite.usf", "MainPS", SF_Pixel);

FDreamSkyCloudViewExtension::FDreamSkyCloudViewExtension(const FAutoRegister& AutoRegister, UWorld* InWorld, UDreamSkySubsystem* InSubsystem)
	: FWorldSceneViewExtension(AutoRegister, InWorld)
	, Subsystem(InSubsystem)
{
}

void FDreamSkyCloudViewExtension::SetupViewFamily(FSceneViewFamily& InViewFamily)
{
	// Game thread, for every family of the world (scene captures included, which skip BeginRenderViewFamily).
	FFrameSettings Settings;
	Settings.DebugMode = CVarDreamSkyCloudRenderDebug.GetValueOnGameThread();
	const UDreamSkySubsystem* Sky = Subsystem.Get();
	const ADreamSky* Actor = Sky ? Sky->GetSky() : nullptr;
	// The dome follows the camera within 2 % of its radius; half the radius parts it safely from the scene.
	Settings.SkyDepth = Actor ? Actor->GetEffectiveDomeRadius() * 0.5f : 1.0e30f;

	TSharedRef<FDreamSkyCloudViewExtension, ESPMode::ThreadSafe> Self = StaticCastSharedRef<FDreamSkyCloudViewExtension>(AsShared());
	ENQUEUE_RENDER_COMMAND(DreamSkyCloudSettings)([Self, Settings](FRHICommandListImmediate&)
		{
			Self->RenderSettings = Settings;
		});
}

void FDreamSkyCloudViewExtension::PostRenderBasePassDeferred_RenderThread(FRDGBuilder& GraphBuilder, FSceneView& InView, const FRenderTargetBindingSlots& RenderTargets, TRDGUniformBufferRef<FSceneTextureUniformParameters> SceneTextures)
{
	const FFrameSettings& Settings = RenderSettings;
	if (Settings.DebugMode <= 0 || !InView.IsPerspectiveProjection())
	{
		return;
	}
	FRDGTextureRef SceneColor = RenderTargets[0].GetTexture();
	FRDGTextureRef SceneDepth = RenderTargets.DepthStencil.GetTexture();
	if (!SceneColor || !SceneDepth)
	{
		return;
	}

	FDreamSkyCloudCompositePS::FParameters* Params = GraphBuilder.AllocParameters<FDreamSkyCloudCompositePS::FParameters>();
	Params->View = InView.ViewUniformBuffer;
	Params->SceneDepth = SceneDepth;
	Params->SkyDepth = Settings.SkyDepth;
	Params->DebugMode = uint32(Settings.DebugMode);
	Params->RenderTargets[0] = FRenderTargetBinding(SceneColor, ERenderTargetLoadAction::ELoad);

	// The view's rectangle inside the scene textures is only known to the renderer; the shader keeps to it.
	const FIntRect Whole(FIntPoint::ZeroValue, SceneColor->Desc.Extent);
	TShaderMapRef<FDreamSkyCloudCompositePS> Shader(GetGlobalShaderMap(InView.GetFeatureLevel()));
	FPixelShaderUtils::AddFullscreenPass(GraphBuilder, GetGlobalShaderMap(InView.GetFeatureLevel()), RDG_EVENT_NAME("DreamSky.CloudComposite (debug %d)", Settings.DebugMode),
		Shader, Params, Whole, TStaticBlendState<CW_RGB, BO_Add, BF_DestColor, BF_Zero>::GetRHI());
}
