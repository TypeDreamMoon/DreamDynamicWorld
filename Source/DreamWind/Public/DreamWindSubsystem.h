// Copyright DreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "DreamWindTypes.h"
#include "DreamWindHlslShim.h"
#include "DreamWorldSharedTargets.h"
#include "Math/Float16Color.h"
#include "DreamWindSubsystem.generated.h"

class UDreamWindSourceComponent;
class UWindDirectionalSourceComponent;
class UTextureRenderTargetVolume;
class UTextureRenderTarget2D;
class UDreamWindTerrainMap;
class UDreamWindPreset;
class FDreamWindVolumeRenderer;
class FDreamWindViewExtension;

/**
 * Thread-safe CPU copy of a terrain map, shared by the subsystem and Niagara CPU sims. Holds the same
 * ATLAS the GPU samples: the map (level 0, rows 0..Height-1) followed by DW_TERRAIN_MAX_LEVEL
 * max-filtered height levels (see DreamWindShared.h, DW_TerrainAtlasUV); Width x 2*Height texels.
 */
struct DREAMWIND_API FDreamWindTerrainCpuData
{
	FBox2D Bounds = FBox2D(ForceInit);
	int32 Width = 0;
	int32 Height = 0;
	/** World Z that texel height 0 stands for (UDreamWindTerrainMap::HeightRange.X). */
	float HeightBase = 0.0f;
	/** The atlas, row-major, Width x (2 * Height). */
	TArray<FVector4f> Texels;

	/** Builds the atlas (level 0 = Map, levels 1..DW_TERRAIN_MAX_LEVEL = max heights) from a base map. */
	void BuildFromMap(const class UDreamWindTerrainMap& Map);

	/** World size of one base texel, cm. */
	float TexelSize() const { return Width > 0 && Bounds.bIsValid ? float(Bounds.GetSize().X / Width) : 1.0f; }

	/** Bilinear fetch of level 0, clamped to the border: (speed multiplier, steer.x, steer.y, height in cm above HeightBase). */
	FVector4f Sample(double WorldX, double WorldY) const;
	/** Nearest fetch of the height (cm above HeightBase) at atlas level Level (0 = the map, 1.. = max heights). */
	float HeightAt(double WorldX, double WorldY, int32 Level) const;
	/** Bilinear fetch of the height (cm above HeightBase) at atlas level Level - what the occlusion march reads. */
	float HeightBilinear(double WorldX, double WorldY, int32 Level) const;
	bool IsInside(double WorldX, double WorldY) const
	{
		return Bounds.bIsValid && WorldX >= Bounds.Min.X && WorldX <= Bounds.Max.X && WorldY >= Bounds.Min.Y && WorldY <= Bounds.Max.Y;
	}

	/**
	 * Wind shadow (0..1) of the level geometry at Pos for a wind blowing along Velocity: the sample's
	 * own texel (inside a solid) plus DW_OCCLUSION_TAPS upwind taps through the baked heights. The
	 * same loop the GPU consumers run, see DreamWindShared.h.
	 */
	float OcclusionShadow(const DreamWind::Hlsl::FDreamWindTerrainParams& T, const DreamWind::Hlsl::float3& Pos, const DreamWind::Hlsl::float3& Velocity) const;

	/** Terrain modulation followed by occlusion, i.e. everything L1 does to an analytic sample. */
	DreamWind::Hlsl::FDreamWindSample ApplyAll(DreamWind::Hlsl::FDreamWindSample S, const DreamWind::Hlsl::FDreamWindTerrainParams& T, const DreamWind::Hlsl::float3& Pos) const;
};

/**
 * The runtime terrain wind map as the CPU has it (gameplay, physics, sound, Niagara CPU sims, the weather's roofs): a
 * grid of cells around the camera whose heights come from box sweeps against the level's collision (no read-back of the
 * GPU's map), with the same derivation the GPU runs: the wind shadow height swept along the wind, the speed multiplier
 * and steering from the blurred heights. An immutable snapshot, shared between threads.
 */
struct DREAMWIND_API FDreamWindTerrainGridData
{
	/** The window's first cell and its size in cells; cell size in cm. Arrays are row-major over the window. */
	FIntPoint WindowMin = FIntPoint::ZeroValue;
	int32 Size = 0;
	float CellSize = 400.0f;
	/** Per cell: (speed multiplier, steer x, steer y, wind shadow height cm). */
	TArray<FVector4f> Derived;
	/** Per cell: the top surface's height (cm), Unknown until swept (or nothing below). */
	TArray<float> Heights;
	DreamWind::Hlsl::FDreamWindL1Params Params;

