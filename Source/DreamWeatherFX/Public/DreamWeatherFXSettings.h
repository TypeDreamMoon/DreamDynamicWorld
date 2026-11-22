// Copyright DreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "Engine/EngineTypes.h"
#include "DreamWeatherFXSettings.generated.h"

class UNiagaraSystem;

/** One kind of precipitation: the effect that draws it, how much of it falls at full strength and how it falls. */
USTRUCT(BlueprintType)
struct DREAMWEATHERFX_API FDreamPrecipitationLayer
{
	GENERATED_BODY()

	/**
	 * The effect. Every frame it is given its fall paths (User.Paths, User.Normals: see DreamFallPath.dfm), the
	 * drops' velocity (User.FallVelocity) and how many drops and splashes to spawn (User.DropRate, User.SplashRate).
	 */
	UPROPERTY(Config, EditAnywhere, Category = "Precipitation", meta = (AllowedClasses = "/Script/Niagara.NiagaraSystem"))
	FSoftObjectPath System;

	/**
	 * Drops spawned per second at full strength. About this times the fall time are in the air at once (rain:
	 * some three seconds), which the effect's drop emitter must have room for.
	 */
	UPROPERTY(Config, EditAnywhere, Category = "Precipitation", meta = (ClampMin = "0", UIMax = "20000"))
	float DropsPerSecond = 10000.0f;

	/** Splashes spawned per second at full strength, if every path landed. */
	UPROPERTY(Config, EditAnywhere, Category = "Precipitation", meta = (ClampMin = "0", UIMax = "8000"))
	float SplashesPerSecond = 1500.0f;

	/** Fall speed in still air, cm/s. */
	UPROPERTY(Config, EditAnywhere, Category = "Precipitation", meta = (ClampMin = "10", UIMax = "2000"))
	float FallSpeed = 900.0f;

	/** Share of the wind's velocity the drops take: rain a little, snow most of it. */
	UPROPERTY(Config, EditAnywhere, Category = "Precipitation", meta = (ClampMin = "0", ClampMax = "1"))
	float WindDrift = 0.3f;

	/** The most the drops slant: sideways speed at most this times the fall speed (1 = 45 degrees). */
	UPROPERTY(Config, EditAnywhere, Category = "Precipitation", meta = (ClampMin = "0", UIMax = "4"))
	float MaxSlant = 0.8f;
};

/**
 * Project Settings > DreamPlugin > Dream Weather Effects: the rain and snow drawn around the view, and the fall
 * paths they are drawn along.
 */
