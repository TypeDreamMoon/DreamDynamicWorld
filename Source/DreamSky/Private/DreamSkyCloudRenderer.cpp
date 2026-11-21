// Copyright DreamMoon. All Rights Reserved.

#include "DreamSkyCloudRenderer.h"
#include "DreamSky.h"
#include "DreamSkyModule.h"
#include "DreamSkySubsystem.h"
#include "DreamWorldSubsystem.h"

#include "DataDrivenShaderPlatformInfo.h"
#include "Engine/Texture.h"
#include "Engine/World.h"
#include "GlobalShader.h"
#include "Materials/MaterialParameterCollection.h"
#include "Materials/MaterialParameterCollectionInstance.h"
#include "PixelShaderUtils.h"
#include "PostProcess/PostProcessMaterialInputs.h"
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
	TEXT(" (right after the base pass: lighting, fog and translucency land on top of it); 2 = the clouds' rays that found")
	TEXT(" no cloud red, the others green. 0 = off."),
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
		SHADER_PARAMETER_ARRAY(FVector4f, LookParameters, [FDreamSkyCloudViewExtension::NumLookParameters])
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

	class FLook : SHADER_PERMUTATION_INT("DSKY_LOOK", 3);
	using FPermutationDomain = TShaderPermutationDomain<FLook>;

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_STRUCT_INCLUDE(DreamSkyCloudRender::FCommonParameters, Common)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, SceneDepth)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, WeatherMap)
		SHADER_PARAMETER_SAMPLER(SamplerState, WeatherMapSampler)
		SHADER_PARAMETER_RDG_TEXTURE(Texture3D, CloudNoise)
		SHADER_PARAMETER_SAMPLER(SamplerState, CloudNoiseSampler)
		SHADER_PARAMETER(int32, MaxSamples)
		SHADER_PARAMETER(uint32, MarchDebug)
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

class FDreamSkyCloudPanoramaCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FDreamSkyCloudPanoramaCS);
	SHADER_USE_PARAMETER_STRUCT(FDreamSkyCloudPanoramaCS, FGlobalShader);

	using FPermutationDomain = FDreamSkyCloudMarchCS::FPermutationDomain;

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_STRUCT_INCLUDE(DreamSkyCloudRender::FCommonParameters, Common)
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
		SHADER_PARAMETER(FIntPoint, PanoramaResolution)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float4>, OutPanorama)
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
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<uint2>, HistoryRaySize)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float4>, OutColor)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float>, OutDepth)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<uint2>, OutRaySize)
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

class FDreamSkyCloudCompositeBelowPS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FDreamSkyCloudCompositeBelowPS);
	SHADER_USE_PARAMETER_STRUCT(FDreamSkyCloudCompositeBelowPS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_STRUCT_INCLUDE(DreamSkyCloudRender::FCommonParameters, Common)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, SceneDepth)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float4>, CloudColor)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float>, CloudDepth)
		RENDER_TARGET_BINDING_SLOTS()
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return DreamSkyCloudRender::Supported(Parameters);
	}
};

IMPLEMENT_GLOBAL_SHADER(FDreamSkyCloudCompositeBelowPS, "/Plugin/DreamDynamicWorld/Private/DreamSkyCloudRender.usf", "CompositeBelowPS", SF_Pixel);
IMPLEMENT_GLOBAL_SHADER(FDreamSkyCloudMarchCS, "/Plugin/DreamDynamicWorld/Private/DreamSkyCloudRender.usf", "MarchCS", SF_Compute);
IMPLEMENT_GLOBAL_SHADER(FDreamSkyCloudSunMapCS, "/Plugin/DreamDynamicWorld/Private/DreamSkyCloudRender.usf", "SunMapCS", SF_Compute);
IMPLEMENT_GLOBAL_SHADER(FDreamSkyCloudShadowCS, "/Plugin/DreamDynamicWorld/Private/DreamSkyCloudRender.usf", "ShadowCS", SF_Compute);
IMPLEMENT_GLOBAL_SHADER(FDreamSkyCloudPanoramaCS, "/Plugin/DreamDynamicWorld/Private/DreamSkyCloudRender.usf", "PanoramaCS", SF_Compute);
IMPLEMENT_GLOBAL_SHADER(FDreamSkyCloudTemporalCS, "/Plugin/DreamDynamicWorld/Private/DreamSkyCloudRender.usf", "TemporalCS", SF_Compute);
IMPLEMENT_GLOBAL_SHADER(FDreamSkyCloudCompositePS, "/Plugin/DreamDynamicWorld/Private/DreamSkyCloudRender.usf", "CompositePS", SF_Pixel);

