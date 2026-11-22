// Copyright DreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "DreamSurfaceTypes.generated.h"

/** What the weather has left on a surface (DreamSurfaceShared.h's state). */
USTRUCT(BlueprintType)
struct DREAMSURFACE_API FDreamSurfaceState
{
	GENERATED_BODY()

	/** The film of water on it, 0 dry .. 1 soaked. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Surface")
	float Wetness = 0.0f;

	/** Water standing in its hollows, 0 none .. 1 full (the deepest dips fill first; full puddles cover some two fifths of a flat surface). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Surface")
	float Water = 0.0f;

	/** Lying snow, 0 none .. 1 the deepest cover (Project Settings > Dream Surface > Max Snow Depth). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Surface")
	float Snow = 0.0f;

	/** Dust or sand settled on it, 0..1. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Surface")
	float Dust = 0.0f;

	FVector4f ToVector() const { return FVector4f(Wetness, Water, Snow, Dust); }
	static FDreamSurfaceState FromVector(const FVector4f& V)
	{
		FDreamSurfaceState S;
		S.Wetness = V.X;
		S.Water = V.Y;
		S.Snow = V.Z;
		S.Dust = V.W;
		return S;
	}

	bool Equals(const FDreamSurfaceState& Other, float Tolerance = 1.0e-4f) const
	{
		return ToVector().Equals(Other.ToVector(), Tolerance);
	}
};

/**
 * What drives a surface over one step: the weather landing on it (already times the exposure there), the air, the
 * sun. The packed form is DreamSurfaceShared.h's D0 / D1.
 */
struct DREAMSURFACE_API FDreamSurfaceDrive
{
	float Rain = 0.0f;
	float Snow = 0.0f;
	float Temperature = 15.0f;
	float Wind = 0.0f;
	float Sunlight = 0.0f;
	float Dust = 0.0f;

	FVector4f D0() const { return FVector4f(Rain, Snow, Temperature, Wind); }
	FVector4f D1() const { return FVector4f(Sunlight, Dust, 0.0f, 0.0f); }
};

/** How fast the weather changes a surface, game hours (DreamSurfaceShared.h's R0..R2). */
USTRUCT(BlueprintType)
struct DREAMSURFACE_API FDreamSurfaceRates
{
	GENERATED_BODY()

	/** Hours a heavy rain takes to soak a dry surface. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Water", meta = (ClampMin = "0.01", Units = "Hours"))
	float WetHours = 0.25f;

	/** Hours a soaked surface takes to dry in a warm sun (longer in the shade, the cold or still air). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Water", meta = (ClampMin = "0.01", Units = "Hours"))
	float DryHours = 2.0f;

	/** Hours a heavy rain takes to fill the hollows once the surface is soaked. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Water", meta = (ClampMin = "0.01", Units = "Hours"))
	float FillHours = 1.5f;

	/** Hours full puddles take to dry away in a warm sun: slower than the film, so they outlast it. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Water", meta = (ClampMin = "0.01", Units = "Hours"))
	float DrainHours = 10.0f;

	/** Hours a heavy snowfall takes to lay the deepest cover. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Snow", meta = (ClampMin = "0.01", Units = "Hours"))
	float SettleHours = 6.0f;

	/** Hours the deepest cover takes to melt at 10 Celsius in the sun (faster warmer and in the rain). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Snow", meta = (ClampMin = "0.01", Units = "Hours"))
	float MeltHours = 8.0f;

	/** How much the melt wets the surface: the film rises by this much for a full cover melted (snow holds a lot of water). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Snow", meta = (ClampMin = "0", ClampMax = "4"))
	float MeltToWater = 2.0f;

	/** Hours a full dust storm takes to cover a surface. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dust", meta = (ClampMin = "0.01", Units = "Hours"))
	float DustHours = 2.0f;

	/** Hours a heavy rain takes to wash a full cover of dust off. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dust", meta = (ClampMin = "0.01", Units = "Hours"))
	float DustWashHours = 0.5f;

	/** Hours a full cover of dust takes to clear by itself. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dust", meta = (ClampMin = "0.01", Units = "Hours"))
	float DustClearHours = 72.0f;

	FVector4f R0() const { return FVector4f(WetHours, DryHours, FillHours, DrainHours); }
	FVector4f R1() const { return FVector4f(SettleHours, MeltHours, MeltToWater, 0.0f); }
	FVector4f R2() const { return FVector4f(DustHours, DustWashHours, DustClearHours, 0.0f); }
};
