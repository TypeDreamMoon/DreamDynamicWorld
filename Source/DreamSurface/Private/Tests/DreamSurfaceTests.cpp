// Copyright DreamMoon. All Rights Reserved.

#include "DreamSurfaceSettings.h"
#include "DreamSurfaceSubsystem.h"

#include "DreamWeather.h"
#include "DreamWeatherChannels.h"
#include "DreamWeatherModule.h"
#include "DreamWeatherPreset.h"
#include "DreamWeatherSelector.h"
#include "DreamWeatherShelter.h"
#include "DreamWeatherSubsystem.h"
#include "DreamWorldCelestial.h"
#include "DreamWorldSubsystem.h"

#include "Engine/World.h"
#include "Misc/AutomationTest.h"
#include "UObject/Package.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace DreamSurface::Tests
{
	constexpr EAutomationTestFlags Flags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter;
	using namespace DreamWeather::Tags;

	UDreamWeatherPreset* MakePreset(const TCHAR* Name, std::initializer_list<TPair<FGameplayTag, float>> Channels)
	{
		UDreamWeatherPreset* Preset = NewObject<UDreamWeatherPreset>(GetTransientPackage(), MakeUniqueObjectName(GetTransientPackage(), UDreamWeatherPreset::StaticClass(), Name));
		for (const TPair<FGameplayTag, float>& Channel : Channels)
		{
			Preset->Channels.Add(Channel.Key, Channel.Value);
		}
		return Preset;
	}

	UDreamWeatherPreset* Clear() { return MakePreset(TEXT("SurfClear"), { { Channel_CloudCoverage, 0.05f }, { Channel_Wind, 0.3f } }); }
	UDreamWeatherPreset* Rain() { return MakePreset(TEXT("SurfRain"), { { Channel_CloudCoverage, 1.0f }, { Channel_Precipitation, 0.9f }, { Channel_Wind, 0.4f } }); }
	UDreamWeatherPreset* Blizzard() { return MakePreset(TEXT("SurfSnow"), { { Channel_CloudCoverage, 1.0f }, { Channel_Precipitation, 0.9f }, { Channel_SnowRatio, 1.0f }, { Channel_Temperature, -25.0f } }); }
	UDreamWeatherPreset* Thaw() { return MakePreset(TEXT("SurfThaw"), { { Channel_CloudCoverage, 0.05f }, { Channel_Temperature, 10.0f } }); }
	UDreamWeatherPreset* DustStorm() { return MakePreset(TEXT("SurfDust"), { { Channel_CloudCoverage, 0.3f }, { Channel_Dust, 1.0f }, { Channel_Wind, 1.0f } }); }

	/** A sequence of presets, Hours each, repeating. */
	UDreamWeatherSelector_Sequence* Sequence(std::initializer_list<TPair<UDreamWeatherPreset*, float>> Entries)
	{
		UDreamWeatherSelector_Sequence* S = NewObject<UDreamWeatherSelector_Sequence>(GetTransientPackage());
		for (const TPair<UDreamWeatherPreset*, float>& E : Entries)
		{
			S->Entries.Add({ E.Key, E.Value });
		}
		return S;
	}

	/** Midsummer midnight, plus Hours. */
	double At(double Hours) { return DreamWorld::Celestial::FromDateTime(FDateTime(2026, 6, 21, 0, 0, 0)) + Hours * 3600.0; }

	/** A game world with the clock, the weather (a weather actor with the given selector) and the surfaces. */
	struct FSurfaceWorld
	{
		UWorld* World = nullptr;
		UDreamWorldSubsystem* Clock = nullptr;
		UDreamWeatherSubsystem* Weather = nullptr;
		UDreamSurfaceSubsystem* Surface = nullptr;

		explicit FSurfaceWorld(UDreamWeatherSelector* Selector)
		{
			World = UWorld::CreateWorld(EWorldType::Game, false);
			if (!World)
			{
				return;
			}
			Clock = World->GetSubsystem<UDreamWorldSubsystem>();
			Weather = World->GetSubsystem<UDreamWeatherSubsystem>();
			Surface = World->GetSubsystem<UDreamSurfaceSubsystem>();
			FActorSpawnParameters Params;
			Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
			ADreamWeather* Actor = World->SpawnActor<ADreamWeather>(Params);
			if (Actor && Selector)
			{
				Selector->Rename(nullptr, Actor);
				Actor->Selector = Selector;
			}
			FDreamWorldClockConfig Config;
			Config.StartTime = FDateTime(2026, 6, 21, 0, 0, 0);
			Config.bAnimate = false;
			Config.Seed = 5;
			Clock->ApplyClockConfig(Config, true);
			Clock->ReportView(FVector(0, 0, 100), FRotator::ZeroRotator, true);
		}
		~FSurfaceWorld()
		{
			if (World)
			{
				World->DestroyWorld(false);
			}
		}

		/** Sets the clock and runs a frame there. */
		void GoTo(double Seconds)
		{
			Clock->SetWorldSeconds(Seconds);
			Clock->Tick(0.0f);
		}

		/** Runs the clock forward to Seconds a step at a time, a frame at each (the way a session sees the weather). */
		void RunTo(double Seconds)
		{
			const double Step = UDreamSurfaceSubsystem::StepSeconds();
			for (double T = Clock->GetWorldSeconds() + Step; T < Seconds; T += Step)
			{
				GoTo(T);
			}
			GoTo(Seconds);
		}
	};

	FDreamSurfaceDrive Drive(float Rain, float Snow, float Temperature, float Wind, float Sun, float Dust)
	{
		FDreamSurfaceDrive D;
		D.Rain = Rain;
		D.Snow = Snow;
		D.Temperature = Temperature;
		D.Wind = Wind;
		D.Sunlight = Sun;
		D.Dust = Dust;
		return D;
	}

	FDreamSurfaceState Run(FDreamSurfaceState S, const FDreamSurfaceDrive& D, float Hours, int32 Steps)
	{
		const FDreamSurfaceRates& Rates = UDreamSurfaceSettings::Get()->Rates;
		for (int32 i = 0; i < Steps; ++i)
		{
			S = UDreamSurfaceSubsystem::Step(S, D, Rates, Hours / Steps);
		}
		return S;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDreamSurfaceFormulaTest, "DreamSurface.Formula", DreamSurface::Tests::Flags)
bool FDreamSurfaceFormulaTest::RunTest(const FString& Parameters)
{
	using namespace DreamSurface::Tests;
	const FDreamSurfaceState Dry;

	// Rain: the film soaks first, the hollows only fill once it has.
	const FDreamSurfaceState TenMinutes = Run(Dry, Drive(1, 0, 15, 0, 0, 0), 1.0f / 6.0f, 4);
	TestTrue(TEXT("ten minutes of heavy rain wet a surface"), TenMinutes.Wetness > 0.5f);
	TestTrue(TEXT("but fill no hollows yet"), TenMinutes.Water < 0.05f);
	const FDreamSurfaceState TwoHours = Run(Dry, Drive(1, 0, 15, 0, 0, 0), 2.0f, 8);
	TestTrue(TEXT("two hours soak it"), TwoHours.Wetness > 0.99f);
	TestTrue(TEXT("and fill the hollows"), TwoHours.Water > 0.6f);

	// Drying: in the sun and wind faster than in the shade; the film before the puddles.
	const FDreamSurfaceState Sunny = Run(TwoHours, Drive(0, 0, 25, 0.5f, 1, 0), 1.0f, 4);
	const FDreamSurfaceState Shade = Run(TwoHours, Drive(0, 0, 25, 0.0f, 0, 0), 1.0f, 4);
	TestTrue(TEXT("an hour of sun dries the film"), Sunny.Wetness < 0.4f);
	TestTrue(TEXT("the shade dries slower"), Shade.Wetness > Sunny.Wetness + 0.2f);
	TestTrue(TEXT("puddles outlast the film"), Sunny.Water > Sunny.Wetness);
	TestTrue(TEXT("nothing dries while it rains"), Run(TwoHours, Drive(0.5f, 0, 25, 1, 1, 0), 1.0f, 4).Wetness > 0.99f);

	// Snow: settles in the cold, not on warm ground; melts above freezing and the melt wets.
	const FDreamSurfaceState Snowed = Run(Dry, Drive(0, 1, -5, 0, 0, 0), 6.0f, 24);
	TestTrue(TEXT("six hours of heavy snow lay a full cover"), Snowed.Snow > 0.95f);
	TestTrue(TEXT("snow on warm ground melts as it lands"), Run(Dry, Drive(0, 1, 6, 0, 0, 0), 2.0f, 8).Snow < 0.01f);
	TestTrue(TEXT("snow holds below freezing"), Run(Snowed, Drive(0, 0, -5, 0, 1, 0), 12.0f, 48).Snow > 0.95f);
	const FDreamSurfaceState Thawed = Run(Snowed, Drive(0, 0, 10, 0, 1, 0), 4.0f, 16);
	TestTrue(TEXT("a sunny thaw melts it"), Thawed.Snow < Snowed.Snow - 0.3f);
	TestTrue(TEXT("and the melt wets the ground"), Thawed.Wetness > 0.5f);

	// Dust: a dust storm lays it, rain washes it.
	const FDreamSurfaceState Dusty = Run(Dry, Drive(0, 0, 30, 1, 1, 1), 2.0f, 8);
	TestTrue(TEXT("a dust storm covers a surface"), Dusty.Dust > 0.9f);
	TestTrue(TEXT("rain washes the dust off"), Run(Dusty, Drive(1, 0, 20, 0, 0, 0), 0.5f, 4).Dust < 0.1f);

	// One long step and many short ones agree while the weather holds.
	const FDreamSurfaceDrive Mixed = Drive(0.3f, 0.2f, 1.0f, 0.4f, 0.5f, 0.2f);
	const FDreamSurfaceState One = Run(Dry, Mixed, 0.25f, 1);
	const FDreamSurfaceState Many = Run(Dry, Mixed, 0.25f, 16);
	TestTrue(TEXT("a step's length barely matters"), One.Equals(Many, 0.03f));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDreamSurfaceWeatherAtTest, "DreamSurface.WeatherAtTime", DreamSurface::Tests::Flags)
bool FDreamSurfaceWeatherAtTest::RunTest(const FString& Parameters)
{
	using namespace DreamSurface::Tests;
	UDreamWeatherSelector_Fixed* Fixed = NewObject<UDreamWeatherSelector_Fixed>();
	Fixed->Preset = Rain();
	FSurfaceWorld W(Fixed);
	if (!TestNotNull(TEXT("weather"), W.Weather))
	{
		return false;
	}
	W.GoTo(At(14.0));
	const FVector P(500, 0, 0);
	const FDreamWeatherSample Now = W.Weather->SampleWeather(P);
	const FDreamWeatherSample Same = W.Weather->SampleWeatherAt(P, W.Weather->GetGlobalValues(), W.Clock->GetWorldSeconds());
	TestEqual(TEXT("now through SampleWeatherAt is SampleWeather: rain"), Same.Rain, Now.Rain);
	TestEqual(TEXT("temperature"), Same.Temperature, Now.Temperature);
	TestEqual(TEXT("exposure"), Same.Exposure, Now.Exposure);
	// The same weather twelve hours earlier: the small hours are colder than the afternoon.
	const FDreamWeatherSample Night = W.Weather->SampleWeatherAt(P, W.Weather->GetGlobalValues(), At(2.0));
	TestTrue(TEXT("a past moment has that moment's temperature"), Night.Temperature < Now.Temperature - 1.0f);
	TestEqual(TEXT("and the weather given"), Night.Precipitation, Now.Precipitation);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDreamSurfaceTimelineTest, "DreamSurface.Timeline", DreamSurface::Tests::Flags)
bool FDreamSurfaceTimelineTest::RunTest(const FString& Parameters)
{
	using namespace DreamSurface::Tests;
	// Six hours clear, six hours rain, repeating.
	FSurfaceWorld Lived(Sequence({ { Clear(), 6.0f }, { Rain(), 6.0f } }));
	FSurfaceWorld Jumped(Sequence({ { Clear(), 6.0f }, { Rain(), 6.0f } }));
	if (!TestNotNull(TEXT("surface"), Lived.Surface) || !TestNotNull(TEXT("surface"), Jumped.Surface))
	{
		return false;
	}
	const FVector P(0, 0, 0);
	// One world lives through a day step by step, the other jumps straight to its end.
	Lived.GoTo(At(0.0));
	Lived.RunTo(At(30.0));
	Jumped.GoTo(At(30.0));
	TestTrue(TEXT("the surfaces run"), Lived.Surface->IsActive());
	TestTrue(TEXT("the session records the weather it sees"), Lived.Surface->GetNumRecordedSteps() > 10);

	const double Start = FPlatformTime::Seconds();
	const FDreamSurfaceState A = Lived.Surface->SampleSurface(P);
	AddInfo(FString::Printf(TEXT("one SampleSurface: %.3f ms (%d steps)"), (FPlatformTime::Seconds() - Start) * 1000.0, UDreamSurfaceSubsystem::MemorySteps()));
	const FDreamSurfaceState B = Jumped.Surface->SampleSurface(P);
	TestTrue(TEXT("asking twice gives the same"), A.Equals(Lived.Surface->SampleSurface(P), 0.0f));
	TestTrue(TEXT("a world that jumped there agrees with one that lived there"), A.Equals(B, 1.0e-5f));

	// The schedule's rain runs from hour 6 to 12, 18 to 24, 30 to 36.
	const FDreamSurfaceState Raining = Lived.Surface->SampleSurfaceAt(P, At(35.0));
	const FDreamSurfaceState AfterRain = Lived.Surface->SampleSurfaceAt(P, At(38.0));
	const FDreamSurfaceState Before = Lived.Surface->SampleSurfaceAt(P, At(29.0));
	TestTrue(TEXT("soaked deep in the rain"), Raining.Wetness > 0.95f && Raining.Water > 0.5f);
	TestTrue(TEXT("drying after it"), AfterRain.Wetness < Raining.Wetness);
	TestTrue(TEXT("still drying before the next"), Before.Wetness < 0.5f);
	TestTrue(TEXT("the view's open-sky state follows"), Lived.Surface->GetViewState().Equals(Lived.Surface->SampleSurface(FVector(0, 0, 100)), 1.0e-4f));

	// A jump back: what was seen after it is forgotten, and the past still gives the same.
	Lived.GoTo(At(20.0));
	TestTrue(TEXT("a jump back keeps the past"), Lived.Surface->SampleSurfaceAt(P, At(19.0)).Equals(Jumped.Surface->SampleSurfaceAt(P, At(19.0)), 1.0e-5f));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDreamSurfaceSnowTest, "DreamSurface.SnowAndDust", DreamSurface::Tests::Flags)
bool FDreamSurfaceSnowTest::RunTest(const FString& Parameters)
{
	using namespace DreamSurface::Tests;
	FSurfaceWorld W(Sequence({ { Blizzard(), 12.0f }, { Thaw(), 12.0f }, { DustStorm(), 12.0f }, { Clear(), 12.0f } }));
	if (!TestNotNull(TEXT("surface"), W.Surface))
	{
		return false;
	}
	const FVector P(0, 0, 0);
	W.GoTo(At(96.0));
	// The cycle runs on the world's own clock: find the hour the blizzard begins.
	double Begin = -1.0;
	for (double H = 48.0; H < 96.0 && Begin < 0.0; H += 0.25)
	{
		const float Before = W.Weather->EvaluateSchedule(At(H - 0.25)).Get(FDreamWeatherChannels::SnowRatio);
		const float After = W.Weather->EvaluateSchedule(At(H)).Get(FDreamWeatherChannels::SnowRatio);
		Begin = Before < 0.5f && After >= 0.5f ? H : -1.0;
	}
	if (!TestTrue(TEXT("the schedule has a blizzard"), Begin > 0.0))
	{
		return false;
	}
	const FDreamSurfaceState Blizzard = W.Surface->SampleSurfaceAt(P, At(Begin + 11.0));
	TestTrue(TEXT("a blizzard lays deep snow"), Blizzard.Snow > 0.9f);
	W.GoTo(At(Begin + 11.0));
	TestNearlyEqual(TEXT("its depth"), W.Surface->GetSnowDepthAt(P), Blizzard.Snow * UDreamSurfaceSettings::Get()->MaxSnowDepth, 1.0e-3f);
	// Then the thaw: it melts, and the melt wets the ground under it.
	const FDreamSurfaceState Thawing = W.Surface->SampleSurfaceAt(P, At(Begin + 15.0));
	TestTrue(TEXT("the thaw melts the snow"), Thawing.Snow < Blizzard.Snow - 0.3f);
	TestTrue(TEXT("and wets the ground"), Thawing.Wetness > 0.3f);
	const FDreamSurfaceState Dusty = W.Surface->SampleSurfaceAt(P, At(Begin + 35.0));
	TestTrue(TEXT("a dust storm leaves dust"), Dusty.Dust > 0.8f);
	AddInfo(FString::Printf(TEXT("blizzard from hour %.2f: snow %.2f; thaw +3 h: snow %.2f wet %.2f; dust storm: dust %.2f"),
		Begin, Blizzard.Snow, Thawing.Snow, Thawing.Wetness, Dusty.Dust));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDreamSurfaceShelterTest, "DreamSurface.Shelter", DreamSurface::Tests::Flags)
bool FDreamSurfaceShelterTest::RunTest(const FString& Parameters)
{
	using namespace DreamSurface::Tests;
	UDreamWeatherSelector_Fixed* Fixed = NewObject<UDreamWeatherSelector_Fixed>();
	Fixed->Preset = Rain();
	FSurfaceWorld W(Fixed);
	if (!TestNotNull(TEXT("surface"), W.Surface))
	{
		return false;
	}
	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	ADreamWeatherShelter* Hut = W.World->SpawnActor<ADreamWeatherShelter>(FVector(0, 0, 100), FRotator::ZeroRotator, Params);
	Hut->Shelter->Extent = FVector(300, 300, 200);
	W.GoTo(At(12.0));
	const FDreamSurfaceState Inside = W.Surface->SampleSurface(FVector(0, 0, 0));
	const FDreamSurfaceState Outside = W.Surface->SampleSurface(FVector(3000, 0, 0));
	TestTrue(TEXT("soaked out in the rain"), Outside.Wetness > 0.95f);
	TestTrue(TEXT("dry under the roof"), Inside.Wetness < 0.01f && Inside.Water < 0.01f);
	TestTrue(TEXT("the view's state is the open sky's, roof or not"), W.Surface->GetViewState().Wetness > 0.95f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDreamSurfaceForcedTest, "DreamSurface.ForcedWeather", DreamSurface::Tests::Flags)
bool FDreamSurfaceForcedTest::RunTest(const FString& Parameters)
{
	using namespace DreamSurface::Tests;
	UDreamWeatherSelector_Fixed* Fixed = NewObject<UDreamWeatherSelector_Fixed>();
	Fixed->Preset = Clear();
	FSurfaceWorld W(Fixed);
	if (!TestNotNull(TEXT("surface"), W.Surface))
	{
		return false;
	}
	const FVector P(0, 0, 0);
	W.GoTo(At(10.0));
	TestTrue(TEXT("dry under a clear schedule"), W.Surface->SampleSurface(P).Wetness < 0.01f);
	// Forced rain is what this session saw: the steps it ran through remember it, though the schedule never rained.
	W.Weather->ForceWeather(Rain(), 0.0f);
	W.Clock->Tick(1.0f);
	W.RunTo(At(11.0));
	const FDreamSurfaceState Forced = W.Surface->SampleSurface(P);
	TestTrue(TEXT("forced rain wets the ground"), Forced.Wetness > 0.9f);
	TestTrue(TEXT("asked again, the same"), Forced.Equals(W.Surface->SampleSurface(P), 0.0f));
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
