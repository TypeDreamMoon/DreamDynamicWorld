// Copyright DreamMoon. All Rights Reserved.

#include "DreamWeather.h"
#include "DreamWeatherChannels.h"
#include "DreamWeatherModule.h"
#include "DreamWeatherPreset.h"
#include "DreamWeatherSelector.h"
#include "DreamWeatherSubsystem.h"
#include "DreamWeatherStormSpawner.h"
#include "DreamWeatherZone.h"
#include "DreamWorldSubsystem.h"
#include "DreamWorldCelestial.h"

#include "Components/SplineComponent.h"
#include "Engine/World.h"
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "UObject/Package.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace DreamWeather::StormTests
{
	constexpr EAutomationTestFlags Flags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter;

	UDreamWeatherPreset* MakePreset(const TCHAR* Name, std::initializer_list<TPair<FGameplayTag, float>> Channels)
	{
		UDreamWeatherPreset* Preset = NewObject<UDreamWeatherPreset>(GetTransientPackage(), MakeUniqueObjectName(GetTransientPackage(), UDreamWeatherPreset::StaticClass(), Name));
		for (const TPair<FGameplayTag, float>& Channel : Channels)
		{
			Preset->Channels.Add(Channel.Key, Channel.Value);
		}
		return Preset;
	}

	struct FWorld
	{
		UWorld* World = nullptr;
		UDreamWorldSubsystem* Clock = nullptr;
		UDreamWeatherSubsystem* Weather = nullptr;
		double Start = 0.0;

		FWorld()
		{
			World = UWorld::CreateWorld(EWorldType::Game, false);
			Clock = World ? World->GetSubsystem<UDreamWorldSubsystem>() : nullptr;
			Weather = UDreamWeatherSubsystem::Get(World);
			if (!Clock || !Weather)
			{
				return;
			}
			UDreamWeatherSelector_Fixed* Fixed = NewObject<UDreamWeatherSelector_Fixed>();
			Fixed->Preset = MakePreset(TEXT("Clear"), { { Tags::Channel_CloudCoverage, 0.1f } });
			FActorSpawnParameters Spawn;
			Spawn.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
			ADreamWeather* Actor = World->SpawnActor<ADreamWeather>(Spawn);
			Fixed->Rename(nullptr, Actor);
			Actor->Selector = Fixed;
			FDreamWorldClockConfig Config;
			Config.StartTime = FDateTime(2026, 6, 21, 9, 0, 0);
			Clock->ApplyClockConfig(Config, true);
			Clock->ReportView(FVector(0, 0, 170), FRotator::ZeroRotator, true);
			Clock->Tick(0.016f);
			// The clock stands still between the jumps: each check is at exactly the time it names.
			Clock->SetPaused(true);
			Start = Clock->GetWorldSeconds();
		}

		~FWorld()
		{
			if (World)
			{
				World->DestroyWorld(false);
			}
		}

		bool IsValid() const { return Clock && Weather; }

		ADreamWeatherStorm* SpawnStorm(const FVector& Where)
		{
			FActorSpawnParameters Spawn;
			Spawn.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
			ADreamWeatherStorm* Storm = World->SpawnActor<ADreamWeatherStorm>(Where, FRotator::ZeroRotator, Spawn);
			Storm->Zone->Preset = MakePreset(TEXT("Storm"), { { Tags::Channel_CloudCoverage, 1.0f }, { Tags::Channel_CloudDarkness, 1.0f },
				{ Tags::Channel_Precipitation, 1.0f }, { Tags::Channel_Lightning, 1.0f } });
			Storm->Zone->Extent = FVector2D(500000.0, 500000.0);
			Storm->Zone->BlendDistance = 200000.0f;
			return Storm;
		}

		/** Jump the clock to Hours after the start and run a frame. */
		void At(double Hours)
		{
			Clock->SetWorldSeconds(Start + Hours * 3600.0);
			Clock->Tick(0.016f);
		}
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDreamWeatherStormMotionTest, "DreamWeather.Storm.Motion", DreamWeather::StormTests::Flags)
bool FDreamWeatherStormMotionTest::RunTest(const FString& Parameters)
{
	using namespace DreamWeather::StormTests;
	FWorld W;
	if (!TestTrue(TEXT("world"), W.IsValid()))
	{
		return false;
	}
	// 40 km west, drifting east at 20 km a game hour for six hours: over the view two hours in.
	ADreamWeatherStorm* Storm = W.SpawnStorm(FVector(-4000000.0, 0.0, 0.0));
	UDreamWeatherZoneComponent* Zone = Storm->Zone;
	Zone->Motion.Velocity = FVector2D(20.0, 0.0);
	Zone->Motion.LifetimeHours = 6.0f;
	Zone->Motion.FadeInHours = 0.5f;
	Zone->Motion.FadeOutHours = 1.0f;
	Zone->Motion.bDestroyWhenDone = false;
	Zone->StartMotion(W.Start);

	// Motion without a start: no effect until it starts.
	ADreamWeatherStorm* Waiting = W.SpawnStorm(FVector(0.0, 0.0, 0.0));
	TestEqual(TEXT("a storm that has not started has no effect"), Waiting->Zone->GetCurrentStrength(), 0.0f);

	W.At(0.0);
	TestEqual(TEXT("at its start: where it was placed"), Zone->GetFootprintCenter(), FVector(-4000000.0, 0.0, 0.0));
	TestEqual(TEXT("and not built up yet"), Zone->GetCurrentStrength(), 0.0f);
	W.At(1.0);
	TestTrue(TEXT("an hour on: 20 km east"), Zone->GetFootprintCenter().Equals(FVector(-2000000.0, 0.0, 0.0), 1.0));
	TestNearlyEqual(TEXT("and at full strength"), Zone->GetCurrentStrength(), 1.0f, 1.0e-4f);
	TestTrue(TEXT("still dry at the view"), W.Weather->GetViewSample().Precipitation < 0.05f);
	W.At(2.0);
	TestTrue(FString::Printf(TEXT("two hours on: it pours at the view (%.2f)"), W.Weather->GetViewSample().Precipitation), W.Weather->GetViewSample().Precipitation > 0.9f);
	FVector4f Z0, Z1, Z2;
	TestTrue(TEXT("the weather map draws it where it is"), Zone->MakeMapRecord(Z0, Z1, Z2) && FMath::IsNearlyEqual(Z0.X, 0.0f, 1.0f) && Z2.W > 0.99f);
	W.At(5.5);
	TestTrue(FString::Printf(TEXT("dying out at the end of its life (%.2f)"), Zone->GetCurrentStrength()), Zone->GetCurrentStrength() > 0.0f && Zone->GetCurrentStrength() < 0.9f);
	W.At(6.5);
	TestEqual(TEXT("its life over: gone"), Zone->GetCurrentStrength(), 0.0f);
	TestTrue(TEXT("and the view dry again"), W.Weather->GetViewSample().Precipitation < 0.05f);

	// The past as it was: a query for two hours in, asked at six and a half, finds the storm over the view.
	FDreamWeatherValues Values = W.Weather->GetGlobalValues();
	Zone->ApplyWeatherInfluenceAt(FVector(0, 0, 0), W.Start + 2.0 * 3600.0, Values);
	TestNearlyEqual(TEXT("asked of the past: it poured then"), Values.Get(FDreamWeatherChannels::Precipitation), 1.0f, 1.0e-3f);
	const FDreamWeatherSample Then = W.Weather->SampleWeatherAt(FVector(0, 0, 0), W.Weather->GetGlobalValues(), W.Start + 2.0 * 3600.0);
	TestTrue(FString::Printf(TEXT("and the weather's own query of the past agrees (%.2f)"), Then.Precipitation), Then.Precipitation > 0.9f);
	Values = W.Weather->GetGlobalValues();
	Zone->ApplyWeatherInfluenceAt(FVector(0, 0, 0), W.Start - 3600.0, Values);
	TestTrue(TEXT("before its start: nothing"), Values.Get(FDreamWeatherChannels::Precipitation) < 0.05f);

	// A pure function of the time: the same moment, however it was reached.
	W.At(1.25);
	const FVector Jumped = Zone->GetFootprintCenter();
	W.At(1.0);
	for (int32 i = 0; i < 15; ++i)
	{
		W.Clock->SetWorldSeconds(W.Clock->GetWorldSeconds() + 60.0);
		W.Clock->Tick(0.016f);
	}
	TestTrue(TEXT("run there or jump there: the same place"), Zone->GetFootprintCenter().Equals(Jumped, 0.01));

	// Along a path: the storm's spline, 60 km long, at 30 km a game hour.
	ADreamWeatherStorm* Wanderer = W.SpawnStorm(FVector(0.0, -3000000.0, 0.0));
	Wanderer->Path->ClearSplinePoints(false);
	Wanderer->Path->AddSplinePoint(FVector(0.0, -3000000.0, 0.0), ESplineCoordinateSpace::World, false);
	Wanderer->Path->AddSplinePoint(FVector(0.0, 3000000.0, 0.0), ESplineCoordinateSpace::World, true);
	Wanderer->Zone->Motion.bFollowPath = true;
	Wanderer->Zone->Motion.PathSpeed = 30.0f;
	Wanderer->Zone->Motion.bDestroyWhenDone = false;
	Wanderer->Zone->StartMotion(W.Start);
	W.At(1.0);
	TestTrue(FString::Printf(TEXT("an hour along its path: at the middle (%s)"), *Wanderer->Zone->GetFootprintCenter().ToString()),
		Wanderer->Zone->GetFootprintCenter().Equals(FVector(0.0, 0.0, 0.0), 100.0));
	W.At(3.0);
	TestTrue(TEXT("and stays at the end"), Wanderer->Zone->GetFootprintCenter().Equals(FVector(0.0, 3000000.0, 0.0), 100.0));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDreamWeatherStormFadeTest, "DreamWeather.Storm.Fade", DreamWeather::StormTests::Flags)
bool FDreamWeatherStormFadeTest::RunTest(const FString& Parameters)
{
	FDreamWeatherZoneMotion M;
	M.bEnabled = true;
	M.LifetimeHours = 4.0f;
	M.FadeInHours = 1.0f;
	M.FadeOutHours = 1.0f;
	TestEqual(TEXT("before its start"), M.FadeAt(-1.0), 0.0f);
	TestEqual(TEXT("at its start"), M.FadeAt(0.0), 0.0f);
	TestNearlyEqual(TEXT("halfway built up"), M.FadeAt(1800.0), 0.5f, 1.0e-4f);
	TestEqual(TEXT("in full"), M.FadeAt(2.0 * 3600.0), 1.0f);
	TestNearlyEqual(TEXT("halfway died out"), M.FadeAt(3.5 * 3600.0), 0.5f, 1.0e-4f);
	TestEqual(TEXT("over"), M.FadeAt(4.0 * 3600.0), 0.0f);
	TestTrue(TEXT("and says so"), M.IsOver(4.0 * 3600.0));
	M.LifetimeHours = 0.0f;
	TestEqual(TEXT("no lifetime: for ever"), M.FadeAt(1000.0 * 3600.0), 1.0f);
	M.bEnabled = false;
	TestEqual(TEXT("no motion: always in full"), M.FadeAt(-5.0), 1.0f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDreamWeatherStormSpawnerTest, "DreamWeather.Storm.Spawner", DreamWeather::StormTests::Flags)
bool FDreamWeatherStormSpawnerTest::RunTest(const FString& Parameters)
{
	using namespace DreamWeather::StormTests;
	FWorld W;
	if (!TestTrue(TEXT("world"), W.IsValid()))
	{
		return false;
	}
	FActorSpawnParameters Spawn;
	Spawn.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	ADreamWeatherStormSpawner* Spawner = W.World->SpawnActor<ADreamWeatherStormSpawner>(FVector(100000.0, 0.0, 0.0), FRotator::ZeroRotator, Spawn);
	FDreamStormKind& Kind = Spawner->Kinds.AddDefaulted_GetRef();
	Kind.Preset = MakePreset(TEXT("Storm"), { { DreamWeather::Tags::Channel_CloudCoverage, 1.0f }, { DreamWeather::Tags::Channel_Precipitation, 1.0f } });
	Kind.Winter = 0.0f;

	// Determinism: the same seed and slot, the same storm; another seed, other storms.
	// A hundred days from June (summer, then autumn).
	const int64 June = int64(DreamWorld::Celestial::FromDateTime(FDateTime(2026, 6, 1)) / (12.0 * 3600.0));
	int32 Storms = 0, Same = 0, Differ = 0;
	for (int64 Slot = June; Slot < June + 200; ++Slot)
	{
		FDreamStormPlan A, B, C;
		const bool bA = Spawner->PlanSlot(7, Slot, A);
		const bool bB = Spawner->PlanSlot(7, Slot, B);
		const bool bC = Spawner->PlanSlot(8, Slot, C);
		Storms += bA ? 1 : 0;
		Same += (bA == bB && (!bA || A == B)) ? 1 : 0;
		Differ += (bA != bC || (bA && !(A == C))) ? 1 : 0;
		if (bA)
		{
			TestTrue(TEXT("a storm starts within its slot"), A.StartSeconds >= double(Slot) * 12.0 * 3600.0 && A.StartSeconds < double(Slot + 1) * 12.0 * 3600.0);
			// Halfway through its life it passes near the spawner.
			const FVector2D Mid = A.Origin + A.Velocity * 100000.0 * double(A.LifetimeHours) * 0.5;
			TestTrue(TEXT("and passes within reach of the spawner"), FVector2D::Distance(Mid, FVector2D(100000.0, 0.0)) <= Spawner->PassRadius + 1.0);
			const double Yaw = FMath::RadiansToDegrees(FMath::Atan2(A.Velocity.Y, A.Velocity.X));
			TestTrue(TEXT("from the prevailing direction"), FMath::Abs(Yaw - Spawner->PrevailingYaw) <= Spawner->DirectionVariation + 0.01);
		}
	}
	TestEqual(TEXT("the same seed gives the same storms"), Same, 200);
	TestTrue(FString::Printf(TEXT("another seed gives others (%d of 200 differ)"), Differ), Differ > 50);
	TestTrue(FString::Printf(TEXT("storms come at about the seasons' chance (%d of 200)"), Storms), Storms > 30 && Storms < 120);

	// No storm kind for winter: no winter storms.
	int32 WinterStorms = 0;
	const double SlotSeconds = 12.0 * 3600.0;
	const int64 January = int64(DreamWorld::Celestial::FromDateTime(FDateTime(2027, 1, 1)) / SlotSeconds);
	for (int64 Slot = January; Slot < January + 50; ++Slot)
	{
		FDreamStormPlan P;
		WinterStorms += Spawner->PlanSlot(7, Slot, P) ? 1 : 0;
	}
	TestEqual(TEXT("no kind for winter, no storms in winter"), WinterStorms, 0);

	// In the world: a storm actor for every storm alive, gone when its life is over.
	Spawner->SeasonChance = FVector4f(1.0f, 1.0f, 1.0f, 1.0f);
	Spawner->Refresh();
	const TArray<FDreamStormPlan> Alive = Spawner->GetStormsAt(W.Clock->GetWorldSeconds());
	TestEqual(TEXT("a storm actor per storm alive"), Spawner->GetLiveStorms().Num(), Alive.Num());
	if (Alive.Num() > 0)
	{
		const FDreamStormPlan& Plan = Alive[0];
		const TWeakObjectPtr<ADreamWeatherStorm>* Storm = Spawner->GetLiveStorms().Find(Plan.Slot);
		if (TestTrue(TEXT("spawned"), Storm && Storm->IsValid()))
		{
			const FVector Center = (*Storm)->Zone->GetFootprintCenter();
			const double Hours = (W.Clock->GetWorldSeconds() - Plan.StartSeconds) / 3600.0;
			TestTrue(TEXT("where its plan says"), FVector2D(Center.X, Center.Y).Equals(Plan.Origin + Plan.Velocity * 100000.0 * Hours, 10.0));
		}
		// The weather sees the storm through the spawner (its actor only draws the map): over its centre it rains,
		// and asked of that moment later, after the actor is gone, it still does.
		const double Mid = FMath::Max(W.Clock->GetWorldSeconds(), Plan.StartSeconds + double(Plan.LifetimeHours) * 1800.0);
		const FVector2D MidCenter = ADreamWeatherStormSpawner::CenterAt(Plan, Mid);
		const FVector Over(MidCenter.X, MidCenter.Y, 0.0);
		const FDreamWeatherSample During = W.Weather->SampleWeatherAt(Over, W.Weather->GetGlobalValues(), Mid);
		TestTrue(FString::Printf(TEXT("halfway through its life it rains under it (%.2f)"), During.Precipitation), During.Precipitation > 0.9f);
		if (const TWeakObjectPtr<ADreamWeatherStorm>* Actor = Spawner->GetLiveStorms().Find(Plan.Slot))
		{
			TestFalse(TEXT("its actor only draws the weather map"), Actor->IsValid() && (*Actor)->Zone->bAffectsQueries);
		}

		W.Clock->SetWorldSeconds(Plan.EndSeconds() + 1.0);
		Spawner->Refresh();
		TestFalse(TEXT("its life over: gone"), Spawner->GetLiveStorms().Contains(Plan.Slot));
		const FDreamWeatherSample Remembered = W.Weather->SampleWeatherAt(Over, W.Weather->GetGlobalValues(), Mid);
		TestNearlyEqual(TEXT("and the past still has it"), Remembered.Precipitation, During.Precipitation, 1.0e-4f);
		const FDreamWeatherSample After = W.Weather->SampleWeatherAt(Over, W.Weather->GetGlobalValues(), Plan.EndSeconds() + 1.0);
		TestTrue(TEXT("after its life, dry there"), After.Precipitation < 0.05f || Spawner->GetStormsAt(Plan.EndSeconds() + 1.0).Num() > 0);
	}
	return true;
}

#endif