	/** Swept, nothing below; and not swept yet (the grid does not cover the cell: the baked map, if any, stands in). */
	static constexpr float Unknown = -1.0e7f;
	static constexpr float NotSwept = -2.0e7f;

	bool IsValid() const { return Size > 0 && Derived.Num() == Size * Size && Heights.Num() == Size * Size; }

	/** 1 well inside the window, fading over its outer eighth, 0 outside. */
	float Weight(double WorldX, double WorldY) const;

	/** Bilinear fetch of the derived values, clamped into the window. */
	FVector4f SampleDerived(double WorldX, double WorldY) const;

	/** The top surface's height (cm) of the cell over a point, Unknown outside or unswept. */
	float HeightAt(double WorldX, double WorldY) const;

	/** What the terrain does to an analytic sample here (weighted by the window's coverage; unchanged outside). */
	DreamWind::Hlsl::FDreamWindSample Apply(DreamWind::Hlsl::FDreamWindSample S, const DreamWind::Hlsl::float3& Pos) const;
};

/**
 * Everything a GPU consumer needs for one frame, in the packed layout the shared header unpacks.
 * Built once per tick on the game thread; copied (not referenced) by whoever needs it.
 */
struct DREAMWIND_API FDreamWindFrameData
{
	/** DW_UnpackParams(P0, P1, P2) */
	FVector4f P0 = FVector4f(1, 0, 0, 0);
	FVector4f P1 = FVector4f::Zero();
	FVector4f P2 = FVector4f::Zero();
	/** DW_UnpackVolume(V0, V1) - near level; DW_UnpackVolume(V2, V3) - far level */
	FVector4f V0 = FVector4f::Zero();
	FVector4f V1 = FVector4f::Zero();
	FVector4f V2 = FVector4f::Zero();
	FVector4f V3 = FVector4f::Zero();
	/** DW_UnpackTerrain(T0, T1, T2) */
	FVector4f T0 = FVector4f::Zero();
	FVector4f T1 = FVector4f::Zero();
	FVector4f T2 = FVector4f::Zero();
	/** Live sources this frame, strongest first, already envelope-scaled. */
	TArray<DreamWind::Hlsl::FDreamWindSourceData> Sources;
	/** Volume render targets (null when a level is disabled or its asset is missing). */
	TWeakObjectPtr<UTextureRenderTargetVolume> VolumeTexture;
	TWeakObjectPtr<UTextureRenderTargetVolume> VolumeFarTexture;
	/** Terrain map render target (null when off) and the CPU copy of the active map. */
	TWeakObjectPtr<UTextureRenderTarget2D> TerrainTexture;
	TSharedPtr<const FDreamWindTerrainCpuData, ESPMode::ThreadSafe> TerrainCpu;
	/** The runtime terrain wind map: DW_UnpackL1Level(L1Near / L1Far) (w = 0 when off), DW_UnpackL1Params(L1Params). */
	FVector4f L1Near = FVector4f::Zero();
	FVector4f L1Far = FVector4f::Zero();
	FVector4f L1Params = FVector4f::Zero();
	/** Its derived maps (speed multiplier, steering x, y, wind shadow height) and top-surface heights. */
	TWeakObjectPtr<UTextureRenderTarget2D> L1NearDerived;
	TWeakObjectPtr<UTextureRenderTarget2D> L1FarDerived;
	TWeakObjectPtr<UTextureRenderTarget2D> L1NearHeight;
	TWeakObjectPtr<UTextureRenderTarget2D> L1FarHeight;
	/** The runtime terrain wind map as the CPU has it (null when it does not run). */
	TSharedPtr<const FDreamWindTerrainGridData, ESPMode::ThreadSafe> TerrainGrid;
	/** Game-thread frame this data was built on. */
	uint64 FrameNumber = 0;

