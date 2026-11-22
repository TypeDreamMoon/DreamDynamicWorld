// Copyright DreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "DreamAtmosphereTypes.h"
#include "Engine/DeveloperSettings.h"
#include "DreamAtmosphereSettings.generated.h"

/** The atmosphere's project settings: the air the sky is made of until a sky style brings its own, and the tables' sizes. */
UCLASS(Config = Game, DefaultConfig, meta = (DisplayName = "Dream Atmosphere"))
class DREAMATMOSPHERE_API UDreamAtmosphereSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	virtual FName GetCategoryName() const override { return TEXT("Plugins"); }

	static const UDreamAtmosphereSettings* Get() { return GetDefault<UDreamAtmosphereSettings>(); }

	/** Build the lookup tables and draw the air (off: nothing of this module runs). */
	UPROPERTY(Config, EditAnywhere, Category = "General")
	bool bEnabled = true;

	/** The air, until a sky style sets its own (UDreamAtmosphereSubsystem::SetParameters). */
	UPROPERTY(Config, EditAnywhere, Category = "General")
	FDreamAtmosphereParameters Parameters;

	/** The transmittance table: zenith angles by heights. */
	UPROPERTY(Config, EditAnywhere, Category = "Tables", meta = (ClampMin = "16", ClampMax = "1024"))
	FIntPoint TransmittanceSize = FIntPoint(256, 64);

	/** The multiple scattering table: light angles by heights. */
	UPROPERTY(Config, EditAnywhere, Category = "Tables", meta = (ClampMin = "8", ClampMax = "128"))
	FIntPoint MultiScatteringSize = FIntPoint(32, 32);

	/** The sky table: view elevations, angles round from the light, light elevations. */
	UPROPERTY(Config, EditAnywhere, Category = "Tables", meta = (ClampMin = "8", ClampMax = "256"))
	FIntVector SkySize = FIntVector(64, 32, 32);

	/** Steps along each of the sky table's rays. */
	UPROPERTY(Config, EditAnywhere, Category = "Tables", meta = (ClampMin = "8", ClampMax = "128"))
	int32 SkySteps = 32;

	/**
	 * The sun's illuminance outside the air, in the scene's units (the sky style's: about 10 in the sun at noon, which
	 * the air brings down from this). Until a sky style drives the light from the air itself.
	 */
	UPROPERTY(Config, EditAnywhere, Category = "Lights", meta = (ClampMin = "0"))
	float SunIlluminance = 11.5f;

	UPROPERTY(Config, EditAnywhere, Category = "Lights", meta = (HideAlphaChannel))
	FLinearColor SunColor = FLinearColor::White;

	/** The full moon's illuminance outside the air, in the scene's units (a phase dims it). */
	UPROPERTY(Config, EditAnywhere, Category = "Lights", meta = (ClampMin = "0"))
	float MoonIlluminance = 1.3f;

	UPROPERTY(Config, EditAnywhere, Category = "Lights", meta = (HideAlphaChannel))
	FLinearColor MoonColor = FLinearColor::White;

	/**
	 * The air between the eye and the scene: distant things fade into the sky's colour and take its light (a froxel
	 * volume in front of the camera, laid over the opaque scene by its depth). The weather's fog stays the height fog's.
	 */
	UPROPERTY(Config, EditAnywhere, Category = "Aerial Perspective")
	bool bAerialPerspective = true;

	/** How far the volume reaches; farther (up to the sky) takes its last slice. */
	UPROPERTY(Config, EditAnywhere, Category = "Aerial Perspective", meta = (ClampMin = "1", Units = "km"))
	float AerialMaxDistance = 64.0f;

	/** Stretches the world's distances for the air: above 1 a game's few kilometres read as a landscape's tens. */
	UPROPERTY(Config, EditAnywhere, Category = "Aerial Perspective", meta = (ClampMin = "0", ClampMax = "100"))
	float AerialDistanceScale = 1.0f;

	/** Farther than this is the sky (the sky dome, which carries the air already) and is left alone. */
	UPROPERTY(Config, EditAnywhere, Category = "Aerial Perspective", meta = (ClampMin = "1", Units = "km"))
	float SkyDistance = 500.0f;

	/** The volume's cells: across, down, depth slices. */
	UPROPERTY(Config, EditAnywhere, Category = "Aerial Perspective", meta = (ClampMin = "4", ClampMax = "128"))
	FIntVector AerialVolumeSize = FIntVector(32, 32, 32);

	/** Steps along each cell's ray. */
	UPROPERTY(Config, EditAnywhere, Category = "Aerial Perspective", meta = (ClampMin = "2", ClampMax = "32"))
	int32 AerialSteps = 8;

	/** The world height of the planet's surface (the air's altitudes count from here). */
	UPROPERTY(Config, EditAnywhere, Category = "Aerial Perspective", meta = (Units = "cm"))
	float SeaLevel = 0.0f;
};
