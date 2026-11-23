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

	/** A glaze of clear ice, 0 none .. 1 a soaked film frozen: black ice on cold ground, rain freezing as it lands. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Surface|Cold")
	float Ice = 0.0f;

	/** Hoarfrost, 0..1: white crystals grown on cold ground under a clear, still sky; gone soon after the sun is on it. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Surface|Cold")
	float Frost = 0.0f;

	/** Rime, 0..1: white ice built up from a freezing fog, faster in a wind. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Surface|Cold")
	float Rime = 0.0f;

	/** How much of the standing water is frozen over, 0..1. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Surface|Cold")
	float FrozenWater = 0.0f;

	/** Hailstones lying on it, 0 none .. 1 a white cover: heaps up fast in a hailstorm, melts within minutes on warm ground. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Surface|Hail")
	float Hail = 0.0f;

	/** Wet, water, snow, dust (DreamSurfaceShared.h's S). */
	FVector4f ToVector() const { return FVector4f(Wetness, Water, Snow, Dust); }
	/** Ice, frost, rime, frozen water (DreamSurfaceShared.h's S2). */
	FVector4f ToIceVector() const { return FVector4f(Ice, Frost, Rime, FrozenWater); }

	/** Hail, -, -, - (DreamSurfaceShared.h's S3). */
	FVector4f ToHailVector() const { return FVector4f(Hail, 0.0f, 0.0f, 0.0f); }
	static FDreamSurfaceState FromVector(const FVector4f& V)
	{
		FDreamSurfaceState S;
		S.Wetness = V.X;
		S.Water = V.Y;
		S.Snow = V.Z;
		S.Dust = V.W;
		return S;
	}
	static FDreamSurfaceState FromVectors(const FVector4f& V, const FVector4f& Cold)
	{
		FDreamSurfaceState S = FromVector(V);
		S.Ice = Cold.X;
		S.Frost = Cold.Y;
		S.Rime = Cold.Z;
		S.FrozenWater = Cold.W;
		return S;
	}

	static FDreamSurfaceState FromVectors(const FVector4f& V, const FVector4f& Cold, const FVector4f& HailVector)
	{
		FDreamSurfaceState S = FromVectors(V, Cold);
		S.Hail = HailVector.X;
		return S;
	}

	bool Equals(const FDreamSurfaceState& Other, float Tolerance = 1.0e-4f) const
	{
		return ToVector().Equals(Other.ToVector(), Tolerance) && ToIceVector().Equals(Other.ToIceVector(), Tolerance)
			&& FMath::IsNearlyEqual(Hail, Other.Hail, Tolerance);
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
	/** The clear part of the sky the surface sees (no cloud, no roof), 0..1: what it loses its warmth to at night. */
	float OpenSky = 0.0f;
	/** Fog there, 0..1 (rime grows from a freezing fog). */
	float Fog = 0.0f;
	/** Hail landing on it, 0..1 (the sleet is in Rain and Snow, half each). */
	float Hail = 0.0f;
	/** How much of the weather's wind the place is sheltered from, and how much harder it blows there than in the open, 0..1 (DSF_LocalD2). */
	float Lee = 0.0f;
	float Exposed = 0.0f;

	FVector4f D0() const { return FVector4f(Rain, Snow, Temperature, Wind); }
	FVector4f D1() const { return FVector4f(Sunlight, Dust, OpenSky, Fog); }
	FVector4f D2() const { return FVector4f(Hail, Lee, Exposed, 0.0f); }
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

	/** Hours a soaked film takes to freeze into a glaze with the ground at -2 Celsius (black ice). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cold", meta = (ClampMin = "0.01", Units = "Hours"))
	float FreezeHours = 0.5f;

	/** Hours the puddles take to freeze over with the ground at -2 Celsius. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cold", meta = (ClampMin = "0.01", Units = "Hours"))
	float PuddleFreezeHours = 3.0f;

	/** Hours a full hoarfrost takes to grow on cold ground under a clear, still night sky (five times as long under cloud). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cold", meta = (ClampMin = "0.01", Units = "Hours"))
	float FrostHours = 3.0f;

	/** Hours a full rime takes to build up in a dense freezing fog and a full wind (some three times as long in still air). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cold", meta = (ClampMin = "0.01", Units = "Hours"))
	float RimeHours = 2.0f;

	/** Hours a full glaze takes to melt with the ground at 5 Celsius in the sun (frost three times, rime twice as fast). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cold", meta = (ClampMin = "0.01", Units = "Hours"))
	float ThawHours = 1.5f;

	/** How much warmer than the air the ground gets in a full sun, Celsius. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cold", meta = (ClampMin = "0", UIMax = "20"))
	float SunWarming = 6.0f;

	/**
	 * How much colder than the air the ground gets under a clear, still night sky, Celsius (it radiates its warmth away):
	 * frost and black ice come with the air this far above freezing.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cold", meta = (ClampMin = "0", UIMax = "10"))
	float SkyCooling = 4.0f;

	/** Hours a full hailstorm takes to whiten a surface with hail. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hail", meta = (ClampMin = "0.005", Units = "Hours"))
	float HailSettleHours = 0.08f;

	/** Hours a full cover of hail takes to melt with the ground at 10 Celsius (faster warmer, in the sun and the rain). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hail", meta = (ClampMin = "0.01", Units = "Hours"))
	float HailMeltHours = 0.5f;

	/** How much the melting hail wets the surface: the film rises by this much for a full cover melted. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hail", meta = (ClampMin = "0", ClampMax = "4"))
	float HailMeltToWater = 0.6f;

	FVector4f R0() const { return FVector4f(WetHours, DryHours, FillHours, DrainHours); }
	FVector4f R1() const { return FVector4f(SettleHours, MeltHours, MeltToWater, 0.0f); }
	FVector4f R2() const { return FVector4f(DustHours, DustWashHours, DustClearHours, 0.0f); }
	FVector4f R3() const { return FVector4f(FreezeHours, PuddleFreezeHours, FrostHours, RimeHours); }
	FVector4f R4() const { return FVector4f(ThawHours, SunWarming, SkyCooling, 0.0f); }
	FVector4f R5() const { return FVector4f(HailSettleHours, HailMeltHours, HailMeltToWater, 0.0f); }
};

/** What a foot (a wheel, a blast) met on a surface, the dominant of it. */
UENUM(BlueprintType)
enum class EDreamSurfaceContactKind : uint8
{
	Dry,
	Wet,
	Puddle,
	Snow,
	Dust,
	/** A glaze of ice or a frozen puddle: slippery. */
	Ice,
	/** Hoarfrost or rime. */
	Frost,
	/** Hailstones lying there. */
	Hail,
};

/** How much grip each thing lying on a surface leaves at full cover, a multiplier on the surface's own (1 as dry). */
USTRUCT(BlueprintType)
struct DREAMSURFACE_API FDreamSurfaceGrip
{
	GENERATED_BODY()

	/** A full glaze of ice (black ice). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grip", meta = (ClampMin = "0", ClampMax = "1"))
	float Ice = 0.15f;

	/** A frozen-over puddle. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grip", meta = (ClampMin = "0", ClampMax = "1"))
	float FrozenPuddle = 0.1f;

	/** A full hoarfrost or rime. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grip", meta = (ClampMin = "0", ClampMax = "1"))
	float Frost = 0.6f;

	/** A full snow cover. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grip", meta = (ClampMin = "0", ClampMax = "1"))
	float Snow = 0.45f;

	/** Standing in a puddle. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grip", meta = (ClampMin = "0", ClampMax = "1"))
	float Puddle = 0.7f;

	/** A soaked film. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grip", meta = (ClampMin = "0", ClampMax = "1"))
	float Wet = 0.85f;

	/** A full cover of hail (the stones roll underfoot). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grip", meta = (ClampMin = "0", ClampMax = "1"))
	float Hail = 0.55f;

	/** DreamSurfaceShared.h's DSF_Grip: (ice, frozen puddle, frost, snow), (puddle, wet, hail, -). */
	FVector4f G0() const { return FVector4f(Ice, FrozenPuddle, Frost, Snow); }
	FVector4f G1() const { return FVector4f(Puddle, Wet, Hail, 1.0f); }
};

/** What a contact with a surface found there: for footstep sounds, splashes, puffs of snow. */
USTRUCT(BlueprintType)
struct DREAMSURFACE_API FDreamSurfaceContact
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Surface")
	FVector Location = FVector::ZeroVector;

	UPROPERTY(BlueprintReadOnly, Category = "Surface")
	FVector Normal = FVector::UpVector;

	UPROPERTY(BlueprintReadOnly, Category = "Surface")
	EDreamSurfaceContactKind Kind = EDreamSurfaceContactKind::Dry;

	/** The state there (wet, water, snow, dust; ice, frost, rime, frozen water). */
	UPROPERTY(BlueprintReadOnly, Category = "Surface")
	FDreamSurfaceState State;

	/** How much of a puddle the point stands in, 0..1 (the hollow pattern and the water, as the materials draw it). */
	UPROPERTY(BlueprintReadOnly, Category = "Surface")
	float Puddle = 0.0f;

	/** Lying snow there, cm. */
	UPROPERTY(BlueprintReadOnly, Category = "Surface")
	float SnowDepth = 0.0f;

	/**
	 * How much grip is left there, a multiplier on the surface's own: 1 as dry .. some 0.1 on black ice or a frozen
	 * puddle. The worst of the ice, frost, snow, standing water and wet film there (Project Settings > Dream Surface > Grip).
	 */
	UPROPERTY(BlueprintReadOnly, Category = "Surface")
	float Grip = 1.0f;
};
