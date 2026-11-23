// Copyright DreamMoon. All Rights Reserved.

#include "DreamTerrainFog.h"

#include "Components/LocalFogVolumeComponent.h"
#include "DreamWeatherSubsystem.h"
#include "DreamWindSubsystem.h"
#include "DreamWorldCelestial.h"
#include "DreamWorldSubsystem.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "HAL/IConsoleManager.h"
#include "Math/RandomStream.h"

// ---- The model --------------------------------------------------------------------------------------------------

namespace DreamWeatherFX::TerrainFog
{
	namespace
	{
		/** How far the air is above its dew point, degrees (some 2 degrees a tenth of humidity short of saturation). */
		float DewGap(float Humidity)
		{
			return (1.0f - FMath::Clamp(Humidity, 0.0f, 1.0f)) * 20.0f;
		}

		/** Holds enough water for fog and nothing washes it out. */
		float Moist(const FDreamTerrainFogAir& Air, const FDreamTerrainFogRules& Rules)
		{
			return FMath::SmoothStep(Rules.DriestCold - 3.0f, Rules.DriestCold + 3.0f, Air.Temperature)
				* (1.0f - FMath::SmoothStep(0.1f, 0.4f, Air.Precipitation));
		}

		/** The sun's warmth on the fog: 1 at 15 degrees in a clear sky, less under cloud, none before it is up. */
		float SunHeat(const FDreamTerrainFogAir& Air)
		{
			if (Air.SunElevation <= 0.0f)
			{
				return 0.0f;
			}
			const float Sin = FMath::Sin(FMath::DegreesToRadians(Air.SunElevation));
			return FMath::Min(Sin / FMath::Sin(FMath::DegreesToRadians(15.0f)), 3.0f) * (1.0f - 0.6f * FMath::Clamp(Air.Cloud, 0.0f, 1.0f));
		}
	}

	float RadiationPotential(const FDreamTerrainFogAir& Air, const FDreamTerrainFogRules& Rules)
	{
		// A clear sky lets the ground radiate its warmth away; calm air keeps the chilled layer at the ground; once it has
		// cooled to the dew point it fogs. Before the sun is up only.
		const float Clear = 1.0f - FMath::SmoothStep(0.4f, 0.9f, Air.Cloud);
		const float Calm = 1.0f - FMath::SmoothStep(0.5f * Rules.CalmWind, Rules.CalmWind, Air.WindSpeed);
		const float Dark = 1.0f - FMath::SmoothStep(-2.0f, 4.0f, Air.SunElevation);
		const float Reach = FMath::SmoothStep(0.0f, 2.0f, Rules.SkyCooling * Clear * Calm - DewGap(Air.Humidity));
		return Reach * Dark * Moist(Air, Rules);
	}

	float AdvectionPotential(const FDreamTerrainFogAir& Air, const FDreamTerrainFogRules& Rules)
	{
		if (!Rules.bAdvection)
		{
			return 0.0f;
		}
		// Moist air warmer than the surface it is carried over cools to its dew point there; it takes a wind to bring it,
		// and a strong one lifts it off the surface as low cloud.
		const float Low = FMath::Max(float(Rules.AdvectionWind.X), 0.1f), High = FMath::Max(float(Rules.AdvectionWind.Y), Low + 0.1f);
		const float Carry = FMath::SmoothStep(0.5f * Low, Low, Air.WindSpeed) * (1.0f - FMath::SmoothStep(0.5f * (Low + High), High, Air.WindSpeed));
		const float Reach = FMath::SmoothStep(0.0f, 2.0f, Air.Temperature - Rules.ColdSurfaceTemperature - DewGap(Air.Humidity));
		return Reach * Carry * Moist(Air, Rules);
	}