	DreamWind::Hlsl::FDreamWindParams GetParams() const
	{
		using namespace DreamWind::Hlsl;
		return DW_UnpackParams(ToHlsl(P0), ToHlsl(P1), ToHlsl(P2));
	}
	DreamWind::Hlsl::FDreamWindVolumeParams GetVolumeParams() const
	{
		using namespace DreamWind::Hlsl;
		return DW_UnpackVolume(ToHlsl(V0), ToHlsl(V1));
	}
	DreamWind::Hlsl::FDreamWindVolumeParams GetVolumeFarParams() const
	{
		using namespace DreamWind::Hlsl;
		return DW_UnpackVolume(ToHlsl(V2), ToHlsl(V3));
	}
	DreamWind::Hlsl::FDreamWindTerrainParams GetTerrainParams() const
	{
		using namespace DreamWind::Hlsl;
		return DW_UnpackTerrain(ToHlsl(T0), ToHlsl(T1), ToHlsl(T2));
	}
};

/**
 * The wind field of a world.
 *
 *  L0  analytic global wind (direction, speed, travelling gust fronts, curl turbulence) - a pure
 *      function of (position, time) evaluated identically on CPU and GPU from DreamWindShared.h
 *  L2  a camera-following dynamic volume that carries impulses, wakes and residual gusts (GPU only)
 *  L3  sources / impulses: spherical velocity fields consumers blend toward
 *
 * Consumers: materials through the MPC + MF_DreamWindSample, Niagara through the DreamWind data
 * interface, C++/Blueprint through SampleWind, and every engine consumer of WindDirectionalSource
 * (SpeedTree, Chaos Cloth, AnimDynamics, Groom, Niagara WindForce) through the driven engine source.
 */
