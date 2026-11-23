// Copyright DreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "DreamWindTypes.h"
#include "DreamWindTerrainMap.h"
#include "DreamWindSettings.generated.h"

class UTextureRenderTargetVolume;
class UTextureRenderTarget2D;
class UDreamWindTerrainMap;

/**
 * Project Settings > DreamPlugin > Moon Wind.
 * Defaults for every world; a level can override the global wind with an ADreamWindGlobal actor and
 * gameplay can override anything through UDreamWindSubsystem.
 */
UCLASS(Config = Game, DefaultConfig, meta = (DisplayName = "Moon Wind"))
class DREAMWIND_API UDreamWindSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	UDreamWindSettings();

	static const UDreamWindSettings* Get() { return GetDefault<UDreamWindSettings>(); }

	//~ UDeveloperSettings
	virtual FName GetCategoryName() const override { return TEXT("DreamPlugin"); }
#if WITH_EDITOR
	virtual FText GetSectionText() const override;
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif

	// ---- General ------------------------------------------------------------------------------

	/** Master switch. Off = the subsystem does not tick and every sample returns calm. */
	UPROPERTY(Config, EditAnywhere, Category = "General")
	bool bEnabled = true;

	/** Keep the wind alive in editor worlds too, so materials and previews move in the viewport. */
	UPROPERTY(Config, EditAnywhere, Category = "General")
	bool bTickInEditor = true;

	/** Decorrelates the noise between projects/worlds. */
	UPROPERTY(Config, EditAnywhere, Category = "General")
	int32 Seed = 1337;

	/** Wind used when the level has no ADreamWindGlobal actor. */
	UPROPERTY(Config, EditAnywhere, Category = "General")
	FDreamWindGlobalWind DefaultWind;

	// ---- Engine bridge ------------------------------------------------------------------------

	/**
	 * Drive one engine WindDirectionalSource from the global wind so SpeedTree, Chaos Cloth,
	 * AnimDynamics, Groom (Niagara Wind Force) and anything else reading the engine wind follow.
	 * Remove hand-placed WindDirectionalSource actors when this is on, or they blend in as well.
	 */
	UPROPERTY(Config, EditAnywhere, Category = "Engine Bridge")
	bool bDriveEngineWindSource = true;

	/**
	 * Engine wind Speed = our speed (cm/s) * this. Chaos Cloth reads the engine speed as m/s, so the
	 * default 0.01 is physically right for cloth; SpeedTree treats it as a 0..1 strength.
	 */
	UPROPERTY(Config, EditAnywhere, Category = "Engine Bridge", meta = (ClampMin = "0"))
	float EngineWindSpeedScale = 0.01f;

	/** Engine wind Strength (blend weight against other engine wind sources). */
	UPROPERTY(Config, EditAnywhere, Category = "Engine Bridge", meta = (ClampMin = "0"))
	float EngineWindStrength = 1.0f;

	// The wind's material parameters live in the plugin's shared collection (Project Settings > Dream Dynamic
	// World > Parameter Collection); materials sample them through MF_DreamWindSample.

	// ---- Dynamic volume (L2) ------------------------------------------------------------------

	/** Simulate the camera-following dynamic volume (impulses, wakes, residual gusts). */
	UPROPERTY(Config, EditAnywhere, Category = "Volume")
	bool bVolumeEnabled = true;

	/** Volume render target the compute pass writes and materials/Niagara sample. */
	UPROPERTY(Config, EditAnywhere, Category = "Volume", meta = (AllowedClasses = "/Script/Engine.TextureRenderTargetVolume"))
	FSoftObjectPath VolumeTexture = FSoftObjectPath(TEXT("/DreamDynamicWorld/Wind/RT_DreamWindVolume.RT_DreamWindVolume"));

	/** Voxels per axis. Memory = X*Y*Z*8 bytes (RGBA16F). 64x64x32 = 1 MB. */
	UPROPERTY(Config, EditAnywhere, Category = "Volume", meta = (ClampMin = "8", ClampMax = "256"))
	FIntVector VolumeResolution = FIntVector(64, 64, 32);

	/** Size of one voxel, cm. 100 with 64x64x32 voxels covers 64 m x 64 m x 32 m around the camera. */
	UPROPERTY(Config, EditAnywhere, Category = "Volume", meta = (ClampMin = "10", UIMax = "1000"))
	float VolumeVoxelSize = 100.0f;

	/** Time constant of the residual decay, seconds. Smaller = impulses die faster. */
	UPROPERTY(Config, EditAnywhere, Category = "Volume", meta = (ClampMin = "0.05", UIMax = "10"))
	float VolumeDecayTime = 1.5f;

	/** Fraction of the half-extent over which the volume fades out toward its border. */
	UPROPERTY(Config, EditAnywhere, Category = "Volume", meta = (ClampMin = "0.01", ClampMax = "1"))
	float VolumeBorderFade = 0.15f;

	/** Advect the residual by the analytic wind + itself (semi-Lagrangian). Off = residual stays put. */
	UPROPERTY(Config, EditAnywhere, Category = "Volume")
	bool bVolumeAdvect = true;

	/** Snap the window origin to whole voxels as it follows the camera (avoids swimming). */
	UPROPERTY(Config, EditAnywhere, Category = "Volume")
	bool bVolumeSnapToVoxels = true;

	/**
	 * A second, coarser volume around the first: same voxel count, VolumeFarVoxelScale times the voxel
	 * size, so impulses and wakes keep working far from the camera at one more pass and one more MB.
	 */
	UPROPERTY(Config, EditAnywhere, Category = "Volume")
	bool bVolumeFarLevel = true;

	/** Voxel size of the far level relative to the near one. 4 with 100 cm voxels covers 256 m x 256 m x 128 m. */
	UPROPERTY(Config, EditAnywhere, Category = "Volume", meta = (ClampMin = "2", ClampMax = "16", EditCondition = "bVolumeFarLevel"))
	float VolumeFarVoxelScale = 4.0f;

	/** Volume render target of the far level. */
	UPROPERTY(Config, EditAnywhere, Category = "Volume", meta = (AllowedClasses = "/Script/Engine.TextureRenderTargetVolume", EditCondition = "bVolumeFarLevel"))
	FSoftObjectPath VolumeFarTexture = FSoftObjectPath(TEXT("/DreamDynamicWorld/Wind/RT_DreamWindVolumeFar.RT_DreamWindVolumeFar"));

	/** Maximum live sources uploaded to the GPU per frame; the strongest survive. */
	UPROPERTY(Config, EditAnywhere, Category = "Volume", meta = (ClampMin = "1", ClampMax = "256"))
	int32 MaxGpuSources = 64;

	/**
	 * Solve the volume as a fluid: the air flows round buildings, eddies at their corners and turns back in their lee.
	 * Off: the residual volume (sources' wakes carried by the wind and decaying, no walls). The volumes then hold the
	 * fluid's deviation from the background wind (analytic + terrain map), which consumers add as before.
	 */
	UPROPERTY(Config, EditAnywhere, Category = "Volume|Fluid")
	bool bFluid = true;

	/** Pressure iterations of the near level (red-black Gauss-Seidel, starting from last frame's pressure). */
	UPROPERTY(Config, EditAnywhere, Category = "Volume|Fluid", meta = (ClampMin = "1", ClampMax = "64", EditCondition = "bFluid"))
	int32 FluidPressureIterations = 8;

	/** Pressure iterations of the far level (0: it is only carried and pulled toward the background, not projected). */
	UPROPERTY(Config, EditAnywhere, Category = "Volume|Fluid", meta = (ClampMin = "0", ClampMax = "64", EditCondition = "bFluid"))
	int32 FluidFarPressureIterations = 4;

	/** Over-relaxation of the pressure iterations (1 = plain Gauss-Seidel; up to 1.9 converges faster). */
	UPROPERTY(Config, EditAnywhere, Category = "Volume|Fluid", meta = (ClampMin = "1", ClampMax = "1.9", EditCondition = "bFluid"))
	float FluidPressureRelaxation = 1.5f;

	/** Vorticity confinement of the near level: gives back the eddies the coarse grid smears out (0 = none). */
	UPROPERTY(Config, EditAnywhere, Category = "Volume|Fluid", meta = (ClampMin = "0", ClampMax = "1", EditCondition = "bFluid"))
	float FluidVorticity = 0.4f;

	/**
	 * Seconds over which a grid-scale checkerboard in the fluid dies out (0: never). The collocated grid's projection
	 * cannot see one; this smooths only at that scale (longer is less viscous).
	 */
	UPROPERTY(Config, EditAnywhere, Category = "Volume|Fluid", meta = (ClampMin = "0", UIMax = "5", EditCondition = "bFluid"))
	float FluidCheckerboardDamping = 0.5f;

	/** Second-order advection for the near level (MacCormack, one more pass): sharp enough shear for eddies and the lee's backflow. */
	UPROPERTY(Config, EditAnywhere, Category = "Volume|Fluid", meta = (EditCondition = "bFluid"))
	bool bFluidMacCormack = true;

	/**
	 * Seconds over which the fluid's deviation inside the grid fades back to the background wind. The background's gusts
	 * pass through regardless (consumers add it at every point): this only keeps wakes from lasting for ever.
	 */
	UPROPERTY(Config, EditAnywhere, Category = "Volume|Fluid", meta = (ClampMin = "0.1", UIMax = "60", EditCondition = "bFluid"))
	float FluidNudgeTime = 10.0f;

	/** Cells along the sides and the top over which the pull tightens to FluidSpongeTime, so the border does not reflect. */
	UPROPERTY(Config, EditAnywhere, Category = "Volume|Fluid", meta = (ClampMin = "0", ClampMax = "16", EditCondition = "bFluid"))
	int32 FluidSpongeCells = 4;

	/** The pull's time at the very border, seconds. */
	UPROPERTY(Config, EditAnywhere, Category = "Volume|Fluid", meta = (ClampMin = "0.01", UIMax = "2", EditCondition = "bFluid"))
	float FluidSpongeTime = 0.1f;

	/** The fastest the fluid may blow, cm/s (blasts included). */
	UPROPERTY(Config, EditAnywhere, Category = "Volume|Fluid", meta = (ClampMin = "100", EditCondition = "bFluid"))
	float FluidMaxSpeed = 8000.0f;

	/**
	 * Take the walls from the engine's global distance field (3D: air passes under bridges and through arches), asking
	 * the renderer to keep it up to date (DreamWind.DistanceField); the project needs Generate Mesh Distance Fields.
	 * Where it has none, the walls come from the terrain map's heights (2.5D).
	 */
	UPROPERTY(Config, EditAnywhere, Category = "Volume|Fluid", meta = (EditCondition = "bFluid"))
	bool bFluidDistanceField = true;

	/**
	 * A cell is a wall when a surface lies within this fraction of a cell of its centre. Just under half keeps a wall
	 * thinner than a cell watertight (along the axes and on the diagonal, from 10 cm thick at 1 m cells) and leaves a face
	 * on a cell boundary (flat ground under the grid, which sits on whole cells) where it is, the cells beside it air.
	 */
	UPROPERTY(Config, EditAnywhere, Category = "Volume|Fluid", meta = (ClampMin = "0.3", ClampMax = "1", EditCondition = "bFluid"))
	float FluidWallThickness = 0.45f;

	/** Moving bodies the fluid takes each frame (UDreamWindMoverComponent), the nearest to the camera. */
	UPROPERTY(Config, EditAnywhere, Category = "Volume|Fluid", meta = (ClampMin = "0", ClampMax = "256", EditCondition = "bFluid"))
	int32 MaxMovers = 32;

	/** Cells of each level below the ground under the camera (the grids sit on the ground, rising with a camera high above it). */
	UPROPERTY(Config, EditAnywhere, Category = "Volume|Fluid", meta = (ClampMin = "1", ClampMax = "16", EditCondition = "bFluid"))
	int32 FluidCellsBelowGround = 4;

	// ---- Terrain modulation (L1) --------------------------------------------------------------

	/** Modulate the wind by a baked terrain map (ridges faster, hollows sheltered, steering toward valleys). */
	UPROPERTY(Config, EditAnywhere, Category = "Terrain")
	bool bTerrainEnabled = true;

	/** Multiplies the baked steering offset. */
	UPROPERTY(Config, EditAnywhere, Category = "Terrain", meta = (ClampMin = "0", UIMax = "3"))
	float TerrainSteerScale = 1.0f;

	/** 0 = ignore the baked speed multiplier, 1 = apply it fully. */
	UPROPERTY(Config, EditAnywhere, Category = "Terrain", meta = (ClampMin = "0", ClampMax = "1"))
	float TerrainSpeedScale = 1.0f;

	/**
	 * Level geometry blocks the wind: every sample marches upwind through the baked heights and is
	 * damped by whatever rises above it (a wall, a rock, a hill crest), and is calm inside solids.
	 * 0 = off, 1 = a full wind shadow. Needs a baked terrain map; put a UDreamWindPassThroughComponent
	 * (or the actor tag DreamWind.PassThrough) on things that should not block, then re-bake.
	 */
	UPROPERTY(Config, EditAnywhere, Category = "Terrain|Occlusion", meta = (ClampMin = "0", ClampMax = "1"))
	float OcclusionStrength = 0.9f;

	/** How far upwind (cm) a sample looks for geometry that shades it. 12 taps, denser near the sample. */
	UPROPERTY(Config, EditAnywhere, Category = "Terrain|Occlusion", meta = (ClampMin = "0", UIMax = "10000"))
	float OcclusionRange = 3000.0f;

	/** Rise / run of the wind-shadow wedge: an obstacle H above the sample shades it out to H / slope downwind (0.2 = five heights). */
	UPROPERTY(Config, EditAnywhere, Category = "Terrain|Occlusion", meta = (ClampMin = "0.02", ClampMax = "2"))
	float OcclusionSlope = 0.2f;

	/** 2D render target materials read the active terrain map through; the subsystem uploads the map into it. */
	UPROPERTY(Config, EditAnywhere, Category = "Terrain", meta = (AllowedClasses = "/Script/Engine.TextureRenderTarget2D"))
	FSoftObjectPath TerrainTexture = FSoftObjectPath(TEXT("/DreamDynamicWorld/Wind/RT_DreamWindTerrain.RT_DreamWindTerrain"));

	/**
	 * Compute the terrain wind map at run time around the camera (a top-down capture of whatever stands there, World
	 * Partition and moving buildings included) instead of reading the level's baked map. Off, or where it cannot run
	 * (mobile, no compute), the baked map stands in.
	 */
	UPROPERTY(Config, EditAnywhere, Category = "Terrain|Runtime")
	bool bRuntimeTerrain = true;

	/** The near level: its width (cm) and texels a side. 25600 at 256 = 1 m texels over 256 m: buildings, walls, rocks. */
	UPROPERTY(Config, EditAnywhere, Category = "Terrain|Runtime", meta = (ClampMin = "1000", EditCondition = "bRuntimeTerrain"))
	float RuntimeNearExtent = 25600.0f;

	UPROPERTY(Config, EditAnywhere, Category = "Terrain|Runtime", meta = (ClampMin = "64", ClampMax = "1024", EditCondition = "bRuntimeTerrain"))
	int32 RuntimeNearResolution = 256;

	/** The far level: 204800 at 256 = 8 m texels over 2 km: ridges and hollows, steering, the lee of hills. */
	UPROPERTY(Config, EditAnywhere, Category = "Terrain|Runtime", meta = (ClampMin = "10000", EditCondition = "bRuntimeTerrain"))
	float RuntimeFarExtent = 204800.0f;

	UPROPERTY(Config, EditAnywhere, Category = "Terrain|Runtime", meta = (ClampMin = "64", ClampMax = "1024", EditCondition = "bRuntimeTerrain"))
	int32 RuntimeFarResolution = 256;

	/** How far (cm) the far level's lee reaches behind a hill at most (the near level's is Occlusion Range). */
	UPROPERTY(Config, EditAnywhere, Category = "Terrain|Runtime", meta = (ClampMin = "0", EditCondition = "bRuntimeTerrain"))
	float RuntimeFarShadowRange = 50000.0f;

	/** Height (cm) over which a point goes from the open into an obstacle's lee. */
	UPROPERTY(Config, EditAnywhere, Category = "Terrain|Runtime", meta = (ClampMin = "1", EditCondition = "bRuntimeTerrain"))
	float RuntimeShadowFadeBand = 100.0f;

	/** The capture looks down from this far above the camera (cm); anything taller is cut off at it. */
	UPROPERTY(Config, EditAnywhere, Category = "Terrain|Runtime", meta = (ClampMin = "1000", EditCondition = "bRuntimeTerrain"))
	float RuntimeCaptureAltitude = 100000.0f;

	/** So moving buildings and streamed-in cells show up, each level is captured again strip by strip, in this many
	 *  strips. (A building moved by code shows up at once through MarkObstaclesDirty.) */
	UPROPERTY(Config, EditAnywhere, Category = "Terrain|Runtime", meta = (ClampMin = "4", ClampMax = "256", EditCondition = "bRuntimeTerrain"))
	int32 RuntimeRefreshStrips = 16;

	/** Frames between two refresh strips, the levels taking turns: each capture is a scene render with a fixed cost of a
	 *  few tenths of a millisecond, so at most one runs a frame. With 16 strips and 4 frames, the near level is captured
	 *  afresh every 128 frames. What comes into view as the camera moves is captured first, without waiting. */
	UPROPERTY(Config, EditAnywhere, Category = "Terrain|Runtime", meta = (ClampMin = "1", ClampMax = "60", EditCondition = "bRuntimeTerrain"))
	int32 RuntimeRefreshInterval = 4;

	/** The far level's shapes: how the speed-up over ridges and the steering toward valleys come out of the heights. */
	UPROPERTY(Config, EditAnywhere, Category = "Terrain|Runtime", meta = (EditCondition = "bRuntimeTerrain"))
	FDreamWindTerrainBakeSettings RuntimeShape;

	/**
	 * The runtime terrain wind map's render targets (what the wind computes around the camera instead of baking): per
	 * level, the derived map (speed multiplier, steering x, y, wind shadow height in cm; RGBA32F) and the top surface's
	 * height (cm; R32F, the roofs the weather and the surfaces read). Wrap addressing: they are ring buffers over the world.
	 */
	UPROPERTY(Config, EditAnywhere, Category = "Terrain|Runtime", meta = (AllowedClasses = "/Script/Engine.TextureRenderTarget2D"))
	FSoftObjectPath TerrainNearTexture = FSoftObjectPath(TEXT("/DreamDynamicWorld/Wind/RT_DreamWindTerrainNear.RT_DreamWindTerrainNear"));

	UPROPERTY(Config, EditAnywhere, Category = "Terrain|Runtime", meta = (AllowedClasses = "/Script/Engine.TextureRenderTarget2D"))
	FSoftObjectPath TerrainFarTexture = FSoftObjectPath(TEXT("/DreamDynamicWorld/Wind/RT_DreamWindTerrainFar.RT_DreamWindTerrainFar"));

	UPROPERTY(Config, EditAnywhere, Category = "Terrain|Runtime", meta = (AllowedClasses = "/Script/Engine.TextureRenderTarget2D"))
	FSoftObjectPath HeightNearTexture = FSoftObjectPath(TEXT("/DreamDynamicWorld/Wind/RT_DreamWindHeightNear.RT_DreamWindHeightNear"));

	UPROPERTY(Config, EditAnywhere, Category = "Terrain|Runtime", meta = (AllowedClasses = "/Script/Engine.TextureRenderTarget2D"))
	FSoftObjectPath HeightFarTexture = FSoftObjectPath(TEXT("/DreamDynamicWorld/Wind/RT_DreamWindHeightFar.RT_DreamWindHeightFar"));

	/** Terrain map used when the level's ADreamWindGlobal has none. */
	UPROPERTY(Config, EditAnywhere, Category = "Terrain", meta = (AllowedClasses = "/Script/DreamWind.DreamWindTerrainMap"))
	FSoftObjectPath DefaultTerrainMap;

	// ---- Debug --------------------------------------------------------------------------------

	/** Print the global wind and the live source count on screen. */
	UPROPERTY(Config, EditAnywhere, Category = "Debug")
	bool bShowDebugText = false;
};
