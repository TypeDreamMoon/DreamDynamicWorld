// Copyright DreamMoon. All Rights Reserved.

#include "DreamSkyCloudRenderer.h"
#include "DreamAtmosphereSubsystem.h"
#include "DreamAtmosphereTables.h"
#include "Engine/TextureRenderTargetVolume.h"
#include "DreamSky.h"
#include "DreamSkyModule.h"
#include "DreamSkyStyle.h"
#include "DreamSkySubsystem.h"
#include "DreamWorldSubsystem.h"

#include "DataDrivenShaderPlatformInfo.h"
#include "Engine/Texture.h"
#include "Engine/World.h"
#include "GlobalRenderResources.h"
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
	TEXT(" no cloud red, the others green; 3 = how each ray took the frames before: green its history, yellow none this frame")
	TEXT(" (a cut, a view first drawn: its neighbours' mean), red its history off screen or not drawn (the mean), blue this frame")
	TEXT(" alone (brighter where the clouds cover more); 4 = the march's per-pixel jitter held still (each ray's steps where the")
	TEXT(" others are). 0 = off."),
	ECVF_RenderThreadSafe);

static TAutoConsoleVariable<int32> CVarDreamSkyCloudRenderDownsample(
	TEXT("DreamSky.CloudRender.Downsample"), 2,
	TEXT("The sky cloud renderer marches one ray per this many pixels across and down (1..4)."),
	ECVF_RenderThreadSafe);

static TAutoConsoleVariable<int32> CVarDreamSkyCloudRenderCinematic(
	TEXT("DreamSky.CloudRender.Cinematic"), 1,
	TEXT("1 = in the world's cinematic mode (DreamWorld.CinematicMode, a Movie Render Queue render) every frame of the clouds")
	TEXT(" stands on its own and is drawn for its looks: every ray at full resolution, twice the samples, no history. 0 = as")
	TEXT(" in play."),
	ECVF_RenderThreadSafe);

static TAutoConsoleVariable<int32> CVarDreamSkyCloudRenderSamples(
	TEXT("DreamSky.CloudRender.Samples"), 160,
	TEXT("The most samples of the cloud field a ray of the sky cloud renderer takes (16..512)."),
	ECVF_RenderThreadSafe);

static TAutoConsoleVariable<int32> CVarDreamSkyCloudRenderGroundReference(
	TEXT("DreamSky.CloudRender.GroundShadowReference"), 0,
	TEXT("Debug: the clouds' shadow on the ground worked out for each of its texels by a march of this many even steps from")
	TEXT(" the layer's base up through the cloud field toward the sun (a reference for the sun map's ground maps it is")
	TEXT(" otherwise read from: the two should agree within their texels). 0 = off (the ground maps)."),
	ECVF_RenderThreadSafe);

static TAutoConsoleVariable<int32> CVarDreamSkyCloudRenderSunMapSlices(
	TEXT("DreamSky.CloudRender.SunMapSlices"), 6,
	TEXT("In how many slices the sky cloud renderer redraws a sun map cascade over the frames (the two cascades take turns,")
	TEXT(" a slice a frame; 1 = a whole cascade a frame). A cascade is redrawn whole at once when the view moves a texel, the")
	TEXT(" light turns, or the clock jumps, and every frame in the cinematic mode (DreamWorld.CinematicMode)."),
	ECVF_RenderThreadSafe);

static TAutoConsoleVariable<float> CVarDreamSkyCloudRenderHistory(
	TEXT("DreamSky.CloudRender.History"), 0.9f,
	TEXT("How much of the previous frames the sky cloud renderer keeps (0 = none: every frame stands alone, its jittered")
	TEXT(" samples show as noise; 0.9 = a tenth of each new frame)."),
	ECVF_RenderThreadSafe);

static TAutoConsoleVariable<float> CVarDreamSkyLightShafts(
	TEXT("DreamSky.LightShafts"), 1.0f,
	TEXT("Multiplies how bright the sky cloud renderer draws the light shafts below its clouds (the style's LightShafts);")
	TEXT(" 0 = none."),
	ECVF_RenderThreadSafe);

static TAutoConsoleVariable<int32> CVarDreamSkyLightShaftsDebug(
	TEXT("DreamSky.LightShafts.Debug"), 0,
	TEXT("1 = the light shafts show, in grey over the scene, how much of the sun the air along each ray gets (the clouds'")
	TEXT(" shadow alone, without the haze or the light). 0 = off."),
	ECVF_RenderThreadSafe);

static TAutoConsoleVariable<int32> CVarDreamSkyLightShaftSamples(
	TEXT("DreamSky.LightShafts.Samples"), 24,
	TEXT("How many samples of the air below the clouds a ray of the light shafts takes (4..128)."),
	ECVF_RenderThreadSafe);

static TAutoConsoleVariable<float> CVarDreamSkyLensFlare(
	TEXT("DreamSky.LensFlare"), 1.0f,
	TEXT("Multiplies how bright the sun's lens flare is drawn (the style's LensFlare); 0 = none."),
	ECVF_RenderThreadSafe);

static TAutoConsoleVariable<int32> CVarDreamSkyLensFlarePreset(
	TEXT("DreamSky.LensFlare.Preset"), -1,
	TEXT("The sun's lens flare as this preset whatever the style says: 0 none, 1 natural, 2 anamorphic, 3 stylized; -1 = the style's."),
	ECVF_RenderThreadSafe);

static TAutoConsoleVariable<int32> CVarDreamSkyCloudRenderAtmosphere(
	TEXT("DreamSky.CloudRender.Atmosphere"), 1,
	TEXT("1: the clouds' light through the air from the atmosphere's tables (DreamAtmosphere) once built; 0: the analytic air."),
	ECVF_RenderThreadSafe);

static TAutoConsoleVariable<int32> CVarDreamSkyCloudRenderColours(
	TEXT("DreamSky.CloudRender.Colours"), -1,
	TEXT("The toon and painted clouds' colours whatever the style says: 0 from the physical sky's light, 1 the style's curves as they are; -1 = the style's bCloudColorsAbsolute."),
	ECVF_RenderThreadSafe);

static TAutoConsoleVariable<float> CVarDreamSkyScreenDroplets(
	TEXT("DreamSky.ScreenDroplets"), -1.0f,
	TEXT("The rain drops on the lens at this intensity whatever the style says (0 = none); -1 = the style's ScreenDroplets."),
	ECVF_RenderThreadSafe);

static TAutoConsoleVariable<float> CVarDreamSkyScreenFrost(
	TEXT("DreamSky.ScreenFrost"), -1.0f,
	TEXT("The frost on the lens at this intensity whatever the style says (0 = none); -1 = the style's ScreenFrost."),
	ECVF_RenderThreadSafe);

static TAutoConsoleVariable<float> CVarDreamSkyHeatHaze(
	TEXT("DreamSky.HeatHaze"), -1.0f,
	TEXT("The hot air's shimmer at this intensity whatever the style says (0 = none); -1 = the style's HeatHaze."),
	ECVF_RenderThreadSafe);