	FDreamTerrainFogState Advance(const FDreamTerrainFogState& State, const FDreamTerrainFogAir& Air, const FDreamTerrainFogRules& Rules, float Hours)
	{
		FDreamTerrainFogState Next = State;
		if (Hours <= 0.0f)
		{
			return Next;
		}
		const float Sun = SunHeat(Air);
		const float Stir = FMath::SmoothStep(0.5f * Rules.CalmWind, 1.5f * Rules.CalmWind, Air.WindSpeed);

		// Radiation fog: fills toward what the night makes of the air over some hours; the sun (a full fog gone within
		// ClearHours at 15 degrees), a wind or a cloudy night take it away.
		const float Radiation = RadiationPotential(Air, Rules);
		if (Radiation >= State.Radiation && Sun <= 0.0f)
		{
			Next.Radiation = Radiation + (State.Radiation - Radiation) * FMath::Exp(-Hours / FMath::Max(Rules.BuildHours, 0.1f));
		}
		else if (Radiation < State.Radiation)
		{
			const float Rate = (3.0f * Sun + 2.0f * Stir + 0.3f) / FMath::Max(Rules.ClearHours, 0.1f);
			Next.Radiation = Radiation + (State.Radiation - Radiation) * FMath::Exp(-Hours * Rate);
		}

		// Advection fog: comes in and goes within the hour as the wind and the air change; the sun thins it a little.
		const float Advection = AdvectionPotential(Air, Rules) * (1.0f - 0.3f * FMath::Min(Sun, 1.0f));
		Next.Advection = Advection + (State.Advection - Advection) * FMath::Exp(-Hours / 0.75f);

		Next.Radiation = FMath::Clamp(Next.Radiation, 0.0f, 1.0f);
		Next.Advection = FMath::Clamp(Next.Advection, 0.0f, 1.0f);
		return Next;
	}
}

// ---- The actor --------------------------------------------------------------------------------------------------

namespace DreamWeatherFX::TerrainFogActor
{
	constexpr float NoGround = TNumericLimits<float>::Max();
	constexpr float FadeSeconds = 1.5f;
}

ADreamTerrainFog::ADreamTerrainFog()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = true;
	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
}

float ADreamTerrainFog::CellSize() const
{
	return float(2.0 * FMath::Max(Extent.X, Extent.Y)) / float(FMath::Clamp(Cells, 4, 128));
}

FVector ADreamTerrainFog::CellCenter(int32 Cell) const
{
	const int32 N = FMath::Clamp(Cells, 4, 128);
	const FVector Center = GetActorLocation();
	const double StepX = 2.0 * Extent.X / N, StepY = 2.0 * Extent.Y / N;
	const int32 X = Cell % N, Y = Cell / N;
	const float H = Heights.IsValidIndex(Cell) && Heights[Cell] != DreamWeatherFX::TerrainFogActor::NoGround ? Heights[Cell] : float(Center.Z - Extent.Z);
	return FVector(Center.X - Extent.X + (X + 0.5) * StepX, Center.Y - Extent.Y + (Y + 0.5) * StepY, H);
}

void ADreamTerrainFog::ReadTerrain()
{
	UWorld* World = GetWorld();
	const int32 N = FMath::Clamp(Cells, 4, 128);
	Heights.Init(DreamWeatherFX::TerrainFogActor::NoGround, N * N);
	bHeightsGiven = false;
	if (World)
	{
		FCollisionQueryParams Params(TEXT("DreamTerrainFog"), true, this);
		const FVector Center = GetActorLocation();
		for (int32 Cell = 0; Cell < N * N; ++Cell)
		{
			const FVector P = CellCenter(Cell);
			FHitResult Hit;
			if (World->LineTraceSingleByChannel(Hit, FVector(P.X, P.Y, Center.Z + Extent.Z), FVector(P.X, P.Y, Center.Z - Extent.Z), ECC_WorldStatic, Params))
			{
				Heights[Cell] = float(Hit.ImpactPoint.Z);
			}
		}
	}
	SetTerrainHeights(TArray<float>(Heights));
	bHeightsGiven = false;
}

