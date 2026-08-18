// Copyright DreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "Engine/EngineTypes.h"
#include "DreamWindTerrainMap.generated.h"

/** How a terrain map is baked from the level's geometry. Stored on the map for reference. */
USTRUCT(BlueprintType)
struct DREAMWIND_API FDreamWindTerrainBakeSettings
{
	GENERATED_BODY()

	/** Texels per axis. 256 over a 2 km level = 8 m per texel, 1 MB of CPU data. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Bake", meta = (ClampMin = "16", ClampMax = "1024"))
	int32 Resolution = 256;

	/** Height is compared to the terrain blurred over this radius (cm): the scale of a 'ridge' or 'hollow'. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Bake", meta = (ClampMin = "100"))
	float BlurRadius = 4000.0f;

	/** Height difference (cm) above/below the blurred terrain that counts as fully exposed / sheltered. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Bake", meta = (ClampMin = "10"))
	float ExposureScale = 800.0f;

	/** Speed multiplier swing: exposed ridges blow at 1 + gain, sheltered hollows at 1 - gain. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Bake", meta = (ClampMin = "0", ClampMax = "0.9"))
	float ExposureGain = 0.5f;

	/** Steering toward valleys: the blurred slope (rise/run) times this is added to the wind direction. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Bake", meta = (ClampMin = "0", UIMax = "10"))
	float SteerGain = 2.0f;

	/** Upper bound on the steering offset's length; 1 bends the wind by up to ~45 degrees and never flips it. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Bake", meta = (ClampMin = "0", ClampMax = "2"))
	float MaxSteer = 0.8f;

	/** Traces start this far above the bake bounds' top and end this far below its bottom. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Bake", meta = (ClampMin = "0"))
	float TraceMargin = 20000.0f;

	/** Trace channel; landscape and static geometry are WorldStatic by default. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Bake")
	TEnumAsByte<ECollisionChannel> TraceChannel = ECC_WorldStatic;
};

/**
 * A baked, level-sized 2D map that modulates the analytic wind by the terrain: ridges blow harder,
 * hollows are sheltered, and the wind steers toward valleys. Texel = (speed multiplier, steer.x,
 * steer.y, normalised height). Kept as plain floats so the CPU sample reads exactly what the GPU does.
 * Bake it from an ADreamWindGlobal actor (Bake Terrain Map) or UDreamWindEditorLibrary::BakeTerrainMap.
 */
UCLASS(BlueprintType)
class DREAMWIND_API UDreamWindTerrainMap : public UDataAsset
{
	GENERATED_BODY()

public:
	/** World-space XY covered by the map. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Terrain")
	FBox2D Bounds = FBox2D(ForceInit);

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Terrain")
	int32 Width = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Terrain")
	int32 Height = 0;

	/** Lowest / highest terrain height seen while baking, cm. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Terrain")
	FVector2D HeightRange = FVector2D::ZeroVector;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Terrain")
	FDreamWindTerrainBakeSettings BakeSettings;

	/** Row-major, Width*Height texels: (speed multiplier, steer.x, steer.y, height 0..1). */
	UPROPERTY()
	TArray<FVector4f> Texels;

	bool IsValidMap() const { return Width > 0 && Height > 0 && Texels.Num() == Width * Height && Bounds.bIsValid; }

	/** Bilinear fetch at a world XY; clamps to the border. */
	FVector4f Sample(const FVector2D& WorldXY) const;

	/** Texel-space accessor with clamping. */
	FVector4f Texel(int32 X, int32 Y) const
	{
		X = FMath::Clamp(X, 0, Width - 1);
		Y = FMath::Clamp(Y, 0, Height - 1);
		return Texels.IsValidIndex(Y * Width + X) ? Texels[Y * Width + X] : FVector4f(1, 0, 0, 0);
	}
};
