// Copyright DreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "RenderGraphResources.h"
#include "SceneViewExtension.h"
#include "ScreenPass.h"
#include "DreamSkyLensFlare.h"
#include "DreamSkyScreen.h"

class FTextureResource;
struct FDreamSkyCloudLookParameters;
class FGlobalShaderMap;
namespace DreamSkyCloudRender { class FCommonParameters; }
class UDreamSkySubsystem;

/**
 * The sky cloud renderer (ADreamSky::DrawsClouds): per view, right after the base pass (before lighting, fog and
 * translucency), a compute pass marches the cloud layer at reduced resolution (DreamSkyCloudRender.usf), a second one
 * blends it with the previous frames reprojected by the clouds' depth, and a full-screen pass composites it into the sky
 * pixels -- the far plane and whatever lies beyond the dome's depth -- so the engine's fog, aerial perspective and
 * translucency land on top of it. The dome draws the sky behind (and, in the sky light capture, the renderer's panorama:
 * DDW_CloudLayer.w = 1).
 */
class FDreamSkyCloudViewExtension : public FWorldSceneViewExtension
{
public:
	FDreamSkyCloudViewExtension(const FAutoRegister& AutoRegister, UWorld* InWorld, UDreamSkySubsystem* InSubsystem);

	//~ ISceneViewExtension
	virtual void SetupViewFamily(FSceneViewFamily& InViewFamily) override;
	virtual void SetupView(FSceneViewFamily& InViewFamily, FSceneView& InView) override {}
	virtual void BeginRenderViewFamily(FSceneViewFamily& InViewFamily) override {}
	virtual void PostRenderBasePassDeferred_RenderThread(FRDGBuilder& GraphBuilder, FSceneView& InView, const FRenderTargetBindingSlots& RenderTargets, TRDGUniformBufferRef<FSceneTextureUniformParameters> SceneTextures) override;
	virtual void SubscribeToPostProcessingPass(EPostProcessingPass Pass, const FSceneView& InView, FPostProcessingPassDelegateArray& InOutPassCallbacks, bool bIsPassEnabled) override;

	void Detach() { Subsystem = nullptr; }

	/** The shared parameter collection's vectors the march reads, in this order (DreamSkyCloudRender.usf's DSKY_V_*). */
	static const TArray<FName>& GetParameterNames();
	static constexpr int32 NumParameters = 31;
	/** The realistic and painted looks' settings (FDreamSkyCloudLookParameters), packed for the shaders. */
	static constexpr int32 NumLookParameters = 18 + 128 + 7 + 4 + 5 + 1; // (the toon ramps: FDreamSkyToonBaked::RampSize / 2; its finer settings; the painted look's; the clouds' colour scales)
	static void PackLookParameters(const FDreamSkyCloudLookParameters& Look, FVector4f Out[NumLookParameters]);

private:
	/** What the game thread decided for the frame; copied to the render thread ahead of the family's rendering. */
	struct FFrameSettings
	{
		/** View depth (cm) beyond which a pixel counts as sky; the far plane always does. */
		float SkyDepth = 1.0e9f;
		int32 DebugMode = 0;
		bool bClouds = false;
		int32 Look = 0;
		int32 Downsample = 2;
		int32 MaxSamples = 160;
		float HistoryWeight = 0.9f;
		FVector4f Parameters[NumParameters];
		FVector4f LookParameters[NumLookParameters];
		FTextureResource* Noise = nullptr;
		FTextureResource* Map = nullptr;
		/** The atmosphere's transmittance table (DreamAtmosphere), and the air it was built for (null: not built yet,
		 *  the light through the air is the analytic one). */
		FTextureResource* Transmittance = nullptr;
		/** The atmosphere's sky table (the air between the clouds, as the dome's). */
		FTextureResource* SkyTable = nullptr;
		FIntPoint TransmittanceSize = FIntPoint::ZeroValue;
		FVector4f Atmosphere[5] = { FVector4f(0.0f), FVector4f(0.0f), FVector4f(0.0f), FVector4f(0.0f), FVector4f(0.0f) };
		/** The ground shadow's texture (ADreamSky::GetCloudShadowTarget) and where it lies; null: none. */
		FTextureResource* Shadow = nullptr;
		/** The panorama the dome shows in the sky light capture (RT_DreamCloudPanorama); null: none. */
		FTextureResource* Panorama = nullptr;
		FVector4f ShadowPlacement = FVector4f(0.0f, 0.0f, 1.0f, 0.0f);
		/** The light shafts (DreamSkyCloudRender.usf's SH_*); drawn while bShafts. */
		bool bShafts = false;
		FVector4f Shafts[2];
		/** The sun's lens flare (drawn whether or not the clouds are). */
		DreamSky::LensFlare::FSettings Flare;
		/** What the weather leaves on the lens (drawn whether or not the clouds are). */
		DreamSky::Screen::FSettings Screen;
		/** The sun maps: in how many slices a cascade is redrawn over the frames (DreamSky.CloudRender.SunMapSlices; 1:
		 *  whole); whole every frame in the world's cinematic mode; the clock's time-jump serial (a jump redraws them
		 *  whole at once). */
		int32 SunMapSlices = 6;
		bool bSunMapsWhole = false;
		uint32 TimeJumpSerial = 0;
	};

