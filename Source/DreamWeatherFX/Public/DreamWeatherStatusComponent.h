// Copyright DreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "DreamWeatherStatusComponent.generated.h"

class UNiagaraComponent;
class UNiagaraSystem;

/** What the weather does to an actor, for events. */
UENUM(BlueprintType)
enum class EDreamWeatherExposure : uint8
{
	/** Rain is landing on it. */
	Rain,
	/** Snow is landing on it. */
	Snow,
	/** A strong wind blows on it. */
	Wind,
	/** Dust or sand blows on it. */
	Dust,
	/** The air feels below ColdTemperature. */
	Cold,
	/** The air is above HotTemperature. */
	Heat,
	/** Wet through (Wetness above 0.8). */
	Soaked,
	/** Chilled through (Chill above 0.8). */
	Freezing,
	Num UMETA(Hidden)
};

/** An actor's weather: what reaches it now and what has built up on it. */
USTRUCT(BlueprintType)
struct DREAMWEATHERFX_API FDreamWeatherStatus
{
	GENERATED_BODY()

	/** Rain, snow, wind and dust reaching it now, 0..1 (after roofs and walls). */
	UPROPERTY(BlueprintReadOnly, Category = "Weather") float RainExposure = 0.0f;
	UPROPERTY(BlueprintReadOnly, Category = "Weather") float SnowExposure = 0.0f;
	UPROPERTY(BlueprintReadOnly, Category = "Weather") float WindExposure = 0.0f;
	UPROPERTY(BlueprintReadOnly, Category = "Weather") float DustExposure = 0.0f;

	/** What has built up, 0..1: soaked, snowed on, dusty, chilled. */
	UPROPERTY(BlueprintReadOnly, Category = "Weather") float Wetness = 0.0f;
	UPROPERTY(BlueprintReadOnly, Category = "Weather") float SnowCover = 0.0f;
	UPROPERTY(BlueprintReadOnly, Category = "Weather") float Dustiness = 0.0f;
	UPROPERTY(BlueprintReadOnly, Category = "Weather") float Chill = 0.0f;

	/** The air's temperature there, and how cold it feels (wind and wet clothes), degrees Celsius. */
	UPROPERTY(BlueprintReadOnly, Category = "Weather") float Temperature = 15.0f;
	UPROPERTY(BlueprintReadOnly, Category = "Weather") float FeelsLike = 15.0f;

	/** Under a roof. */
	UPROPERTY(BlueprintReadOnly, Category = "Weather") bool bSheltered = false;
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FDreamWeatherExposureEvent, EDreamWeatherExposure, Exposure, bool, bStarted);

/**
 * Put it on the player or anyone the weather should touch. A few times a second it asks the weather what reaches
 * the actor (rain and snow after the weather's shelters and, with bTraceShelter, any roof above it; the wind field's
 * speed there, which knows walls and terrain; the dust; the temperature with temperature volumes), lets wetness,
 * snow, dust and chill build up and wear off, fires events as exposures start and stop (with hysteresis), writes the
 * amounts into the actor's mesh materials (scalar parameters, see MaterialParameters) and, when it feels cold,
 * breathes out mist at BreathSocket.
 */
