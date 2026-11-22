// Copyright DreamMoon. All Rights Reserved.

#include "DreamSurfaceHlslShim.h"
#include "DreamSurfaceInteraction.h"
#include "DreamSurfaceMaps.h"
#include "DreamSurfaceMask.h"
#include "DreamSurfaceRoofCapture.h"
#include "DreamSurfaceSettings.h"
#include "DreamSurfaceSubsystem.h"

#include "DreamWeather.h"
#include "DreamWeatherChannels.h"
#include "DreamWeatherModule.h"
#include "DreamWeatherPreset.h"
#include "DreamWeatherSelector.h"
#include "DreamWeatherShelter.h"
#include "DreamWeatherSubsystem.h"
#include "DreamWeatherZone.h"
#include "DreamWorldCelestial.h"
#include "DreamWorldSubsystem.h"

#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Engine/World.h"
#include "Misc/App.h"
#include "Misc/ScopeExit.h"
#include "RenderingThread.h"
#include "TextureResource.h"
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
	const FDreamSurfaceState WarmSnow = Run(Dry, Drive(0, 1, 6, 0, 0, 0), 2.0f, 8);
	TestTrue(TEXT("snow on warm ground melts as it lands"), WarmSnow.Snow < 0.01f);
	TestTrue(TEXT("and wets it as rain would"), WarmSnow.Wetness > 0.99f);
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

namespace DreamSurface::Tests
{
	struct FGlassPatch
	{
		float Water = 0.0f;     // the share of the patch under water
		float Running = 0.0f;   // the share crossed by running drops
		float MaxSlope = 0.0f;
		uint32 Hash = 0;        // of the whole field, to compare two moments
	};

	/** The rain on a metre of glass, sampled every 5 mm. */
	static FGlassPatch GlassPatch(float Time, float Rain, float Wet, float Steep)
	{
		using namespace DreamWind::Hlsl;
		FGlassPatch P;
		int32 Count = 0;
		for (float Y = 0.0f; Y < 100.0f; Y += 0.5f)
		{
			for (float X = 0.0f; X < 100.0f; X += 0.5f)
			{
				const float4 R = DSF_GlassRain(float2(X, Y), Time, Rain, Wet, Steep);
				P.Water += R.z > 0.3f ? 1.0f : 0.0f;
				P.Running += R.w > 0.3f ? 1.0f : 0.0f;
				P.MaxSlope = FMath::Max(P.MaxSlope, FMath::Max(FMath::Abs(R.x), FMath::Abs(R.y)));
				P.Hash = HashCombineFast(P.Hash, GetTypeHash(FMath::RoundToInt32(R.z * 1000.0f)));
				++Count;
			}
		}
		P.Water /= Count;
		P.Running /= Count;
		return P;
	}

