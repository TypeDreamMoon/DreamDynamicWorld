// Copyright DreamMoon. All Rights Reserved.

#include "DreamSkyCloudRenderer.h"
#include "DreamSky.h"
#include "DreamSkySubsystem.h"
#include "DreamWorldSubsystem.h"

#include "DataDrivenShaderPlatformInfo.h"
#include "Engine/Texture.h"
#include "Engine/World.h"
#include "GlobalShader.h"
#include "Materials/MaterialParameterCollection.h"
#include "Materials/MaterialParameterCollectionInstance.h"
#include "PixelShaderUtils.h"
#include "RenderGraphBuilder.h"
#include "RenderGraphUtils.h"
#include "SceneView.h"
#include "ShaderParameterStruct.h"
#include "SystemTextures.h"
#include "Engine/TextureRenderTarget2D.h"
#include "TextureResource.h"

static TAutoConsoleVariable<int32> CVarDreamSkyCloudRenderDebug(
	TEXT("DreamSky.CloudRender.Debug"), 0,
	TEXT("1 = multiply the sky pixels by a grid of view directions where the sky cloud renderer composites its clouds")
	TEXT(" (right after the base pass: lighting, fog and translucency land on top of it). 0 = off."),
	ECVF_RenderThreadSafe);

static TAutoConsoleVariable<int32> CVarDreamSkyCloudRenderLook(
	TEXT("DreamSky.CloudRender.Look"), 0,
	TEXT("How the sky cloud renderer draws the clouds: 0 = toon (the surface, lit in the style's tones), 1 = realistic (the")
	TEXT(" density integrated along the view, lit by scattering)."),
	ECVF_RenderThreadSafe);

static TAutoConsoleVariable<int32> CVarDreamSkyCloudRenderDownsample(
	TEXT("DreamSky.CloudRender.Downsample"), 2,
	TEXT("The sky cloud renderer marches one ray per this many pixels across and down (1..4)."),
	ECVF_RenderThreadSafe);

static TAutoConsoleVariable<int32> CVarDreamSkyCloudRenderSamples(
	TEXT("DreamSky.CloudRender.Samples"), 160,
	TEXT("The most samples of the cloud field a ray of the sky cloud renderer takes (16..512)."),
	ECVF_RenderThreadSafe);

static TAutoConsoleVariable<float> CVarDreamSkyCloudRenderHistory(
	TEXT("DreamSky.CloudRender.History"), 0.9f,
	TEXT("How much of the previous frames the sky cloud renderer keeps (0 = none: every frame stands alone, its jittered")
	TEXT(" samples show as noise; 0.9 = a tenth of each new frame)."),
	ECVF_RenderThreadSafe);

namespace DreamSkyCloudRender
{
	BEGIN_SHADER_PARAMETER_STRUCT(FCommonParameters, )
		SHADER_PARAMETER_STRUCT_REF(FViewUniformShaderParameters, View)
		SHADER_PARAMETER_ARRAY(FVector4f, Parameters, [FDreamSkyCloudViewExtension::NumParameters])
		SHADER_PARAMETER(FVector3f, CameraWorld)
		SHADER_PARAMETER(float, SkyDepth)
		SHADER_PARAMETER(int32, Downsample)
	END_SHADER_PARAMETER_STRUCT()

	static bool Supported(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
	}
}

class FDreamSkyCloudMarchCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FDreamSkyCloudMarchCS);
	SHADER_USE_PARAMETER_STRUCT(FDreamSkyCloudMarchCS, FGlobalShader);

	class FRealistic : SHADER_PERMUTATION_BOOL("DSKY_REALISTIC");
	using FPermutationDomain = TShaderPermutationDomain<FRealistic>;

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_STRUCT_INCLUDE(DreamSkyCloudRender::FCommonParameters, Common)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, SceneDepth)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, WeatherMap)
		SHADER_PARAMETER_SAMPLER(SamplerState, WeatherMapSampler)
		SHADER_PARAMETER_RDG_TEXTURE(Texture3D, CloudNoise)
		SHADER_PARAMETER_SAMPLER(SamplerState, CloudNoiseSampler)
		SHADER_PARAMETER(int32, MaxSamples)
		SHADER_PARAMETER_RDG_TEXTURE(Texture3D<float>, NearTau)
		SHADER_PARAMETER_RDG_TEXTURE(Texture3D<float>, FarTau)
		SHADER_PARAMETER_SAMPLER(SamplerState, TauSampler)
		SHADER_PARAMETER(FVector4f, NearCenterExtent)
		SHADER_PARAMETER(FVector4f, FarCenterExtent)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float4>, OutColor)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float>, OutDepth)
	END_SHADER_PARAMETER_STRUCT()

	static constexpr int32 GroupSize = 8;

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return DreamSkyCloudRender::Supported(Parameters);
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.SetDefine(TEXT("THREADGROUP_SIZE"), GroupSize);
	}
};

class FDreamSkyCloudSunMapCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FDreamSkyCloudSunMapCS);
	SHADER_USE_PARAMETER_STRUCT(FDreamSkyCloudSunMapCS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_STRUCT_INCLUDE(DreamSkyCloudRender::FCommonParameters, Common)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, WeatherMap)
		SHADER_PARAMETER_SAMPLER(SamplerState, WeatherMapSampler)
		SHADER_PARAMETER_RDG_TEXTURE(Texture3D, CloudNoise)
		SHADER_PARAMETER_SAMPLER(SamplerState, CloudNoiseSampler)
		SHADER_PARAMETER(FVector4f, MapCenterExtent)
		SHADER_PARAMETER(FIntVector, MapResolution)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture3D<float>, OutTau)
	END_SHADER_PARAMETER_STRUCT()

	static constexpr int32 GroupSize = 4;

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return DreamSkyCloudRender::Supported(Parameters);
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.SetDefine(TEXT("THREADGROUP_SIZE"), GroupSize);
	}
};

class FDreamSkyCloudShadowCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FDreamSkyCloudShadowCS);
	SHADER_USE_PARAMETER_STRUCT(FDreamSkyCloudShadowCS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_RDG_TEXTURE(Texture3D<float>, NearTau)
		SHADER_PARAMETER_RDG_TEXTURE(Texture3D<float>, FarTau)
		SHADER_PARAMETER_SAMPLER(SamplerState, TauSampler)
		SHADER_PARAMETER(FVector4f, NearCenterExtent)
		SHADER_PARAMETER(FVector4f, FarCenterExtent)
		SHADER_PARAMETER(FVector4f, ShadowPlacement)
		SHADER_PARAMETER(FIntPoint, ShadowResolution)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float>, OutShadow)
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return DreamSkyCloudRender::Supported(Parameters);
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.SetDefine(TEXT("THREADGROUP_SIZE"), FDreamSkyCloudMarchCS::GroupSize);
	}
};

class FDreamSkyCloudTemporalCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FDreamSkyCloudTemporalCS);
	SHADER_USE_PARAMETER_STRUCT(FDreamSkyCloudTemporalCS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_STRUCT_INCLUDE(DreamSkyCloudRender::FCommonParameters, Common)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float4>, CurrentColor)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float>, CurrentDepth)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float4>, HistoryColor)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float>, HistoryDepth)
		SHADER_PARAMETER_SAMPLER(SamplerState, HistorySampler)
		SHADER_PARAMETER(FVector2f, HistoryInvExtent)
		SHADER_PARAMETER(float, HistoryWeight)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float4>, OutColor)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float>, OutDepth)
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return DreamSkyCloudRender::Supported(Parameters);
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.SetDefine(TEXT("THREADGROUP_SIZE"), FDreamSkyCloudMarchCS::GroupSize);
	}
};

class FDreamSkyCloudCompositePS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FDreamSkyCloudCompositePS);
	SHADER_USE_PARAMETER_STRUCT(FDreamSkyCloudCompositePS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_STRUCT_INCLUDE(DreamSkyCloudRender::FCommonParameters, Common)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, SceneDepth)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float4>, CloudColor)
		SHADER_PARAMETER_SAMPLER(SamplerState, CloudSampler)
		SHADER_PARAMETER(FVector2f, CloudInvExtent)
		SHADER_PARAMETER(uint32, DebugMode)
		RENDER_TARGET_BINDING_SLOTS()
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return DreamSkyCloudRender::Supported(Parameters);
	}
};

