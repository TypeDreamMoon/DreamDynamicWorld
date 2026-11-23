// Copyright DreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "RenderGraphResources.h"
#include <atomic>

class FTextureRenderTargetResource;
class FRDGBuilder;
class UTextureRenderTarget2D;

/**
 * Draws a surface map (DreamSurfaceMap.usf's StateCS) into its render target, and the cover map (CoverCS). Keeps, per
 * map, the state at the start of the current step in a texture of its own: an update that integrates writes it, the
 * ones between only add the part of the step gone by.
 */
class FDreamSurfaceMapRenderer : public TSharedFromThis<FDreamSurfaceMapRenderer, ESPMode::ThreadSafe>
{
public:
	struct FStateArgs
	{
		FTextureRenderTargetResource* Target = nullptr;
		/** The ice, frost, rime and frozen water beside it (the state's second half). */
		FTextureRenderTargetResource* IceTarget = nullptr;
		FVector2f Center = FVector2f::ZeroVector;
		float Extent = 12800.0f;
		/** Integrate the whole steps first (a step went by, the map moved, the zones changed). */
		bool bIntegrate = true;
		/** Three float4 per whole step (FDreamSurfaceMapSteps::Steps). */
		TArray<FVector4f> Steps;
		float StepHours = 0.25f;
		FVector4f PartialWeather = FVector4f::Zero();
		FVector4f PartialChannels = FVector4f::Zero();
		FVector4f PartialAir = FVector4f::Zero();
		float PartialHours = 0.0f;
		FVector4f Rates[5];
		FVector2f PrecipitationGate = FVector2f::ZeroVector;
		/** NumZones records of five float4 (DreamSurfaceMap.usf), lowest priority first, then the polygons' points. */
		TArray<FVector4f> Zones;
		int32 NumZones = 0;
		/** When integrating: per zone, per whole step, its footprint's centre (xy) and strength (z) at the step's start. */
		TArray<FVector4f> ZoneTracks;
	};

	struct FCoverArgs
	{
		FTextureRenderTargetResource* Target = nullptr;
		FVector2f Center = FVector2f::ZeroVector;
		float Extent = 12800.0f;
		/** The wind's terrain map (null: none) and its DW_UnpackTerrain vectors. */
		FTextureRenderTargetResource* Terrain = nullptr;
		FVector4f TerrainParams[3];
		/** How far around a texel the ground's lows are judged (cm). */
		float LowRadius = 300.0f;
		/** Three float4 per shelter box. */
		TArray<FVector4f> Shelters;
		/** Up to four roof captures: their depth targets and three float4 each (DreamSurfaceMap.usf). */
		TArray<FTextureRenderTargetResource*, TInlineAllocator<4>> RoofTargets;
		TArray<FVector4f> Roofs;
		/** The mask map the pass writes beside the cover (null: none), and three float4 per mask brush. */
		FTextureRenderTargetResource* MaskTarget = nullptr;
		TArray<FVector4f> Masks;
	};

	struct FTrailArgs
	{
		FTextureRenderTargetResource* Target = nullptr;
		FVector2f Center = FVector2f::ZeroVector;
		FVector2f PreviousCenter = FVector2f::ZeroVector;
		float Extent = 5120.0f;
		/** How much of a print and of the stirred water stays over this update. */
		float PrintKeep = 1.0f;
		float RippleKeep = 1.0f;
		/** Start from nothing (the first update, a jump). */
		bool bReset = false;
		/** Two float4 per print (UDreamSurfaceSubsystem::GetPendingStamps). */
		TArray<FVector4f> Stamps;
	};

	/** Game thread: update the trail map this frame. */
	void EnqueueTrails(FTrailArgs&& Args);

	/** Game thread: draw a state map this frame. Slot tells the maps apart (each keeps its own step start). */
	void EnqueueState(int32 Slot, FStateArgs&& Args);

	/** Game thread: draw the cover map this frame. */
	void EnqueueCover(FCoverArgs&& Args);

	/** Maps drawn so far, and of them those that integrated (any thread; for tests and stats). */
	uint32 GetDrawCount() const { return DrawCount.load(); }
	uint32 GetIntegrateCount() const { return IntegrateCount.load(); }

private:
	void State_RenderThread(FRDGBuilder& GraphBuilder, int32 Slot, const FStateArgs& Args);
	void Cover_RenderThread(FRDGBuilder& GraphBuilder, const FCoverArgs& Args);
	void Trails_RenderThread(FRDGBuilder& GraphBuilder, const FTrailArgs& Args);

	/** Render thread: the previous trail map (what an update carries over). */
	TRefCountPtr<IPooledRenderTarget> TrailHistory;

	/** Render thread: the step-start textures of each map (the state's two halves). */
	TMap<int32, TRefCountPtr<IPooledRenderTarget>> StepStarts;
	TMap<int32, TRefCountPtr<IPooledRenderTarget>> IceStepStarts;

	std::atomic<uint32> DrawCount { 0 };
	std::atomic<uint32> IntegrateCount { 0 };
};