void ADreamTerrainFog::SetTerrainHeights(const TArray<float>& InHeights)
{
	const int32 N = FMath::Clamp(Cells, 4, 128);
	Heights = InHeights;
	Heights.SetNum(N * N);
	bHeightsGiven = true;
	Lowest = DreamWeatherFX::TerrainFogActor::NoGround;
	for (float& H : Heights)
	{
		if (!FMath::IsFinite(H))
		{
			H = DreamWeatherFX::TerrainFogActor::NoGround;
		}
		Lowest = FMath::Min(Lowest, H);
	}
	FindHollows();
	// The cells in an order of their own (the same for the same box where it stands), so the patches spread over what
	// the fog covers and keep their cells as it grows.
	FRandomStream Random(int32(GetTypeHash(GetActorLocation())));
	Order.SetNum(N * N);
	for (int32 i = 0; i < Order.Num(); ++i)
	{
		Order[i] = i;
	}
	for (int32 i = Order.Num() - 1; i > 0; --i)
	{
		Order.Swap(i, Random.RandRange(0, i));
	}
	Apply(0.0f);
}

void ADreamTerrainFog::FindHollows()
{
	using DreamWeatherFX::TerrainFogActor::NoGround;
	const int32 N = FMath::Clamp(Cells, 4, 128);
	Hollows.Init(0.0f, N * N);
	if (Heights.Num() != N * N || Lowest == NoGround)
	{
		return;
	}
	// Closed basins: filled from the box's rim inward, lowest first, each cell to the height the water would stand at
	// before it spilled out over the rim (a priority flood). Cells without ground drain like the rim.
	TArray<float> Filled;
	Filled.Init(NoGround, N * N);
	struct FOpen { float Level; int32 Cell; };
	TArray<FOpen> Open;
	auto Less = [](const FOpen& A, const FOpen& B) { return A.Level < B.Level; };
	for (int32 Cell = 0; Cell < N * N; ++Cell)
	{
		const int32 X = Cell % N, Y = Cell / N;
		if (X == 0 || Y == 0 || X == N - 1 || Y == N - 1 || Heights[Cell] == NoGround)
		{
			Filled[Cell] = Heights[Cell] == NoGround ? Lowest : Heights[Cell];
			Open.HeapPush({ Filled[Cell], Cell }, Less);
		}
	}
	while (Open.Num() > 0)
	{
		FOpen Here;
		Open.HeapPop(Here, Less);
		const int32 X = Here.Cell % N, Y = Here.Cell / N;
		const int32 Next[4][2] = { { X - 1, Y }, { X + 1, Y }, { X, Y - 1 }, { X, Y + 1 } };
		for (const int32 (&P)[2] : Next)
		{
			if (P[0] < 0 || P[1] < 0 || P[0] >= N || P[1] >= N)
			{
				continue;
			}
			const int32 Cell = P[1] * N + P[0];
			if (Filled[Cell] == NoGround)
			{
				Filled[Cell] = FMath::Max(Heights[Cell], Here.Level);
				Open.HeapPush({ Filled[Cell], Cell }, Less);
			}
		}
	}
	// Valleys, open at an end: how far below the mean of the land round it a cell lies.
	const float Reach = HollowRadius > 0.0f ? HollowRadius : 0.25f * float(2.0 * FMath::Max(Extent.X, Extent.Y));
	const int32 K = FMath::Clamp(FMath::RoundToInt(Reach / CellSize()), 1, N);
	for (int32 Cell = 0; Cell < N * N; ++Cell)
	{
		if (Heights[Cell] == NoGround)
		{
			continue;
		}
		const int32 X = Cell % N, Y = Cell / N;
		double Sum = 0.0;
		int32 Count = 0;
		for (int32 V = FMath::Max(Y - K, 0); V <= FMath::Min(Y + K, N - 1); ++V)
		{
			for (int32 U = FMath::Max(X - K, 0); U <= FMath::Min(X + K, N - 1); ++U)
			{
				if (Heights[V * N + U] != NoGround && FMath::Square(U - X) + FMath::Square(V - Y) <= K * K)
				{
					Sum += Heights[V * N + U];
					++Count;
				}
			}
		}
		const float Mean = Count > 0 ? float(Sum / Count) : Heights[Cell];
		Hollows[Cell] = FMath::Max3(Filled[Cell] - Heights[Cell], Mean - Heights[Cell], 0.0f);
	}
}