	/** Where the drops running down one column of glass are (the mean height of their heads, cm), in a window lower
	 *  than the shortest gap between two drops; false when the window holds none. */
	static bool RunnerHeight(float Column, float Time, float& OutHeight)
	{
		using namespace DreamWind::Hlsl;
		constexpr float Width = 2.5f;
		double Sum = 0.0;
		int32 Count = 0;
		for (float Y = 0.0f; Y < 45.0f; Y += 0.05f)
		{
			for (float X = Column * Width + 0.01f; X < (Column + 1.0f) * Width; X += 0.05f)
			{
				if (DSF_GlassRunners(float2(X, Y), Time, 1.0f, Width).z > 0.9f)
				{
					Sum += Y;
					++Count;
				}
			}
		}
		OutHeight = Count ? float(Sum / Count) : 0.0f;
		return Count > 0;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDreamSurfaceGlassRainTest, "DreamSurface.GlassRain", DreamSurface::Tests::Flags)
bool FDreamSurfaceGlassRainTest::RunTest(const FString& Parameters)
{
	using namespace DreamSurface::Tests;

	const FGlassPatch Dry = GlassPatch(3.0f, 0.0f, 0.0f, 1.0f);
	TestTrue(TEXT("a dry pane has no drops"), Dry.Water == 0.0f && Dry.MaxSlope == 0.0f);

	const FGlassPatch Pouring = GlassPatch(3.0f, 1.0f, 1.0f, 1.0f);
	const FGlassPatch Drizzle = GlassPatch(3.0f, 0.2f, 0.2f, 1.0f);
	AddInfo(FString::Printf(TEXT("water on the pane: pouring %.3f, drizzle %.3f; running %.3f"), Pouring.Water, Drizzle.Water, Pouring.Running));
	TestTrue(TEXT("heavy rain covers part of an upright pane, not all of it"), Pouring.Water > 0.08f && Pouring.Water < 0.7f);
	TestTrue(TEXT("a drizzle fewer drops"), Drizzle.Water < Pouring.Water * 0.7f && Drizzle.Water > 0.0f);
	TestTrue(TEXT("drops run down it"), Pouring.Running > 0.005f);
	TestTrue(TEXT("the water's slope stays bounded"), Pouring.MaxSlope < 4.0f);
	TestTrue(TEXT("the drops come and go while it rains"), GlassPatch(9.0f, 1.0f, 1.0f, 1.0f).Hash != Pouring.Hash);

	const FGlassPatch Flat = GlassPatch(3.0f, 1.0f, 1.0f, 0.0f);
	TestTrue(TEXT("on a flat pane drops sit"), Flat.Water > 0.05f);
	TestTrue(TEXT("but none run"), Flat.Running == 0.0f);

	// After the rain: the sitting drops stay put while the pane is wet, and go as it dries.
	const FGlassPatch After = GlassPatch(3.0f, 0.0f, 0.8f, 1.0f);
	TestTrue(TEXT("drops stay on a wet pane after the rain"), After.Water > 0.05f && After.Running == 0.0f);
	TestTrue(TEXT("and do not move"), GlassPatch(40.0f, 0.0f, 0.8f, 1.0f).Hash == After.Hash);
	TestTrue(TEXT("fewer as it dries"), GlassPatch(3.0f, 0.0f, 0.2f, 1.0f).Water < After.Water * 0.7f);

	// A running drop goes down, at a speed between the slowest and the fastest a drop runs (6..20 cm/s).
	bool bMeasured = false;
	for (float Column = 0.0f; Column < 60.0f && !bMeasured; Column += 1.0f)
	{
		for (float Time = 0.0f; Time < 10.0f && !bMeasured; Time += 0.7f)
		{
			float Before = 0.0f, Later = 0.0f;
			if (RunnerHeight(Column, Time, Before) && RunnerHeight(Column, Time + 0.2f, Later) && Before > 8.0f && Before < 40.0f)
			{
				bMeasured = true;
				const float Speed = (Before - Later) / 0.2f;
				AddInfo(FString::Printf(TEXT("column %.0f: a drop at %.1f cm runs at %.1f cm/s"), Column, Before, Speed));
				TestTrue(TEXT("a running drop goes down at its speed"), Speed > 6.0f * 0.8f && Speed < 20.0f * 1.2f);
			}
		}
	}
	TestTrue(TEXT("a running drop was found to measure"), bMeasured);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDreamSurfaceWhereWaterTest, "DreamSurface.WhereWaterAndSnowLie", DreamSurface::Tests::Flags)
bool FDreamSurfaceWhereWaterTest::RunTest(const FString& Parameters)
{
	using namespace DreamWind::Hlsl;

	// The ground's lows: a dip is low, a rise is not, a box beside the point does not make it a dip.
	TestTrue(TEXT("a dip lies low"), DSF_GroundLow(-20.0f, float4(0, 0, 0, 0), float4(0, 0, 0, 0)) > 15.0f);
	TestTrue(TEXT("a crest does not"), DSF_GroundLow(20.0f, float4(0, 0, 0, 0), float4(0, 0, 0, 0)) < 0.0f);
	TestTrue(TEXT("a box beside flat ground is no dip"), FMath::Abs(DSF_GroundLow(0.0f, float4(100, 0, 0, 0), float4(0, 0, 120, 0))) < 1.0f);

	// Full of water, flat hard ground holds puddles only in its deepest hollows; a real dip all over; soft ground faintly.
	int32 Count = 0;
	float Flat = 0.0f, InDip = 0.0f, Soft = 0.0f;
	for (float Y = 0.0f; Y < 3000.0f; Y += 10.0f)
	{
		for (float X = 0.0f; X < 3000.0f; X += 10.0f)
		{
			const float Hollow = DSF_Hollow(X, Y, 150.0f);
			Flat += DSF_PuddleCover(1.0f, 1.0f, DSF_PuddleDepth(Hollow, 0.0f), 0.0f);
			InDip += DSF_PuddleCover(1.0f, 1.0f, DSF_PuddleDepth(Hollow, 30.0f), 0.0f);
			Soft += DSF_PuddleCover(1.0f, 1.0f, DSF_PuddleDepth(Hollow, 0.0f), 1.0f);
			++Count;
		}
	}
	Flat /= Count;
	InDip /= Count;
	Soft /= Count;
	AddInfo(FString::Printf(TEXT("puddles when full: flat %.3f, in a dip %.3f, flat soft ground %.3f"), Flat, InDip, Soft));
	TestTrue(TEXT("flat ground: puddles in the deepest hollows, not everywhere"), Flat > 0.02f && Flat < 0.2f);
	TestTrue(TEXT("a dip a quarter of a metre deep holds water all over"), InDip > 0.95f);
	TestTrue(TEXT("soft ground's puddles are faint"), Soft < Flat * 0.4f);
	TestTrue(TEXT("no water, no puddles"), DSF_PuddleCover(0.0f, 1.0f, 1.0f, 0.0f) < 0.01f);

	// Snow: a full cover is whole on what faces up, a steep slope keeps less; where it thins out it breaks up over
	// metres (soft, patchy), not along one line.
	float MinFull = 1.0f;
	int32 Partial = 0, Thin = 0;
	for (float X = 0.0f; X < 6000.0f; X += 25.0f)
	{
		MinFull = FMath::Min(MinFull, DSF_SnowCover(1.0f, 1.0f, DSF_Hollow(X, 0.0f, 260.0f), DSF_SnowEdge(X, 0.0f)));
		const float Half = DSF_SnowCover(0.4f, 1.0f, DSF_Hollow(X, 0.0f, 260.0f), DSF_SnowEdge(X, 0.0f));
		Partial += (Half > 0.1f && Half < 0.9f) ? 1 : 0;
		++Thin;
	}
	TestTrue(TEXT("a full cover is whole on flat ground"), MinFull > 0.99f);
	TestTrue(TEXT("a thinning cover is soft and patchy"), Partial > Thin / 5);
	TestTrue(TEXT("a steep slope keeps less snow"), DSF_SnowCover(0.6f, 0.5f, 0.5f, 0.5f) < DSF_SnowCover(0.6f, 1.0f, 0.5f, 0.5f));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDreamSurfaceHollowPatternTest, "DreamSurface.HollowPattern", DreamSurface::Tests::Flags)
bool FDreamSurfaceHollowPatternTest::RunTest(const FString& Parameters)
{
	using namespace DreamWind::Hlsl;

	// The hollow pattern (puddles, the snow's edge, the wander of a snow border) is continuous: no step where it crosses a
	// line of its lattice, of either octave, near the origin or kilometres out. The lattice corners are hashed in integer
	// arithmetic (DSF_LatticeHash), so every cell sharing a corner gets the same value from it on the GPU too.
	const float Scale = 600.0f;
	float WorstStep = 0.0f;
	for (const float Far : { 0.0f, 250000.0f, -1200000.0f })
	{
		for (int32 Line = -6; Line <= 6; ++Line)
		{
			for (const float Octave : { 1.0f, 2.03f })
			{
				// Where the octave's lattice has a line: X / Scale * Octave (+ 17 in the second) is a whole number.
				const float Offset = Octave > 1.5f ? 17.0f : 0.0f;
				const float N = FMath::Floor(Far / Scale * Octave + Offset) + float(Line);
				const float X = (N - Offset) / Octave * Scale;
				for (const float Y : { 37.0f, 911.0f, -2203.0f })
				{
					WorstStep = FMath::Max(WorstStep, FMath::Abs(DSF_Hollow(X + 0.5f, Y, Scale) - DSF_Hollow(X - 0.5f, Y, Scale)));
					WorstStep = FMath::Max(WorstStep, FMath::Abs(DSF_Hollow(Y, X + 0.5f, Scale) - DSF_Hollow(Y, X - 0.5f, Scale)));
				}
			}
		}
	}
	AddInfo(FString::Printf(TEXT("largest change across a centimetre at a lattice line: %.4f"), WorstStep));
	TestTrue(TEXT("no step at the lattice lines"), WorstStep < 0.02f);

	// It spans its range evenly: about half on either side of the middle, reaching both ends.
	float Sum = 0.0f, Lo = 1.0f, Hi = 0.0f;
	int32 Count = 0;
	for (float Y = 0.0f; Y < 20000.0f; Y += 97.0f)
	{
		for (float X = 0.0f; X < 20000.0f; X += 89.0f)
		{
			const float H = DSF_Hollow(X, Y, Scale);
			Sum += H;
			Lo = FMath::Min(Lo, H);
			Hi = FMath::Max(Hi, H);
			++Count;
		}
	}
	const float Mean = Sum / float(Count);
	AddInfo(FString::Printf(TEXT("hollow pattern: mean %.3f, range %.3f..%.3f"), Mean, Lo, Hi));
	TestTrue(TEXT("centred on the middle"), FMath::Abs(Mean - 0.5f) < 0.06f);
	TestTrue(TEXT("reaching towards both ends"), Lo < 0.25f && Hi > 0.75f);
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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDreamSurfaceMaskTest, "DreamSurface.Mask", DreamSurface::Tests::Flags)
bool FDreamSurfaceMaskTest::RunTest(const FString& Parameters)
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
	// A circle 10 m across kept dry, its edge fading over 2 m; a box beside it that doubles the standing water.
	ADreamSurfaceMask* Dry = W.World->SpawnActor<ADreamSurfaceMask>(FVector(0, 0, 0), FRotator::ZeroRotator, Params);
	Dry->Mask->Shape = EDreamSurfaceMaskShape::Circle;
	Dry->Mask->Extent = FVector2D(500.0, 500.0);
	Dry->Mask->FadeDistance = 200.0f;
	Dry->Mask->Wetness = 0.0f;
	Dry->Mask->Water = 0.0f;
	ADreamSurfaceMask* Gutter = W.World->SpawnActor<ADreamSurfaceMask>(FVector(3000, 0, 0), FRotator(0, 30, 0), Params);
	Gutter->Mask->Extent = FVector2D(200.0, 50.0);
	Gutter->Mask->FadeDistance = 0.0f;
	Gutter->Mask->Wetness = 1.0f;
	Gutter->Mask->Water = 2.0f;
	Gutter->Mask->Snow = 1.0f;
	Gutter->Mask->Dust = 1.0f;
	W.GoTo(At(10.75));

