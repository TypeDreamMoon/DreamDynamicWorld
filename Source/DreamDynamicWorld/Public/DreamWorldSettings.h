// Copyright DreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "Engine/EngineTypes.h"
#include "DreamWorldTypes.h"
#include "DreamWorldSettings.generated.h"

/**
 * Project Settings > DreamPlugin > Dream Dynamic World: what every world starts with. A level overrides the
 * clock through its sky actor; gameplay through UDreamWorldSubsystem.
 */
UCLASS(Config = Game, DefaultConfig, meta = (DisplayName = "Dream Dynamic World"))
class DREAMDYNAMICWORLD_API UDreamWorldSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	static const UDreamWorldSettings* Get() { return GetDefault<UDreamWorldSettings>(); }

	//~ UDeveloperSettings
	virtual FName GetCategoryName() const override { return TEXT("DreamPlugin"); }

	/** Run the world clock and everything driven by it in editor worlds, so the viewport shows the sky. */
	UPROPERTY(Config, EditAnywhere, Category = "General")
	bool bTickInEditor = true;

	/** The clock of a world whose level does not set one. */
	UPROPERTY(Config, EditAnywhere, Category = "General")
	FDreamWorldClockConfig DefaultClock;

	/**
	 * The Material Parameter Collection every subsystem of the plugin writes (wind, sky, ...). One collection
	 * for all of them, so a material reading several still leaves a collection slot for the project.
	 * Created by the Setup step (DreamDynamicWorldSetup commandlet) when missing.
	 */
	UPROPERTY(Config, EditAnywhere, Category = "Materials", meta = (AllowedClasses = "/Script/Engine.MaterialParameterCollection"))
	FSoftObjectPath ParameterCollection = FSoftObjectPath(TEXT("/DreamDynamicWorld/MPC_DreamDynamicWorld.MPC_DreamDynamicWorld"));

	/** Milliseconds per frame the scheduled background work (sky light recaptures and the like) may use. */
	UPROPERTY(Config, EditAnywhere, Category = "Performance", meta = (ClampMin = "0.01", UIMax = "4"))
	float SchedulerBudgetMs = 0.5f;

	// ---- View probe: is the camera indoors? ----------------------------------------------------

	/**
	 * Cast rays around the view to tell indoors from outdoors (FDreamViewOcclusion): the sky lifts the exposure
	 * indoors, the weather muffles its sounds. A few rays a frame, a full round every ProbeRoundSeconds.
	 */
	UPROPERTY(Config, EditAnywhere, Category = "View Probe")
	bool bProbeViewOcclusion = true;

	/** The channel the rays trace on. Walls and roofs must block it. */
	UPROPERTY(Config, EditAnywhere, Category = "View Probe")
	TEnumAsByte<ECollisionChannel> ProbeChannel = ECC_Visibility;

	/** How far a ray looks for a wall, cm. About the size of the largest room that should count as indoors. */
	UPROPERTY(Config, EditAnywhere, Category = "View Probe", meta = (ClampMin = "100", UIMax = "20000"))
	float ProbeDistance = 4500.0f;

	/** Seconds to cast every ray once. The rays of a round are spread over its frames. */
	UPROPERTY(Config, EditAnywhere, Category = "View Probe", meta = (ClampMin = "0.02", UIMax = "2"))
	float ProbeRoundSeconds = 0.35f;

	/** Seconds the occlusion takes to follow a change (walking through a door). */
	UPROPERTY(Config, EditAnywhere, Category = "View Probe", meta = (ClampMin = "0", UIMax = "3"))
	float ProbeResponseSeconds = 0.5f;

	/** Share of rays that must hit before the view counts as partly indoors, and the share at which it is fully indoors. */
	UPROPERTY(Config, EditAnywhere, Category = "View Probe")
	FVector2D ProbeInteriorRange = FVector2D(0.2, 0.9);

	/** A view that moves farther than this in one frame (a teleport, a cut) is probed whole at once, cm. */
	UPROPERTY(Config, EditAnywhere, Category = "View Probe", meta = (ClampMin = "0"))
	float ProbeTeleportDistance = 1000.0f;
};
