// Copyright DreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "DreamWindTypes.h"
#include "DreamWindHlslShim.h"
#include "DreamWindSubsystem.generated.h"

class UDreamWindSourceComponent;
class UWindDirectionalSourceComponent;
class UMaterialParameterCollection;
class UMaterialParameterCollectionInstance;
class UTextureRenderTargetVolume;
class FDreamWindVolumeRenderer;
class FDreamWindViewExtension;

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
	/** DW_UnpackVolume(V0, V1) */
	FVector4f V0 = FVector4f::Zero();
	FVector4f V1 = FVector4f::Zero();
	/** Live sources this frame, strongest first, already envelope-scaled. */
	TArray<DreamWind::Hlsl::FDreamWindSourceData> Sources;
	/** Volume render target (may be null when the volume is disabled or the asset is missing). */
	TWeakObjectPtr<UTextureRenderTargetVolume> VolumeTexture;
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

	/** Persistent, possibly moving sources register themselves here (see UDreamWindSourceComponent). */
	void RegisterSourceComponent(UDreamWindSourceComponent* Component);
	void UnregisterSourceComponent(UDreamWindSourceComponent* Component);

	// ---- GPU / render side --------------------------------------------------------------------

	/** Packed data for this frame (game thread). */
	const FDreamWindFrameData& GetFrameData() const { return FrameData; }

	/** The volume render target in use, or null. */
	UTextureRenderTargetVolume* GetVolumeTexture() const { return VolumeTexture; }

	/** Where the volume window is centred this frame (world space). */
	FVector GetVolumeCenter() const { return VolumeCenter; }

	/** Called by the view extension with the origin of the most recently set-up view. */
	void ReportViewOrigin(const FVector& ViewOrigin, bool bIsGameView);

	/** Force the volume window centre instead of following the camera (cinematics, tests). */
	UFUNCTION(BlueprintCallable, Category = "DreamWind|Volume")
	void OverrideVolumeCenter(FVector Center, bool bEnable);

	// ---- Introspection ------------------------------------------------------------------------

	UFUNCTION(BlueprintPure, Category = "DreamWind")
	bool IsWindEnabled() const { return bEnabledThisFrame; }

	/** Total simulated frames; handy for tests. */
	uint64 GetTickCount() const { return TickCounter; }

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
	void UpdateDebugDisplay();
	void ResolveAssets();
	FVector ChooseVolumeCenter() const;

	static DreamWind::Hlsl::FDreamWindSourceData PackSource(const FDreamWindSourceDesc& Desc, float StrengthScale);

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

	// materials
	UPROPERTY(Transient)
	TObjectPtr<UMaterialParameterCollection> ParameterCollection;
	UPROPERTY(Transient)
	TObjectPtr<UMaterialParameterCollectionInstance> ParameterCollectionInstance;

	// volume
	UPROPERTY(Transient)
	TObjectPtr<UTextureRenderTargetVolume> VolumeTexture;
	TSharedPtr<FDreamWindVolumeRenderer, ESPMode::ThreadSafe> VolumeRenderer;
	TSharedPtr<FDreamWindViewExtension, ESPMode::ThreadSafe> ViewExtension;
	FVector VolumeCenter = FVector::ZeroVector;
	FVector PrevVolumeCenter = FVector::ZeroVector;
	bool bVolumeHistoryDirty = true;
	FVector LatestViewOrigin = FVector::ZeroVector;
	bool bHasViewOrigin = false;
	bool bHasGameViewOrigin = false;
	FVector VolumeCenterOverride = FVector::ZeroVector;
	bool bVolumeCenterOverridden = false;
	bool bAssetsResolved = false;
	double LastAssetResolveTime = -1000.0;
};