UCLASS(ClassGroup = "Dream", meta = (BlueprintSpawnableComponent, DisplayName = "Dream Weather Status"))
class DREAMWEATHERFX_API UDreamWeatherStatusComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UDreamWeatherStatusComponent();

	UFUNCTION(BlueprintPure, Category = "Dream Weather")
	const FDreamWeatherStatus& GetStatus() const { return Status; }

	/** Whether an exposure is on now (what the last event said). */
	UFUNCTION(BlueprintPure, Category = "Dream Weather")
	bool IsExposed(EDreamWeatherExposure Exposure) const { return Exposure < EDreamWeatherExposure::Num && bOn[(int32)Exposure]; }

	/** An exposure started or stopped. */
	UPROPERTY(BlueprintAssignable, Category = "Dream Weather")
	FDreamWeatherExposureEvent OnExposureChanged;

	/** Dry off, shake off the snow and dust, warm up at once (a change of clothes, a respawn). */
	UFUNCTION(BlueprintCallable, Category = "Dream Weather")
	void ResetStatus();

	/** Update now, as if Seconds had passed (tests; it runs on its own with the component's tick). */
	void UpdateStatus(float Seconds);

	// ---- Where ----------------------------------------------------------------------------------

	/** The point it samples, relative to the actor (a character's chest). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weather")
	FVector SampleOffset = FVector(0.0, 0.0, 60.0);

	/** Also trace for a roof above (along the rain's slant), so geometry the weather does not know of keeps it dry. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weather")
	bool bTraceShelter = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weather", meta = (EditCondition = "bTraceShelter", ClampMin = "100"))
	float ShelterTraceLength = 3000.0f;

	// ---- How fast -------------------------------------------------------------------------------

	/** Real seconds to be soaked in full rain, and to dry from soaked (in mild air; heat and wind dry faster). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rates", meta = (ClampMin = "0.1"))
	float WetSeconds = 20.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rates", meta = (ClampMin = "0.1"))
	float DrySeconds = 240.0f;

	/** Real seconds to be white with snow, and to lose it (above freezing it melts, and wets). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rates", meta = (ClampMin = "0.1"))
	float SnowSeconds = 60.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rates", meta = (ClampMin = "0.1"))
	float SnowMeltSeconds = 120.0f;

	/** Real seconds to be covered in dust, and to lose it (rain washes it off). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rates", meta = (ClampMin = "0.1"))
	float DustSeconds = 60.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rates", meta = (ClampMin = "0.1"))
	float DustClearSeconds = 300.0f;

	/** Real seconds to be chilled through at 15 degrees below ColdTemperature, and to warm up. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rates", meta = (ClampMin = "0.1"))
	float ChillSeconds = 120.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rates", meta = (ClampMin = "0.1"))
	float WarmSeconds = 60.0f;

	// ---- Thresholds -----------------------------------------------------------------------------

	/** It feels cold below this, degrees Celsius. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Thresholds")
	float ColdTemperature = 5.0f;

	/** It is hot above this, degrees Celsius. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Thresholds")
	float HotTemperature = 30.0f;

	/** Wind speed (cm/s) that counts as full exposure to the wind. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Thresholds", meta = (ClampMin = "100"))
	float WindFullSpeed = 1500.0f;

	/** An exposure starts above this (0..1) and stops below half of it. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Thresholds", meta = (ClampMin = "0.01", ClampMax = "1"))
	float ExposureThreshold = 0.2f;

	// ---- Materials ------------------------------------------------------------------------------

	/** Write the amounts into every mesh of the actor as scalar parameters (dynamic material instances). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Materials")
	bool bWriteMaterialParameters = true;

	/** The scalar parameters: wetness, snow cover, dustiness, wind exposure. None skips one. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Materials")
	FName WetParameter = TEXT("DreamWet");

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Materials")
	FName SnowParameter = TEXT("DreamSnow");

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Materials")
	FName DustParameter = TEXT("DreamDust");

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Materials")
	FName WindParameter = TEXT("DreamWind");

	// ---- Breath ---------------------------------------------------------------------------------

	/** Breathe out mist when it feels colder than BreathTemperature. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Breath")
	bool bFreezingBreath = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Breath", meta = (EditCondition = "bFreezingBreath"))
	float BreathTemperature = 3.0f;

	/** The socket (or bone) of the first skeletal mesh the mist leaves from; none or missing = SampleOffset raised to the head. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Breath", meta = (EditCondition = "bFreezingBreath"))
	FName BreathSocket = TEXT("head");

	/** Where the mouth is relative to the actor (+X forward) when there is no such socket. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Breath", meta = (EditCondition = "bFreezingBreath"))
	FVector BreathOffset = FVector(15.0, 0.0, 70.0);

	/** The effect: given User.Intensity (0..1, how cold) every update. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Breath", meta = (EditCondition = "bFreezingBreath", AllowedClasses = "/Script/Niagara.NiagaraSystem"))
	FSoftObjectPath BreathEffect = FSoftObjectPath(TEXT("/DreamDynamicWorld/Weather/Effects/NS_DreamFreezingBreath.NS_DreamFreezingBreath"));

	UNiagaraComponent* GetBreathComponent() const { return Breath; }

	//~ UActorComponent
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

private:
	float TracedCover(const FVector& Position, const FVector& RainDirection) const;
	void FireEvents();
	void WriteMaterials();
	void UpdateBreath();

	FDreamWeatherStatus Status;
	bool bOn[(int32)EDreamWeatherExposure::Num] = {};

	UPROPERTY(Transient)
	TObjectPtr<UNiagaraComponent> Breath;
};
