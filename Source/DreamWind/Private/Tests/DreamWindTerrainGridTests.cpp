// Copyright DreamMoon. All Rights Reserved.

// The runtime terrain wind map's CPU grid: a wall across the wind shelters what stands behind it, up to the lee's wedge,
// and not what stands before it or above the wedge (within 5 %: the wall also nudges the blurred terrain's speed-up); cells
// not swept yet leave the sample to whatever stands in.

#include "CoreMinimal.h"
#include "DreamWindHlslShim.h"
#include "DreamWindSettings.h"
#include "DreamWindSubsystem.h"
#include "DreamWindTerrainGrid.h"

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace DreamWindTerrainGridTests
{
	constexpr EAutomationTestFlags Flags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter;

	/** The wind along +x at 1000 cm/s, sampled through the grid at a point. */
	float SpeedAt(const FDreamWindTerrainGridData& Grid, const FVector& P)
	{
		using namespace DreamWind::Hlsl;
		FDreamWindSample S;
		S.Velocity = float3(1000.0f, 0.0f, 0.0f);
		S.Gust = 1.0f;
		const FDreamWindSample Out = Grid.Apply(S, float3(float(P.X), float(P.Y), float(P.Z)));
		return length(Out.Velocity);
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDreamWindTerrainGridShadowTest, "DreamWind.Terrain.GridShadow", DreamWindTerrainGridTests::Flags)
bool FDreamWindTerrainGridShadowTest::RunTest(const FString& Parameters)
{
	using namespace DreamWindTerrainGridTests;
	const UDreamWindSettings* Settings = UDreamWindSettings::Get();
	TSharedRef<FDreamWindTerrainGrid, ESPMode::ThreadSafe> Builder = MakeShared<FDreamWindTerrainGrid, ESPMode::ThreadSafe>();
	const int32 N = Builder->GetSize();
	const float Cell = Builder->GetCellSize();
	// The window centred on the origin; flat ground at 0, a 3 m wall one cell thick across the wind at cell column N / 2.
	const FIntPoint Min(-N / 2, -N / 2);
	const int32 WallColumn = N / 2;
	TArray<float> Heights;
	Heights.Init(0.0f, N * N);
	for (int32 Y = 0; Y < N; ++Y)
	{
		Heights[Y * N + WallColumn] = 300.0f;
	}
	Builder->SetHeightsForTest(Min, Heights, FVector(1.0, 0.0, 0.0));
	const TSharedPtr<const FDreamWindTerrainGridData, ESPMode::ThreadSafe> Grid = Builder->GetSnapshot();
	if (!TestTrue(TEXT("a snapshot"), Grid.IsValid() && Grid->IsValid()))
	{
		return false;
	}
	const double WallX = (Min.X + WallColumn + 0.5) * Cell;
	const float Open = SpeedAt(*Grid, FVector(WallX - 2000.0, 0.0, 100.0));
	const float Behind = SpeedAt(*Grid, FVector(WallX + 1.5 * Cell, 0.0, 100.0));
	const float AboveWedge = SpeedAt(*Grid, FVector(WallX + 1.5 * Cell, 0.0, 600.0));
	// The wedge: 3 m high, falling Slope cm per cm downwind, so it ends 300 / Slope behind the wall.
	const double Reach = 300.0 / FMath::Max(Settings->OcclusionSlope, 0.01f);
	const float FarBehind = SpeedAt(*Grid, FVector(WallX + Reach + 3.0 * Cell, 0.0, 100.0));
	AddInfo(FString::Printf(TEXT("upwind %.0f, just behind at 1 m %.0f, just behind at 6 m %.0f, past the lee %.0f (cm/s)"), Open, Behind, AboveWedge, FarBehind));
	TestTrue(TEXT("upwind of the wall the wind is whole"), FMath::IsNearlyEqual(Open, 1000.0f, 50.0f));
	TestTrue(TEXT("just behind it, low down, the wind is sheltered by the shadow's strength"), Behind < 1000.0f * (1.0f - 0.8f * Settings->OcclusionStrength));
	TestTrue(TEXT("above the lee's wedge the wind is whole"), FMath::IsNearlyEqual(AboveWedge, 1000.0f, 50.0f));
	TestTrue(TEXT("past the wedge's reach the wind is whole again"), FMath::IsNearlyEqual(FarBehind, 1000.0f, 50.0f));
	TestEqual(TEXT("the wall's top is the cell's height"), Grid->HeightAt(WallX, 0.0), 300.0f);

	// The wind turns round: the lee moves to the other side.
	Builder->SetHeightsForTest(Min, Heights, FVector(-1.0, 0.0, 0.0));
	const TSharedPtr<const FDreamWindTerrainGridData, ESPMode::ThreadSafe> Turned = Builder->GetSnapshot();
	TestTrue(TEXT("with the wind from the other side, the lee is on the other side"),
		SpeedAt(*Turned, FVector(WallX - 1.5 * Cell, 0.0, 100.0)) < 1000.0f * (1.0f - 0.8f * Settings->OcclusionStrength)
		&& FMath::IsNearlyEqual(SpeedAt(*Turned, FVector(WallX + 1.5 * Cell, 0.0, 100.0)), 1000.0f, 50.0f));

	// Cells not swept yet: the grid does not cover them (the baked map, if any, stands in).
	TArray<float> Unswept;
	Unswept.Init(FDreamWindTerrainGridData::NotSwept, N * N);
	Builder->SetHeightsForTest(Min, Unswept, FVector(1.0, 0.0, 0.0));
	TestEqual(TEXT("an unswept grid covers nothing"), Builder->GetSnapshot()->Weight(0.0, 0.0), 0.0f);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