	const FDreamSurfaceState Inside = W.Surface->SampleSurface(FVector(100, 100, 0));
	const FDreamSurfaceState Edge = W.Surface->SampleSurface(FVector(600, 0, 0));
	const FDreamSurfaceState Open = W.Surface->SampleSurface(FVector(-2000, 0, 0));
	TestTrue(TEXT("it rains outside the brush"), Open.Wetness > 0.95f);
	TestEqual(TEXT("dry inside"), Inside.Wetness, 0.0f);
	TestTrue(TEXT("half way across the soft edge"), Edge.Wetness > 0.3f && Edge.Wetness < 0.7f);
	TestNearlyEqual(TEXT("the brush's own multipliers there"), W.Surface->GetMaskAt(FVector(600, 0, 0)).X, 0.5f, 1.0e-3f);
	// The gutter doubles the water, held to 1; a point just outside its turned box is untouched.
	const FDreamSurfaceState InGutter = W.Surface->SampleSurface(FVector(3000, 0, 0));
	const FDreamSurfaceState BesideGutter = W.Surface->SampleSurface(FVector(3000, 120, 0));
	TestTrue(TEXT("the gutter holds more water"), InGutter.Water >= BesideGutter.Water);
	TestTrue(TEXT("never above full"), InGutter.Water <= 1.0f);
	TestNearlyEqual(TEXT("the turned box leaves a point beside it"), W.Surface->GetMaskAt(FVector(3000, 120, 0)).Y, 1.0f, 1.0e-4f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDreamSurfaceRoofShelterTest, "DreamSurface.RoofCapture.Shelter", DreamSurface::Tests::Flags)
bool FDreamSurfaceRoofShelterTest::RunTest(const FString& Parameters)
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
	ADreamSurfaceRoofCapture* Box = W.World->SpawnActor<ADreamSurfaceRoofCapture>(FVector(0, 0, 200), FRotator::ZeroRotator, Params);
	Box->Roof->Extent = FVector(400.0, 400.0, 200.0);
	Box->Roof->Resolution = 16;
	// A roof at 300 cm over the box's +x half (the image's top half: rows 0..7), ground at 0 elsewhere.
	TArray<float> Heights;
	Heights.Init(0.0f, 16 * 16);
	for (int32 Y = 0; Y < 8; ++Y)
	{
		for (int32 X = 0; X < 16; ++X)
		{
			Heights[Y * 16 + X] = 300.0f;
		}
	}
	Box->Roof->SetHeightsForTest(Heights);
	W.GoTo(At(10.75));