UCLASS()
class DREAMWIND_API UDreamWindSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	UDreamWindSubsystem();

	/** The wind subsystem of the world the object lives in, or null. */
	UFUNCTION(BlueprintPure, Category = "DreamWind", meta = (WorldContext = "WorldContextObject", DisplayName = "Get Moon Wind"))
	static UDreamWindSubsystem* Get(const UObject* WorldContextObject);

	//~ USubsystem
	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	virtual void PostInitialize() override;

	//~ FTickableGameObject
	virtual void Tick(float DeltaTime) override;
	virtual bool IsTickable() const override;
	virtual bool IsTickableInEditor() const override;
	virtual TStatId GetStatId() const override;

	// ---- Global wind (L0) ---------------------------------------------------------------------

	/** Replace the global wind, optionally blending over BlendTime seconds. */
	UFUNCTION(BlueprintCallable, Category = "DreamWind")
	void SetGlobalWind(const FDreamWindGlobalWind& Wind, float BlendTime = 0.0f);

	/** Convenience: change only direction and speed, keeping gust/turbulence settings. */
	UFUNCTION(BlueprintCallable, Category = "DreamWind")
	void SetGlobalWindDirectionAndSpeed(FVector Direction, float Speed, float BlendTime = 0.0f);

	/** Blend to a preset's wind over BlendTime seconds (0 = snap). Null presets are ignored. */
	UFUNCTION(BlueprintCallable, Category = "DreamWind")
	void ApplyPreset(const UDreamWindPreset* Preset, float BlendTime = 2.0f);

	/**
	 * Blend to a preset's wind but keep the current direction - for weather that changes strength
	 * without turning (or when the level's ADreamWindGlobal owns the direction).
	 */
	UFUNCTION(BlueprintCallable, Category = "DreamWind")
	void ApplyPresetKeepDirection(const UDreamWindPreset* Preset, float BlendTime = 2.0f);

	/** The global wind currently in effect (mid-blend values included). */
	UFUNCTION(BlueprintPure, Category = "DreamWind")
	const FDreamWindGlobalWind& GetGlobalWind() const { return CurrentWind; }

	/** Unit direction of the global wind. */
	UFUNCTION(BlueprintPure, Category = "DreamWind")
	FVector GetGlobalWindDirection() const;

	/** Wind time in seconds; drives every temporal term. Settable for network sync or cinematics. */
	UFUNCTION(BlueprintPure, Category = "DreamWind")
	float GetWindTime() const { return static_cast<float>(WindTime); }

	UFUNCTION(BlueprintCallable, Category = "DreamWind")
	void SetWindTime(float NewTime) { WindTime = NewTime; }

	/** Multiplier on the advance of wind time. 0 freezes the wind. */
	UFUNCTION(BlueprintCallable, Category = "DreamWind")
	void SetTimeScale(float NewTimeScale) { TimeScale = FMath::Max(0.0f, NewTimeScale); }

	// ---- Sampling (CPU) -----------------------------------------------------------------------

	/**
	 * Wind at a world position: analytic wind + every live source. Runs entirely on the CPU (no GPU
	 * read-back), so it is safe to call from gameplay, physics and cloth every frame.
	 * The GPU volume's residual is not included; consumers that need it sample the volume.
	 */
	UFUNCTION(BlueprintPure, Category = "DreamWind")
	FDreamWindSampleResult SampleWind(FVector WorldPosition, EDreamWindQuality Quality = EDreamWindQuality::Cheap) const;

	/** Velocity only, cm/s. */
	UFUNCTION(BlueprintPure, Category = "DreamWind")
	FVector SampleWindVelocity(FVector WorldPosition) const;

	// ---- Sources / impulses (L3) -----------------------------------------------------------------

	/**
	 * Fire a transient source. It lives for Duration seconds and its strength fades to zero over the
	 * last FadeOutTime seconds. Duration <= 0 means one frame.
	 */
	UFUNCTION(BlueprintCallable, Category = "DreamWind")
	FDreamWindImpulseHandle AddImpulse(const FDreamWindSourceDesc& Source, float Duration = 0.3f, float FadeOutTime = 0.15f);

	/** Radial burst helper: explosion / landing. */
	UFUNCTION(BlueprintCallable, Category = "DreamWind")
	FDreamWindImpulseHandle AddRadialImpulse(FVector Position, float Radius, float Strength, float Duration = 0.3f);

	/** Directional gust helper: spell, wing flap, wake. */
	UFUNCTION(BlueprintCallable, Category = "DreamWind")
	FDreamWindImpulseHandle AddDirectionalImpulse(FVector Position, FVector Direction, float Radius, float Strength, float Duration = 0.5f);

	/** Stop an impulse early. */
	UFUNCTION(BlueprintCallable, Category = "DreamWind")
	void CancelImpulse(FDreamWindImpulseHandle Handle);

	/** Number of sources alive this frame (impulses + components). */
	UFUNCTION(BlueprintPure, Category = "DreamWind")
	int32 GetLiveSourceCount() const { return FrameData.Sources.Num(); }

	// ---- Terrain modulation (L1) --------------------------------------------------------------

	/** Use this baked terrain map (null = the project default, or none). ADreamWindGlobal calls this on load. */
	UFUNCTION(BlueprintCallable, Category = "DreamWind|Terrain")
	void SetTerrainMap(UDreamWindTerrainMap* Map);

	UFUNCTION(BlueprintPure, Category = "DreamWind|Terrain")
	UDreamWindTerrainMap* GetTerrainMap() const { return TerrainMap; }

	/** Persistent, possibly moving sources register themselves here (see UDreamWindSourceComponent). */
	void RegisterSourceComponent(UDreamWindSourceComponent* Component);
	void UnregisterSourceComponent(UDreamWindSourceComponent* Component);

	// ---- GPU / render side --------------------------------------------------------------------

	/** Packed data for this frame (game thread). */
	const FDreamWindFrameData& GetFrameData() const { return FrameData; }

	/** The volume render targets in use, or null. */
	UTextureRenderTargetVolume* GetVolumeTexture() const { return VolumeTexture; }
	UTextureRenderTargetVolume* GetVolumeFarTexture() const { return VolumeFarTexture; }

	/** Where the volume window is centred this frame (world space). */
	FVector GetVolumeCenter() const { return VolumeCenter; }

	/** Called by the view extension with the origin of the most recently set-up view. */
	void ReportViewOrigin(const FVector& ViewOrigin, bool bIsGameView);

	/**
	 * Write a horizontal slice of the engine's global distance field, Size cm wide around Center, into Target (a float
	 * RGBA render target that supports UAV) in the next perspective view of this world: R = distance to the nearest surface
	 * (m), G = how many clipmaps the view had (0: no global distance field that frame), B = 1 inside them. Returns the
	 * probe count that GetDistanceProbeCount reaches once it is written (0: no renderer, nothing queued).
	 */
	uint32 RequestDistanceFieldProbe(UTextureRenderTarget2D* Target, FVector Center, float Size);

	/** Distance field probes written so far. */
	uint32 GetDistanceProbeCount() const;

	/** The runtime terrain wind map captures this box again (a building moved, a wall fell); no-op when it is off. */
	UFUNCTION(BlueprintCallable, Category = "DreamWind|Terrain")
	void MarkObstaclesDirty(FBox Bounds);

	/** The runtime terrain wind map's captures and derivations so far (tests, stats); 0 when it does not run here. */
	uint32 GetTerrainCaptureCount() const;
	uint32 GetTerrainDeriveCount() const;

	/** Force the volume window centre instead of following the camera (cinematics, tests). */
	UFUNCTION(BlueprintCallable, Category = "DreamWind|Volume")
	void OverrideVolumeCenter(FVector Center, bool bEnable);

	// ---- Introspection ------------------------------------------------------------------------

	UFUNCTION(BlueprintPure, Category = "DreamWind")
	bool IsWindEnabled() const { return bEnabledThisFrame; }

	/** Total simulated frames; handy for tests. */
	uint64 GetTickCount() const { return TickCounter; }

	/** The engine WindDirectionalSource component this subsystem drives (null when the bridge is off). */
	UFUNCTION(BlueprintPure, Category = "DreamWind")
	UWindDirectionalSourceComponent* GetEngineWindComponent() const { return EngineWindComponent; }

	/** True when this world drives the shared volume render target this frame. */
	UFUNCTION(BlueprintPure, Category = "DreamWind|Volume")
	bool IsVolumeActive() const { return FrameData.V1.W > 0.5f; }

	/** True when the volumes are solved as a fluid this frame (else the residual volume, or nothing). */
	UFUNCTION(BlueprintPure, Category = "DreamWind|Volume")
	bool IsFluidActive() const { return bFluidActive; }

	/** The fluid's steps and its wall lookups in the global distance field so far (tests, stats). */
	uint32 GetFluidStepCount() const;
	uint32 GetFluidWallPassCount() const;

	/**
	 * Read a volume back (tests, stats; flushes rendering, slow): its texels, x fastest, and the window it covered (the
	 * consumers' V0 / V1, or V2 / V3 for the far level). False when the level does not run here.
	 */
	bool ReadVolume(bool bFar, TArray<FFloat16Color>& OutTexels, FIntVector& OutSize, FVector4f& OutV0, FVector4f& OutV1) const;

	/** Packs a source description into the record the shared model (CPU and GPU) evaluates. */
	static DreamWind::Hlsl::FDreamWindSourceData PackSource(const FDreamWindSourceDesc& Desc, float StrengthScale);

