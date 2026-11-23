// Copyright DreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "DreamTerrainFog.generated.h"

class ULocalFogVolumeComponent;

/** The air over a piece of land, as terrain fog reads it. */
USTRUCT(BlueprintType)
struct DREAMWEATHERFX_API FDreamTerrainFogAir
{
	GENERATED_BODY()

	/** Degrees Celsius. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Terrain Fog") float Temperature = 10.0f;
	/** Relative humidity, 0..1. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Terrain Fog") float Humidity = 0.8f;
	/** Cloud cover, 0..1. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Terrain Fog") float Cloud = 0.0f;
	/** Wind near the ground, m/s. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Terrain Fog") float WindSpeed = 0.0f;
	/** The sun's elevation, degrees. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Terrain Fog") float SunElevation = -20.0f;
	/** What falls, 0..1. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Terrain Fog") float Precipitation = 0.0f;
};

/** How much terrain fog there is: each 0..1. */
USTRUCT(BlueprintType)
struct DREAMWEATHERFX_API FDreamTerrainFogState
{
	GENERATED_BODY()

	/** Radiation fog: the cold air of a clear, calm night pooling in the hollows (morning and valley fog). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Terrain Fog") float Radiation = 0.0f;
	/** Advection fog: moist air carried by the wind over a colder surface (a lake, the sea, snow). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Terrain Fog") float Advection = 0.0f;
};

/** When terrain fog forms and how fast it comes and goes. */
USTRUCT(BlueprintType)
struct DREAMWEATHERFX_API FDreamTerrainFogRules
{
	GENERATED_BODY()

	/**
	 * How far a clear, calm night cools the air at the ground below the air above, degrees: fog forms once that reaches
	 * the dew point (the humidity's gap, some 5 degrees at 75 %).
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Terrain Fog", meta = (ClampMin = "0", Units = "C"))
	float SkyCooling = 6.0f;

	/** The wind that stirs the night's cold air up and keeps radiation fog from forming, m/s (half of it already thins it). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Terrain Fog", meta = (ClampMin = "0.1", Units = "MetersPerSecond"))
	float CalmWind = 4.0f;

	/** Below this the air holds too little water for fog, degrees. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Terrain Fog", meta = (Units = "C"))
	float DriestCold = -12.0f;

	/** How long a night takes to fill the hollows, game hours. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Terrain Fog", meta = (ClampMin = "0.1", Units = "Hours"))
	float BuildHours = 3.0f;

	/** How long the sun, 15 degrees up in a clear sky, takes to burn a full fog away, game hours (cloud slows it). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Terrain Fog", meta = (ClampMin = "0.1", Units = "Hours"))
	float ClearHours = 1.0f;

	/** Advection fog at all: the land is beside cold water or snow the wind blows over. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Terrain Fog")
	bool bAdvection = false;

	/** The cold surface's temperature (the water's, the snow's), degrees: warmer moist air over it fogs. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Terrain Fog", meta = (EditCondition = "bAdvection", Units = "C"))
	float ColdSurfaceTemperature = 6.0f;

	/** The wind that carries advection fog in, m/s: full from the first up to halfway, gone by the second (it lifts off as low cloud). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Terrain Fog", meta = (EditCondition = "bAdvection"))
	FVector2D AdvectionWind = FVector2D(1.0, 10.0);
};

namespace DreamWeatherFX::TerrainFog
{
	/** How readily radiation fog forms in this air, 0..1: humid, clear, calm, before the sun is up. */
	DREAMWEATHERFX_API float RadiationPotential(const FDreamTerrainFogAir& Air, const FDreamTerrainFogRules& Rules);

	/** How readily advection fog forms in this air, 0..1 (0 unless the rules allow it). */
	DREAMWEATHERFX_API float AdvectionPotential(const FDreamTerrainFogAir& Air, const FDreamTerrainFogRules& Rules);

	/** The fog Hours on: it builds toward what the air makes over the night, and the sun and the wind take it away. */
	DREAMWEATHERFX_API FDreamTerrainFogState Advance(const FDreamTerrainFogState& State, const FDreamTerrainFogAir& Air, const FDreamTerrainFogRules& Rules, float Hours);
}

/**
 * Fog of the land: morning, valley and advection fog that come by themselves. The actor reads the ground's heights in
 * its box (traces, or heights handed to it) and the air over it (the weather's temperature, cloud and wetness, the wind,
 * the sun): through a clear, calm, humid night the cold air pools in the hollows and fills them with fog, deepest by
 * sunrise, a thin mist lies on the rest, and the sun burns it off within the morning. With advection on, moist air the
 * wind brings over the cold surface lays a broad fog over the low ground at any hour. The fog is drawn with local fog
 * volumes (a few dozen) laid on the lowest ground under it; gameplay can ask how deep the fog is anywhere.
 */
UCLASS(meta = (DisplayName = "Dream Terrain Fog"))
class DREAMWEATHERFX_API ADreamTerrainFog : public AActor
{
	GENERATED_BODY()

public:
	ADreamTerrainFog();

