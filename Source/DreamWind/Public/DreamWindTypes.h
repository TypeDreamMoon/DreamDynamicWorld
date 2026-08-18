// Copyright DreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "DreamWindTypes.generated.h"

/** Shape of a wind source / impulse. Mirrors DW_SOURCE_* in Shaders/Shared/DreamWindShared.h. */
UENUM(BlueprintType)
enum class EDreamWindSourceType : uint8
{
	/** Pushes along Direction everywhere inside the sphere: fans, wakes, spell gusts. */
	Directional = 0,
	/** Pushes away from the centre: explosions, landings, shockwaves. */
	Radial      = 1,
	/** Swirls around Direction used as an axis: tornadoes, spin attacks. */
	Vortex      = 2,
	/**
	 * An oriented box in which the wind becomes Direction * Strength. Strength 0 makes a sheltered
	 * interior (house, cave, the lee of a wall - the L1 terrain bake cannot see "inside"); Strength > 0
	 * makes a wind zone (corridor draft, wind tunnel). Uses BoxExtent / BoxRotation instead of Radius.
	 */
	Shelter     = 3,
};

/** Quality of the analytic (L0) evaluation. */
UENUM(BlueprintType)
enum class EDreamWindQuality : uint8
{
	/** 1 gust octave + temporal flicker + 1 curl octave. For vertex-heavy consumers. */
	Cheap,
	/** 2 gust octaves + temporal flicker + 2 curl octaves + vertical turbulence. */
	Full,
};

/**
 * The global wind (L0). Everything is world space, centimetres, seconds; wavelengths are the
 * spatial period of the noise, so a 4000 gust wavelength means gust fronts about 40 m apart.
 */
USTRUCT(BlueprintType)
struct DREAMWIND_API FDreamWindGlobalWind
{
	GENERATED_BODY()

	/** Direction the wind blows TOWARDS. Need not be normalised or horizontal. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Wind")
	FVector Direction = FVector(1.0, 0.0, 0.0);

	/** Mean wind speed in cm/s. 300 = light breeze, 1000 = fresh breeze, 2500 = gale. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Wind", meta = (ClampMin = "0", UIMax = "5000"))
	float Speed = 600.0f;

	/** Relative amplitude of the travelling gust fronts. 0 = perfectly steady, 1 = speed swings between ~0 and ~2x. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Gusts", meta = (ClampMin = "0", UIMax = "1.5"))
	float GustStrength = 0.5f;

	/** Distance between gust fronts along the wind, cm. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Gusts", meta = (ClampMin = "10", UIMax = "20000"))
	float GustWavelength = 4000.0f;

	/** Gust fronts travel downwind at Speed * this. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Gusts", meta = (ClampMin = "0", UIMax = "2"))
	float GustSpeedScale = 0.8f;

	/** Global temporal flicker of the gust factor, Hz. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Gusts", meta = (ClampMin = "0", UIMax = "2"))
	float GustFrequency = 0.15f;

	/** Relative amplitude of the curl-noise turbulence (lateral swirl). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Turbulence", meta = (ClampMin = "0", UIMax = "1"))
	float TurbulenceStrength = 0.35f;

	/** Size of the turbulence eddies, cm. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Turbulence", meta = (ClampMin = "10", UIMax = "5000"))
	float TurbulenceWavelength = 900.0f;
};

/**
 * A source of wind: a velocity field with a footprint (a sphere, or an oriented box for Shelter).
 * Consumers blend TOWARD it, never add.
 */
USTRUCT(BlueprintType)
struct DREAMWIND_API FDreamWindSourceDesc
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Source")
	EDreamWindSourceType Type = EDreamWindSourceType::Directional;

	/** World-space centre. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Source")
	FVector Position = FVector::ZeroVector;

	/** Footprint radius, cm. Shelter boxes derive it from BoxExtent (bounding sphere, used for culling). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Source", meta = (ClampMin = "1", EditCondition = "Type != EDreamWindSourceType::Shelter"))
	float Radius = 500.0f;

	/**
	 * Length of the footprint along +Direction as a multiple of Radius: 1 = a sphere, 4 = a jet that
	 * reaches four radii ahead of the centre (and one radius behind it). Directional and Vortex only.
	 * This is what makes a fan blow "far": Strength is the speed inside, Radius x Stretch the reach.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Source", meta = (ClampMin = "1", UIMax = "10", EditCondition = "Type == EDreamWindSourceType::Directional || Type == EDreamWindSourceType::Vortex"))
	float Stretch = 1.0f;

	/** Half extents of a Shelter box, cm (local X/Y/Z before BoxRotation). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Source", meta = (EditCondition = "Type == EDreamWindSourceType::Shelter"))
	FVector BoxExtent = FVector(500.0, 500.0, 300.0);

	/** Orientation of a Shelter box. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Source", meta = (EditCondition = "Type == EDreamWindSourceType::Shelter"))
	FRotator BoxRotation = FRotator::ZeroRotator;

	/** Push direction (Directional), swirl axis (Vortex) or zone wind direction (Shelter). Ignored by Radial. Normalised on use. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Source")
	FVector Direction = FVector(1.0, 0.0, 0.0);

	/** Velocity the source wants at full weight, cm/s. A Shelter with 0 is calm air. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Source")
	float Strength = 1500.0f;

	/** How fast the dynamic volume adopts the source's velocity, 1/s. High = instantaneous. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Source", meta = (ClampMin = "0", UIMax = "60"))
	float InjectRate = 20.0f;

	/** Fraction of the radius (or box extent) that receives full weight before the smooth falloff starts. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Source", meta = (ClampMin = "0", ClampMax = "1"))
	float InnerRadiusFraction = 0.2f;

	/** Radius of the sphere (about Position) that contains the footprint: the box's bounding sphere, a stretched jet's tip, or Radius. */
	float GetBoundingRadius() const
	{
		if (Type == EDreamWindSourceType::Shelter)
		{
			return static_cast<float>(BoxExtent.GetAbs().Size());
		}
		return (Type == EDreamWindSourceType::Radial) ? Radius : Radius * FMath::Max(Stretch, 1.0f);
	}
};

/** Result of a wind query. */
USTRUCT(BlueprintType)
struct DREAMWIND_API FDreamWindSampleResult
{
	GENERATED_BODY()

	/** Wind velocity, cm/s, world space. */
	UPROPERTY(BlueprintReadOnly, Category = "Wind")
	FVector Velocity = FVector::ZeroVector;

	/** Speed multiplier applied by the gusts (1 = nominal). */
	UPROPERTY(BlueprintReadOnly, Category = "Wind")
	float Gust = 1.0f;

	/** |Velocity|, cm/s. */
	UPROPERTY(BlueprintReadOnly, Category = "Wind")
	float Speed = 0.0f;

	/** Velocity / Speed, or the global direction when the wind is calm. */
	UPROPERTY(BlueprintReadOnly, Category = "Wind")
	FVector Direction = FVector(1.0, 0.0, 0.0);
};

/** Handle to a live impulse, for cancelling it early. */
USTRUCT(BlueprintType)
struct DREAMWIND_API FDreamWindImpulseHandle
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Wind")
	int32 Id = 0;

	bool IsValid() const { return Id > 0; }
};
