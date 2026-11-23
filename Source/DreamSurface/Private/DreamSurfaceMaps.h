// Copyright DreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "DreamWorldSharedTargets.h"
#include "UObject/WeakObjectPtr.h"

class FDreamSurfaceMapRenderer;
class UDreamSurfaceSubsystem;
class UDreamWeatherSubsystem;
class UDreamWorldSubsystem;
class UTextureRenderTarget2D;

/**
 * The surface maps a world keeps around the camera (game thread side): the near and far state maps and the cover map,
 * when to integrate them, and the shared material parameters that place them. Owned by UDreamSurfaceSubsystem.
 */
class FDreamSurfaceMaps
{
public:
	FDreamSurfaceMaps();
	~FDreamSurfaceMaps();

	/** Every weather frame, after the view's state. */
	void Update(UDreamSurfaceSubsystem& Surface, UDreamWorldSubsystem& Clock, UDreamWeatherSubsystem& Weather);

	/** The maps stop (the weather stopped, the surfaces are off): the materials fall back on the view's state. */
	void Stop(UDreamWorldSubsystem* Clock);

	/** Keeps the render targets alive. */
	void AddReferencedObjects(class FReferenceCollector& Collector);

	UTextureRenderTarget2D* GetNearMap() const { return Near.Target; }
	UTextureRenderTarget2D* GetFarMap() const { return Far.Target; }
	UTextureRenderTarget2D* GetNearIceMap() const { return Near.IceTarget; }
	UTextureRenderTarget2D* GetFarIceMap() const { return Far.IceTarget; }
	UTextureRenderTarget2D* GetCoverMap() const { return Cover; }
	UTextureRenderTarget2D* GetMaskMap() const { return Mask; }
	UTextureRenderTarget2D* GetTrailMap() const { return Trails; }
	FVector2D GetTrailCenter() const { return TrailCenter; }
	FVector2D GetNearCenter() const { return Near.Center; }
	float GetNearExtent() const { return Near.Extent; }
	FVector2D GetFarCenter() const { return Far.Center; }
	float GetFarExtent() const { return Far.Extent; }

	/** Integrate every map afresh on the next update (console: DreamSurface.Remap). */
	void RequestIntegrate() { bIntegrateRequested = true; }

	/** Maps drawn, and of them those that integrated (tests and stats). */
	uint32 GetDrawCount() const;
	uint32 GetIntegrateCount() const;

private:
	struct FMap
	{
		TObjectPtr<UTextureRenderTarget2D> Target;
		/** The ice, frost, rime and frozen water beside it. */
		TObjectPtr<UTextureRenderTarget2D> IceTarget;
		FVector2D Center = FVector2D(1.0e30, 1.0e30);
		float Extent = 0.0f;
		int64 Step = TNumericLimits<int64>::Min();
		double LastDraw = -1.0e30;
		bool bLive = false;
	};

	bool Resolve();
	/** Nothing in the maps is this world's any more (another world drew them): integrate and draw everything afresh. */
	void ForgetDrawn();
	void DrawState(int32 Slot, FMap& Map, const FVector2D& Center, float Extent, bool bForce, UDreamSurfaceSubsystem& Surface, UDreamWeatherSubsystem& Weather, double Now);
	void DrawCover(UDreamWorldSubsystem& Clock, const FVector2D& Center, float Extent, double Now);
	void DrawTrails(UDreamSurfaceSubsystem& Surface, UDreamWorldSubsystem& Clock, const struct FDreamWeatherSample& View, double Now);

	/** The maps are shared by every world: this world draws them only while it owns them. */
	DreamWorld::SharedTargets::FWriter Writer;
	FMap Near;
	FMap Far;
	TObjectPtr<UTextureRenderTarget2D> Cover;
	TObjectPtr<UTextureRenderTarget2D> Mask;
	TObjectPtr<UTextureRenderTarget2D> Trails;
	FVector2D TrailCenter = FVector2D(1.0e30, 1.0e30);
	double LastTrailDraw = -1.0e30;
	/** Real time until which the water in the puddles may still be moving (the trail map updates every frame till then). */
	double WavesUntil = -1.0e30;
	double LastTrailWorldSeconds = -1.0e30;
	FVector2D CoverCenter = FVector2D(1.0e30, 1.0e30);
	double LastCoverDraw = -1.0e30;
	int32 LastRoofCaptures = -1;
	double LastResolve = -1.0e30;
	TArray<FVector4f> LastZones;
	TArray<FVector4f> LastZoneTracks;
	int32 LastNumZones = 0;
	bool bIntegrateRequested = false;
	TSharedPtr<FDreamSurfaceMapRenderer, ESPMode::ThreadSafe> Renderer;
};
