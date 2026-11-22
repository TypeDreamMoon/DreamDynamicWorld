// Copyright DreamMoon. All Rights Reserved.

#include "DreamWindCameraShake.h"
#include "DreamWindSubsystem.h"
#include "DreamWorldSubsystem.h"

#include "Camera/PlayerCameraManager.h"
#include "GameFramework/PlayerController.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(DreamWindCameraShake)

UDreamWindCameraModifier::UDreamWindCameraModifier()
{
	Priority = 120;
}

UDreamWindCameraModifier* UDreamWindCameraModifier::Enable(APlayerController* Player)
{
	APlayerCameraManager* Camera = Player ? Player->PlayerCameraManager.Get() : nullptr;
	if (!Camera)
	{
		return nullptr;
	}
	if (UDreamWindCameraModifier* Existing = Cast<UDreamWindCameraModifier>(Camera->FindCameraModifierByClass(UDreamWindCameraModifier::StaticClass())))
	{
		return Existing;
	}
	return Cast<UDreamWindCameraModifier>(Camera->AddNewCameraModifier(UDreamWindCameraModifier::StaticClass()));
}

float UDreamWindCameraModifier::IntensityFor(float WindSpeed, float Gust, float Interior) const
{
	// A gust pushes the speed over the threshold for a moment: the shake comes in bursts.
	const float Strength = FMath::SmoothStep(Threshold, FMath::Max(FullSpeed, Threshold + 1.0f), WindSpeed * FMath::Max(Gust, 0.0f));
	return Strength * (1.0f - FMath::Clamp(Interior, 0.0f, 1.0f) * InteriorDamping);
}

bool UDreamWindCameraModifier::ModifyCamera(float DeltaTime, FMinimalViewInfo& InOutPOV)
{
	Super::ModifyCamera(DeltaTime, InOutPOV);
	const UDreamWindSubsystem* Wind = UDreamWindSubsystem::Get(CameraOwner);
	const UDreamWorldSubsystem* Clock = UDreamWorldSubsystem::Get(CameraOwner);
	float Target = 0.0f;
	if (Wind)
	{
		const FDreamWindSampleResult Sample = Wind->SampleWind(InOutPOV.Location);
		const FDreamViewOcclusion& Occlusion = Clock ? Clock->GetViewOcclusion() : FDreamViewOcclusion();
		// The wind sample already carries the gust in its speed; the multiplier sharpens the bursts.
		Target = IntensityFor(Sample.Speed / FMath::Max(Sample.Gust, 0.1f), Sample.Gust, Occlusion.bValid ? Occlusion.Interior : 0.0f);
	}
	Intensity = FMath::FInterpTo(Intensity, Target, DeltaTime, 3.0f);
	if (Intensity <= 1.0e-3f)
	{
		return false;
	}
	Phase += double(DeltaTime) * FMath::Lerp(Frequency.X, Frequency.Y, double(Intensity));
	const float P = float(Phase);
	InOutPOV.Rotation.Pitch += Amplitude.X * Intensity * FMath::PerlinNoise1D(P + 11.3f);
	InOutPOV.Rotation.Yaw += Amplitude.Y * Intensity * FMath::PerlinNoise1D(P * 0.9f + 47.1f);
	InOutPOV.Rotation.Roll += Amplitude.Z * Intensity * FMath::PerlinNoise1D(P * 1.3f + 83.7f);
	return false;
}
