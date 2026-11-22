// Copyright DreamMoon. All Rights Reserved.

#include "DreamWorldTestListener.h"
#include "DreamDynamicWorldModule.h"
#include "DreamWorldCelestial.h"
#include "DreamWorldParameters.h"
#include "DreamWorldScheduler.h"
#include "DreamWorldSettings.h"
#include "DreamWorldSubsystem.h"
#include "DreamWorldViewProbe.h"

#include "Components/BoxComponent.h"
#include "Engine/CollisionProfile.h"
#include "Engine/World.h"
#include "Misc/AutomationTest.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(DreamWorldTestListener)

void UDreamWorldTestListener::Bind(UDreamWorldSubsystem* Clock)
{
	Clock->OnHourChanged.AddDynamic(this, &UDreamWorldTestListener::HandleHour);
	Clock->OnDateChanged.AddDynamic(this, &UDreamWorldTestListener::HandleDate);
	Clock->OnSunrise.AddDynamic(this, &UDreamWorldTestListener::HandleSunrise);
	Clock->OnSunset.AddDynamic(this, &UDreamWorldTestListener::HandleSunset);
	Clock->OnTimeJumped.AddDynamic(this, &UDreamWorldTestListener::HandleJump);
}

#if WITH_DEV_AUTOMATION_TESTS

namespace DreamWorld::Tests
{
	constexpr EAutomationTestFlags Flags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter;

	double At(int32 Year, int32 Month, int32 Day, int32 Hour, int32 Minute = 0)
	{
		return Celestial::FromDateTime(FDateTime(Year, Month, Day, Hour, Minute, 0));
	}

	/** The highest the sun gets on a day, sampled each minute, and where it stands then. */
	FDreamCelestialState HighestSun(const FDreamCelestialSettings& S, int32 Year, int32 Month, int32 Day)
	{
		FDreamCelestialState Best;
		Best.SunElevation = -1000.0f;
		for (int32 Minute = 0; Minute < 1440; ++Minute)
		{
			const FDreamCelestialState State = Celestial::Evaluate(S, At(Year, Month, Day, 0) + Minute * 60.0);
			if (State.SunElevation > Best.SunElevation)
			{
				Best = State;
			}
		}
		return Best;
	}

	struct FTestWorld
	{
		UWorld* World = nullptr;
		UDreamWorldSubsystem* Clock = nullptr;

		FTestWorld()
		{
			World = UWorld::CreateWorld(EWorldType::Game, false);
			Clock = World ? World->GetSubsystem<UDreamWorldSubsystem>() : nullptr;
		}
		~FTestWorld()
		{
			if (World)
			{
				World->DestroyWorld(false);
			}
		}
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDreamWorldArtisticSunTest, "DreamWorld.Celestial.ArtisticSunPath", DreamWorld::Tests::Flags)
bool FDreamWorldArtisticSunTest::RunTest(const FString& Parameters)
{
	using namespace DreamWorld;
	FDreamCelestialSettings S;
	S.Mode = EDreamCelestialMode::Artistic;
	S.SunriseHour = 6.0f;
	S.SunsetHour = 18.0f;
	S.NoonElevation = 60.0f;

	const FDreamCelestialState Rise = Celestial::Evaluate(S, Tests::At(2026, 6, 21, 6));
	const FDreamCelestialState Noon = Celestial::Evaluate(S, Tests::At(2026, 6, 21, 12));
	const FDreamCelestialState Set = Celestial::Evaluate(S, Tests::At(2026, 6, 21, 18));
	const FDreamCelestialState Midnight = Celestial::Evaluate(S, Tests::At(2026, 6, 21, 0));
	TestNearlyEqual(TEXT("sunrise on the horizon"), Rise.SunElevation, 0.0f, 0.01f);
	TestNearlyEqual(TEXT("sunrise in the east"), Rise.SunAzimuth, 90.0f, 0.01f);
	TestNearlyEqual(TEXT("noon elevation"), Noon.SunElevation, 60.0f, 0.01f);
	TestNearlyEqual(TEXT("noon in the south"), Noon.SunAzimuth, 180.0f, 0.01f);
	TestNearlyEqual(TEXT("sunset on the horizon"), Set.SunElevation, 0.0f, 0.01f);
	TestNearlyEqual(TEXT("sunset in the west"), Set.SunAzimuth, 270.0f, 0.01f);
	TestNearlyEqual(TEXT("midnight below"), Midnight.SunElevation, -60.0f, 0.01f);
	TestNearlyEqual(TEXT("moon opposite the sun"), Midnight.MoonElevation, 60.0f, 0.01f);

	// No jumps anywhere in the day, including across midnight and an uneven day.
	S.SunriseHour = 5.0f;
	S.SunsetHour = 20.5f;
	FVector Prev = Celestial::Evaluate(S, Tests::At(2026, 6, 20, 23, 59)).SunDirection;
	double MaxStep = 0.0;
	for (int32 Minute = 0; Minute <= 1440; ++Minute)
	{
		const FVector Dir = Celestial::Evaluate(S, Tests::At(2026, 6, 21, 0) + Minute * 60.0).SunDirection;
		MaxStep = FMath::Max(MaxStep, FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(FVector::DotProduct(Dir, Prev), -1.0, 1.0))));
		Prev = Dir;
	}
	TestTrue(FString::Printf(TEXT("sun moves smoothly (largest step per minute %.3f deg)"), MaxStep), MaxStep < 0.6);

	// NorthYaw turns the whole sky.
	S.NorthYaw = 90.0f;
	const FDreamCelestialState Turned = Celestial::Evaluate(S, Tests::At(2026, 6, 21, 5));
	TestTrue(TEXT("with north along +Y the sun rises along -X"), Turned.SunDirection.X < -0.99);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDreamWorldRealisticSunTest, "DreamWorld.Celestial.RealisticSun", DreamWorld::Tests::Flags)