	/** Half the box, cm, round the actor: the ground is looked for within it. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Terrain Fog")
	FVector Extent = FVector(5000.0, 5000.0, 2000.0);

	/** Cells along each side of the box the ground is read in. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Terrain Fog", meta = (ClampMin = "4", ClampMax = "128"))
	int32 Cells = 32;

	/** When and how fast. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Terrain Fog")
	FDreamTerrainFogRules Rules;

	/** The air's humidity here with nothing else, 0..1 (a river valley, a coast: higher). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Terrain Fog", meta = (ClampMin = "0", ClampMax = "1"))
	float Humidity = 0.8f;

	/** How much a wet ground (the weather's wetness) adds to the humidity. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Terrain Fog", meta = (ClampMin = "0", ClampMax = "1"))
	float WetGroundHumidity = 0.15f;

	/**
	 * How deep the cold air pools at most in a full radiation fog, cm: in each hollow up to its brim (a closed basin to
	 * where it would spill over, a valley to the height of the land round it), no deeper than this.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Terrain Fog", meta = (ClampMin = "10", Units = "cm"))
	float PoolDepth = 1500.0f;

	/** How far round a cell the land is looked at to tell a valley, cm (0: a quarter of the box). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Terrain Fog", meta = (ClampMin = "0", Units = "cm"))
	float HollowRadius = 0.0f;

	/** The mist over all the rest in a full radiation fog, cm. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Terrain Fog", meta = (ClampMin = "0", Units = "cm"))
	float MistThickness = 150.0f;

	/** How deep advection fog lies over the lowest ground at full, cm. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Terrain Fog", meta = (ClampMin = "10", Units = "cm"))
	float AdvectionDepth = 2500.0f;

	/** How thick the pooled fog is (a local fog volume's density at its heart). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Terrain Fog", meta = (ClampMin = "0", UIMax = "4"))
	float Density = 1.5f;

	/** How thick the mist is. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Terrain Fog", meta = (ClampMin = "0", UIMax = "2"))
	float MistDensity = 0.35f;

	/** Fog volumes for the pooled fog (laid on the lowest cells under it). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Terrain Fog", meta = (ClampMin = "1", ClampMax = "64"))
	int32 Patches = 28;

	/** Fog volumes for the mist (spread over the box). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Terrain Fog", meta = (ClampMin = "0", ClampMax = "16"))
	int32 MistPatches = 4;

	/** A pooled patch's radius in cells. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Terrain Fog", meta = (ClampMin = "0.5", UIMax = "6"))
	float PatchCells = 2.5f;

	/** Holds the fog at ForcedState instead of the air's (the showcase, cinematics). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Terrain Fog")
	bool bForce = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Terrain Fog", meta = (EditCondition = "bForce"))
	FDreamTerrainFogState ForcedState;

	/** How much fog there is now. */
	UFUNCTION(BlueprintPure, Category = "Dream Weather")
	FDreamTerrainFogState GetFogState() const { return bForce ? ForcedState : State; }

	/** The air over the box now, as the fog reads it. */
	UFUNCTION(BlueprintPure, Category = "Dream Weather")
	FDreamTerrainFogAir GetAir() const { return Air; }

	/** How deep the fog lies over the ground at a point, cm (0 outside the box or where there is none). */
	UFUNCTION(BlueprintPure, Category = "Dream Weather")
	float GetFogDepthAt(FVector Position) const;

	/** A point is in the fog: within the box, under the fog's top over the ground there. */
	UFUNCTION(BlueprintPure, Category = "Dream Weather")
	bool IsInFog(FVector Position) const;

	/** How far a point lies below its surroundings, cm: the hollow the cold air fills there (0 on open or high ground). */
	UFUNCTION(BlueprintPure, Category = "Dream Weather")
	float GetHollowAt(FVector Position) const;

	/** The ground's height at a point, from the cells read (the box's floor where none). */
	UFUNCTION(BlueprintPure, Category = "Dream Weather")
	float GetGroundHeightAt(FVector Position) const;

	/** Reads the ground again with traces down through the box. */
	UFUNCTION(BlueprintCallable, Category = "Dream Weather")
	void ReadTerrain();

	/**
	 * Uses these heights (world Z, cm; Cells x Cells, row by row from the box's -X -Y corner) instead of traces; they are
	 * kept with the level (for a land without collision, or heights from a height map).
	 */
	UFUNCTION(BlueprintCallable, Category = "Dream Weather")
	void SetTerrainHeights(const TArray<float>& Heights);

	/** Reads the air and moves the fog on to the world's time (it does every tick; a jump of the clock settles it). */
	void Refresh();

	/** Settles the fog for the world's time: the last twelve hours of air run through. */
	void Settle();

	/** The fog volumes, and how many draw now. */
	int32 GetPatchCount() const { return Volumes.Num(); }
	int32 GetVisiblePatchCount() const;
	FVector GetPatchLocation(int32 Index) const;

	virtual void PostRegisterAllComponents() override;
	virtual void Tick(float DeltaSeconds) override;
	virtual bool ShouldTickIfViewportsOnly() const override { return true; }
#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& Event) override;
#endif

private:
	struct FSlot
	{
		int32 Cell = INDEX_NONE;
		int32 Target = INDEX_NONE;
		float Fade = 0.0f;
	};

	FDreamTerrainFogAir ReadAir(double WorldSeconds, float TemperatureShift) const;
	float DepthInCell(int32 Cell) const;
	void FindHollows();
	float SampleCells(TFunctionRef<float(int32)> Value, const FVector& Position, float Fallback) const;
	float CellSize() const;
	FVector CellCenter(int32 Cell) const;
	void BuildVolumes();
	void Apply(float RealSeconds);

	FDreamTerrainFogState State;
	FDreamTerrainFogAir Air;
	TArray<int32> Order;
	TArray<float> Hollows;
	float Lowest = 0.0f;
	double LastWorldSeconds = -1.0;
	uint32 LastJump = MAX_uint32;

	/** The ground's heights, world Z (traced, or handed in and then kept with the level: a land without collision). */
	UPROPERTY()
	TArray<float> Heights;

	UPROPERTY()
	bool bHeightsGiven = false;

	UPROPERTY(Transient, DuplicateTransient)
	TArray<TObjectPtr<ULocalFogVolumeComponent>> Volumes;

	TArray<FSlot> Slots;
};