namespace DreamSkyCloudRender
{
	/** A view's clouds, kept from the base pass for the composite over the scene before the post processing (the clouds
	 * below the view, when it is over the layer's base). */
	struct FBelow
	{
		FRDGTextureRef Color = nullptr;
		FRDGTextureRef Depth = nullptr;
		FRDGTextureRef SceneDepth = nullptr;
		int32 Downsample = 2;
		FVector4f Parameters[FDreamSkyCloudViewExtension::NumParameters];
		FVector4f LookParameters[FDreamSkyCloudViewExtension::NumLookParameters];
	};

	/**
	 * What the clouds keep for one render graph: its references to the sun maps, its views' clouds for the composite
	 * below. On the graph's blackboard: no graph's resources are ever read by another. (A scene capture renders in a graph
	 * of its own, in the same frame; held on the extension and told apart by the frame and the builder's address -- which
	 * the next graph may well reuse -- a capture's march was handed the main view's sun maps from a graph long gone.)
	 */
	struct FGraphClouds
	{
		FRDGTextureRef SunMaps[2] = {nullptr, nullptr};
		bool bSunMaps = false;
		TMap<const FSceneView*, FBelow> Below;
	};
}
RDG_REGISTER_BLACKBOARD_STRUCT(DreamSkyCloudRender::FGraphClouds);

const TArray<FName>& FDreamSkyCloudViewExtension::GetParameterNames()
{
	static const TArray<FName> Names = {
		TEXT("DDW_SunDir"), TEXT("DDW_SkyZenith"), TEXT("DDW_SkyHorizon"), TEXT("DDW_SkyGround"), TEXT("DDW_SunHalo"),
		TEXT("DDW_Cloud0"), TEXT("DDW_Cloud1"), TEXT("DDW_Cloud2"), TEXT("DDW_Cloud3"), TEXT("DDW_CloudLit"), TEXT("DDW_CloudShadow"),
		TEXT("DDW_MainLight"), TEXT("DDW_MainLightColor"), TEXT("DDW_CloudLayer"), TEXT("DDW_WeatherMap"), TEXT("DDW_SkyExtra"),
		TEXT("DDW_SkyHaze"), TEXT("DDW_Flash"),
	};
	check(Names.Num() == NumParameters);
	return Names;
}