bool FDreamWorldRealisticSunTest::RunTest(const FString& Parameters)
{
	using namespace DreamWorld;
	FDreamCelestialSettings S;
	S.Mode = EDreamCelestialMode::Realistic;

	// Tokyo at the June solstice: 90 - (35.68 - 23.44) degrees, due south.
	S.Latitude = 35.68;
	S.Longitude = 139.69;
	S.UtcOffsetHours = 9.0f;
	const FDreamCelestialState Tokyo = Tests::HighestSun(S, 2026, 6, 21);
	TestNearlyEqual(TEXT("Tokyo solstice noon elevation"), Tokyo.SunElevation, 77.76f, 0.6f);
	TestNearlyEqual(TEXT("Tokyo solstice noon azimuth"), Tokyo.SunAzimuth, 180.0f, 2.0f);
	TestTrue(TEXT("Tokyo midnight is dark"), Celestial::Evaluate(S, Tests::At(2026, 6, 21, 0)).SunElevation < -20.0f);

	// The equator at the March equinox: overhead at noon, rising due east.
	S.Latitude = 0.0;
	S.Longitude = 0.0;
	S.UtcOffsetHours = 0.0f;
	TestTrue(TEXT("equinox sun overhead at the equator"), Tests::HighestSun(S, 2026, 3, 20).SunElevation > 89.0f);
	const FDreamCelestialState Morning = Celestial::Evaluate(S, Tests::At(2026, 3, 20, 7));
	TestNearlyEqual(TEXT("equinox morning sun in the east"), Morning.SunAzimuth, 90.0f, 2.0f);

	// The southern hemisphere sees the noon sun in the north.
	S.Latitude = -33.87;
	S.Longitude = 151.21;
	S.UtcOffsetHours = 10.0f;
	TestNearlyEqual(TEXT("Sydney noon sun in the north"), FMath::Fmod(Tests::HighestSun(S, 2026, 6, 21).SunAzimuth + 180.0f, 360.0f), 180.0f, 3.0f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDreamWorldStarRotationTest, "DreamWorld.Celestial.StarRotation", DreamWorld::Tests::Flags)
bool FDreamWorldStarRotationTest::RunTest(const FString& Parameters)
{
	using namespace DreamWorld;
	FDreamCelestialSettings Artistic;
	Artistic.Mode = EDreamCelestialMode::Artistic;
	FDreamCelestialSettings Greenwich;
	Greenwich.Mode = EDreamCelestialMode::Realistic;
	Greenwich.Latitude = 51.48;
	Greenwich.Longitude = 0.0;
	Greenwich.UtcOffsetHours = 0.0f;
	auto SiderealHours = [](const FDreamCelestialSettings& S, double Seconds) { return double(Celestial::Evaluate(S, Seconds).StarRotation) / 15.0; };
	auto HoursApart = [](double A, double B) { return FMath::Abs(FMath::Fmod(A - B + 36.0, 24.0) - 12.0); };

	// The epoch: Greenwich sidereal time at 2000-01-01 12:00 UT is 18.697 h.
	TestTrue(TEXT("J2000.0: 18.70 h at Greenwich"), HoursApart(SiderealHours(Greenwich, Tests::At(2000, 1, 1, 12)), 18.697) < 0.01);

	// A January evening: the artistic sky turns with the real one (Orion high in the south at 21:00), 4.69 h.
	const double Evening = Tests::At(2026, 1, 15, 21);
	const double Art = SiderealHours(Artistic, Evening);
	TestTrue(FString::Printf(TEXT("15 January 21:00, artistic %.2f h against Greenwich %.2f h"), Art, SiderealHours(Greenwich, Evening)),
		HoursApart(Art, SiderealHours(Greenwich, Evening)) < 0.1);
	TestTrue(FString::Printf(TEXT("and the real value (%.2f h)"), Art), HoursApart(Art, 4.69) < 0.1);

	// Night after night at the same hour the stars stand some four minutes further on (a sidereal day is 23 h 56 m);
	// over a year that adds up to a whole turn.
	int32 Off = 0;
	double Total = 0.0;
	for (int32 Day = 0; Day < 365; ++Day)
	{
		const double Tonight = SiderealHours(Artistic, Evening + Day * 86400.0);
		const double Tomorrow = SiderealHours(Artistic, Evening + (Day + 1) * 86400.0);
		const double Minutes = FMath::Fmod(Tomorrow - Tonight + 24.0, 24.0) * 60.0;
		Off += FMath::IsNearlyEqual(Minutes, 3.94, 0.05) ? 0 : 1;
		Total += Minutes;
	}
	TestEqual(TEXT("every night four minutes earlier"), Off, 0);
	TestTrue(FString::Printf(TEXT("a whole turn in a year (%.0f min)"), Total), FMath::IsNearlyEqual(Total, 1440.0, 5.0));

	// Continuous: through midnight and across the turn of the year it moves by its rate, never by a jump.
	int32 Jumps = 0;
	for (const double Start : { Tests::At(2026, 1, 15, 23), Tests::At(2026, 12, 31, 23) })
	{
		for (int32 Minute = 0; Minute < 120; ++Minute)
		{
			const double Step = FMath::Fmod(SiderealHours(Artistic, Start + (Minute + 1) * 60.0) - SiderealHours(Artistic, Start + Minute * 60.0) + 24.0, 24.0);
			Jumps += FMath::IsNearlyEqual(Step * 60.0, 1.0027, 0.002) ? 0 : 1;
		}
	}
	TestEqual(TEXT("no jump at midnight or new year"), Jumps, 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDreamWorldMoonPhaseTest, "DreamWorld.Celestial.MoonPhase", DreamWorld::Tests::Flags)
bool FDreamWorldMoonPhaseTest::RunTest(const FString& Parameters)
{
	using namespace DreamWorld;
	FDreamCelestialSettings S;
	S.Mode = EDreamCelestialMode::Realistic;
	S.Latitude = 0.0;
	S.Longitude = 0.0;
	S.UtcOffsetHours = 0.0f;

	// The total lunar eclipse of 2026-03-03 (full) and the total solar eclipse of 2026-08-12 (new).
	const FDreamCelestialState Full = Celestial::Evaluate(S, Tests::At(2026, 3, 3, 11, 33));
	const FDreamCelestialState New = Celestial::Evaluate(S, Tests::At(2026, 8, 12, 17, 46));
	TestTrue(FString::Printf(TEXT("full moon lit (%.3f)"), Full.MoonIllumination), Full.MoonIllumination > 0.97f);
	TestTrue(FString::Printf(TEXT("new moon dark (%.3f)"), New.MoonIllumination), New.MoonIllumination < 0.03f);

	// At a solar eclipse the moon stands in front of the sun.
	const double Separation = FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(FVector::DotProduct(New.SunDirection, New.MoonDirection), -1.0, 1.0)));
	TestTrue(FString::Printf(TEXT("eclipse: moon near the sun (%.2f deg)"), Separation), Separation < 3.0);

	// The mean phase the artistic mode uses agrees within a day or so.
	const double MeanFull = Celestial::MeanMoonPhase(1721425.5 + Tests::At(2026, 3, 3, 11, 33) / 86400.0);
	TestNearlyEqual(TEXT("mean phase at the full moon"), MeanFull, 0.5, 0.06);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDreamWorldDayPhaseTest, "DreamWorld.Celestial.DayPhase", DreamWorld::Tests::Flags)