float ADreamTerrainFog::DepthInCell(int32 Cell) const
{
	if (!Heights.IsValidIndex(Cell) || !Hollows.IsValidIndex(Cell) || Heights[Cell] == DreamWeatherFX::TerrainFogActor::NoGround || Lowest == DreamWeatherFX::TerrainFogActor::NoGround)
	{
		return 0.0f;
	}
	// Radiation fog fills each hollow toward its brim (no deeper than the pool); advection fog lies over all the low
	// ground, as deep over the lowest.
	const FDreamTerrainFogState Now = GetFogState();
	const float Pooled = Now.Radiation * FMath::Min(Hollows[Cell], PoolDepth);
	const float Carried = FMath::Max(Lowest + Now.Advection * AdvectionDepth - Heights[Cell], 0.0f);
	return FMath::Max(Pooled, Carried);
}

float ADreamTerrainFog::SampleCells(TFunctionRef<float(int32)> Value, const FVector& Position, float Fallback) const
{
	using DreamWeatherFX::TerrainFogActor::NoGround;
	const int32 N = FMath::Clamp(Cells, 4, 128);
	const FVector Center = GetActorLocation();
	if (Heights.Num() != N * N)
	{
		return Fallback;
	}
	// Between the four cells round the point (those with ground).
	const double U = (Position.X - (Center.X - Extent.X)) / (2.0 * Extent.X) * N - 0.5;
	const double V = (Position.Y - (Center.Y - Extent.Y)) / (2.0 * Extent.Y) * N - 0.5;
	const int32 X0 = FMath::Clamp(FMath::FloorToInt(U), 0, N - 1), Y0 = FMath::Clamp(FMath::FloorToInt(V), 0, N - 1);
	const int32 X1 = FMath::Min(X0 + 1, N - 1), Y1 = FMath::Min(Y0 + 1, N - 1);
	const float A = float(FMath::Clamp(U - X0, 0.0, 1.0)), B = float(FMath::Clamp(V - Y0, 0.0, 1.0));
	const int32 Corner[4] = { Y0 * N + X0, Y0 * N + X1, Y1 * N + X0, Y1 * N + X1 };
	const float Weight[4] = { (1 - A) * (1 - B), A * (1 - B), (1 - A) * B, A * B };
	float Sum = 0.0f, Total = 0.0f;
	for (int32 i = 0; i < 4; ++i)
	{
		if (Heights[Corner[i]] != NoGround)
		{
			Sum += Value(Corner[i]) * Weight[i];
			Total += Weight[i];
		}
	}
	return Total > 1.0e-4f ? Sum / Total : Fallback;
}

float ADreamTerrainFog::GetHollowAt(FVector Position) const
{
	return SampleCells([this](int32 Cell) { return Hollows.IsValidIndex(Cell) ? Hollows[Cell] : 0.0f; }, Position, 0.0f);
}

float ADreamTerrainFog::GetGroundHeightAt(FVector Position) const
{
	const float Floor = float(GetActorLocation().Z - Extent.Z);
	return SampleCells([this](int32 Cell) { return Heights[Cell]; }, Position, Floor);
}