	/** A view's clouds of the frame before (render thread only). */
	struct FHistory
	{
		TRefCountPtr<IPooledRenderTarget> Color;
		TRefCountPtr<IPooledRenderTarget> Depth;
		TRefCountPtr<FRDGPooledBuffer> RaySize;   // the ray grid it was drawn at (the view's size under its screen percentage)
		FIntPoint ViewSize = FIntPoint::ZeroValue;
		int32 Downsample = 0;
		int32 Look = -1;
		uint64 LastFrame = 0;
	};

	/**
	 * The optical depth toward the sun round the view, in two cascades (some 48 km across at 500 m texels, 180 km at
	 * 2.8 km), each 10 km tall over the layer's base: what the realistic clouds are shadowed by. Kept between frames:
	 * a frame redraws a slice of one cascade (rows of it, the cascades taking turns), so each is redrawn whole every
	 * 2 x SunMapSlices frames; it is redrawn whole at once when it moves with the view (a texel), when the light has
	 * turned since its slices began (UpdateSunMaps), on a jump of the clock, and every frame in the cinematic mode.
	 */
	struct FSunMap
	{
		TRefCountPtr<IPooledRenderTarget> Texture;
		/** The ground's: the optical depth from the layer's base the whole way up (the ground shadow reads it). */
		TRefCountPtr<IPooledRenderTarget> Ground;
		FVector4f CenterExtent = FVector4f(0.0f, 0.0f, 1.0f, 0.0f);
		/** The light it was drawn for: the oldest of its slices' (the light when the round before the last began). */
		FVector3f Light = FVector3f::ZeroVector;
		/** The light when this round of its slices began, and the slice drawn next. */
		FVector3f RoundLight = FVector3f::ZeroVector;
		int32 NextSlice = 0;
	};

	void RenderClouds(FRDGBuilder& GraphBuilder, FSceneView& InView, const FRenderTargetBindingSlots& RenderTargets);

	/** True when the view stands over the cloud layer's base: clouds may stand between it and the scene. */
	bool IsOverCloudBase(const FSceneView& InView) const;

	FScreenPassTexture CompositeBelow(FRDGBuilder& GraphBuilder, const FSceneView& InView, const FPostProcessMaterialInputs& Inputs);
	/** The light shafts below the clouds, over the scene before the post processing (and before the clouds below). */
	/** The sun's lens flare, over the scene after the depth of field. */
	/** The drops on the lens, over the finished picture (after the upscale). */
	/** The atmosphere's part of the march's common parameters (the transmittance table, the air), from Settings. */
	static void FillAtmosphere(DreamSkyCloudRender::FCommonParameters& Common, const FFrameSettings& Settings);
	FScreenPassTexture DrawScreen(FRDGBuilder& GraphBuilder, const FSceneView& InView, const FPostProcessMaterialInputs& Inputs);
	FScreenPassTexture DrawLensFlare(FRDGBuilder& GraphBuilder, const FSceneView& InView, const FPostProcessMaterialInputs& Inputs);
	FScreenPassTexture DrawLightShafts(FRDGBuilder& GraphBuilder, const FSceneView& InView, const FPostProcessMaterialInputs& Inputs);
	void UpdateSunMaps(FRDGBuilder& GraphBuilder, const FSceneView& InView, FGlobalShaderMap* ShaderMap, FRDGTextureRef WeatherMap,
		FRDGTextureRef CloudNoise, TFunctionRef<void(DreamSkyCloudRender::FCommonParameters&)> FillCommon);

	TWeakObjectPtr<UDreamSkySubsystem> Subsystem;
	FFrameSettings RenderSettings;
	TMap<uint32, TSharedPtr<FHistory>> Histories;
	FSunMap SunMaps[2];
	/**
	 * How far each cell over the weather map is from the nearest place a sample searches for storms (cm): how far a march
	 * over the layer may step where its sample searched for none. Redrawn a frame, with the sun maps.
	 */
	TRefCountPtr<IPooledRenderTarget> StormProximity;
	/** The frame the sun maps were last brought up to date in (by whichever graph drew first in it), how many frames
	 *  have brought them up to date (the cascade's turn), and the clock's time-jump serial they saw last. */
	uint64 SunMapFrame = ~uint64(0);
	uint64 SunMapTurn = 0;
	uint32 SunMapJumpSerial = 0;
};