static TAutoConsoleVariable<float> CVarDreamSkyRainbow(
	TEXT("DreamSky.Rainbow"), -1.0f,
	TEXT("The rainbow at this intensity whatever the style says (0 = none); -1 = the style's Rainbow."),
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
		// The atmosphere (DreamAtmosphere): its transmittance table (TransmittanceSize 0: not built, the analytic air) and
		// the air it was built for (FDreamAtmosphereParameters::Pack).
		SHADER_PARAMETER_TEXTURE(Texture2D, TransmittanceLut)
		SHADER_PARAMETER_SAMPLER(SamplerState, TransmittanceSampler)
		SHADER_PARAMETER(FIntPoint, TransmittanceSize)
		SHADER_PARAMETER_ARRAY(FVector4f, Atmosphere, [5])
		SHADER_PARAMETER_TEXTURE(Texture3D, SkyTable)
		SHADER_PARAMETER_SAMPLER(SamplerState, SkyTableSampler)
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
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float>, StormProximity)
		SHADER_PARAMETER(int32, ProximityResolution)
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
		SHADER_PARAMETER(FIntVector, MapOffset)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture3D<float>, OutTau)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float>, OutGroundTau)
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

/** The storm proximity map's cells from which a sample may search for storms. */
class FDreamSkyStormSeedCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FDreamSkyStormSeedCS);
	SHADER_USE_PARAMETER_STRUCT(FDreamSkyStormSeedCS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_STRUCT_INCLUDE(DreamSkyCloudRender::FCommonParameters, Common)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, WeatherMap)
		SHADER_PARAMETER_SAMPLER(SamplerState, WeatherMapSampler)
		SHADER_PARAMETER(int32, ProximityResolution)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float2>, OutSeed)
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

/** A round of the jump flood that spreads the nearest of those cells over the map. */
class FDreamSkyStormJumpCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FDreamSkyStormJumpCS);
	SHADER_USE_PARAMETER_STRUCT(FDreamSkyStormJumpCS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER(int32, ProximityResolution)
		SHADER_PARAMETER(int32, JumpStep)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float2>, InSeed)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float2>, OutSeed)
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

/** The distance to the nearest of them, the march reads. */
class FDreamSkyStormProximityCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FDreamSkyStormProximityCS);
	SHADER_USE_PARAMETER_STRUCT(FDreamSkyStormProximityCS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_STRUCT_INCLUDE(DreamSkyCloudRender::FCommonParameters, Common)
		SHADER_PARAMETER(int32, ProximityResolution)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float2>, InSeed)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float>, OutProximity)
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

class FDreamSkyCloudShadowCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FDreamSkyCloudShadowCS);
	SHADER_USE_PARAMETER_STRUCT(FDreamSkyCloudShadowCS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_STRUCT_INCLUDE(DreamSkyCloudRender::FCommonParameters, Common)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, WeatherMap)
		SHADER_PARAMETER_SAMPLER(SamplerState, WeatherMapSampler)
		SHADER_PARAMETER_RDG_TEXTURE(Texture3D, CloudNoise)
		SHADER_PARAMETER_SAMPLER(SamplerState, CloudNoiseSampler)
		SHADER_PARAMETER(int32, ReferenceSteps)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float>, NearGround)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float>, FarGround)
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
		SHADER_PARAMETER(uint32, MarchDebug)   // (the marches it shares read it; always 0 here)
		SHADER_PARAMETER_RDG_TEXTURE(Texture3D<float>, NearTau)
		SHADER_PARAMETER_RDG_TEXTURE(Texture3D<float>, FarTau)
		SHADER_PARAMETER_SAMPLER(SamplerState, TauSampler)
		SHADER_PARAMETER(FVector4f, NearCenterExtent)
		SHADER_PARAMETER(FVector4f, FarCenterExtent)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float>, StormProximity)
		SHADER_PARAMETER(int32, ProximityResolution)
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
		SHADER_PARAMETER(FVector2f, CloudDrift)
		SHADER_PARAMETER(float, HistoryFresh)
		SHADER_PARAMETER(uint32, TemporalDebug)
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

class FDreamSkyLightShaftsCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FDreamSkyLightShaftsCS);
	SHADER_USE_PARAMETER_STRUCT(FDreamSkyLightShaftsCS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_STRUCT_INCLUDE(DreamSkyCloudRender::FCommonParameters, Common)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, SceneDepth)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float>, ShaftShadow)
		SHADER_PARAMETER_SAMPLER(SamplerState, ShaftShadowSampler)
		SHADER_PARAMETER(FVector4f, ShadowPlacement)
		SHADER_PARAMETER_ARRAY(FVector4f, ShaftParameters, [2])
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float4>, OutShafts)
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return DreamSkyCloudRender::Supported(Parameters);
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.SetDefine(TEXT("THREADGROUP_SIZE"), 8);
	}
};

class FDreamSkyLightShaftsCompositePS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FDreamSkyLightShaftsCompositePS);
	SHADER_USE_PARAMETER_STRUCT(FDreamSkyLightShaftsCompositePS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_STRUCT_INCLUDE(DreamSkyCloudRender::FCommonParameters, Common)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, SceneDepth)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float4>, Shafts)
		SHADER_PARAMETER_ARRAY(FVector4f, ShaftParameters, [2])
		RENDER_TARGET_BINDING_SLOTS()
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return DreamSkyCloudRender::Supported(Parameters);
	}
};

IMPLEMENT_GLOBAL_SHADER(FDreamSkyLightShaftsCS, "/Plugin/DreamDynamicWorld/Private/DreamSkyCloudRender.usf", "LightShaftsCS", SF_Compute);
IMPLEMENT_GLOBAL_SHADER(FDreamSkyLightShaftsCompositePS, "/Plugin/DreamDynamicWorld/Private/DreamSkyCloudRender.usf", "LightShaftsCompositePS", SF_Pixel);
IMPLEMENT_GLOBAL_SHADER(FDreamSkyCloudCompositeBelowPS, "/Plugin/DreamDynamicWorld/Private/DreamSkyCloudRender.usf", "CompositeBelowPS", SF_Pixel);
IMPLEMENT_GLOBAL_SHADER(FDreamSkyCloudMarchCS, "/Plugin/DreamDynamicWorld/Private/DreamSkyCloudRender.usf", "MarchCS", SF_Compute);
IMPLEMENT_GLOBAL_SHADER(FDreamSkyCloudSunMapCS, "/Plugin/DreamDynamicWorld/Private/DreamSkyCloudRender.usf", "SunMapCS", SF_Compute);
IMPLEMENT_GLOBAL_SHADER(FDreamSkyCloudShadowCS, "/Plugin/DreamDynamicWorld/Private/DreamSkyCloudRender.usf", "ShadowCS", SF_Compute);
IMPLEMENT_GLOBAL_SHADER(FDreamSkyStormSeedCS, "/Plugin/DreamDynamicWorld/Private/DreamSkyCloudRender.usf", "StormSeedCS", SF_Compute);
IMPLEMENT_GLOBAL_SHADER(FDreamSkyStormJumpCS, "/Plugin/DreamDynamicWorld/Private/DreamSkyCloudRender.usf", "StormJumpCS", SF_Compute);
IMPLEMENT_GLOBAL_SHADER(FDreamSkyStormProximityCS, "/Plugin/DreamDynamicWorld/Private/DreamSkyCloudRender.usf", "StormProximityCS", SF_Compute);
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
		FRDGTextureRef GroundMaps[2] = {nullptr, nullptr};
		FRDGTextureRef StormProximity = nullptr;
		/** The clouds' shadow on the ground as this graph wrote it (the light shafts read it). */
		FRDGTextureRef Shadow = nullptr;
		bool bSunMaps = false;
		TMap<const FSceneView*, FBelow> Below;
	};
}
RDG_REGISTER_BLACKBOARD_STRUCT(DreamSkyCloudRender::FGraphClouds);