float ADreamTerrainFog::GetFogDepthAt(FVector Position) const
{
	const FVector Center = GetActorLocation();
	if (FMath::Abs(Position.X - Center.X) > Extent.X || FMath::Abs(Position.Y - Center.Y) > Extent.Y || Lowest == DreamWeatherFX::TerrainFogActor::NoGround)
	{
		return 0.0f;
	}
	return SampleCells([this](int32 Cell) { return DepthInCell(Cell); }, Position, 0.0f) + GetFogState().Radiation * MistThickness;
}

bool ADreamTerrainFog::IsInFog(FVector Position) const
{
	const float Depth = GetFogDepthAt(Position);
	return Depth > 1.0f && Position.Z < GetGroundHeightAt(Position) + Depth;
}

FDreamTerrainFogAir ADreamTerrainFog::ReadAir(double WorldSeconds, float TemperatureShift) const
{
	FDreamTerrainFogAir Out;
	const UWorld* World = GetWorld();
	if (!World)
	{
		return Out;
	}
	const FVector Center = GetActorLocation();
	const FVector Low(Center.X, Center.Y, Lowest != DreamWeatherFX::TerrainFogActor::NoGround ? Lowest : Center.Z);
	Out.Humidity = Humidity;
	if (const UDreamWeatherSubsystem* Weather = World->GetSubsystem<UDreamWeatherSubsystem>(); Weather && Weather->IsActive())
	{
		const FDreamWeatherSample Sample = Weather->SampleWeather(Low);
		Out.Temperature = Sample.Temperature + TemperatureShift;
		Out.Cloud = Sample.CloudCoverage;
		Out.Precipitation = Sample.Precipitation;
		// A wet ground and a foggy weather make the air moister.
		Out.Humidity = FMath::Clamp(Humidity + WetGroundHumidity * Sample.Wetness + 0.25f * Sample.Fog, 0.0f, 1.0f);
	}
	if (const UDreamWindSubsystem* Wind = World->GetSubsystem<UDreamWindSubsystem>())
	{
		// The wind a couple of metres over the lowest ground: what stirs the pooled air.
		Out.WindSpeed = float(Wind->SampleWindVelocity(Low + FVector(0, 0, 200)).Size2D()) / 100.0f;
	}
	if (const UDreamWorldSubsystem* Clock = World->GetSubsystem<UDreamWorldSubsystem>())
	{
		Out.SunElevation = float(DreamWorld::Celestial::Evaluate(Clock->GetCelestialSettings(), WorldSeconds).SunElevation);
	}
	return Out;
}

void ADreamTerrainFog::Settle()
{
	const UWorld* World = GetWorld();
	const UDreamWorldSubsystem* Clock = World ? World->GetSubsystem<UDreamWorldSubsystem>() : nullptr;
	if (!Clock)
	{
		return;
	}
	const double Now = Clock->GetWorldSeconds();
	LastWorldSeconds = Now;
	LastJump = Clock->GetTimeJumpSerial();
	// The last twelve hours in ten-minute steps: the weather as it is now, the temperature of each hour.
	const UDreamWeatherSubsystem* Weather = World->GetSubsystem<UDreamWeatherSubsystem>();
	const FVector Low(GetActorLocation().X, GetActorLocation().Y, Lowest != DreamWeatherFX::TerrainFogActor::NoGround ? Lowest : GetActorLocation().Z);
	const FDreamTerrainFogAir NowAir = ReadAir(Now, 0.0f);
	const bool bTemperature = Weather && Weather->IsActive();
	const float BaseNow = bTemperature ? Weather->GetBaseTemperatureAt(Low, 0.0f, NowAir.Cloud, Now) : 0.0f;
	constexpr int32 Steps = 72;
	State = FDreamTerrainFogState();
	for (int32 i = Steps; i >= 1; --i)
	{
		const double Then = Now - i * 600.0;
		FDreamTerrainFogAir Past = NowAir;
		Past.SunElevation = float(DreamWorld::Celestial::Evaluate(Clock->GetCelestialSettings(), Then).SunElevation);
		if (bTemperature)
		{
			Past.Temperature = NowAir.Temperature + Weather->GetBaseTemperatureAt(Low, 0.0f, NowAir.Cloud, Then) - BaseNow;
		}
		State = DreamWeatherFX::TerrainFog::Advance(State, Past, Rules, 1.0f / 6.0f);
	}
	Air = NowAir;
	Apply(0.0f);
}