bool FDreamWorldDayPhaseTest::RunTest(const FString& Parameters)
{
	using namespace DreamWorld;
	float PrevDay = 0.0f;
	bool bSums = true;
	bool bMonotone = true;
	for (float E = -90.0f; E <= 90.0f; E += 0.25f)
	{
		const FDreamDayPhase P = Celestial::DayPhase(E);
		bSums &= FMath::IsNearlyEqual(P.Day + P.Twilight + P.Night, 1.0f, 1e-4f) && P.Twilight >= 0.0f;
		bMonotone &= P.Day >= PrevDay - 1e-5f;
		PrevDay = P.Day;
	}
	TestTrue(TEXT("weights sum to one"), bSums);
	TestTrue(TEXT("day weight rises with the sun"), bMonotone);
	TestNearlyEqual(TEXT("noon is day"), Celestial::DayPhase(45.0f).Day, 1.0f);
	TestNearlyEqual(TEXT("midnight is night"), Celestial::DayPhase(-45.0f).Night, 1.0f);
	TestTrue(TEXT("sunset is mostly twilight"), Celestial::DayPhase(0.0f).Twilight > 0.6f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDreamWorldClockTest, "DreamWorld.Clock.RunAndEvents", DreamWorld::Tests::Flags)
bool FDreamWorldClockTest::RunTest(const FString& Parameters)
{
	DreamWorld::Tests::FTestWorld W;
	if (!TestNotNull(TEXT("clock subsystem in a game world"), W.Clock))
	{
		return false;
	}
	FDreamWorldClockConfig Config;
	Config.StartTime = FDateTime(2026, 6, 21, 5, 59, 0);
	Config.DayLengthMinutes = 24.0f; // 60 game seconds per real second
	Config.bAnimate = true;
	Config.Celestial.Mode = EDreamCelestialMode::Artistic;
	W.Clock->ApplyClockConfig(Config, true);

	UDreamWorldTestListener* Listener = NewObject<UDreamWorldTestListener>();
	Listener->Bind(W.Clock);

	W.Clock->Tick(0.0f); // primes the events after the jump the config made
	for (int32 i = 0; i < 20; ++i)
	{
		W.Clock->Tick(0.1f);
	}
	TestNearlyEqual(TEXT("two real seconds ran two game minutes"), W.Clock->GetTimeOfDay(), 6.0f + 1.0f / 60.0f, 1e-4f);
	TestEqual(TEXT("one hour event"), Listener->Hours, 1);
	TestEqual(TEXT("for six o'clock"), Listener->LastHour, 6);
	TestEqual(TEXT("sunrise fired once"), Listener->Sunrises, 1);

	// A jump fires only OnTimeJumped, whatever it skips.
	W.Clock->SetTimeOfDay(23.99f);
	W.Clock->Tick(0.0f);
	TestEqual(TEXT("jump reported"), Listener->Jumps, 1);
	TestEqual(TEXT("no hour events for the skipped hours"), Listener->Hours, 1);
	TestEqual(TEXT("no sunset for a jump"), Listener->Sunsets, 0);

	// Running over midnight changes the date.
	for (int32 i = 0; i < 10; ++i)
	{
		W.Clock->Tick(0.1f);
	}
	TestEqual(TEXT("date changed at midnight"), Listener->Dates, 1);
	TestEqual(TEXT("it is the 22nd"), W.Clock->GetDateTime().GetDay(), 22);

	// Paused clocks stand still.
	W.Clock->SetPaused(true);
	const double Before = W.Clock->GetWorldSeconds();
	W.Clock->Tick(1.0f);
	TestEqual(TEXT("paused"), W.Clock->GetWorldSeconds(), Before);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDreamWorldTransitionTest, "DreamWorld.Clock.Transition", DreamWorld::Tests::Flags)
bool FDreamWorldTransitionTest::RunTest(const FString& Parameters)
{
	DreamWorld::Tests::FTestWorld W;
	if (!TestNotNull(TEXT("clock subsystem"), W.Clock))
	{
		return false;
	}
	FDreamWorldClockConfig Config;
	Config.StartTime = FDateTime(2026, 6, 21, 8, 0, 0);
	Config.bAnimate = false;
	W.Clock->ApplyClockConfig(Config, true);

	W.Clock->TransitionToTimeOfDay(10.0f, 2.0f, true);
	W.Clock->Tick(1.0f);
	TestNearlyEqual(TEXT("halfway after half the time"), W.Clock->GetTimeOfDay(), 9.0f, 0.01f);
	W.Clock->Tick(1.5f);
	TestNearlyEqual(TEXT("arrives"), W.Clock->GetTimeOfDay(), 10.0f, 1e-4f);
	TestFalse(TEXT("done"), W.Clock->IsTransitioning());

	// Forward to an earlier hour runs into the next day.
	W.Clock->TransitionToTimeOfDay(6.0f, 1.0f, true);
	W.Clock->Tick(2.0f);
	TestEqual(TEXT("next day"), W.Clock->GetDateTime().GetDay(), 22);
	TestNearlyEqual(TEXT("at six"), W.Clock->GetTimeOfDay(), 6.0f, 1e-4f);

	// Not forward: the shorter way, backwards.
	W.Clock->TransitionToTimeOfDay(5.0f, 1.0f, false);
	W.Clock->Tick(2.0f);
	TestEqual(TEXT("same day"), W.Clock->GetDateTime().GetDay(), 22);
	TestNearlyEqual(TEXT("back to five"), W.Clock->GetTimeOfDay(), 5.0f, 1e-4f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDreamWorldSnapshotTest, "DreamWorld.Clock.Snapshot", DreamWorld::Tests::Flags)
bool FDreamWorldSnapshotTest::RunTest(const FString& Parameters)
{
	DreamWorld::Tests::FTestWorld A;
	DreamWorld::Tests::FTestWorld B;
	if (!TestNotNull(TEXT("clock A"), A.Clock) || !TestNotNull(TEXT("clock B"), B.Clock))
	{
		return false;
	}
	FDreamWorldClockConfig Config;
	Config.StartTime = FDateTime(2026, 10, 3, 17, 42, 0);
	Config.Seed = 4242;
	Config.DayLengthMinutes = 12.0f;
	Config.Celestial.Mode = EDreamCelestialMode::Realistic;
	Config.Celestial.Latitude = 51.5;
	Config.Celestial.Longitude = -0.12;
	Config.Celestial.UtcOffsetHours = 1.0f;
	A.Clock->ApplyClockConfig(Config, true);
	for (int32 i = 0; i < 37; ++i)
	{
		A.Clock->Tick(0.033f);
	}

	FDreamWorldSnapshot Snapshot = A.Clock->CaptureSnapshot();
	Snapshot.Extensions.Add(TEXT("Test"), TEXT("kept"));
	B.Clock->ApplySnapshot(Snapshot);
	B.Clock->Tick(0.0f);

	TestEqual(TEXT("same time"), B.Clock->GetWorldSeconds(), A.Clock->GetWorldSeconds());
	TestEqual(TEXT("same seed"), B.Clock->GetSeed(), 4242);
	TestNearlyEqual(TEXT("same day length"), B.Clock->GetDayLengthMinutes(), 12.0f, 1e-3f);
	TestEqual(TEXT("same sun"), B.Clock->GetCelestial().SunDirection, A.Clock->GetCelestial().SunDirection);
	TestEqual(TEXT("same moon"), B.Clock->GetCelestial().MoonDirection, A.Clock->GetCelestial().MoonDirection);
	TestTrue(TEXT("same mode"), B.Clock->GetCelestialSettings().Mode == EDreamCelestialMode::Realistic);
	TestEqual(TEXT("extensions survive"), Snapshot.Extensions.FindRef(TEXT("Test")), FString(TEXT("kept")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDreamWorldSchedulerTest, "DreamWorld.Scheduler", DreamWorld::Tests::Flags)
bool FDreamWorldSchedulerTest::RunTest(const FString& Parameters)
{
	FDreamWorldScheduler Scheduler;
	TArray<FName> Order;
	auto Spin = [](double Ms) { const double End = FPlatformTime::Seconds() + Ms / 1000.0; while (FPlatformTime::Seconds() < End) {} };

	Scheduler.Register(TEXT("Low"), 0, 0.0, 0.0, [&] { Order.Add(TEXT("Low")); return true; });
	Scheduler.Register(TEXT("High"), 10, 0.0, 0.0, [&] { Order.Add(TEXT("High")); Spin(2.0); return true; });
	const int32 Slow = Scheduler.Register(TEXT("Slow"), 5, 100.0, 0.0, [&] { Order.Add(TEXT("Slow")); return true; });
	const int32 Overdue = Scheduler.Register(TEXT("Overdue"), -5, 1.0, 1.0, [&] { Order.Add(TEXT("Overdue")); return true; });

	Scheduler.Tick(1000.0, 1.0);
	TestTrue(TEXT("overdue first, then by priority until the budget is spent"), Order == TArray<FName>{ TEXT("Overdue"), TEXT("High") });

	Order.Reset();
	Scheduler.Tick(1000.5, 100.0);
	TestTrue(TEXT("with budget to spare everything due runs, by priority"), Order == TArray<FName>{ TEXT("High"), TEXT("Slow"), TEXT("Low") });
	TestFalse(TEXT("a task that ran waits its min interval"), Order.Contains(TEXT("Overdue")));

	Order.Reset();
	Scheduler.Request(Slow);
	Scheduler.Unregister(Overdue);
	Scheduler.Tick(1001.0, 100.0);
	TestTrue(TEXT("a request makes a task due early"), Order.Contains(TEXT("Slow")));
	TestFalse(TEXT("unregistered tasks never run"), Order.Contains(TEXT("Overdue")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDreamWorldParametersTest, "DreamWorld.Parameters", DreamWorld::Tests::Flags)
bool FDreamWorldParametersTest::RunTest(const FString& Parameters)
{
	const FGuid A1 = FDreamWorldParameters::MakeStableId(TEXT("DDW_SunDir"));
	const FGuid A2 = FDreamWorldParameters::MakeStableId(TEXT("DDW_SunDir"));
	const FGuid B = FDreamWorldParameters::MakeStableId(TEXT("DDW_MoonDir"));
	TestTrue(TEXT("ids are stable"), A1 == A2);
	TestTrue(TEXT("ids differ by name"), A1 != B);
	TestTrue(TEXT("ids are valid"), A1.IsValid());
	TestTrue(TEXT("the clock's parameters are registered"), FDreamWorldParameters::IsRegistered(DreamWorld::Names::MPC_SunDir));

	TSet<FName> Names;
	TSet<FGuid> Ids;
	for (const FDreamWorldParameterDesc& Desc : FDreamWorldParameters::GetAll())
	{
		Names.Add(Desc.Name);
		Ids.Add(Desc.Id);
	}
	TestEqual(TEXT("no duplicate ids"), Ids.Num(), Names.Num());
	return true;
}

namespace DreamWorld::Tests
{
	/** A solid box that blocks every trace channel. */
	void AddBlock(UWorld* World, const FVector& Center, const FVector& Extent)
	{
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		AActor* Actor = World->SpawnActor<AActor>(Params);
		UBoxComponent* Box = NewObject<UBoxComponent>(Actor);
		Box->SetBoxExtent(Extent, false);
		Box->SetCollisionProfileName(UCollisionProfile::BlockAll_ProfileName);
		Actor->SetRootComponent(Box);
		Box->RegisterComponent();
		Box->SetWorldLocation(Center);
	}

	/** A room 10 m square and 4 m high around (X, 0): floor, four walls and (optionally) a roof. */
	void AddRoom(UWorld* World, double X, bool bRoof, bool bWalls)
	{
		AddBlock(World, FVector(X, 0, -10), FVector(520, 520, 10));
		if (bRoof)
		{
			AddBlock(World, FVector(X, 0, 410), FVector(520, 520, 10));
		}
		if (bWalls)
		{
			AddBlock(World, FVector(X + 510, 0, 200), FVector(10, 520, 210));
			AddBlock(World, FVector(X - 510, 0, 200), FVector(10, 520, 210));
			AddBlock(World, FVector(X, 510, 200), FVector(520, 10, 210));
			AddBlock(World, FVector(X, -510, 200), FVector(520, 10, 210));
		}
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDreamWorldViewProbeTest, "DreamWorld.ViewProbe", DreamWorld::Tests::Flags)
bool FDreamWorldViewProbeTest::RunTest(const FString& Parameters)
{
	using namespace DreamWorld::Tests;
	FTestWorld W;
	if (!TestNotNull(TEXT("clock"), W.Clock))
	{
		return false;
	}
	AddRoom(W.World, 0.0, true, true);       // a closed room at the origin
	AddRoom(W.World, 20000.0, true, false);  // a roof on posts (a porch) 200 m away
	const int32 Rays = FDreamViewProbe::GetDirections().Num();

	// No view, no probing.
	W.Clock->Tick(0.016f);
	TestFalse(TEXT("nothing probed before a view is reported"), W.Clock->GetViewOcclusion().bValid);

	// Inside the closed room: the first look casts the whole round at once.
	W.Clock->ReportViewLocation(FVector(0, 0, 170), true);
	W.Clock->Tick(0.016f);
	const FDreamViewOcclusion Inside = W.Clock->GetViewOcclusion();
	TestTrue(TEXT("a full round on the first look"), Inside.bValid);
	TestEqual(TEXT("every ray was cast once"), W.Clock->GetViewProbe().GetRaysCast(), int64(Rays));
	TestTrue(FString::Printf(TEXT("a closed room is indoors (%.2f)"), Inside.Interior), Inside.Interior > 0.99f);
	TestTrue(FString::Printf(TEXT("with a roof overhead (%.2f)"), Inside.Overhead), Inside.Overhead > 0.99f);
	TestTrue(TEXT("and walls on every side"), FMath::Min(FMath::Min(Inside.PlusX, Inside.MinusX), FMath::Min(Inside.PlusY, Inside.MinusY)) > 0.99f);

	// Afterwards the rays spread over the round: about a round's worth per ProbeRoundSeconds.
	const float Round = UDreamWorldSettings::Get()->ProbeRoundSeconds;
	const int64 Before = W.Clock->GetViewProbe().GetRaysCast();
	for (int32 i = 0; i < 20; ++i)
	{
		W.Clock->Tick(Round / 20.0f);
	}
	const int64 Spread = W.Clock->GetViewProbe().GetRaysCast() - Before;
	TestTrue(FString::Printf(TEXT("about one round per %.2f s (%lld rays for %d)"), Round, Spread, Rays), Spread >= Rays - 2 && Spread <= Rays + 2);

	// Teleport into the open: the probe snaps.
	W.Clock->ReportViewLocation(FVector(-20000, 0, 170), true);
	W.Clock->Tick(0.016f);
	const FDreamViewOcclusion Open = W.Clock->GetViewOcclusion();
	TestEqual(TEXT("out in the open"), Open.Interior, 0.0f);
	TestEqual(TEXT("nothing overhead"), Open.Overhead, 0.0f);

	// Under the porch: a roof, but open on all sides.
	W.Clock->ReportViewLocation(FVector(20000, 0, 170), true);
	W.Clock->Tick(0.016f);
	const FDreamViewOcclusion Porch = W.Clock->GetViewOcclusion();
	TestTrue(FString::Printf(TEXT("a porch has a roof (%.2f)"), Porch.Overhead), Porch.Overhead > 0.99f);
	TestTrue(FString::Printf(TEXT("but is less indoors than a room (%.2f)"), Porch.Interior), Porch.Interior < Inside.Interior - 0.1f);
	TestTrue(FString::Printf(TEXT("and open at the sides (%.2f)"), Porch.PlusX), Porch.PlusX < 0.7f);

	// Walking (not jumping) from the porch into the open eases over ProbeResponseSeconds.
	W.Clock->ReportViewLocation(FVector(20900, 0, 170), true);
	W.Clock->Tick(0.016f);
	const float Step = W.Clock->GetViewOcclusion().Overhead;
	TestTrue(FString::Printf(TEXT("a step eases (%.2f)"), Step), Step > 0.5f && Step <= 1.0f);
	for (int32 i = 0; i < 200; ++i)
	{
		W.Clock->Tick(0.016f);
	}
	TestTrue(FString::Printf(TEXT("and settles in the open (%.3f)"), W.Clock->GetViewOcclusion().Overhead), W.Clock->GetViewOcclusion().Overhead < 0.02f);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