namespace DreamSkyCloudRender
{
	/** The storm proximity map's cells across the weather map (four of its texels each). */
	constexpr int32 ProximityResolution = 128;
}

const TArray<FName>& FDreamSkyCloudViewExtension::GetParameterNames()
{
	static const TArray<FName> Names = {
		TEXT("DDW_SunDir"), TEXT("DDW_SkyZenith"), TEXT("DDW_SkyHorizon"), TEXT("DDW_SkyGround"), TEXT("DDW_SunHalo"),
		TEXT("DDW_Cloud0"), TEXT("DDW_Cloud1"), TEXT("DDW_Cloud2"), TEXT("DDW_Cloud3"), TEXT("DDW_CloudLit"), TEXT("DDW_CloudShadow"),
		TEXT("DDW_MainLight"), TEXT("DDW_MainLightColor"), TEXT("DDW_CloudLayer"), TEXT("DDW_WeatherMap"), TEXT("DDW_SkyExtra"),
		TEXT("DDW_SkyHaze"), TEXT("DDW_Flash"), TEXT("DDW_SkyModClouds"), TEXT("DDW_NightGlow"), TEXT("DDW_LightPollution"),
		TEXT("DDW_MoonDir"), TEXT("DDW_SkyGrade0"), TEXT("DDW_SkyGrade1"), TEXT("DDW_SkyGrade2"), TEXT("DDW_SkyGrade3"),
		TEXT("DDW_AtmosphereSun"), TEXT("DDW_AtmosphereMoon"), TEXT("DDW_AmbientSky"), TEXT("DDW_AmbientEquator"),
		TEXT("DDW_CloudClearing"),
		TEXT("DDW_Funnel0"), TEXT("DDW_Funnel1"), TEXT("DDW_Funnel2"), TEXT("DDW_Funnel3"),
		TEXT("DDW_FunnelShape0"), TEXT("DDW_FunnelShape1"), TEXT("DDW_FunnelShape2"), TEXT("DDW_FunnelShape3"),
		TEXT("DDW_FunnelDecay"),
	};
	check(Names.Num() == NumParameters);
	return Names;
}

void FDreamSkyCloudViewExtension::FillAtmosphere(DreamSkyCloudRender::FCommonParameters& Common, const FFrameSettings& Settings)
{
	const bool bTable = Settings.Transmittance && Settings.Transmittance->TextureRHI;
	Common.TransmittanceLut = bTable ? Settings.Transmittance->TextureRHI.GetReference() : GBlackTexture->TextureRHI.GetReference();
	Common.TransmittanceSampler = TStaticSamplerState<SF_Bilinear, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();
	Common.TransmittanceSize = bTable ? Settings.TransmittanceSize : FIntPoint::ZeroValue;
	for (int32 Index = 0; Index < 5; ++Index)
	{
		Common.Atmosphere[Index] = Settings.Atmosphere[Index];
	}
	const bool bSky = Settings.SkyTable && Settings.SkyTable->TextureRHI;
	Common.SkyTable = bSky ? Settings.SkyTable->TextureRHI.GetReference() : GBlackVolumeTexture->TextureRHI.GetReference();
	Common.SkyTableSampler = TStaticSamplerState<SF_Bilinear, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();
}