void ADreamTerrainFog::Refresh()
{
	const UWorld* World = GetWorld();
	const UDreamWorldSubsystem* Clock = World ? World->GetSubsystem<UDreamWorldSubsystem>() : nullptr;
	if (!Clock)
	{
		return;
	}
	if (LastWorldSeconds < 0.0 || Clock->GetTimeJumpSerial() != LastJump)
	{
		Settle();
		return;
	}
	const double Now = Clock->GetWorldSeconds();
	const float Hours = float(FMath::Clamp((Now - LastWorldSeconds) / 3600.0, 0.0, 1.0));
	LastWorldSeconds = Now;
	Air = ReadAir(Now, 0.0f);
	State = DreamWeatherFX::TerrainFog::Advance(State, Air, Rules, Hours);
}

int32 ADreamTerrainFog::GetVisiblePatchCount() const
{
	int32 Count = 0;
	for (const ULocalFogVolumeComponent* Volume : Volumes)
	{
		Count += Volume && Volume->IsVisible() ? 1 : 0;
	}
	return Count;
}

FVector ADreamTerrainFog::GetPatchLocation(int32 Index) const
{
	return Volumes.IsValidIndex(Index) && Volumes[Index] ? Volumes[Index]->GetComponentLocation() : FVector::ZeroVector;
}

void ADreamTerrainFog::BuildVolumes()
{
	UWorld* World = GetWorld();
	const int32 Wanted = FMath::Clamp(Patches, 1, 64) + FMath::Clamp(MistPatches, 0, 16);
	if (!World || World->IsNetMode(NM_DedicatedServer) || Volumes.Num() == Wanted)
	{
		return;
	}
	for (ULocalFogVolumeComponent* Volume : Volumes)
	{
		if (Volume)
		{
			Volume->DestroyComponent();
		}
	}
	Volumes.Reset();
	for (int32 i = 0; i < Wanted; ++i)
	{
		ULocalFogVolumeComponent* Volume = NewObject<ULocalFogVolumeComponent>(this, NAME_None, RF_Transient | RF_TextExportTransient | RF_DuplicateTransient);
		Volume->SetupAttachment(RootComponent);
		Volume->SetUsingAbsoluteLocation(true);
		Volume->SetUsingAbsoluteRotation(true);
		Volume->SetUsingAbsoluteScale(true);
		Volume->SetVisibility(false);
		Volume->RegisterComponent();
		Volumes.Add(Volume);
	}
	Slots.Reset();
	Slots.SetNum(FMath::Clamp(Patches, 1, 64));
}

namespace DreamWeatherFX::TerrainFogActor
{
	/**
	 * A layer of fog Depth deep on the ground at Ground, Radius across. A local fog volume is a sphere (its scale is
	 * made uniform), so the layer is its height fog: the sphere sits on the ground and the fog thins from there to a
	 * seventh at Depth up, and as much of the sphere's radial fog (densest at its heart, none at its rim) to soften its sides.
	 */
	void Layer(ULocalFogVolumeComponent* Volume, const FVector& Ground, float Radius, float Depth, float Density)
	{
		const float Scale = Radius / ULocalFogVolumeComponent::GetBaseVolumeSize();
		const float Falloff = 2.0f * Radius / FMath::Max(Depth, 1.0f);
		Volume->SetWorldLocation(Ground);
		Volume->SetWorldScale3D(FVector(Scale));
		Volume->SetRadialFogExtinction(Density * 0.5f);
		Volume->SetHeightFogExtinction(Density * 0.5f);
		// The component's falloff is a hundred times the unit sphere's.
		Volume->SetHeightFogFalloff(FMath::Clamp(Falloff * 100.0f, 100.0f, 50000.0f));
		Volume->SetHeightFogOffset(0.0f);
		Volume->SetVisibility(true);
	}
}

