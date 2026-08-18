// Copyright DreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "DreamWindTypes.h"
#include "DreamWindSettings.generated.h"

class UMaterialParameterCollection;
class UTextureRenderTargetVolume;

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

	// ---- Materials ----------------------------------------------------------------------------

	/**
	 * Material Parameter Collection the subsystem writes every frame. Materials sample it through
	 * MF_DreamWindSample. Created by Tools > DreamWind > Setup Assets when missing.
	 */
	UPROPERTY(Config, EditAnywhere, Category = "Materials", meta = (AllowedClasses = "/Script/Engine.MaterialParameterCollection"))
	FSoftObjectPath ParameterCollection = FSoftObjectPath(TEXT("/DreamWind/MPC_DreamWind.MPC_DreamWind"));

	// ---- Dynamic volume (L2) ------------------------------------------------------------------

	/** Simulate the camera-following dynamic volume (impulses, wakes, residual gusts). */
	UPROPERTY(Config, EditAnywhere, Category = "Volume")
	bool bVolumeEnabled = true;

	/** Volume render target the compute pass writes and materials/Niagara sample. */
	UPROPERTY(Config, EditAnywhere, Category = "Volume", meta = (AllowedClasses = "/Script/Engine.TextureRenderTargetVolume"))
	FSoftObjectPath VolumeTexture = FSoftObjectPath(TEXT("/DreamWind/RT_DreamWindVolume.RT_DreamWindVolume"));

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

	/** Maximum live sources uploaded to the GPU per frame; the strongest survive. */
	UPROPERTY(Config, EditAnywhere, Category = "Volume", meta = (ClampMin = "1", ClampMax = "256"))
	int32 MaxGpuSources = 64;

	// ---- Debug --------------------------------------------------------------------------------

	/** Print the global wind and the live source count on screen. */
	UPROPERTY(Config, EditAnywhere, Category = "Debug")
	bool bShowDebugText = false;
};