void FDreamSkyCloudViewExtension::PackLookParameters(const FDreamSkyCloudLookParameters& L, FVector4f Out[NumLookParameters])
{
	static_assert(NumLookParameters == 18 + FDreamSkyToonBaked::RampSize / 2 + 7 + 4 + 5 + 1, "the toon ramps, two samples a vector, after the 18 looks' vectors; the toon's finer settings; the painted look's; the colour scales");
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
	// The toon tones (DSKY_TOON_*) and the painted look's own constants; then the toon ramps, two samples a vector.
	FDreamSkyToonBaked Toon = L.Toon;
	if (!Toon.IsValid())
	{
		FDreamSkyToonClouds().Bake(Toon);
	}
	Out[11] = Toon.Half;
	Out[12] = Toon.Deep;
	Out[13] = Toon.Key;
	Out[14] = Toon.Key2;
	Out[15] = FVector4f(Toon.RimFocus, L.PaintKeyFalloff, L.PaintRimFalloff, L.PaintRimPower);
	Out[16] = FVector4f(L.PaintHalfToneTint.R, L.PaintHalfToneTint.G, L.PaintHalfToneTint.B, L.PaintHalfToneMix);
	Out[17] = FVector4f(L.PaintShadeAtBase, L.PaintBaseFloor, 0.0f, 0.0f);
	for (int32 Index = 0; Index < FDreamSkyToonBaked::RampSize / 2; ++Index)
	{
		const FVector2f A = Toon.Ramp[2 * Index], B = Toon.Ramp[2 * Index + 1];
		Out[18 + Index] = FVector4f(A.X, A.Y, B.X, B.Y);
	}
	for (int32 Index = 0; Index < 7; ++Index)
	{
		Out[18 + FDreamSkyToonBaked::RampSize / 2 + Index] = Toon.Advanced[Index];
	}
	Out[153] = FVector4f(L.PaintWarmFade, L.PaintWarmTowardSun, L.PaintWarmMax, L.PaintSkyInShade);
	Out[154] = FVector4f(L.PaintKeyAtBase, L.PaintKeyLow, L.PaintKeyHigh, FMath::Max(L.PaintDeckThinColumn, 1.0f) * 100.0f);
	Out[155] = FVector4f(L.PaintDeckStormThinning, L.PaintDeckFrom, FMath::Max(L.PaintDeckTo, L.PaintDeckFrom + 0.001f), L.PaintStormLitDarkening);
	Out[156] = FVector4f(L.PaintStormTopDarkening, L.PaintFloorAtTop, L.PaintFloorInStorm, 0.0f);
	Out[157] = FVector4f(L.CloudLitScale.R, L.CloudLitScale.G, L.CloudLitScale.B, L.bCloudColorsAbsolute ? 1.0f : 0.0f);
	Out[158] = FVector4f(L.CloudShadeScale.R, L.CloudShadeScale.G, L.CloudShadeScale.B, L.bCloudMatchStyleBrightness ? 1.0f : 0.0f);
	Out[159] = FVector4f(L.PaintedCloudLitScale.R, L.PaintedCloudLitScale.G, L.PaintedCloudLitScale.B, 0.0f);
	Out[160] = FVector4f(L.PaintedCloudShadeScale.R, L.PaintedCloudShadeScale.G, L.PaintedCloudShadeScale.B, 0.0f);
	Out[161] = L.CloudToStyle;
	Out[162] = FVector4f(L.MoonlightCloudSaturation, 0.0f, 0.0f, 0.0f);
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
		// The panorama is shared by every world: only the world that owns it draws it (or makes it writable).
		const bool bOwnsPanorama = Sky->PanoramaWriter.Acquire(ViewWorld);
		if (bOwnsPanorama && !Sky->CloudPanorama)
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
		Settings.Panorama = bOwnsPanorama && Sky->CloudPanorama ? Sky->CloudPanorama->GameThread_GetRenderTargetResource() : nullptr;
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
		// (The atmosphere's transmittance table once built: the light through the air, as the sky's.)
		const UDreamAtmosphereSubsystem* Atmosphere = UDreamAtmosphereSubsystem::Get(ViewWorld);
		const UDreamAtmosphereTables* Tables = Atmosphere ? Atmosphere->GetTables() : nullptr;
		if (Tables && Tables->IsBuilt() && Tables->GetTransmittance() && CVarDreamSkyCloudRenderAtmosphere.GetValueOnGameThread() != 0)
		{
			Settings.Transmittance = Tables->GetTransmittance()->GetResource();
			Settings.TransmittanceSize = FIntPoint(Tables->GetTransmittance()->SizeX, Tables->GetTransmittance()->SizeY);
			Tables->GetBuiltParameters().Pack(Settings.Atmosphere);
		}
		if (Tables && Tables->GetSky())
		{
			Settings.SkyTable = Tables->GetSky()->GetResource();
		}
		Settings.Look = int32(Actor->GetEffectiveCloudLook());
		FDreamSkyCloudLookParameters Look;
		Actor->GetCloudLookParameters(Look);
		if (CVarDreamSkyCloudRenderColours.GetValueOnGameThread() >= 0)
		{
			Look.bCloudColorsAbsolute = CVarDreamSkyCloudRenderColours.GetValueOnGameThread() > 0;
		}
		// (The toon tones as the sky modifiers have blended them.)
		if (Sky->GetLook().Toon.IsValid())
		{
			Look.Toon = Sky->GetLook().Toon;
		}
		PackLookParameters(Look, Settings.LookParameters);

		Settings.Downsample = FMath::Clamp(CVarDreamSkyCloudRenderDownsample.GetValueOnGameThread(), 1, 4);
		Settings.MaxSamples = FMath::Clamp(CVarDreamSkyCloudRenderSamples.GetValueOnGameThread(), 16, 512);
		Settings.HistoryWeight = FMath::Clamp(CVarDreamSkyCloudRenderHistory.GetValueOnGameThread(), 0.0f, 0.98f);
		Settings.WorldSeconds = Clock->GetWorldSeconds();
		Settings.SunMapSlices = FMath::Clamp(CVarDreamSkyCloudRenderSunMapSlices.GetValueOnGameThread(), 1, 8);
		Settings.bSunMapsWhole = Clock && Clock->IsCinematicMode();
		// A cinematic frame (the world's cinematic mode, a Movie Render Queue render) stands on its own and is drawn for its
		// looks, whatever it costs: every ray at full resolution, twice the samples, no history -- a sequence runs the clock
		// hours in seconds, and the frame before's clouds are not this frame's. The temporal pass then gives this frame's
		// neighbourhood mean (a render's own temporal samples average on top).
		Settings.bCinematic = Settings.bSunMapsWhole && CVarDreamSkyCloudRenderCinematic.GetValueOnGameThread() > 0;
		if (Settings.bCinematic)
		{
			Settings.Downsample = 1;
			Settings.MaxSamples = FMath::Clamp(Settings.MaxSamples * 2, 320, 512);
		}
		Settings.TimeJumpSerial = Clock ? Clock->GetTimeJumpSerial() : 0;

		// The light shafts, where the clouds break the sun and their shadow is drawn.
		const UDreamSkyStyle* Style = Actor->GetEffectiveStyle();
		const float Shafts = Style ? FMath::Max(CVarDreamSkyLightShafts.GetValueOnGameThread(), 0.0f) * Sky->GetLightShaftStrength() : 0.0f;
		if (Settings.bClouds && Settings.Shadow && Shafts > 0.0f)
		{
			const FDreamSkyLightShafts& S = Style->LightShafts;
			const float Haze = 1.0f + FMath::Max(S.WeatherHaze, 0.0f) * FMath::Clamp(Sky->GetLook().Haze.A, 0.0f, 1.0f);
			// (Per cm.)
			Settings.Shafts[0] = FVector4f(Shafts, FMath::Max(S.Density, 0.0f) * 1.0e-5f * Haze, 1.0f / (FMath::Max(S.ScaleHeight, 0.05f) * 1.0e5f),
				FMath::Clamp(S.Anisotropy, -0.9f, 0.9f));
			Settings.Shafts[1] = FVector4f(FMath::Max(S.Distance, 0.1f) * 1.0e5f, float(FMath::Clamp(CVarDreamSkyLightShaftSamples.GetValueOnGameThread(), 4, 128)),
				CVarDreamSkyLightShaftsDebug.GetValueOnGameThread() > 0 ? 1.0f : 0.0f, 0.0f);
			Settings.bShafts = Settings.Shafts[0].Y > 0.0f;
		}
	}

	// The sun's lens flare: whatever draws the clouds.
	if (const UDreamSkyStyle* Style = Actor ? Actor->GetEffectiveStyle() : nullptr)
	{
		const FDreamSkyLensFlare& F = Style->LensFlare;
		const int32 Forced = CVarDreamSkyLensFlarePreset.GetValueOnGameThread();
		const EDreamSkyLensFlarePreset Preset = Forced >= 0 ? EDreamSkyLensFlarePreset(FMath::Min(Forced, 3)) : F.Preset;
		const float Strength = FMath::Max(CVarDreamSkyLensFlare.GetValueOnGameThread(), 0.0f) * FMath::Max(Sky->GetLook().LensFlare, 0.0f);
		if (Clock && Preset != EDreamSkyLensFlarePreset::None && Strength > 0.0f)
		{
			const FVector Sun = Clock->GetCelestial().SunDirection;
			Settings.Flare.bEnabled = Sun.Z > -0.05;
			Settings.Flare.SunDirection = FVector4f(float(Sun.X), float(Sun.Y), float(Sun.Z), FMath::DegreesToRadians(FMath::Max(Style->SunDiskSize * Sky->GetLook().SunDiskScale, 0.05f)));
			Settings.Flare.Flare = FVector4f(Strength, FMath::Max(F.Threshold, 0.0f), float(uint8(Preset)), 0.0f);
			Settings.Flare.Tint = FVector4f(F.Tint.R, F.Tint.G, F.Tint.B, 1.0f);
		}

		// The drops on the lens, while it is wet.
		const FDreamSkyScreenDroplets& D = Style->ScreenDroplets;
		const float ForcedDrops = CVarDreamSkyScreenDroplets.GetValueOnGameThread();
		const float Drops = ForcedDrops >= 0.0f ? ForcedDrops : (D.bEnabled ? FMath::Max(D.Intensity, 0.0f) : 0.0f);
		const float Wet = Sky->GetScreenWetness();
		if (Drops > 0.0f && Wet > 1.0e-3f)
		{
			Settings.Screen.bEnabled = true;
			Settings.Screen.Screen = FVector4f(Wet, Drops, FMath::Max(D.DropSize, 0.25f), FMath::Max(D.Refraction, 0.0f));
		}

		// And the frost, while there is some.
		const FDreamSkyScreenFrost& Fr = Style->ScreenFrost;
		const float ForcedFrost = CVarDreamSkyScreenFrost.GetValueOnGameThread();
		const float FrostStrength = ForcedFrost >= 0.0f ? ForcedFrost : (Fr.bEnabled ? FMath::Max(Fr.Intensity, 0.0f) : 0.0f);
		const float Frosted = Sky->GetScreenFrost();
		if (FrostStrength > 0.0f && Frosted > 1.0e-3f)
		{
			Settings.Screen.bEnabled = true;
			Settings.Screen.Frost = FVector4f(Frosted, FrostStrength, FMath::Max(Fr.CrystalSize, 0.25f), FMath::Max(Fr.Blur, 0.0f));
			Settings.Screen.FrostTint = FVector4f(Fr.Tint.R, Fr.Tint.G, Fr.Tint.B, FMath::Max(Fr.Refraction, 0.0f));
		}

		// And the hot air's shimmer, on a hot day.
		const FDreamSkyHeatHaze& Hz = Style->HeatHaze;
		const float ForcedHaze = CVarDreamSkyHeatHaze.GetValueOnGameThread();
		const float HazeStrength = ForcedHaze >= 0.0f ? ForcedHaze : (Hz.bEnabled ? FMath::Max(Hz.Intensity, 0.0f) : 0.0f);
		const float Haze = Sky->GetHeatHaze() * HazeStrength;
		if (Haze > 1.0e-3f)
		{
			Settings.Screen.bEnabled = true;
			const float Near = FMath::Max(Hz.NearDistance, 0.0f) * 100.0f;
			Settings.Screen.Haze = FVector4f(Haze, FMath::Sin(FMath::DegreesToRadians(FMath::Max(Hz.BandDegrees, 0.1f))), Near,
				FMath::Max(Hz.FullDistance * 100.0f, Near + 100.0f));
			Settings.Screen.HazeMotion = FVector4f(FMath::Max(Hz.Speed, 0.0f), 0.0f, 0.0f, 0.0f);
		}

		// And the rainbow, while there is rain in the air in the low light.
		const FDreamSkyRainbow& Rb = Style->Rainbow;
		const float ForcedBow = CVarDreamSkyRainbow.GetValueOnGameThread();
		const float BowStrength = ForcedBow >= 0.0f ? ForcedBow : (Rb.bEnabled ? FMath::Max(Rb.Intensity, 0.0f) : 0.0f);
		const float Bow = Sky->GetRainbow() * BowStrength;
		if (Bow > 1.0e-3f)
		{
			const FDreamSkyLighting& L = Sky->GetLighting();
			const FVector Away = -L.MainLightDirection.GetSafeNormal();
			Settings.Screen.bEnabled = true;
			Settings.Screen.Rainbow = FVector4f(Bow, FMath::Clamp(Rb.SecondBow, 0.0f, 1.0f), FMath::Clamp(Rb.Patchiness, 0.0f, 1.0f), FMath::Max(Rb.Width, 0.5f));
			Settings.Screen.RainbowAxis = FVector4f(float(Away.X), float(Away.Y), float(Away.Z), FMath::Max(L.MainLightIlluminance, 0.0f));
			Settings.Screen.RainbowLight = FVector4f(L.MainLightColor.R, L.MainLightColor.G, L.MainLightColor.B, 0.0f);
			const float Near = FMath::Max(Rb.NearDistance, 0.0f) * 100.0f;
			Settings.Screen.RainbowRange = FVector4f(Near, FMath::Max(Rb.FullDistance * 100.0f, Near + 100.0f), 0.0f, 0.0f);
		}
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
		FillAtmosphere(Common, Settings);
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
			Params->MarchDebug = Settings.DebugMode == 2 ? 1u : (Settings.DebugMode == 4 ? 2u : 0u);
			const DreamSkyCloudRender::FGraphClouds& Graph = GraphBuilder.Blackboard.GetChecked<DreamSkyCloudRender::FGraphClouds>();
			Params->NearTau = Graph.SunMaps[0];
			Params->FarTau = Graph.SunMaps[1];
			Params->TauSampler = TStaticSamplerState<SF_Bilinear, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();
			Params->NearCenterExtent = SunMaps[0].CenterExtent;
			Params->FarCenterExtent = SunMaps[1].CenterExtent;
			Params->StormProximity = Graph.StormProximity;
			Params->ProximityResolution = DreamSkyCloudRender::ProximityResolution;
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
		// (How far the wind has carried the clouds' field, as DCS_Shape reads it: DDW_Cloud1's x, y in 2.2 km times the
		// scale, DDW_Cloud0's z.)
		const FVector2f Drift = FVector2f(Settings.Parameters[6].X, Settings.Parameters[6].Y) * 220000.0f * FMath::Max(Settings.Parameters[5].Z, 0.05f);
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
			Params->HistoryWeight = Settings.bCinematic ? -1.0f : (bHistory ? Settings.HistoryWeight : (Settings.HistoryWeight > 0.0f ? -1.0f : 0.0f));
			// How far the wind has carried the clouds since the history was drawn: a cloud there stood that much further
			// on (a jump of the clock or the drift's wrap, tens of kilometres, none: the neighbourhood then holds it).
			const FVector2f Moved = Drift - (bHistory ? History->Drift : Drift);
			Params->CloudDrift = Moved.Size() < 5.0e6f ? Moved : FVector2f::ZeroVector;
			// And how much of the history still holds by how far the world's clock ran since: the clouds form, dissolve
			// and light up within the game seconds a frame spans, which the drift does not follow. Held at the still
			// view's weight, the neighbourhood's range cut the old frames into blocks round the clouds' edges, a darker rim
			// with them (the dusk sequence: six hours in twenty seconds). All of it in real time, 0.94 of it at the
			// showcase's twelve-minute day, 0.6 in the dusk sequence, half at a game minute a frame, none across a jump of
			// the clock.
			const double Ran = bHistory ? FMath::Abs(Settings.WorldSeconds - History->WorldSeconds) : 0.0;
			Params->HistoryFresh = float(1.0 / (1.0 + Ran / 60.0));
			Params->TemporalDebug = Settings.DebugMode == 3 ? 1u : 0u;
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
			History->Drift = Drift;
			History->WorldSeconds = Settings.WorldSeconds;
		}
		Clouds = OutColor;

		// Kept for the composite over the scene before the post processing, when the view looks down on clouds, and for
		// the light shafts.
		if (IsOverCloudBase(InView) || Settings.bShafts)
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
	Params->DebugMode = Clouds || Settings.DebugMode == 3 ? 0u : uint32(Settings.DebugMode);
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
	// them): a slice of one cascade, the near and the far taking turns (FSunMap), or a cascade whole when it must be. Each
	// graph registers them for itself.
	const uint64 Frame = GFrameCounterRenderThread;
	DreamSkyCloudRender::FGraphClouds& Graph = GraphBuilder.Blackboard.GetOrCreate<DreamSkyCloudRender::FGraphClouds>();
	if (Graph.bSunMaps)
	{
		return;
	}
	Graph.bSunMaps = true;
	const bool bFirstThisFrame = SunMapFrame != Frame;
	SunMapFrame = Frame;
	const int32 Slices = FMath::Clamp(RenderSettings.SunMapSlices, 1, 8);
	const FVector4f& MainLight = RenderSettings.Parameters[11];   // (DreamSkyCloudRender.usf's P_MainLight)
	const FVector3f Light = FVector3f(MainLight.X, MainLight.Y, MainLight.Z).GetSafeNormal();
	const bool bJumped = RenderSettings.TimeJumpSerial != SunMapJumpSerial;
	const int32 Turn = int32(SunMapTurn & 1);
	if (bFirstThisFrame)
	{
		SunMapJumpSerial = RenderSettings.TimeJumpSerial;
		++SunMapTurn;
	}
	// (How far the light may have turned since the oldest slice of a cascade was drawn before it is redrawn whole: the
	// shadows are 500 m texels, and a tower's shadow 10 km out moves some 45 m a quarter of a degree.)
	const float MaxTurnCos = FMath::Cos(FMath::DegreesToRadians(0.25f));
	for (int32 Cascade = 0; Cascade < 2; ++Cascade)
	{
		FSunMap& Map = SunMaps[Cascade];
		const FIntVector Resolution = Cascade == 0 ? FIntVector(96, 96, 24) : FIntVector(64, 64, 16);
		const float HalfExtent = Cascade == 0 ? 2400000.0f : 9000000.0f;
		// (Centred on the view, snapped to its texels: the clouds' shadows do not swim as the view moves.)
		const double Texel = 2.0 * HalfExtent / Resolution.X;
		const FVector2D Centre(FMath::RoundToDouble(InView.ViewLocation.X / Texel) * Texel, FMath::RoundToDouble(InView.ViewLocation.Y / Texel) * Texel);
		const FVector4f CenterExtent(float(Centre.X), float(Centre.Y), HalfExtent, 1.0f);

		// (Whole at once: none yet, the cinematic mode, a jump of the clock. Whole on its turn, a frame at most behind: moved
		// a texel with the view -- the old one still covers the view, a texel off its middle --, the light turned -- a
		// quick sun redraws the cascades whole by turns, as before the slices --, or no slices.)
		const bool bHave = Map.Texture.IsValid() && Map.Ground.IsValid() && Map.Texture->GetDesc().GetSize() == Resolution;
		const bool bStale = CenterExtent != Map.CenterExtent || FVector3f::DotProduct(Light, Map.Light) < MaxTurnCos || Slices == 1;
		const bool bWhole = !bHave || (bFirstThisFrame && (RenderSettings.bSunMapsWhole || bJumped || (bStale && Turn == Cascade)));
		const bool bSlice = !bWhole && bFirstThisFrame && Slices > 1 && Turn == Cascade;
		if (!bWhole && !bSlice)
		{
			Graph.SunMaps[Cascade] = GraphBuilder.RegisterExternalTexture(Map.Texture);
			Graph.GroundMaps[Cascade] = GraphBuilder.RegisterExternalTexture(Map.Ground);
			continue;
		}

		FRDGTextureRef Tau = nullptr;
		FRDGTextureRef Ground = nullptr;
		if (bHave)
		{
			Tau = GraphBuilder.RegisterExternalTexture(Map.Texture);
			Ground = GraphBuilder.RegisterExternalTexture(Map.Ground);
		}
		else
		{
			Tau = GraphBuilder.CreateTexture(FRDGTextureDesc::Create3D(Resolution, PF_R16F, FClearValueBinding::None,
				TexCreate_ShaderResource | TexCreate_UAV), Cascade == 0 ? TEXT("DreamSky.SunMapNear") : TEXT("DreamSky.SunMapFar"));
			Ground = GraphBuilder.CreateTexture(FRDGTextureDesc::Create2D(FIntPoint(Resolution.X, Resolution.Y), PF_R16F,
				FClearValueBinding::None, TexCreate_ShaderResource | TexCreate_UAV), Cascade == 0 ? TEXT("DreamSky.GroundMapNear") : TEXT("DreamSky.GroundMapFar"));
		}
		// (The rows of this slice; a whole cascade from its first. A whole redraw starts the rounds of slices over.)
		const int32 Rows = bWhole ? Resolution.Y : FMath::DivideAndRoundUp(Resolution.Y, Slices);
		const int32 FirstRow = bWhole ? 0 : Map.NextSlice * Rows;
		if (bWhole)
		{
			Map.CenterExtent = CenterExtent;
			Map.Light = Light;
			Map.RoundLight = Light;
			Map.NextSlice = 0;
		}
		else
		{
			if (Map.NextSlice == 0)
			{
				// (A round begins: the oldest slices now are the round before's.)
				Map.Light = Map.RoundLight;
				Map.RoundLight = Light;
			}
			Map.NextSlice = (Map.NextSlice + 1) % Slices;
		}
		// (One more layer of threads: the ground's march, SunMapCS.)
		const FIntVector Count(Resolution.X, FMath::Min(Rows, Resolution.Y - FirstRow), Resolution.Z + 1);

		FDreamSkyCloudSunMapCS::FParameters* Params = GraphBuilder.AllocParameters<FDreamSkyCloudSunMapCS::FParameters>();
		FillCommon(Params->Common);
		Params->WeatherMap = WeatherMap;
		Params->WeatherMapSampler = TStaticSamplerState<SF_Bilinear, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();
		Params->CloudNoise = CloudNoise;
		Params->CloudNoiseSampler = TStaticSamplerState<SF_Trilinear, AM_Wrap, AM_Wrap, AM_Wrap>::GetRHI();
		Params->MapCenterExtent = Map.CenterExtent;
		Params->MapResolution = Resolution;
		Params->MapOffset = FIntVector(0, FirstRow, 0);
		Params->OutTau = GraphBuilder.CreateUAV(Tau);
		Params->OutGroundTau = GraphBuilder.CreateUAV(Ground);
		TShaderMapRef<FDreamSkyCloudSunMapCS> Shader(ShaderMap);
		FComputeShaderUtils::AddPass(GraphBuilder, RDG_EVENT_NAME("SunMap %s %dx%dx%d%s", Cascade == 0 ? TEXT("near") : TEXT("far"), Count.X, Count.Y, Resolution.Z,
			bWhole ? TEXT("") : TEXT(" (slice)")), Shader, Params, FComputeShaderUtils::GetGroupCount(Count, FDreamSkyCloudSunMapCS::GroupSize));
		if (!bHave)
		{
			// (Kept for the frames after, once drawn.)
			GraphBuilder.QueueTextureExtraction(Tau, &Map.Texture);
			GraphBuilder.QueueTextureExtraction(Ground, &Map.Ground);
		}
		Graph.SunMaps[Cascade] = Tau;
		Graph.GroundMaps[Cascade] = Ground;
	}

	// The storm proximity map: the cells a sample may search for storms from, then the nearest of them spread over the
	// map in a jump flood (64 cells, 32, ... 1), then how far that is. Once a frame (the weather map moves with the view).
	if (!StormProximity.IsValid() || bFirstThisFrame)
	{
		const int32 Resolution = DreamSkyCloudRender::ProximityResolution;
		const FIntPoint Size(Resolution, Resolution);
		const FIntVector Groups = FComputeShaderUtils::GetGroupCount(Size, FDreamSkyCloudMarchCS::GroupSize);
		const FRDGTextureDesc SeedDesc = FRDGTextureDesc::Create2D(Size, PF_G16R16F, FClearValueBinding::None, TexCreate_ShaderResource | TexCreate_UAV);
		FRDGTextureRef Seeds[2] = {GraphBuilder.CreateTexture(SeedDesc, TEXT("DreamSky.StormSeed")), GraphBuilder.CreateTexture(SeedDesc, TEXT("DreamSky.StormSeed"))};
		{
			FDreamSkyStormSeedCS::FParameters* Params = GraphBuilder.AllocParameters<FDreamSkyStormSeedCS::FParameters>();
			FillCommon(Params->Common);
			Params->WeatherMap = WeatherMap;
			Params->WeatherMapSampler = TStaticSamplerState<SF_Bilinear, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();
			Params->ProximityResolution = Resolution;
			Params->OutSeed = GraphBuilder.CreateUAV(Seeds[0]);
			FComputeShaderUtils::AddPass(GraphBuilder, RDG_EVENT_NAME("StormSeed %dx%d", Resolution, Resolution),
				TShaderMapRef<FDreamSkyStormSeedCS>(ShaderMap), Params, Groups);
		}
		int32 Current = 0;
		for (int32 Step = Resolution / 2; Step >= 1; Step /= 2)
		{
			FDreamSkyStormJumpCS::FParameters* Params = GraphBuilder.AllocParameters<FDreamSkyStormJumpCS::FParameters>();
			Params->ProximityResolution = Resolution;
			Params->JumpStep = Step;
			Params->InSeed = Seeds[Current];
			Params->OutSeed = GraphBuilder.CreateUAV(Seeds[1 - Current]);
			FComputeShaderUtils::AddPass(GraphBuilder, RDG_EVENT_NAME("StormJump %d", Step), TShaderMapRef<FDreamSkyStormJumpCS>(ShaderMap), Params, Groups);
			Current = 1 - Current;
		}
		FRDGTextureRef Proximity = GraphBuilder.CreateTexture(FRDGTextureDesc::Create2D(Size, PF_R32_FLOAT, FClearValueBinding::None,
			TexCreate_ShaderResource | TexCreate_UAV), TEXT("DreamSky.StormProximity"));
		{
			FDreamSkyStormProximityCS::FParameters* Params = GraphBuilder.AllocParameters<FDreamSkyStormProximityCS::FParameters>();
			FillCommon(Params->Common);
			Params->ProximityResolution = Resolution;
			Params->InSeed = Seeds[Current];
			Params->OutProximity = GraphBuilder.CreateUAV(Proximity);
			FComputeShaderUtils::AddPass(GraphBuilder, RDG_EVENT_NAME("StormProximity %dx%d", Resolution, Resolution),
				TShaderMapRef<FDreamSkyStormProximityCS>(ShaderMap), Params, Groups);
		}
		GraphBuilder.QueueTextureExtraction(Proximity, &StormProximity);
		Graph.StormProximity = Proximity;
	}
	else
	{
		Graph.StormProximity = GraphBuilder.RegisterExternalTexture(StormProximity);
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
		Params->MarchDebug = 0u;
		Params->NearTau = Graph.SunMaps[0];
		Params->FarTau = Graph.SunMaps[1];
		Params->TauSampler = TStaticSamplerState<SF_Bilinear, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();
		Params->NearCenterExtent = SunMaps[0].CenterExtent;
		Params->FarCenterExtent = SunMaps[1].CenterExtent;
		Params->StormProximity = Graph.StormProximity;
		Params->ProximityResolution = DreamSkyCloudRender::ProximityResolution;
		Params->PanoramaResolution = Resolution;
		Params->OutPanorama = GraphBuilder.CreateUAV(Panorama);
		FDreamSkyCloudMarchCS::FPermutationDomain Permutation;
		Permutation.Set<FDreamSkyCloudMarchCS::FLook>(RenderSettings.Look);
		TShaderMapRef<FDreamSkyCloudPanoramaCS> Shader(ShaderMap, Permutation);
		FComputeShaderUtils::AddPass(GraphBuilder, RDG_EVENT_NAME("Panorama %dx%d", Resolution.X, Resolution.Y), Shader, Params,
			FComputeShaderUtils::GetGroupCount(Resolution, FDreamSkyCloudMarchCS::GroupSize));
		GraphBuilder.SetTextureAccessFinal(Panorama, ERHIAccess::SRVMask);
	}

	// The clouds' shadow on the ground: the cascades' ground maps (the transmittance from the layer's base the whole way up
	// toward the sun), into the texture the sun light's light function reads (ADreamSky::UpdateCloudShadow).
	const FFrameSettings& Settings = RenderSettings;
	FRHITexture* ShadowRHI = Settings.Shadow ? Settings.Shadow->TextureRHI.GetReference() : nullptr;
	if (ShadowRHI && EnumHasAnyFlags(ShadowRHI->GetFlags(), TexCreate_UAV))
	{
		FRDGTextureRef Shadow = RegisterExternalTexture(GraphBuilder, ShadowRHI, TEXT("DreamSky.CloudShadow"));
		const FIntPoint Resolution(ShadowRHI->GetSizeXYZ().X, ShadowRHI->GetSizeXYZ().Y);
		FDreamSkyCloudShadowCS::FParameters* Params = GraphBuilder.AllocParameters<FDreamSkyCloudShadowCS::FParameters>();
		FillCommon(Params->Common);
		Params->WeatherMap = WeatherMap;
		Params->WeatherMapSampler = TStaticSamplerState<SF_Bilinear, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();
		Params->CloudNoise = CloudNoise;
		Params->CloudNoiseSampler = TStaticSamplerState<SF_Trilinear, AM_Wrap, AM_Wrap, AM_Wrap>::GetRHI();
		Params->ReferenceSteps = FMath::Clamp(CVarDreamSkyCloudRenderGroundReference.GetValueOnRenderThread(), 0, 4096);
		Params->NearGround = Graph.GroundMaps[0];
		Params->FarGround = Graph.GroundMaps[1];
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
		Graph.Shadow = Shadow;
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
	if (Pass == EPostProcessingPass::MotionBlur && RenderSettings.Screen.bEnabled)
	{
		InOutPassCallbacks.Add(FPostProcessingPassDelegate::CreateRaw(this, &FDreamSkyCloudViewExtension::DrawScreen));
	}
	if (Pass == EPostProcessingPass::AfterDOF && RenderSettings.Flare.bEnabled && InView.IsPerspectiveProjection())
	{
		InOutPassCallbacks.Add(FPostProcessingPassDelegate::CreateRaw(this, &FDreamSkyCloudViewExtension::DrawLensFlare));
	}
	if (Pass != EPostProcessingPass::BeforeDOF)
	{
		return;
	}
	// (The shafts first: the clouds below the view cover the air beneath them.)
	if (RenderSettings.bClouds && RenderSettings.bShafts && InView.IsPerspectiveProjection())
	{
		InOutPassCallbacks.Add(FPostProcessingPassDelegate::CreateRaw(this, &FDreamSkyCloudViewExtension::DrawLightShafts));
	}
	if (IsOverCloudBase(InView))
	{
		InOutPassCallbacks.Add(FPostProcessingPassDelegate::CreateRaw(this, &FDreamSkyCloudViewExtension::CompositeBelow));
	}
}

