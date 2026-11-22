// Copyright DreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Camera/CameraModifier.h"
#include "DreamWindCameraShake.generated.h"

class APlayerController;

/**
 * The camera shakes in a strong wind: from Threshold wind speed at the camera (the wind field, so walls and terrain
 * calm it) up to FullSpeed, a buffeting of the view's pitch, yaw and roll that grows with the speed and the gusts,
 * and fades out indoors (the view probe). Add it to a player's camera manager with Enable, or let the weather do it
 * (Dream Weather Effects > Wind Camera Shake).
 */
UCLASS(BlueprintType)
class DREAMWEATHERFX_API UDreamWindCameraModifier : public UCameraModifier
{
	GENERATED_BODY()

public:
	UDreamWindCameraModifier();

	/** Add the shake to a player's camera (or return the one it has). */
	UFUNCTION(BlueprintCallable, Category = "Dream Weather", meta = (DisplayName = "Enable Wind Camera Shake"))
	static UDreamWindCameraModifier* Enable(APlayerController* Player);

	/** Wind speed (cm/s) where the shaking starts, and where it is at its strongest. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Wind Shake")
	float Threshold = 1200.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Wind Shake")
	float FullSpeed = 2600.0f;

	/** Degrees of pitch, yaw and roll at full strength. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Wind Shake")
	FVector Amplitude = FVector(0.6, 0.4, 0.8);

	/** Buffets per second at the threshold and at full strength. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Wind Shake")
	FVector2D Frequency = FVector2D(1.5, 4.0);

	/** How much indoors calms it, 0..1 (1 = no shake inside). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Wind Shake", meta = (ClampMin = "0", ClampMax = "1"))
	float InteriorDamping = 1.0f;

	/** How strong it is now, 0..1. */
	UFUNCTION(BlueprintPure, Category = "Wind Shake")
	float GetIntensity() const { return Intensity; }

	/** Strength for a wind speed, a gust multiplier and how indoors the view is. Pure. */
	float IntensityFor(float WindSpeed, float Gust, float Interior) const;

	//~ UCameraModifier
	virtual bool ModifyCamera(float DeltaTime, FMinimalViewInfo& InOutPOV) override;

private:
	float Intensity = 0.0f;
	double Phase = 0.0;
};