void ADreamTerrainFog::Apply(float RealSeconds)
{
	using DreamWeatherFX::TerrainFogActor::Layer;
	const int32 Pool = FMath::Min(Slots.Num(), Volumes.Num());
	const float Size = CellSize();
	const float MinDepth = FMath::Max(10.0f, 0.05f * (Rules.bAdvection ? FMath::Min(PoolDepth, AdvectionDepth) : PoolDepth));
	const bool bSnap = RealSeconds <= 0.0f;

	// The cells under the pooled fog, in the box's own order: as many as there are patches.
	TArray<int32, TInlineAllocator<64>> Targets;
	for (int32 Cell : Order)
	{
		if (Targets.Num() >= Pool)
		{
			break;
		}
		if (DepthInCell(Cell) > MinDepth)
		{
			Targets.Add(Cell);
		}
	}
	// A patch keeps its cell while that stays under the fog; the others take the new cells, fading out and in.
	TArray<bool, TInlineAllocator<64>> Taken;
	Taken.Init(false, Targets.Num());
	for (FSlot& Slot : Slots)
	{
		const int32 Kept = Targets.Find(Slot.Cell);
		Slot.Target = Kept != INDEX_NONE && !Taken[Kept] ? Slot.Cell : INDEX_NONE;
		if (Kept != INDEX_NONE && Slot.Target != INDEX_NONE)
		{
			Taken[Kept] = true;
		}
	}
	int32 Next = 0;
	for (FSlot& Slot : Slots)
	{
		if (Slot.Target != INDEX_NONE)
		{
			continue;
		}
		while (Next < Targets.Num() && Taken[Next])
		{
			++Next;
		}
		if (Next < Targets.Num())
		{
			Slot.Target = Targets[Next];
			Taken[Next] = true;
		}
	}
	const float FadeStep = bSnap ? 1.0f : RealSeconds / DreamWeatherFX::TerrainFogActor::FadeSeconds;
	for (int32 i = 0; i < Pool; ++i)
	{
		FSlot& Slot = Slots[i];
		if (Slot.Cell != Slot.Target)
		{
			Slot.Fade = bSnap ? 0.0f : Slot.Fade - FadeStep;
			if (Slot.Fade <= 0.0f)
			{
				Slot.Fade = 0.0f;
				Slot.Cell = Slot.Target;
			}
		}
		if (Slot.Cell == Slot.Target && Slot.Cell != INDEX_NONE)
		{
			Slot.Fade = FMath::Min(Slot.Fade + FadeStep, 1.0f);
		}
		ULocalFogVolumeComponent* Volume = Volumes[i];
		if (!Volume)
		{
			continue;
		}
		const float Depth = Slot.Cell != INDEX_NONE ? DepthInCell(Slot.Cell) : 0.0f;
		const float Here = Density * Slot.Fade * FMath::SmoothStep(MinDepth, MinDepth * 3.0f, Depth);
		if (Here <= 1.0e-4f)
		{
			Volume->SetVisibility(false);
			continue;
		}
		Layer(Volume, CellCenter(Slot.Cell), Size * PatchCells, Depth, Here);
	}

	// The mist: wide, low lenses spread over the box, as thick as the radiation fog.
	const int32 Mist = Volumes.Num() - Pool;
	const int32 Side = FMath::Max(1, FMath::CeilToInt(FMath::Sqrt(float(Mist))));
	const FVector Center = GetActorLocation();
	const float MistNow = GetFogState().Radiation;
	for (int32 m = 0; m < Mist; ++m)
	{
		ULocalFogVolumeComponent* Volume = Volumes[Pool + m];
		if (!Volume)
		{
			continue;
		}
		const float Here = MistDensity * MistNow;
		if (Here <= 1.0e-4f || MistThickness <= 1.0f || Lowest == DreamWeatherFX::TerrainFogActor::NoGround)
		{
			Volume->SetVisibility(false);
			continue;
		}
		const int32 X = m % Side, Y = m / Side;
		const FVector P(Center.X - Extent.X + (X + 0.5) * 2.0 * Extent.X / Side, Center.Y - Extent.Y + (Y + 0.5) * 2.0 * Extent.Y / Side, 0.0);
		const float Radius = float(FMath::Max(Extent.X, Extent.Y)) * 2.0f / Side * 0.8f;
		Layer(Volume, FVector(P.X, P.Y, GetGroundHeightAt(P)), Radius, FMath::Max(MistThickness * MistNow, 20.0f), Here);
	}
}