FScreenPassTexture FDreamSkyCloudViewExtension::DrawScreen(FRDGBuilder& GraphBuilder, const FSceneView& InView, const FPostProcessMaterialInputs& Inputs)
{
	return DreamSky::Screen::AddPasses(GraphBuilder, InView, Inputs, RenderSettings.Screen);
}

FScreenPassTexture FDreamSkyCloudViewExtension::DrawLensFlare(FRDGBuilder& GraphBuilder, const FSceneView& InView, const FPostProcessMaterialInputs& Inputs)
{
	FScreenPassTexture SceneColor = FScreenPassTexture::CopyFromSlice(GraphBuilder, Inputs.GetInput(EPostProcessMaterialInput::SceneColor));
	DreamSky::LensFlare::AddPasses(GraphBuilder, InView, SceneColor, RenderSettings.Flare);
	return SceneColor;
}

FScreenPassTexture FDreamSkyCloudViewExtension::DrawLightShafts(FRDGBuilder& GraphBuilder, const FSceneView& InView, const FPostProcessMaterialInputs& Inputs)
{
	FScreenPassTexture SceneColor = FScreenPassTexture::CopyFromSlice(GraphBuilder, Inputs.GetInput(EPostProcessMaterialInput::SceneColor));
	const DreamSkyCloudRender::FGraphClouds* Graph = GraphBuilder.Blackboard.Get<DreamSkyCloudRender::FGraphClouds>();
	const DreamSkyCloudRender::FBelow* Entry = Graph ? Graph->Below.Find(&InView) : nullptr;
	const FFrameSettings& Settings = RenderSettings;
	if (!Entry || !Entry->SceneDepth || !SceneColor.IsValid() || !Settings.bShafts)
	{
		return SceneColor;
	}
	FRDGTextureRef Shadow = Graph->Shadow;
	if (!Shadow)
	{
		FRHITexture* ShadowRHI = Settings.Shadow ? Settings.Shadow->TextureRHI.GetReference() : nullptr;
		if (!ShadowRHI)
		{
			return SceneColor;
		}
		Shadow = RegisterExternalTexture(GraphBuilder, ShadowRHI, TEXT("DreamSky.CloudShadow"));
	}
	RDG_EVENT_SCOPE(GraphBuilder, "DreamSky.LightShafts");
	FGlobalShaderMap* ShaderMap = GetGlobalShaderMap(InView.GetFeatureLevel());
	auto FillCommon = [&](DreamSkyCloudRender::FCommonParameters& Common)
	{
		Common.View = InView.ViewUniformBuffer;
		for (int32 Index = 0; Index < NumParameters; ++Index)
		{
			Common.Parameters[Index] = Entry->Parameters[Index];
		}
		for (int32 Index = 0; Index < NumLookParameters; ++Index)
		{
			Common.LookParameters[Index] = Entry->LookParameters[Index];
		}
		FillAtmosphere(Common, RenderSettings);
		Common.CameraWorld = FVector3f(InView.ViewLocation);
		Common.SkyDepth = Settings.SkyDepth;
		Common.Downsample = Entry->Downsample;
	};

	// One ray per block of the clouds' rays, at their resolution; then up to the scene's, softened over the neighbours
	// that stand as far.
	const FIntPoint Half = FIntPoint::DivideAndRoundUp(Entry->SceneDepth->Desc.Extent, Entry->Downsample);
	FRDGTextureRef Shafts = GraphBuilder.CreateTexture(FRDGTextureDesc::Create2D(Half, PF_FloatRGBA, FClearValueBinding::None,
		TexCreate_ShaderResource | TexCreate_UAV), TEXT("DreamSky.LightShafts"));
	{
		FDreamSkyLightShaftsCS::FParameters* Params = GraphBuilder.AllocParameters<FDreamSkyLightShaftsCS::FParameters>();
		FillCommon(Params->Common);
		Params->SceneDepth = Entry->SceneDepth;
		Params->ShaftShadow = Shadow;
		Params->ShaftShadowSampler = TStaticSamplerState<SF_Bilinear, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();
		Params->ShadowPlacement = Settings.ShadowPlacement;
		Params->ShaftParameters[0] = Settings.Shafts[0];
		Params->ShaftParameters[1] = Settings.Shafts[1];
		Params->OutShafts = GraphBuilder.CreateUAV(Shafts);
		TShaderMapRef<FDreamSkyLightShaftsCS> Shader(ShaderMap);
		FComputeShaderUtils::AddPass(GraphBuilder, RDG_EVENT_NAME("March %dx%d", Half.X, Half.Y), Shader, Params, FComputeShaderUtils::GetGroupCount(Half, 8));
	}
	FDreamSkyLightShaftsCompositePS::FParameters* Params = GraphBuilder.AllocParameters<FDreamSkyLightShaftsCompositePS::FParameters>();
	FillCommon(Params->Common);
	Params->SceneDepth = Entry->SceneDepth;
	Params->Shafts = Shafts;
	Params->ShaftParameters[0] = Settings.Shafts[0];
	Params->ShaftParameters[1] = Settings.Shafts[1];
	Params->RenderTargets[0] = FRenderTargetBinding(SceneColor.Texture, ERenderTargetLoadAction::ELoad);
	TShaderMapRef<FDreamSkyLightShaftsCompositePS> Shader(ShaderMap);
	FPixelShaderUtils::AddFullscreenPass(GraphBuilder, ShaderMap, RDG_EVENT_NAME("Composite"), Shader, Params, SceneColor.ViewRect,
		TStaticBlendState<CW_RGB, BO_Add, BF_One, BF_One>::GetRHI());
	return SceneColor;
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
	FillAtmosphere(Params->Common, RenderSettings);
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