IMPLEMENT_GLOBAL_SHADER(FDreamSkyCloudMarchCS, "/Plugin/DreamDynamicWorld/Private/DreamSkyCloudRender.usf", "MarchCS", SF_Compute);
IMPLEMENT_GLOBAL_SHADER(FDreamSkyCloudSunMapCS, "/Plugin/DreamDynamicWorld/Private/DreamSkyCloudRender.usf", "SunMapCS", SF_Compute);
IMPLEMENT_GLOBAL_SHADER(FDreamSkyCloudShadowCS, "/Plugin/DreamDynamicWorld/Private/DreamSkyCloudRender.usf", "ShadowCS", SF_Compute);
IMPLEMENT_GLOBAL_SHADER(FDreamSkyCloudTemporalCS, "/Plugin/DreamDynamicWorld/Private/DreamSkyCloudRender.usf", "TemporalCS", SF_Compute);
IMPLEMENT_GLOBAL_SHADER(FDreamSkyCloudCompositePS, "/Plugin/DreamDynamicWorld/Private/DreamSkyCloudRender.usf", "CompositePS", SF_Pixel);

const TArray<FName>& FDreamSkyCloudViewExtension::GetParameterNames()
{
	static const TArray<FName> Names = {
		TEXT("DDW_SunDir"), TEXT("DDW_SkyZenith"), TEXT("DDW_SkyHorizon"), TEXT("DDW_SkyGround"), TEXT("DDW_SunHalo"),
		TEXT("DDW_Cloud0"), TEXT("DDW_Cloud1"), TEXT("DDW_Cloud2"), TEXT("DDW_Cloud3"), TEXT("DDW_CloudLit"), TEXT("DDW_CloudShadow"),
		TEXT("DDW_MainLight"), TEXT("DDW_MainLightColor"), TEXT("DDW_CloudLayer"), TEXT("DDW_WeatherMap"), TEXT("DDW_SkyExtra"),
		TEXT("DDW_SkyHaze"),
	};
	check(Names.Num() == NumParameters);
	return Names;
}

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
	UDreamSkySubsystem* Sky = Subsystem.Get();
	const ADreamSky* Actor = Sky ? Sky->GetSky() : nullptr;
	// The dome follows the camera within 2 % of its radius; half the radius parts it safely from the scene.
	Settings.SkyDepth = Actor ? Actor->GetEffectiveDomeRadius() * 0.5f : 1.0e30f;

	const UDreamWorldSubsystem* Clock = Sky ? Sky->ClockSubsystem.Get() : nullptr;
	UWorld* ViewWorld = GetWorld();
	UMaterialParameterCollection* Collection = Clock ? Clock->GetParameterCollection() : nullptr;
	if (Actor && Collection && ViewWorld && Actor->GetEffectiveClouds() == EDreamSkyClouds::Rendered)
	{
		if (!Sky->CloudNoise)
		{
			Sky->CloudNoise = LoadObject<UTexture>(nullptr, TEXT("/DreamDynamicWorld/Sky/T_DreamCloudNoise.T_DreamCloudNoise"));
		}
		if (!Sky->WeatherMap)
		{
			Sky->WeatherMap = LoadObject<UTexture>(nullptr, TEXT("/DreamDynamicWorld/Weather/RT_DreamWeatherMap.RT_DreamWeatherMap"));
		}
		Settings.Noise = Sky->CloudNoise ? Sky->CloudNoise->GetResource() : nullptr;
		if (UTextureRenderTarget2D* Shadow = Actor->GetCloudShadowTarget())
		{
			Settings.Shadow = Shadow->GameThread_GetRenderTargetResource();
			Settings.ShadowPlacement = Actor->GetCloudShadowPlacement();
		}
		Settings.Map = Sky->WeatherMap ? Sky->WeatherMap->GetResource() : nullptr;
		const UMaterialParameterCollectionInstance* Instance = ViewWorld->GetParameterCollectionInstance(Collection);
		const TArray<FName>& Names = GetParameterNames();
		for (int32 Index = 0; Index < NumParameters; ++Index)
		{
			FLinearColor Value = FLinearColor::Transparent;
			if (!Instance || !Instance->GetVectorParameterValue(Names[Index], Value))
			{
				if (const FCollectionVectorParameter* Default = Collection->GetVectorParameterByName(Names[Index]))
				{
					Value = Default->DefaultValue;
				}
			}
			Settings.Parameters[Index] = FVector4f(Value.R, Value.G, Value.B, Value.A);
		}
		Settings.bClouds = Settings.Noise && Settings.Map;
		Settings.Look = FMath::Clamp(CVarDreamSkyCloudRenderLook.GetValueOnGameThread(), 0, 1);
		Settings.Downsample = FMath::Clamp(CVarDreamSkyCloudRenderDownsample.GetValueOnGameThread(), 1, 4);
		Settings.MaxSamples = FMath::Clamp(CVarDreamSkyCloudRenderSamples.GetValueOnGameThread(), 16, 512);
		Settings.HistoryWeight = FMath::Clamp(CVarDreamSkyCloudRenderHistory.GetValueOnGameThread(), 0.0f, 0.98f);
	}

	TSharedRef<FDreamSkyCloudViewExtension, ESPMode::ThreadSafe> Self = StaticCastSharedRef<FDreamSkyCloudViewExtension>(AsShared());
	ENQUEUE_RENDER_COMMAND(DreamSkyCloudSettings)([Self, Settings](FRHICommandListImmediate&)
		{
			Self->RenderSettings = Settings;
		});
}

