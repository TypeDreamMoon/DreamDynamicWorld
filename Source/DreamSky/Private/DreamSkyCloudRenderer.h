// Copyright DreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "RenderGraphResources.h"
#include "SceneViewExtension.h"

class FTextureResource;
class FGlobalShaderMap;
namespace DreamSkyCloudRender { class FCommonParameters; }
class UDreamSkySubsystem;

/**
 * The sky cloud renderer (EDreamSkyClouds::Rendered): per view, right after the base pass (before lighting, fog and
 * translucency), a compute pass marches the cloud layer at reduced resolution (DreamSkyCloudRender.usf), a second one
 * blends it with the previous frames reprojected by the clouds' depth, and a full-screen pass composites it into the sky
 * pixels -- the far plane and whatever lies beyond the dome's depth -- so the engine's fog, aerial perspective and
 * translucency land on top of it. The dome leaves its own clouds out (DDW_CloudLayer.w = 1) and draws the sky behind.
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

	void Detach() { Subsystem = nullptr; }

	/** The shared parameter collection's vectors the march reads, in this order (DreamSkyCloudRender.usf's DSKY_V_*). */
	static const TArray<FName>& GetParameterNames();
	static constexpr int32 NumParameters = 18;

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
		FTextureResource* Noise = nullptr;
		FTextureResource* Map = nullptr;
		/** The ground shadow's texture (ADreamSky::GetCloudShadowTarget) and where it lies; null: none. */
		FTextureResource* Shadow = nullptr;
		/** The panorama the dome shows in the sky light capture (RT_DreamCloudPanorama); null: none. */
		FTextureResource* Panorama = nullptr;
		FVector4f ShadowPlacement = FVector4f(0.0f, 0.0f, 1.0f, 0.0f);
	};

	/** A view's clouds of the frame before (render thread only). */
	struct FHistory
	{
		TRefCountPtr<IPooledRenderTarget> Color;
		TRefCountPtr<IPooledRenderTarget> Depth;
		FIntPoint ViewSize = FIntPoint::ZeroValue;
		int32 Downsample = 0;
		int32 Look = -1;
		uint64 LastFrame = 0;
	};

	/**
	 * The optical depth toward the sun round the view, in two cascades (some 48 km across at 500 m texels, 180 km at
	 * 2.8 km), each 10 km tall over the layer's base: what the realistic clouds are shadowed by. Kept between frames,
	 * one cascade redrawn a frame.
	 */
	struct FSunMap
	{
		TRefCountPtr<IPooledRenderTarget> Texture;
		FVector4f CenterExtent = FVector4f(0.0f, 0.0f, 1.0f, 0.0f);
		FRDGTextureRef Current = nullptr;
	};

	void RenderClouds(FRDGBuilder& GraphBuilder, FSceneView& InView, const FRenderTargetBindingSlots& RenderTargets);
	void UpdateSunMaps(FRDGBuilder& GraphBuilder, const FSceneView& InView, FGlobalShaderMap* ShaderMap, FRDGTextureRef WeatherMap,
		FRDGTextureRef CloudNoise, TFunctionRef<void(DreamSkyCloudRender::FCommonParameters&)> FillCommon);

	TWeakObjectPtr<UDreamSkySubsystem> Subsystem;
	FFrameSettings RenderSettings;
	TMap<uint32, TSharedPtr<FHistory>> Histories;
	FSunMap SunMaps[2];
	uint64 SunMapFrame = ~uint64(0);
	const FRDGBuilder* SunMapBuilder = nullptr;
};