private:
	struct FActiveImpulse
	{
		FDreamWindSourceDesc Desc;
		int32 Id = 0;
		double StartTime = 0.0;
		double EndTime = 0.0;
		float FadeOutTime = 0.0f;
		bool bApplied = false;
	};

	void AdvanceGlobalWind(float DeltaTime);
	void GatherSources();
	void BuildFrameData(float DeltaTime);
	void UpdateEngineWindSource();
	void UpdateMaterialParameterCollection();
	void UpdateVolume(float DeltaTime);
	/** The fluid's step (UpdateVolume, once this world owns the volumes); false when it cannot run. */
	bool UpdateFluid(float WindDelta, bool bRedrawAll);
	/** A fluid level's window: on the ground under the camera, rising with a camera high above it; whole cells. */
	FVector ChooseFluidCenter(const FVector& Camera, const FIntVector& Res, float CellSize, double& InOutFloorZ) const;
	/** The top surface under a point as the terrain maps know it (cm); false where none does. */
	bool FindGroundHeight(const FVector& Position, double& OutZ) const;
	void UpdateTerrainTexture();
	/** Applies the terrain map (if any) to an analytic sample on the CPU. */
	DreamWind::Hlsl::FDreamWindSample ApplyTerrain(DreamWind::Hlsl::FDreamWindSample S, DreamWind::Hlsl::float3 Pos) const;
	bool UpdateVolumeLevel(float WindDelta, UTextureRenderTargetVolume* Texture, const TSharedPtr<FDreamWindVolumeRenderer, ESPMode::ThreadSafe>& Renderer,
		float VoxelSize, const FVector& Center, FVector& InOutPrevCenter, bool& bInOutHistoryDirty);
	static void EnsureVolumeTextureMatches(UTextureRenderTargetVolume* Texture, const FIntVector& Res, const TSharedPtr<FDreamWindVolumeRenderer, ESPMode::ThreadSafe>& Renderer);
	FVector SnapToVoxels(const FVector& Center, float VoxelSize) const;
	void UpdateDebugDisplay();
	void DrawDebug();
	void DrawDebugField(UWorld* World);
	/** Player camera in game worlds, the last reported view origin in the editor. */
	FVector FindCameraLocation() const;
	void ResolveAssets();
	FVector ChooseVolumeCenter() const;