void FDreamSkyCloudViewExtension::PostRenderBasePassDeferred_RenderThread(FRDGBuilder& GraphBuilder, FSceneView& InView, const FRenderTargetBindingSlots& RenderTargets, TRDGUniformBufferRef<FSceneTextureUniformParameters> SceneTextures)
{
	const FFrameSettings& Settings = RenderSettings;
	if ((Settings.DebugMode <= 0 && !Settings.bClouds) || !InView.IsPerspectiveProjection())
	{
		return;
	}
	if (!RenderTargets[0].GetTexture() || !RenderTargets.DepthStencil.GetTexture())
	{
		return;
	}
	RDG_EVENT_SCOPE(GraphBuilder, "DreamSky.Clouds");
	RenderClouds(GraphBuilder, InView, RenderTargets);
	// Views that stopped drawing (closed viewports, finished captures) let go of their history.
	if ((GFrameCounterRenderThread & 255) == 0)
	{
		const uint64 Now = GFrameCounterRenderThread;
		for (auto It = Histories.CreateIterator(); It; ++It)
		{
			if (!It.Value().IsValid() || It.Value()->LastFrame + 300 < Now)
			{
				It.RemoveCurrent();
			}
		}
	}
}

void FDreamSkyCloudViewExtension::RenderClouds(FRDGBuilder& GraphBuilder, FSceneView& InView, const FRenderTargetBindingSlots& RenderTargets)
{
	const FFrameSettings& Settings = RenderSettings;
	FRDGTextureRef SceneColor = RenderTargets[0].GetTexture();
	FRDGTextureRef SceneDepth = RenderTargets.DepthStencil.GetTexture();
	FGlobalShaderMap* ShaderMap = GetGlobalShaderMap(InView.GetFeatureLevel());
	const FIntPoint Extent = SceneColor->Desc.Extent;
	const int32 Downsample = Settings.Downsample;
	// (The view's rectangle inside the scene textures is only known to the renderer: the passes cover the whole texture,
	// the shaders keep to the rectangle the view's uniform buffer gives.)
	const FIntPoint Half = FIntPoint::DivideAndRoundUp(Extent, Downsample);

	auto FillCommon = [&](DreamSkyCloudRender::FCommonParameters& Common)
	{
		Common.View = InView.ViewUniformBuffer;
		for (int32 Index = 0; Index < NumParameters; ++Index)
		{
			Common.Parameters[Index] = Settings.Parameters[Index];
		}
		Common.CameraWorld = FVector3f(InView.ViewLocation);
		Common.SkyDepth = Settings.SkyDepth;
		Common.Downsample = Downsample;
	};

	FRDGTextureRef Clouds = nullptr;
	if (Settings.bClouds)
	{
		const FRDGTextureDesc ColorDesc = FRDGTextureDesc::Create2D(Half, PF_FloatRGBA, FClearValueBinding::None, TexCreate_ShaderResource | TexCreate_UAV);
		const FRDGTextureDesc DepthDesc = FRDGTextureDesc::Create2D(Half, PF_R32_FLOAT, FClearValueBinding::None, TexCreate_ShaderResource | TexCreate_UAV);
		FRDGTextureRef MarchColor = GraphBuilder.CreateTexture(ColorDesc, TEXT("DreamSky.CloudMarch"));
		FRDGTextureRef MarchDepth = GraphBuilder.CreateTexture(DepthDesc, TEXT("DreamSky.CloudMarchDepth"));
		FRDGTextureRef WeatherMap = RegisterExternalTexture(GraphBuilder, Settings.Map->TextureRHI, TEXT("DreamSky.WeatherMap"));
		FRDGTextureRef CloudNoise = RegisterExternalTexture(GraphBuilder, Settings.Noise->TextureRHI, TEXT("DreamSky.CloudNoise"));
		UpdateSunMaps(GraphBuilder, InView, ShaderMap, WeatherMap, CloudNoise, FillCommon);

		{
			FDreamSkyCloudMarchCS::FParameters* Params = GraphBuilder.AllocParameters<FDreamSkyCloudMarchCS::FParameters>();
			FillCommon(Params->Common);
			Params->SceneDepth = SceneDepth;
			Params->WeatherMap = WeatherMap;
			Params->WeatherMapSampler = TStaticSamplerState<SF_Bilinear, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();
			Params->CloudNoise = CloudNoise;
			Params->CloudNoiseSampler = TStaticSamplerState<SF_Trilinear, AM_Wrap, AM_Wrap, AM_Wrap>::GetRHI();
			Params->MaxSamples = Settings.MaxSamples;
			Params->NearTau = SunMaps[0].Current;
			Params->FarTau = SunMaps[1].Current;
			Params->TauSampler = TStaticSamplerState<SF_Bilinear, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();
			Params->NearCenterExtent = SunMaps[0].CenterExtent;
			Params->FarCenterExtent = SunMaps[1].CenterExtent;
			Params->OutColor = GraphBuilder.CreateUAV(MarchColor);
			Params->OutDepth = GraphBuilder.CreateUAV(MarchDepth);
			FDreamSkyCloudMarchCS::FPermutationDomain Permutation;
			Permutation.Set<FDreamSkyCloudMarchCS::FRealistic>(Settings.Look == 1);
			TShaderMapRef<FDreamSkyCloudMarchCS> Shader(ShaderMap, Permutation);
			FComputeShaderUtils::AddPass(GraphBuilder, RDG_EVENT_NAME("March %dx%d (1/%d)", Half.X, Half.Y, Downsample), Shader, Params,
				FComputeShaderUtils::GetGroupCount(Half, FDreamSkyCloudMarchCS::GroupSize));
		}

		// The previous frames, where this view has any (scene captures without a view state have none).
		const uint32 Key = InView.GetViewKey();
		TSharedPtr<FHistory> History;
		if (Key != 0 && Settings.HistoryWeight > 0.0f)
		{
			TSharedPtr<FHistory>& Slot = Histories.FindOrAdd(Key);
			if (!Slot.IsValid())
			{
				Slot = MakeShared<FHistory>();
			}
			History = Slot;
		}
		const FIntPoint ViewSize = InView.UnscaledViewRect.Size();
		const bool bHistory = History.IsValid() && History->Color.IsValid() && History->Depth.IsValid() && History->LastFrame + 2 >= GFrameCounterRenderThread
			&& History->ViewSize == ViewSize && History->Downsample == Downsample && History->Look == Settings.Look
			&& History->Color->GetDesc().Extent == Half;

		FRDGTextureRef OutColor = GraphBuilder.CreateTexture(ColorDesc, TEXT("DreamSky.Clouds"));
		FRDGTextureRef OutDepth = GraphBuilder.CreateTexture(DepthDesc, TEXT("DreamSky.CloudsDepth"));
		{
			FDreamSkyCloudTemporalCS::FParameters* Params = GraphBuilder.AllocParameters<FDreamSkyCloudTemporalCS::FParameters>();
			FillCommon(Params->Common);
			Params->CurrentColor = MarchColor;
			Params->CurrentDepth = MarchDepth;
			Params->HistoryColor = bHistory ? GraphBuilder.RegisterExternalTexture(History->Color) : MarchColor;
			Params->HistoryDepth = bHistory ? GraphBuilder.RegisterExternalTexture(History->Depth) : MarchDepth;
			Params->HistorySampler = TStaticSamplerState<SF_Bilinear, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();
			Params->HistoryInvExtent = FVector2f(1.0f / Half.X, 1.0f / Half.Y);
			Params->HistoryWeight = bHistory ? Settings.HistoryWeight : 0.0f;
			Params->OutColor = GraphBuilder.CreateUAV(OutColor);
			Params->OutDepth = GraphBuilder.CreateUAV(OutDepth);
			TShaderMapRef<FDreamSkyCloudTemporalCS> Shader(ShaderMap);
			FComputeShaderUtils::AddPass(GraphBuilder, RDG_EVENT_NAME("Temporal%s", bHistory ? TEXT("") : TEXT(" (no history)")), Shader, Params,
				FComputeShaderUtils::GetGroupCount(Half, FDreamSkyCloudMarchCS::GroupSize));
		}
		if (History.IsValid())
		{
			GraphBuilder.QueueTextureExtraction(OutColor, &History->Color);
			GraphBuilder.QueueTextureExtraction(OutDepth, &History->Depth);
			History->ViewSize = ViewSize;
			History->Downsample = Downsample;
			History->Look = Settings.Look;
			History->LastFrame = GFrameCounterRenderThread;
		}
		Clouds = OutColor;
	}

	FDreamSkyCloudCompositePS::FParameters* Params = GraphBuilder.AllocParameters<FDreamSkyCloudCompositePS::FParameters>();
	FillCommon(Params->Common);
	Params->SceneDepth = SceneDepth;
	Params->CloudColor = Clouds ? Clouds : GSystemTextures.GetBlackDummy(GraphBuilder);
	Params->CloudSampler = TStaticSamplerState<SF_Bilinear, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();
	Params->CloudInvExtent = FVector2f(1.0f / Half.X, 1.0f / Half.Y);
	Params->DebugMode = Clouds ? 0u : uint32(Settings.DebugMode);
	Params->RenderTargets[0] = FRenderTargetBinding(SceneColor, ERenderTargetLoadAction::ELoad);
	const FIntRect Whole(FIntPoint::ZeroValue, Extent);
	TShaderMapRef<FDreamSkyCloudCompositePS> Shader(ShaderMap);
	// Clouds: over the sky behind them (premultiplied: the sky times their transmittance plus their light). The direction
	// grid: multiplied in.
	FRHIBlendState* Blend = Clouds ? TStaticBlendState<CW_RGB, BO_Add, BF_One, BF_SourceAlpha>::GetRHI() : TStaticBlendState<CW_RGB, BO_Add, BF_DestColor, BF_Zero>::GetRHI();
	FPixelShaderUtils::AddFullscreenPass(GraphBuilder, ShaderMap, RDG_EVENT_NAME("Composite"), Shader, Params, Whole, Blend);
}