void FDreamSkyCloudViewExtension::PackLookParameters(const FDreamSkyCloudLookParameters& L, FVector4f Out[NumLookParameters])
{
	// DreamSkyCloudRender.usf's LP_* (extinctions per cm).
	Out[0] = FVector4f(L.Extinction * 0.01f, L.Albedo, L.Glow, L.BaseGlow);
	Out[1] = FVector4f(L.ScatterForward, L.ScatterBackward, L.ScatterBlend, L.MultiScatter);
	Out[2] = FVector4f(L.MultiScatterOcclusion, L.MultiScatterEccentricity, L.ViewThinning, L.DeckMottle);
	Out[3] = FVector4f(L.ShadeTint.R, L.ShadeTint.G, L.ShadeTint.B, L.SunScattering);
	Out[4] = FVector4f(L.PaintLitTint.R, L.PaintLitTint.G, L.PaintLitTint.B, L.PaintGain);
	Out[5] = FVector4f(L.PaintShadeTint.R, L.PaintShadeTint.G, L.PaintShadeTint.B, L.PaintExtinction * 0.01f);
	Out[6] = FVector4f(L.EdgeSoftness, L.HalfToneAt, L.LitToneAt, L.ToneSoftness);
	Out[7] = FVector4f(L.SilverLining, L.BillowShade, L.ShadowTracingDistance * 100000.0f, 0.0f);
	Out[8] = FVector4f(L.LightIlluminance.R, L.LightIlluminance.G, L.LightIlluminance.B, L.ShadowReach);
	// (The height fog as the engine's renderer takes it: density and falloff per cm.)
	Out[9] = FVector4f(L.FogDensity * 0.001f, L.FogHeightFalloff * 0.001f, L.FogHeight, L.FogStartDistance);
	Out[10] = FVector4f(L.FogColor.R, L.FogColor.G, L.FogColor.B, 1.0f - FMath::Clamp(L.FogMaxOpacity, 0.0f, 1.0f));
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
	Settings.SkyDepth = Actor ? Actor->DomeRadius * 0.5f : 1.0e30f;

	const UDreamWorldSubsystem* Clock = Sky ? Sky->ClockSubsystem.Get() : nullptr;
	UWorld* ViewWorld = GetWorld();
	UMaterialParameterCollection* Collection = Clock ? Clock->GetParameterCollection() : nullptr;
	if (Actor && Collection && ViewWorld && Actor->DrawsClouds())
	{
		if (!Sky->CloudNoise)
		{
			Sky->CloudNoise = LoadObject<UTexture>(nullptr, TEXT("/DreamDynamicWorld/Sky/T_DreamCloudNoise.T_DreamCloudNoise"));
		}
		if (!Sky->CloudPanorama)
		{
			Sky->CloudPanorama = LoadObject<UTextureRenderTarget2D>(nullptr, TEXT("/DreamDynamicWorld/Sky/RT_DreamCloudPanorama.RT_DreamCloudPanorama"), nullptr, LOAD_NoWarn | LOAD_Quiet);
			if (Sky->CloudPanorama && !Sky->CloudPanorama->bSupportsUAV)
			{
				// The compute pass writes it, so its resource needs a UAV.
				Sky->CloudPanorama->bSupportsUAV = true;
				Sky->CloudPanorama->InitCustomFormat(Sky->CloudPanorama->SizeX, Sky->CloudPanorama->SizeY, PF_FloatRGBA, true);
				Sky->CloudPanorama->UpdateResourceImmediate(true);
			}
		}
		Settings.Panorama = Sky->CloudPanorama ? Sky->CloudPanorama->GameThread_GetRenderTargetResource() : nullptr;
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
		Settings.Look = int32(Actor->GetEffectiveCloudLook());
		FDreamSkyCloudLookParameters Look;
		Actor->GetCloudLookParameters(Look);
		PackLookParameters(Look, Settings.LookParameters);

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
	// Views that stopped drawing (closed viewports, finished captures) let go of their history -- after some minutes: an
	// editor viewport that is not realtime may stand still that long and is drawn again with it.
	if ((GFrameCounterRenderThread & 255) == 0)
	{
		const uint64 Now = GFrameCounterRenderThread;
		for (auto It = Histories.CreateIterator(); It; ++It)
		{
			if (!It.Value().IsValid() || It.Value()->LastFrame + 20000 < Now)
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
		for (int32 Index = 0; Index < NumLookParameters; ++Index)
		{
			Common.LookParameters[Index] = Settings.LookParameters[Index];
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
			Params->MarchDebug = Settings.DebugMode == 2 ? 1u : 0u;
			const DreamSkyCloudRender::FGraphClouds& Graph = GraphBuilder.Blackboard.GetChecked<DreamSkyCloudRender::FGraphClouds>();
			Params->NearTau = Graph.SunMaps[0];
			Params->FarTau = Graph.SunMaps[1];
			Params->TauSampler = TStaticSamplerState<SF_Bilinear, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();
			Params->NearCenterExtent = SunMaps[0].CenterExtent;
			Params->FarCenterExtent = SunMaps[1].CenterExtent;
			Params->OutColor = GraphBuilder.CreateUAV(MarchColor);
			Params->OutDepth = GraphBuilder.CreateUAV(MarchDepth);
			FDreamSkyCloudMarchCS::FPermutationDomain Permutation;
			Permutation.Set<FDreamSkyCloudMarchCS::FLook>(Settings.Look);
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
		// (However long ago this view was drawn: an editor viewport that is not realtime, or one in the background, is
		// drawn again only now and then, and with the history dropped each of its frames was a single jittered march, all
		// grain. The reprojection and the neighbourhood clamp keep an old history from ghosting.)
		// (Nor whatever size it was drawn at: it is read by where the clouds were on screen, at the ray grid it keeps, so a
		// change of screen percentage or of the window resamples it instead of reading it at the wrong place.)
		const bool bHistory = History.IsValid() && History->Color.IsValid() && History->Depth.IsValid() && History->RaySize.IsValid()
			&& History->Downsample == Downsample && History->Look == Settings.Look;
		UE_LOG(LogDreamSky, VeryVerbose, TEXT("Clouds: view key %u, %dx%d (march %dx%d), %s, family frame %u"), Key, ViewSize.X, ViewSize.Y, Half.X, Half.Y,
			bHistory ? TEXT("history") : (History.IsValid() ? TEXT("history dropped") : TEXT("no history kept")), InView.Family ? InView.Family->FrameNumber : 0u);

		FRDGTextureRef OutColor = GraphBuilder.CreateTexture(ColorDesc, TEXT("DreamSky.Clouds"));
		FRDGTextureRef OutDepth = GraphBuilder.CreateTexture(DepthDesc, TEXT("DreamSky.CloudsDepth"));
		FRDGBufferRef OutRaySize = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateStructuredDesc(sizeof(uint32) * 2, 1), TEXT("DreamSky.CloudsRaySize"));
		FRDGBufferRef HistoryRaySize = bHistory ? GraphBuilder.RegisterExternalBuffer(History->RaySize) : nullptr;
		if (!HistoryRaySize)
		{
			// (Not read without a history; bound all the same.)
			HistoryRaySize = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateStructuredDesc(sizeof(uint32) * 2, 1), TEXT("DreamSky.CloudsRaySizeNone"));
			AddClearUAVPass(GraphBuilder, GraphBuilder.CreateUAV(HistoryRaySize), 0u);
		}
		{
			FDreamSkyCloudTemporalCS::FParameters* Params = GraphBuilder.AllocParameters<FDreamSkyCloudTemporalCS::FParameters>();
			FillCommon(Params->Common);
			Params->CurrentColor = MarchColor;
			Params->CurrentDepth = MarchDepth;
			Params->HistoryColor = bHistory ? GraphBuilder.RegisterExternalTexture(History->Color) : MarchColor;
			Params->HistoryDepth = bHistory ? GraphBuilder.RegisterExternalTexture(History->Depth) : MarchDepth;
			Params->HistorySampler = TStaticSamplerState<SF_Bilinear, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();
			const FIntPoint HistoryExtent = bHistory ? History->Color->GetDesc().Extent : Half;
			Params->HistoryInvExtent = FVector2f(1.0f / HistoryExtent.X, 1.0f / HistoryExtent.Y);
			Params->HistoryRaySize = GraphBuilder.CreateSRV(HistoryRaySize);
			Params->OutRaySize = GraphBuilder.CreateUAV(OutRaySize);
			// (Without one, the temporal pass puts out the neighbourhood's mean: a smooth frame, not one march's grain.)
			Params->HistoryWeight = bHistory ? Settings.HistoryWeight : (Settings.HistoryWeight > 0.0f ? -1.0f : 0.0f);
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
			GraphBuilder.QueueBufferExtraction(OutRaySize, &History->RaySize);
			History->ViewSize = ViewSize;
			History->Downsample = Downsample;
			History->Look = Settings.Look;
			History->LastFrame = GFrameCounterRenderThread;
		}
		Clouds = OutColor;

		// Kept for the composite over the scene before the post processing, when the view looks down on clouds.
		if (IsOverCloudBase(InView))
		{
			DreamSkyCloudRender::FBelow& Entry = GraphBuilder.Blackboard.GetOrCreate<DreamSkyCloudRender::FGraphClouds>().Below.FindOrAdd(&InView);
			Entry.Color = OutColor;
			Entry.Depth = OutDepth;
			Entry.SceneDepth = SceneDepth;
			Entry.Downsample = Downsample;
			FMemory::Memcpy(Entry.Parameters, Settings.Parameters, sizeof(Entry.Parameters));
			FMemory::Memcpy(Entry.LookParameters, Settings.LookParameters, sizeof(Entry.LookParameters));
		}
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
	// Once per frame (the first view draws them; the others, and the other graphs of the frame -- scene captures --, read
	// them), one cascade a frame: the near one on even frames, the far one on odd ones, either whenever it has never been
	// drawn. Each graph registers them for itself.
	const uint64 Frame = GFrameCounterRenderThread;
	DreamSkyCloudRender::FGraphClouds& Graph = GraphBuilder.Blackboard.GetOrCreate<DreamSkyCloudRender::FGraphClouds>();
	if (Graph.bSunMaps)
	{
		return;
	}
	Graph.bSunMaps = true;
	const bool bFirstThisFrame = SunMapFrame != Frame;
	SunMapFrame = Frame;
	for (int32 Cascade = 0; Cascade < 2; ++Cascade)
	{
		FSunMap& Map = SunMaps[Cascade];
		const bool bDraw = !Map.Texture.IsValid() || (bFirstThisFrame && int32(Frame & 1) == Cascade);
		if (!bDraw)
		{
			Graph.SunMaps[Cascade] = GraphBuilder.RegisterExternalTexture(Map.Texture);
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
		Graph.SunMaps[Cascade] = Tau;
	}
	// (The panorama and the ground shadow: once a frame too.)
	if (!bFirstThisFrame)
	{
		return;
	}

	// The panorama of the clouds round the view the dome shows in the sky light capture, every fourth frame (the capture
	// itself is spread over frames): from the view, a ray a texel, half the samples, without the weather haze (the dome
	// adds it).
	FRHITexture* PanoramaRHI = RenderSettings.Panorama ? RenderSettings.Panorama->TextureRHI.GetReference() : nullptr;
	if (PanoramaRHI && EnumHasAnyFlags(PanoramaRHI->GetFlags(), TexCreate_UAV) && (Frame & 3) == 0)
	{
		FRDGTextureRef Panorama = RegisterExternalTexture(GraphBuilder, PanoramaRHI, TEXT("DreamSky.CloudPanorama"));
		const FIntPoint Resolution(PanoramaRHI->GetSizeXYZ().X, PanoramaRHI->GetSizeXYZ().Y);
		FDreamSkyCloudPanoramaCS::FParameters* Params = GraphBuilder.AllocParameters<FDreamSkyCloudPanoramaCS::FParameters>();
		FillCommon(Params->Common);
		Params->WeatherMap = WeatherMap;
		Params->WeatherMapSampler = TStaticSamplerState<SF_Bilinear, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();
		Params->CloudNoise = CloudNoise;
		Params->CloudNoiseSampler = TStaticSamplerState<SF_Trilinear, AM_Wrap, AM_Wrap, AM_Wrap>::GetRHI();
		Params->MaxSamples = FMath::Max(RenderSettings.MaxSamples / 2, 16);
		Params->NearTau = Graph.SunMaps[0];
		Params->FarTau = Graph.SunMaps[1];
		Params->TauSampler = TStaticSamplerState<SF_Bilinear, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();
		Params->NearCenterExtent = SunMaps[0].CenterExtent;
		Params->FarCenterExtent = SunMaps[1].CenterExtent;
		Params->PanoramaResolution = Resolution;
		Params->OutPanorama = GraphBuilder.CreateUAV(Panorama);
		FDreamSkyCloudMarchCS::FPermutationDomain Permutation;
		Permutation.Set<FDreamSkyCloudMarchCS::FLook>(RenderSettings.Look);
		TShaderMapRef<FDreamSkyCloudPanoramaCS> Shader(ShaderMap, Permutation);
		FComputeShaderUtils::AddPass(GraphBuilder, RDG_EVENT_NAME("Panorama %dx%d", Resolution.X, Resolution.Y), Shader, Params,
			FComputeShaderUtils::GetGroupCount(Resolution, FDreamSkyCloudMarchCS::GroupSize));
		GraphBuilder.SetTextureAccessFinal(Panorama, ERHIAccess::SRVMask);
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
		Params->NearTau = Graph.SunMaps[0];
		Params->FarTau = Graph.SunMaps[1];
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

bool FDreamSkyCloudViewExtension::IsOverCloudBase(const FSceneView& InView) const
{
	// (DDW_CloudLayer.x: the layer's base over the ground, cm; a little short of it already, as a rising view enters the
	// layer.)
	return RenderSettings.bClouds && InView.ViewLocation.Z > RenderSettings.Parameters[13].X - 20000.0f;
}

void FDreamSkyCloudViewExtension::SubscribeToPostProcessingPass(EPostProcessingPass Pass, const FSceneView& InView, FPostProcessingPassDelegateArray& InOutPassCallbacks, bool bIsPassEnabled)
{
	if (Pass == EPostProcessingPass::BeforeDOF && IsOverCloudBase(InView))
	{
		InOutPassCallbacks.Add(FPostProcessingPassDelegate::CreateRaw(this, &FDreamSkyCloudViewExtension::CompositeBelow));
	}
}

FScreenPassTexture FDreamSkyCloudViewExtension::CompositeBelow(FRDGBuilder& GraphBuilder, const FSceneView& InView, const FPostProcessMaterialInputs& Inputs)
{
	FScreenPassTexture SceneColor = FScreenPassTexture::CopyFromSlice(GraphBuilder, Inputs.GetInput(EPostProcessMaterialInput::SceneColor));
	const DreamSkyCloudRender::FGraphClouds* Graph = GraphBuilder.Blackboard.Get<DreamSkyCloudRender::FGraphClouds>();
	const DreamSkyCloudRender::FBelow* Entry = Graph ? Graph->Below.Find(&InView) : nullptr;
	if (!Entry || !Entry->Color || !SceneColor.IsValid())
	{
		return SceneColor;
	}
	RDG_EVENT_SCOPE(GraphBuilder, "DreamSky.CloudsBelow");
	FDreamSkyCloudCompositeBelowPS::FParameters* Params = GraphBuilder.AllocParameters<FDreamSkyCloudCompositeBelowPS::FParameters>();
	Params->Common.View = InView.ViewUniformBuffer;
	for (int32 Index = 0; Index < NumParameters; ++Index)
	{
		Params->Common.Parameters[Index] = Entry->Parameters[Index];
	}
	for (int32 Index = 0; Index < NumLookParameters; ++Index)
	{
		Params->Common.LookParameters[Index] = Entry->LookParameters[Index];
	}
	Params->Common.CameraWorld = FVector3f(InView.ViewLocation);
	Params->Common.SkyDepth = RenderSettings.SkyDepth;
	Params->Common.Downsample = Entry->Downsample;
	Params->SceneDepth = Entry->SceneDepth;
	Params->CloudColor = Entry->Color;
	Params->CloudDepth = Entry->Depth;
	Params->RenderTargets[0] = FRenderTargetBinding(SceneColor.Texture, ERenderTargetLoadAction::ELoad);
	TShaderMapRef<FDreamSkyCloudCompositeBelowPS> Shader(GetGlobalShaderMap(InView.GetFeatureLevel()));
	// (Over the scene as over the sky: premultiplied, the scene times the clouds' transmittance plus their light.)
	FPixelShaderUtils::AddFullscreenPass(GraphBuilder, GetGlobalShaderMap(InView.GetFeatureLevel()), RDG_EVENT_NAME("Composite"), Shader, Params,
		SceneColor.ViewRect, TStaticBlendState<CW_RGB, BO_Add, BF_One, BF_SourceAlpha>::GetRHI());
	return SceneColor;
}