private:
	FDreamWindGlobalWind CurrentWind;
	FDreamWindGlobalWind BlendFromWind;
	FDreamWindGlobalWind BlendToWind;
	float BlendDuration = 0.0f;
	float BlendElapsed = 0.0f;
	bool bBlending = false;

	double WindTime = 0.0;
	float TimeScale = 1.0f;
	uint64 TickCounter = 0;
	bool bEnabledThisFrame = false;

	TArray<FActiveImpulse> Impulses;
	int32 NextImpulseId = 1;
	TArray<TWeakObjectPtr<UDreamWindSourceComponent>> SourceComponents;

	FDreamWindFrameData FrameData;

	// engine bridge
	UPROPERTY(Transient)
	TObjectPtr<UWindDirectionalSourceComponent> EngineWindComponent;
	FVector LastEngineDirection = FVector::ZeroVector;
	float LastEngineSpeed = -1.0f;
	float LastEngineMinGust = -1.0f;
	float LastEngineMaxGust = -1.0f;
	float LastEngineStrength = -1.0f;

	// volume
	UPROPERTY(Transient)
	TObjectPtr<UTextureRenderTargetVolume> VolumeTexture;
	UPROPERTY(Transient)
	TObjectPtr<UTextureRenderTargetVolume> VolumeFarTexture;
	TSharedPtr<FDreamWindVolumeRenderer, ESPMode::ThreadSafe> VolumeRenderer;
	TSharedPtr<FDreamWindVolumeRenderer, ESPMode::ThreadSafe> VolumeRendererFar;
	FVector VolumeCenterFar = FVector::ZeroVector;
	FVector PrevVolumeCenterFar = FVector::ZeroVector;
	bool bVolumeHistoryDirtyFar = true;
	/** The wind fluid (null until it first runs) and its windows' floors (they move only by two cells or more). */
	TSharedPtr<class FDreamWindFluid, ESPMode::ThreadSafe> Fluid;
	double FluidFloorZ = 0.0;
	double FluidFloorZFar = 0.0;
	bool bFluidFloorValid = false;
	bool bFluidActive = false;
	bool bFluidHistoryDirty = true;
	bool bFluidDistanceFieldRequested = false;

	// terrain
	UPROPERTY(Transient)
	TObjectPtr<UDreamWindTerrainMap> TerrainMap;
	UPROPERTY(Transient)
	TObjectPtr<UTextureRenderTarget2D> TerrainTexture;
	TSharedPtr<const FDreamWindTerrainCpuData, ESPMode::ThreadSafe> TerrainCpu;
	bool bTerrainTextureDirty = false;
	bool bTerrainMapExplicit = false;
	TSharedPtr<FDreamWindViewExtension, ESPMode::ThreadSafe> ViewExtension;
	FVector VolumeCenter = FVector::ZeroVector;
	FVector PrevVolumeCenter = FVector::ZeroVector;
	bool bVolumeHistoryDirty = true;
	/** The volumes and the terrain map are shared by every world: this world writes them only while it owns them. */
	DreamWorld::SharedTargets::FWriter VolumeWriter;
	DreamWorld::SharedTargets::FWriter TerrainWriter;
	DreamWorld::SharedTargets::FWriter RuntimeTerrainWriter;
	/** The runtime terrain wind map (null until it first runs), and its CPU grid. */
	TSharedPtr<class FDreamWindTerrainField> TerrainField;
	TSharedPtr<class FDreamWindTerrainGrid, ESPMode::ThreadSafe> TerrainGrid;
	void UpdateRuntimeTerrain();
	FVector LatestViewOrigin = FVector::ZeroVector;
	bool bHasViewOrigin = false;
	bool bHasGameViewOrigin = false;
	FVector VolumeCenterOverride = FVector::ZeroVector;
	bool bVolumeCenterOverridden = false;
	bool bAssetsResolved = false;
	double LastAssetResolveTime = -1000.0;
};
