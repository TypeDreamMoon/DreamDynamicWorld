// Copyright DreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "DreamSurfaceTypes.h"
#include "DreamSurfaceSettings.generated.h"

/** Project Settings > DreamPlugin > Dream Surface. */
UCLASS(Config = Game, DefaultConfig, meta = (DisplayName = "Dream Surface"))
class DREAMSURFACE_API UDreamSurfaceSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	static const UDreamSurfaceSettings* Get() { return GetDefault<UDreamSurfaceSettings>(); }

	//~ UDeveloperSettings
	virtual FName GetCategoryName() const override { return TEXT("DreamPlugin"); }

	/** Run the surfaces wherever the weather runs. Off: every surface stays dry and clean. */
	UPROPERTY(Config, EditAnywhere, Category = "General")
	bool bEnabled = true;

	/** How fast the weather wets, snows over and dusts a surface, and how fast it recovers. */
	UPROPERTY(Config, EditAnywhere, Category = "Rates", meta = (ShowOnlyInnerProperties))
	FDreamSurfaceRates Rates;

	/**
	 * Game hours of the weather's past a surface remembers: its state is the weather integrated over this long. Longer
	 * keeps deep snow through a long thaw; shorter is cheaper to ask.
	 */
	UPROPERTY(Config, EditAnywhere, Category = "Timeline", meta = (ClampMin = "1", ClampMax = "720", Units = "Hours"))
	float MemoryHours = 48.0f;

	/** Game minutes per step of that integral (the weather is taken as holding still over a step). */
	UPROPERTY(Config, EditAnywhere, Category = "Timeline", meta = (ClampMin = "1", ClampMax = "120", Units = "Minutes"))
	float StepMinutes = 15.0f;

	/** The depth of the deepest snow cover (Snow = 1), cm: what the terrain's displacement and footprints use. */
	UPROPERTY(Config, EditAnywhere, Category = "Snow", meta = (ClampMin = "0", Units = "cm"))
	float MaxSnowDepth = 30.0f;
};
