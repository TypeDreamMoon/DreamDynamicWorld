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
};