	TestNearlyEqual(TEXT("the roof's height over the +x half"), Box->Roof->GetHeightAt(200.0, 0.0), 300.0f, 1.0e-3f);
	TestNearlyEqual(TEXT("the ground over the -x half"), Box->Roof->GetHeightAt(-200.0, 0.0), 0.0f, 1.0e-3f);
	TestTrue(TEXT("nothing outside the box"), Box->Roof->GetHeightAt(900.0, 0.0) < -1.0e8f);
	TestEqual(TEXT("under the roof: sheltered"), W.Weather->GetExposureAt(FVector(200, 0, 0)), 0.0f);
	TestEqual(TEXT("on the ground beside it: open"), W.Weather->GetExposureAt(FVector(-200, 0, 0)), 1.0f);
	TestEqual(TEXT("on the roof itself: open"), W.Weather->GetExposureAt(FVector(200, 0, 300)), 1.0f);
	TestTrue(TEXT("dry under the roof"), W.Surface->SampleSurface(FVector(200, 0, 0)).Wetness < 0.01f);
	TestTrue(TEXT("wet beside it"), W.Surface->SampleSurface(FVector(-200, 0, 0)).Wetness > 0.95f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDreamSurfaceGpuRoofTest, "DreamSurface.Gpu.RoofCapture", DreamSurface::Tests::Flags)
bool FDreamSurfaceGpuRoofTest::RunTest(const FString& Parameters)
{
	using namespace DreamSurface::Tests;
	if (!FApp::CanEverRender() || GUsingNullRHI)
	{
		AddInfo(TEXT("No RHI: skipped (run it in an editor that renders)."));
		return true;
	}
	UDreamWeatherSelector_Fixed* Fixed = NewObject<UDreamWeatherSelector_Fixed>();
	Fixed->Preset = Rain();
	FSurfaceWorld W(Fixed);
	if (!TestNotNull(TEXT("surface"), W.Surface))
	{
		return false;
	}
	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	// A slab 3 m up over the +x half of a capture box turned 30 degrees.
	const FRotator Turn(0.0, 30.0, 0.0);
	AStaticMeshActor* Slab = W.World->SpawnActor<AStaticMeshActor>(Turn.RotateVector(FVector(200, 0, 300)), Turn, Params);
	Slab->GetStaticMeshComponent()->SetMobility(EComponentMobility::Movable);
	Slab->GetStaticMeshComponent()->SetStaticMesh(LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube")));
	Slab->SetActorScale3D(FVector(4.0, 8.0, 0.2));
	ADreamSurfaceRoofCapture* Box = W.World->SpawnActor<ADreamSurfaceRoofCapture>(FVector(0, 0, 200), Turn, Params);
	Box->Roof->Extent = FVector(400.0, 400.0, 200.0);
	Box->Roof->Resolution = 64;
	W.Clock->ReportView(FVector(0, 0, 0), FRotator::ZeroRotator, true);
	W.GoTo(At(10.75));
	Box->Roof->CaptureNow();
	TestTrue(TEXT("it captured"), Box->Roof->GetCaptureCount() > 0);

	// The slab's top is at 310 cm (3 m, 20 cm thick).
	const FVector OverSlab = Turn.RotateVector(FVector(200, 0, 0));
	const FVector Beside = Turn.RotateVector(FVector(-200, 0, 0));
	const float Top = Box->Roof->GetHeightAt(OverSlab.X, OverSlab.Y);
	AddInfo(FString::Printf(TEXT("captured height over the slab %.1f, beside it %.1f"), Top, Box->Roof->GetHeightAt(Beside.X, Beside.Y)));
	TestTrue(TEXT("the slab's top, captured"), FMath::Abs(Top - 310.0f) < 6.0f);
	TestTrue(TEXT("nothing beside it (the image is the right way round)"), Box->Roof->GetHeightAt(Beside.X, Beside.Y) < 50.0f);
	TestTrue(TEXT("sheltered under the slab"), W.Weather->GetExposureAt(OverSlab) < 0.01f);

	// The cover map has the slab too.
	W.Clock->Tick(0.0f);
	W.Clock->Tick(1.0f);
	FlushRenderingCommands();
	const FDreamSurfaceMaps* Maps = W.Surface->GetMaps();
	UTextureRenderTarget2D* Cover = Maps ? Maps->GetCoverMap() : nullptr;
	if (!TestNotNull(TEXT("cover map (run the Setup)"), Cover))
	{
		return false;
	}
	TArray<FLinearColor> Pixels;
	Cover->GameThread_GetRenderTargetResource()->ReadLinearColorPixels(Pixels);
	const int32 Size = Cover->SizeX;
	const FVector2D UV = (FVector2D(OverSlab) - Maps->GetNearCenter()) / Maps->GetNearExtent() + 0.5;
	const FLinearColor Texel = Pixels[FMath::Clamp(int32(UV.Y * Size), 0, Size - 1) * Size + FMath::Clamp(int32(UV.X * Size), 0, Size - 1)];
	AddInfo(FString::Printf(TEXT("cover map over the slab: height %.1f, strength %.2f"), Texel.R, Texel.G));
	TestTrue(TEXT("the cover map holds the slab's height"), FMath::Abs(Texel.R - 310.0f) < 6.0f && Texel.G > 0.99f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDreamSurfaceInteractionTest, "DreamSurface.Interaction", DreamSurface::Tests::Flags)
bool FDreamSurfaceInteractionTest::RunTest(const FString& Parameters)
{
	using namespace DreamSurface::Tests;
	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	auto MakeFoot = [&Params](UWorld* World, const FVector& At)
	{
		AActor* Walker = World->SpawnActor<AActor>(AActor::StaticClass(), FTransform(At), Params);
		UDreamSurfaceInteractionComponent* Foot = NewObject<UDreamSurfaceInteractionComponent>(Walker);
		Walker->SetRootComponent(Foot);
		Foot->bTraceGround = false;
		Foot->RegisterComponent();
		Foot->SetWorldLocation(At);
		return Foot;
	};
	{
		// Deep snow: a step finds snow and how deep, and leaves a print to draw.
		UDreamWeatherSelector_Fixed* Fixed = NewObject<UDreamWeatherSelector_Fixed>();
		Fixed->Preset = Blizzard();
		FSurfaceWorld W(Fixed);
		if (!TestNotNull(TEXT("surface"), W.Surface))
		{
			return false;
		}
		W.GoTo(At(12.0));
		UDreamSurfaceInteractionComponent* Foot = MakeFoot(W.World, FVector(100, 100, 0));
		const FDreamSurfaceContact Contact = Foot->PrintNow();
		TestEqual(TEXT("a step in deep snow finds snow"), Contact.Kind, EDreamSurfaceContactKind::Snow);
		TestTrue(TEXT("and how deep"), Contact.SnowDepth > 10.0f);
		TestEqual(TEXT("one print waits to be drawn"), W.Surface->GetPendingStamps().Num(), 2);
		// It prints again once it has moved a step, not before.
		Foot->SetWorldLocation(FVector(105, 100, 0));
		Foot->TickComponent(0.1f, LEVELTICK_All, nullptr);
		TestEqual(TEXT("no print within a step"), Foot->GetPrintCount(), 1);
		Foot->SetWorldLocation(FVector(130, 100, 0));
		Foot->TickComponent(0.1f, LEVELTICK_All, nullptr);
		TestEqual(TEXT("a print a step on"), Foot->GetPrintCount(), 2);
	}
	{
		// After hours of rain: at the bottom of a hollow the foot stands in a puddle, on a crest only on wet ground.
		UDreamWeatherSelector_Fixed* Fixed = NewObject<UDreamWeatherSelector_Fixed>();
		Fixed->Preset = Rain();
		FSurfaceWorld W(Fixed);
		if (!TestNotNull(TEXT("surface"), W.Surface))
		{
			return false;
		}
		W.GoTo(At(12.0));
		FVector Dip(0.0), Crest(0.0);
		float Deepest = -1.0f, Highest = 2.0f;
		for (int32 i = 0; i < 400; ++i)
		{
			const FVector P(i * 37.0, i * 11.0, 0.0);
			// The materials' puddle pattern (DSM_Realistic): small hollows of Puddle Scale.
			const float H = DreamWind::Hlsl::DSF_Hollow(float(P.X) + 57.0f, float(P.Y) - 23.0f, UDreamSurfaceSettings::Get()->PuddleScale);
			if (H > Deepest) { Deepest = H; Dip = P; }
			if (H < Highest) { Highest = H; Crest = P; }
		}
		TestEqual(TEXT("in a hollow: a puddle"), MakeFoot(W.World, Dip)->PrintNow().Kind, EDreamSurfaceContactKind::Puddle);
		TestEqual(TEXT("on a crest: wet ground"), MakeFoot(W.World, Crest)->PrintNow().Kind, EDreamSurfaceContactKind::Wet);
		const FDreamSurfaceContact Contact = W.Surface->SampleContact(Dip, FVector::UpVector);
		TestTrue(TEXT("the puddle's share"), Contact.Puddle > 0.5f);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDreamSurfaceGpuTrailsTest, "DreamSurface.Gpu.Trails", DreamSurface::Tests::Flags)
bool FDreamSurfaceGpuTrailsTest::RunTest(const FString& Parameters)
{
	using namespace DreamSurface::Tests;
	if (!FApp::CanEverRender() || GUsingNullRHI)
	{
		AddInfo(TEXT("No RHI: skipped (run it in an editor that renders)."));
		return true;
	}
	UDreamWeatherSelector_Fixed* Fixed = NewObject<UDreamWeatherSelector_Fixed>();
	Fixed->Preset = Blizzard();
	FSurfaceWorld W(Fixed);
	if (!TestNotNull(TEXT("surface"), W.Surface))
	{
		return false;
	}
	// Every frame draws the maps here (the frames run faster than real time).
	UDreamSurfaceSettings* Settings = GetMutableDefault<UDreamSurfaceSettings>();
	const float Interval = Settings->MapInterval;
	Settings->MapInterval = 0.0f;
	ON_SCOPE_EXIT { Settings->MapInterval = Interval; };

	W.Clock->ReportView(FVector(0, 0, 0), FRotator::ZeroRotator, true);
	W.GoTo(At(12.0));
	const FVector Print(300, -200, 0);
	W.Surface->Stamp(Print, 30.0f, 1.0f, true, true);
	W.Clock->Tick(0.0f);
	FlushRenderingCommands();

	const FDreamSurfaceMaps* Maps = W.Surface->GetMaps();
	UTextureRenderTarget2D* Trails = Maps ? Maps->GetTrailMap() : nullptr;
	if (!TestNotNull(TEXT("trail map (run the Setup)"), Trails))
	{
		return false;
	}
	auto TrailAt = [Trails, Maps](const FVector& P)
	{
		TArray<FLinearColor> Pixels;
		Trails->GameThread_GetRenderTargetResource()->ReadLinearColorPixels(Pixels);
		const int32 Size = Trails->SizeX;
		const FVector2D UV = (FVector2D(P) - Maps->GetTrailCenter()) / UDreamSurfaceSettings::Get()->TrailExtent + 0.5;
		return Pixels[FMath::Clamp(int32(UV.Y * Size), 0, Size - 1) * Size + FMath::Clamp(int32(UV.X * Size), 0, Size - 1)];
	};
	const FLinearColor Fresh = TrailAt(Print);
	const FLinearColor Beside = TrailAt(Print + FVector(200, 0, 0));
	AddInfo(FString::Printf(TEXT("fresh print %.2f (stirred %.2f), beside it %.2f"), Fresh.R, Fresh.G, Beside.R));
	TestTrue(TEXT("the print is pressed in"), Fresh.R > 0.95f);
	TestTrue(TEXT("nothing beside it"), Beside.R < 0.01f);
	// An hour of heavy snow fills it.
	W.GoTo(At(13.0));
	FlushRenderingCommands();
	const FLinearColor Filled = TrailAt(Print);
	AddInfo(FString::Printf(TEXT("after an hour of snow %.2f"), Filled.R));
	TestTrue(TEXT("new snow fills the print"), Filled.R < Fresh.R * 0.6f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDreamSurfaceGpuTest, "DreamSurface.Gpu.MatchesCpu", DreamSurface::Tests::Flags)
bool FDreamSurfaceGpuTest::RunTest(const FString& Parameters)
{
	using namespace DreamSurface::Tests;
	if (!FApp::CanEverRender() || GUsingNullRHI)
	{
		AddInfo(TEXT("No RHI: skipped (run it in an editor that renders)."));
		return true;
	}
	FSurfaceWorld W(Sequence({ { Clear(), 6.0f }, { Rain(), 6.0f } }));
	if (!TestNotNull(TEXT("surface"), W.Surface))
	{
		return false;
	}
	// A zone that keeps the rain off a circle 40 m east: the map applies it per texel, the query at its point.
	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	ADreamWeatherZone* Dry = W.World->SpawnActor<ADreamWeatherZone>(FVector(4000, 0, 0), FRotator::ZeroRotator, Params);
	Dry->Zone->Shape = EDreamWeatherZoneShape::Circle;
	Dry->Zone->Extent = FVector2D(1500, 1500);
	Dry->Zone->BlendDistance = 600.0f;
	Dry->Zone->Channels.Add(Channel_Precipitation, 0.0f);
	// And a cold zone 40 m west where what falls is snow: its snow ratio and temperature reach the map too.
	ADreamWeatherZone* Cold = W.World->SpawnActor<ADreamWeatherZone>(FVector(-4000, 0, 0), FRotator::ZeroRotator, Params);
	Cold->Zone->Shape = EDreamWeatherZoneShape::Circle;
	Cold->Zone->Extent = FVector2D(1500, 1500);
	Cold->Zone->BlendDistance = 600.0f;
	Cold->Zone->Channels.Add(Channel_SnowRatio, 1.0f);
	Cold->Zone->Channels.Add(Channel_Temperature, -25.0f);

	W.Clock->ReportView(FVector(0, 0, 0), FRotator::ZeroRotator, true);
	// An hour and a bit after the rain: drying, the water still standing (values between the ends).
	W.GoTo(At(37.3));
	W.Clock->Tick(0.0f);
	FlushRenderingCommands();

	const FDreamSurfaceMaps* Maps = W.Surface->GetMaps();
	UTextureRenderTarget2D* Near = Maps ? Maps->GetNearMap() : nullptr;
	UTextureRenderTarget2D* Far = Maps ? Maps->GetFarMap() : nullptr;
	if (!TestNotNull(TEXT("near map (run the Setup)"), Near) || !TestNotNull(TEXT("far map"), Far))
	{
		return false;
	}
	TestTrue(TEXT("the maps were drawn and integrated"), Maps->GetIntegrateCount() >= 2);

	auto Check = [this, &W](UTextureRenderTarget2D* Map, const FVector2D& Center, float Extent, const FVector2D& Point, const TCHAR* What)
	{
		TArray<FLinearColor> Pixels;
		Map->GameThread_GetRenderTargetResource()->ReadLinearColorPixels(Pixels);
		const int32 Size = Map->SizeX;
		if (!TestEqual(TEXT("map size"), Pixels.Num(), Size * Map->SizeY))
		{
			return;
		}
		const FVector2D UV = (Point - Center) / Extent + 0.5;
		const int32 X = FMath::Clamp(FMath::FloorToInt32(UV.X * Size), 0, Size - 1);
		const int32 Y = FMath::Clamp(FMath::FloorToInt32(UV.Y * Size), 0, Size - 1);
		const FVector2D Texel = Center + (FVector2D(X + 0.5, Y + 0.5) / Size - 0.5) * Extent;
		const FLinearColor Gpu = Pixels[Y * Size + X];
		const FDreamSurfaceState Cpu = W.Surface->SampleSurface(FVector(Texel, 0.0));
		AddInfo(FString::Printf(TEXT("%s: GPU wet %.3f water %.3f snow %.3f dust %.3f / CPU %.3f %.3f %.3f %.3f"), What,
			Gpu.R, Gpu.G, Gpu.B, Gpu.A, Cpu.Wetness, Cpu.Water, Cpu.Snow, Cpu.Dust));
		TestTrue(FString::Printf(TEXT("%s: the map holds the query's state"), What),
			Cpu.Equals(FDreamSurfaceState::FromVector(FVector4f(Gpu.R, Gpu.G, Gpu.B, Gpu.A)), 0.01f));
	};
	Check(Near, Maps->GetNearCenter(), Maps->GetNearExtent(), FVector2D(-1200, 300), TEXT("near, in the rain"));
	Check(Near, Maps->GetNearCenter(), Maps->GetNearExtent(), FVector2D(4000, 0), TEXT("near, in the dry zone"));
	Check(Near, Maps->GetNearCenter(), Maps->GetNearExtent(), FVector2D(5800, 0), TEXT("near, at the zone's edge"));
	Check(Near, Maps->GetNearCenter(), Maps->GetNearExtent(), FVector2D(-4000, 200), TEXT("near, in the cold zone"));
	Check(Far, Maps->GetFarCenter(), Maps->GetFarExtent(), FVector2D(0, 0), TEXT("far, at the view"));
	TestTrue(TEXT("the cold zone holds snow"), W.Surface->SampleSurface(FVector(-4000, 200, 0)).Snow > 0.3f);
	return true;
}


IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDreamSurfaceGpuPolygonTest, "DreamSurface.Gpu.PolygonZone", DreamSurface::Tests::Flags)
bool FDreamSurfaceGpuPolygonTest::RunTest(const FString& Parameters)
{
	using namespace DreamSurface::Tests;
	if (!FApp::CanEverRender() || GUsingNullRHI)
	{
		AddInfo(TEXT("No RHI: skipped (run it in an editor that renders)."));
		return true;
	}
	FSurfaceWorld W(Sequence({ { Clear(), 6.0f }, { Rain(), 6.0f } }));
	if (!TestNotNull(TEXT("surface"), W.Surface))
	{
		return false;
	}
	// A cold L around the view where what falls is snow; the quarter north-east of its centre is cut out and gets rain.
	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	ADreamWeatherZone* Cold = W.World->SpawnActor<ADreamWeatherZone>(FVector(0, 0, 0), FRotator(0.0, 20.0, 0.0), Params);
	Cold->Zone->SetPolygonPoints({ FVector2D(-3000, -3000), FVector2D(3000, -3000), FVector2D(3000, 0), FVector2D(0, 0), FVector2D(0, 3000), FVector2D(-3000, 3000) });
	Cold->Zone->BlendDistance = 400.0f;
	Cold->Zone->Channels.Add(Channel_SnowRatio, 1.0f);
	Cold->Zone->Channels.Add(Channel_Temperature, -25.0f);

	W.Clock->ReportView(FVector(0, 0, 0), FRotator::ZeroRotator, true);
	W.GoTo(At(37.3));
	W.Clock->Tick(0.0f);
	FlushRenderingCommands();

	const FDreamSurfaceMaps* Maps = W.Surface->GetMaps();
	UTextureRenderTarget2D* Near = Maps ? Maps->GetNearMap() : nullptr;
	if (!TestNotNull(TEXT("near map (run the Setup)"), Near))
	{
		return false;
	}
	TArray<FLinearColor> Pixels;
	Near->GameThread_GetRenderTargetResource()->ReadLinearColorPixels(Pixels);
	const int32 Size = Near->SizeX;
	if (!TestEqual(TEXT("map size"), Pixels.Num(), Size * Near->SizeY))
	{
		return false;
	}
	const FVector2D Center = Maps->GetNearCenter();
	const float Extent = Maps->GetNearExtent();
	// Points in the zone's frame (turned by its 20 degrees into the world).
	const FTransform Frame(FRotator(0.0, 20.0, 0.0));
	auto Check = [&](const FVector2D& Local, const TCHAR* What) -> FDreamSurfaceState
	{
		const FVector World = Frame.TransformPosition(FVector(Local, 0.0));
		const FVector2D UV = (FVector2D(World) - Center) / Extent + 0.5;
		const int32 X = FMath::Clamp(FMath::FloorToInt32(UV.X * Size), 0, Size - 1);
		const int32 Y = FMath::Clamp(FMath::FloorToInt32(UV.Y * Size), 0, Size - 1);
		const FVector2D Texel = Center + (FVector2D(X + 0.5, Y + 0.5) / Size - 0.5) * Extent;
		const FLinearColor Gpu = Pixels[Y * Size + X];
		const FDreamSurfaceState Cpu = W.Surface->SampleSurface(FVector(Texel, 0.0));
		AddInfo(FString::Printf(TEXT("%s: GPU wet %.3f water %.3f snow %.3f dust %.3f / CPU %.3f %.3f %.3f %.3f"), What,
			Gpu.R, Gpu.G, Gpu.B, Gpu.A, Cpu.Wetness, Cpu.Water, Cpu.Snow, Cpu.Dust));
		TestTrue(FString::Printf(TEXT("%s: the map holds the query's state"), What),
			Cpu.Equals(FDreamSurfaceState::FromVector(FVector4f(Gpu.R, Gpu.G, Gpu.B, Gpu.A)), 0.01f));
		return Cpu;
	};
	const FDreamSurfaceState Arm = Check(FVector2D(1500, -1500), TEXT("in an arm"));
	Check(FVector2D(-1500, 1500), TEXT("in the other arm"));
	const FDreamSurfaceState Cut = Check(FVector2D(1800, 1800), TEXT("in the cut-out quarter"));
	Check(FVector2D(1500, 150), TEXT("by the cut's edge"));
	Check(FVector2D(150, 1500), TEXT("by its other edge"));
	Check(FVector2D(3200, -1000), TEXT("just outside"));
	TestTrue(TEXT("snow in the L"), Arm.Snow > 0.3f);
	TestTrue(TEXT("none in the cut"), Cut.Snow < 0.02f);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