void ADreamTerrainFog::PostRegisterAllComponents()
{
	Super::PostRegisterAllComponents();
	BuildVolumes();
	if (bHeightsGiven && Heights.Num() == FMath::Square(FMath::Clamp(Cells, 4, 128)))
	{
		SetTerrainHeights(TArray<float>(Heights));
	}
	else
	{
		ReadTerrain();
	}
	Refresh();
	Apply(0.0f);
}

void ADreamTerrainFog::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	// The ground may come in after the actor (streaming): read it again until some is found.
	if (Lowest == DreamWeatherFX::TerrainFogActor::NoGround && !bHeightsGiven)
	{
		ReadTerrain();
	}
	Refresh();
	Apply(DeltaSeconds);
}

#if WITH_EDITOR
void ADreamTerrainFog::PostEditChangeProperty(FPropertyChangedEvent& Event)
{
	Super::PostEditChangeProperty(Event);
	BuildVolumes();
	if (!bHeightsGiven || Heights.Num() != FMath::Square(FMath::Clamp(Cells, 4, 128)))
	{
		ReadTerrain();
	}
	Settle();
	Apply(0.0f);
}
#endif

namespace DreamWeatherFX::TerrainFogConsole
{
	static FAutoConsoleCommandWithWorldAndArgs Command(
		TEXT("DreamWeather.TerrainFog"),
		TEXT("DreamWeather.TerrainFog [radiation [advection] | auto]: list the terrain fogs (the air, the fog now, its patches); ")
		TEXT("hold them at the given radiation / advection fog (0..1), or give them back to the air."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
		{
			if (!World)
			{
				return;
			}
			for (TActorIterator<ADreamTerrainFog> It(World); It; ++It)
			{
				if (Args.Num() >= 1 && Args[0].Equals(TEXT("auto"), ESearchCase::IgnoreCase))
				{
					It->bForce = false;
				}
				else if (Args.Num() >= 1)
				{
					It->bForce = true;
					It->ForcedState.Radiation = FMath::Clamp(FCString::Atof(*Args[0]), 0.0f, 1.0f);
					It->ForcedState.Advection = Args.Num() >= 2 ? FMath::Clamp(FCString::Atof(*Args[1]), 0.0f, 1.0f) : 0.0f;
				}
				const FDreamTerrainFogAir A = It->GetAir();
				const FDreamTerrainFogState S = It->GetFogState();
				UE_LOG(LogTemp, Display, TEXT("%s: %.1f C, humidity %.2f, cloud %.2f, wind %.1f m/s, sun %.1f deg -> radiation %.2f, advection %.2f%s; %d of %d patches"),
					*It->GetActorNameOrLabel(), A.Temperature, A.Humidity, A.Cloud, A.WindSpeed, A.SunElevation, S.Radiation, S.Advection,
					It->bForce ? TEXT(" (held)") : TEXT(""), It->GetVisiblePatchCount(), It->GetPatchCount());
			}
		}));
}