void FDreamSkyCloudViewExtension::UpdateSunMaps(FRDGBuilder& GraphBuilder, const FSceneView& InView, FGlobalShaderMap* ShaderMap,
	FRDGTextureRef WeatherMap, FRDGTextureRef CloudNoise, TFunctionRef<void(DreamSkyCloudRender::FCommonParameters&)> FillCommon)
{
	// Once per frame (the first view draws them; the others read them), one cascade a frame: the near one on even
	// frames, the far one on odd ones, either whenever it has never been drawn.
	const uint64 Frame = GFrameCounterRenderThread;
	if (SunMapFrame == Frame && SunMapBuilder == &GraphBuilder)
	{
		return;
	}
	SunMapFrame = Frame;
	SunMapBuilder = &GraphBuilder;
	for (int32 Cascade = 0; Cascade < 2; ++Cascade)
	{
		FSunMap& Map = SunMaps[Cascade];
		const bool bDraw = !Map.Texture.IsValid() || int32(Frame & 1) == Cascade;
		if (!bDraw)
		{
			Map.Current = GraphBuilder.RegisterExternalTexture(Map.Texture);
			continue;
		}
		const FIntVector Resolution = Cascade == 0 ? FIntVector(96, 96, 24) : FIntVector(64, 64, 16);
		const float HalfExtent = Cascade == 0 ? 2400000.0f : 9000000.0f;
		// (Centred on the view, snapped to its texels: the clouds' shadows do not swim as the view moves.)
		const double Texel = 2.0 * HalfExtent / Resolution.X;
		const FVector2D Centre(FMath::RoundToDouble(InView.ViewLocation.X / Texel) * Texel, FMath::RoundToDouble(InView.ViewLocation.Y / Texel) * Texel);
		Map.CenterExtent = FVector4f(float(Centre.X), float(Centre.Y), HalfExtent, 1.0f);

		FRDGTextureRef Tau = GraphBuilder.CreateTexture(FRDGTextureDesc::Create3D(Resolution, PF_R16F, FClearValueBinding::None,
			TexCreate_ShaderResource | TexCreate_UAV), Cascade == 0 ? TEXT("DreamSky.SunMapNear") : TEXT("DreamSky.SunMapFar"));
		FDreamSkyCloudSunMapCS::FParameters* Params = GraphBuilder.AllocParameters<FDreamSkyCloudSunMapCS::FParameters>();
		FillCommon(Params->Common);
		Params->WeatherMap = WeatherMap;
		Params->WeatherMapSampler = TStaticSamplerState<SF_Bilinear, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();
		Params->CloudNoise = CloudNoise;
		Params->CloudNoiseSampler = TStaticSamplerState<SF_Trilinear, AM_Wrap, AM_Wrap, AM_Wrap>::GetRHI();
		Params->MapCenterExtent = Map.CenterExtent;
		Params->MapResolution = Resolution;
		Params->OutTau = GraphBuilder.CreateUAV(Tau);
		TShaderMapRef<FDreamSkyCloudSunMapCS> Shader(ShaderMap);
		FComputeShaderUtils::AddPass(GraphBuilder, RDG_EVENT_NAME("SunMap %s %dx%dx%d", Cascade == 0 ? TEXT("near") : TEXT("far"), Resolution.X, Resolution.Y, Resolution.Z),
			Shader, Params, FComputeShaderUtils::GetGroupCount(Resolution, FDreamSkyCloudSunMapCS::GroupSize));
		GraphBuilder.QueueTextureExtraction(Tau, &Map.Texture);
		Map.Current = Tau;
	}

	// The clouds' shadow on the ground: the sun map's transmittance from the layer's base toward the sun, into the texture
	// the sun light's light function reads (ADreamSky::UpdateCloudShadow).
	const FFrameSettings& Settings = RenderSettings;
	FRHITexture* ShadowRHI = Settings.Shadow ? Settings.Shadow->TextureRHI.GetReference() : nullptr;
	if (ShadowRHI && EnumHasAnyFlags(ShadowRHI->GetFlags(), TexCreate_UAV))
	{
		FRDGTextureRef Shadow = RegisterExternalTexture(GraphBuilder, ShadowRHI, TEXT("DreamSky.CloudShadow"));
		const FIntPoint Resolution(ShadowRHI->GetSizeXYZ().X, ShadowRHI->GetSizeXYZ().Y);
		FDreamSkyCloudShadowCS::FParameters* Params = GraphBuilder.AllocParameters<FDreamSkyCloudShadowCS::FParameters>();
		Params->NearTau = SunMaps[0].Current;
		Params->FarTau = SunMaps[1].Current;
		Params->TauSampler = TStaticSamplerState<SF_Bilinear, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();
		Params->NearCenterExtent = SunMaps[0].CenterExtent;
		Params->FarCenterExtent = SunMaps[1].CenterExtent;
		Params->ShadowPlacement = Settings.ShadowPlacement;
		Params->ShadowResolution = Resolution;
		Params->OutShadow = GraphBuilder.CreateUAV(Shadow);
		TShaderMapRef<FDreamSkyCloudShadowCS> Shader(ShaderMap);
		FComputeShaderUtils::AddPass(GraphBuilder, RDG_EVENT_NAME("GroundShadow %dx%d", Resolution.X, Resolution.Y), Shader, Params,
			FComputeShaderUtils::GetGroupCount(Resolution, FDreamSkyCloudMarchCS::GroupSize));
		GraphBuilder.SetTextureAccessFinal(Shadow, ERHIAccess::SRVMask);
	}
}
