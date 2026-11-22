// Copyright DreamMoon. All Rights Reserved.

#include "DreamWeather.h"
#include "DreamWeatherChannels.h"
#include "DreamWeatherModule.h"
#include "DreamWeatherPreset.h"
#include "DreamWeatherSelector.h"
#include "DreamWeatherSubsystem.h"
#include "DreamWeatherZone.h"
#include "DreamWorldSubsystem.h"

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

#endif