UCLASS(Config = Game, DefaultConfig, meta = (DisplayName = "Dream Weather Effects"))
class DREAMWEATHERFX_API UDreamWeatherFXSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	UDreamWeatherFXSettings();

	static const UDreamWeatherFXSettings* Get() { return GetDefault<UDreamWeatherFXSettings>(); }

	//~ UDeveloperSettings
	virtual FName GetCategoryName() const override { return TEXT("DreamPlugin"); }

	/** Draw rain and snow at all (in game worlds). */
	UPROPERTY(Config, EditAnywhere, Category = "General")
	bool bPrecipitation = true;

	/** Draw them in editor viewports too. */
	UPROPERTY(Config, EditAnywhere, Category = "General")
	bool bPrecipitationInEditor = true;

	/** Every local player's camera buffets in a strong wind, calmed indoors (UDreamWindCameraModifier). */
	UPROPERTY(Config, EditAnywhere, Category = "Wind")
	bool bWindCameraShake = true;

	UPROPERTY(Config, EditAnywhere, Category = "Precipitation")
	FDreamPrecipitationLayer Rain;

	UPROPERTY(Config, EditAnywhere, Category = "Precipitation")
	FDreamPrecipitationLayer Snow;

	/**
	 * Dust and sand blown along the ground: its paths run nearly flat downwind (FallSpeed is how fast motes settle,
	 * MaxSlant how flat they may go), so motes stream past outside and never blow indoors. Its strength is the Dust
	 * channel; the same paths carry the wind's debris.
	 */
	UPROPERTY(Config, EditAnywhere, Category = "Dust and Debris")
	FDreamPrecipitationLayer Dust;

	/**
	 * Debris in a strong wind (leaves, twigs, scraps): spawned on the dust layer's paths like the motes and carried by
	 * the wind field. Given User.Paths / User.Normals, User.DebrisRate (per second) and User.DebrisColor (the season's).
	 */
	UPROPERTY(Config, EditAnywhere, Category = "Dust and Debris", meta = (AllowedClasses = "/Script/Niagara.NiagaraSystem"))
	FSoftObjectPath WindDebris;

	/** Wind speed at the view (cm/s) where debris starts to fly, and where it is at its most. */
	UPROPERTY(Config, EditAnywhere, Category = "Dust and Debris")
	FVector2D DebrisWindSpeed = FVector2D(900.0, 2200.0);

	/** Pieces spawned per second at the most. */
	UPROPERTY(Config, EditAnywhere, Category = "Dust and Debris", meta = (ClampMin = "0", UIMax = "1000"))
	float DebrisPerSecond = 300.0f;

	/** How much debris each season has (spring, summer, autumn, winter): autumn leaves, a bare winter. */
	UPROPERTY(Config, EditAnywhere, Category = "Dust and Debris")
	FVector4f DebrisSeasonAmount = FVector4f(0.6f, 0.4f, 1.6f, 0.25f);

	/** The debris's colour in each season. */
	UPROPERTY(Config, EditAnywhere, Category = "Dust and Debris")
	FLinearColor DebrisSpring = FLinearColor(0.35f, 0.55f, 0.15f);

	UPROPERTY(Config, EditAnywhere, Category = "Dust and Debris")
	FLinearColor DebrisSummer = FLinearColor(0.25f, 0.45f, 0.12f);

	UPROPERTY(Config, EditAnywhere, Category = "Dust and Debris")
	FLinearColor DebrisAutumn = FLinearColor(0.75f, 0.42f, 0.10f);

	UPROPERTY(Config, EditAnywhere, Category = "Dust and Debris")
	FLinearColor DebrisWinter = FLinearColor(0.42f, 0.33f, 0.24f);

	/** Fall paths kept per kind of precipitation. More = less visible repetition, more traces. */
	UPROPERTY(Config, EditAnywhere, Category = "Fall Paths", meta = (ClampMin = "16", UIMax = "8192"))
	int32 PathCount = 1024;

	/** Seconds to re-trace every path once (PathCount / this = line traces per second while it rains). */
	UPROPERTY(Config, EditAnywhere, Category = "Fall Paths", meta = (ClampMin = "0.1", UIMax = "5"))
	float PathRefreshSeconds = 1.0f;

	/** Paths land within this radius around the view, cm. */
	UPROPERTY(Config, EditAnywhere, Category = "Fall Paths", meta = (ClampMin = "100", UIMax = "10000"))
	float PathRadius = 3000.0f;

	/** 0 = all around the view .. 1 = mostly in front of it. */
	UPROPERTY(Config, EditAnywhere, Category = "Fall Paths", meta = (ClampMin = "0", ClampMax = "1"))
	float PathForwardBias = 0.6f;

	/** Height above the view where the paths start, cm. */
	UPROPERTY(Config, EditAnywhere, Category = "Fall Paths", meta = (ClampMin = "100", UIMax = "10000"))
	float PathStartHeight = 2500.0f;

	/** A path that hits nothing ends after falling this far below its start, cm. */
	UPROPERTY(Config, EditAnywhere, Category = "Fall Paths", meta = (ClampMin = "100", UIMax = "20000"))
	float PathMaxLength = 8000.0f;

	/** The channel the paths trace on: roofs, ground and canopies must block it. */
	UPROPERTY(Config, EditAnywhere, Category = "Fall Paths")
	TEnumAsByte<ECollisionChannel> PathChannel = ECC_Visibility;

	/** Seconds between hand-overs of the paths to the effects. */
	UPROPERTY(Config, EditAnywhere, Category = "Fall Paths", meta = (ClampMin = "0", UIMax = "1"))
	float UploadInterval = 0.1f;

	/** Draw a bolt where lightning strikes (the flash on the sky is drawn either way). */
	UPROPERTY(Config, EditAnywhere, Category = "Lightning")
	bool bLightningBolts = true;

	/**
	 * The bolt. It is given User.Start (the cloud base above the strike), User.End (the strike point on the ground),
	 * User.Seed (its shape) and, every frame, User.Glow (the flash's strokes, 0..1): see NS_DreamLightningBolt.
	 */
	UPROPERTY(Config, EditAnywhere, Category = "Lightning", meta = (AllowedClasses = "/Script/Niagara.NiagaraSystem"))
	FSoftObjectPath LightningBolt;

	/** Height above the ground where a bolt leaves the cloud, cm. */
	UPROPERTY(Config, EditAnywhere, Category = "Lightning", meta = (ClampMin = "1000", UIMax = "500000"))
	float BoltHeight = 120000.0f;

	/** Strikes further from the view than this draw no bolt, cm. */
	UPROPERTY(Config, EditAnywhere, Category = "Lightning", meta = (ClampMin = "0", UIMax = "5000000"))
	float BoltMaxDistance = 1500000.0f;
};
