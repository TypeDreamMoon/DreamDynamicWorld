// Copyright DreamMoon. All Rights Reserved.

#include "DreamWeatherTestListener.h"
#include "DreamWeather.h"
#include "DreamWeatherChannels.h"
#include "DreamWeatherCloudPaint.h"
#include "DreamWeatherFallPaths.h"
#include "DreamWeatherHlslShim.h"
#include "DreamWeatherModule.h"
#include "DreamWeatherPreset.h"
#include "DreamWeatherSelector.h"
#include "DreamWeatherSettings.h"
#include "DreamWeatherShelter.h"
#include "DreamWeatherSubsystem.h"
#include "DreamWeatherZone.h"
#include "DreamWorldConfig.h"
#include "DreamWorldWidgets.h"
#include "DreamWindSubsystem.h"
#include "DreamWorldCelestial.h"
#include "DreamWorldSubsystem.h"

#include "Components/BoxComponent.h"
#include "Components/SplineComponent.h"
#include "Engine/CollisionProfile.h"
#include "Engine/Texture2D.h"
#include "Engine/World.h"
#include "Framework/Application/SlateApplication.h"
#include "HAL/IConsoleManager.h"
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "UObject/Package.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(DreamWeatherTestListener)

void UDreamWeatherTestListener::Bind(UDreamWeatherSubsystem* Weather)
{
	Weather->OnWeatherChanged.AddDynamic(this, &UDreamWeatherTestListener::HandleChanged);
	Weather->OnPrecipitationStarted.AddDynamic(this, &UDreamWeatherTestListener::HandleStarted);
	Weather->OnPrecipitationStopped.AddDynamic(this, &UDreamWeatherTestListener::HandleStopped);
	Weather->OnWeatherTagsChanged.AddDynamic(this, &UDreamWeatherTestListener::HandleTags);
	Weather->OnLightningStrike.AddDynamic(this, &UDreamWeatherTestListener::HandleStrike);
}

#if WITH_DEV_AUTOMATION_TESTS

namespace DreamWeather::Tests
{
	constexpr EAutomationTestFlags Flags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter;
	using C = FDreamWeatherChannels;

	UDreamWeatherPreset* MakePreset(const TCHAR* Name, std::initializer_list<TPair<FGameplayTag, float>> Channels)
	{
		UDreamWeatherPreset* Preset = NewObject<UDreamWeatherPreset>(GetTransientPackage(), MakeUniqueObjectName(GetTransientPackage(), UDreamWeatherPreset::StaticClass(), Name));
		for (const TPair<FGameplayTag, float>& Channel : Channels)
		{
			Preset->Channels.Add(Channel.Key, Channel.Value);
		}
		return Preset;
	}

	UDreamWeatherPreset* Clear() { return MakePreset(TEXT("Clear"), { { Tags::Channel_CloudCoverage, 0.1f }, { Tags::Channel_Wind, 0.2f } }); }
	UDreamWeatherPreset* Rain() { return MakePreset(TEXT("Rain"), { { Tags::Channel_CloudCoverage, 0.95f }, { Tags::Channel_Precipitation, 0.8f }, { Tags::Channel_CloudDarkness, 0.5f }, { Tags::Channel_Wind, 0.6f } }); }

	double Hours(double H) { return DreamWorld::Celestial::FromDateTime(FDateTime(2026, 6, 21, 0, 0, 0)) + H * 3600.0; }

	/** A game world with a clock, the weather and a weather actor whose selector the test sets. */
	struct FWeatherWorld
	{
		UWorld* World = nullptr;
		UDreamWorldSubsystem* Clock = nullptr;
		UDreamWeatherSubsystem* Weather = nullptr;
		ADreamWeather* Actor = nullptr;

		explicit FWeatherWorld(UDreamWeatherSelector* Selector)
		{
			World = UWorld::CreateWorld(EWorldType::Game, false);
			if (!World)
			{
				return;
			}
			Clock = World->GetSubsystem<UDreamWorldSubsystem>();
			Weather = World->GetSubsystem<UDreamWeatherSubsystem>();
			FActorSpawnParameters Params;
			Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
			Actor = World->SpawnActor<ADreamWeather>(Params);
			if (Actor && Selector)
			{
				Selector->Rename(nullptr, Actor);
				Actor->Selector = Selector;
			}
			FDreamWorldClockConfig Config;
			Config.StartTime = FDateTime(2026, 6, 21, 0, 0, 0);
			Config.bAnimate = true;
			Config.Seed = 77;
			Clock->ApplyClockConfig(Config, true);
		}
		~FWeatherWorld()
		{
			if (World)
			{
				World->DestroyWorld(false);
			}
		}

		ADreamWeatherZone* AddZone(const FVector& Location, float Radius, float Blend, int32 Priority, std::initializer_list<TPair<FGameplayTag, float>> Channels)
		{
			FActorSpawnParameters Params;
			Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
			ADreamWeatherZone* Zone = World->SpawnActor<ADreamWeatherZone>(Location, FRotator::ZeroRotator, Params);
			Zone->Zone->Shape = EDreamWeatherZoneShape::Circle;
			Zone->Zone->Extent = FVector2D(Radius, Radius);
			Zone->Zone->BlendDistance = Blend;
			Zone->Zone->Priority = Priority;
			for (const TPair<FGameplayTag, float>& Channel : Channels)
			{
				Zone->Zone->Channels.Add(Channel.Key, Channel.Value);
			}
			return Zone;
		}
	};

	UDreamWeatherSelector_Sequence* ClearThenRain(UObject* Outer = GetTransientPackage())
	{
		UDreamWeatherSelector_Sequence* Sequence = NewObject<UDreamWeatherSelector_Sequence>(Outer);
		Sequence->Entries.Add({ Clear(), 3.0f });
		Sequence->Entries.Add({ Rain(), 3.0f });
		return Sequence;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDreamWeatherRegistryTest, "DreamWeather.Channels.Registry", DreamWeather::Tests::Flags)
bool FDreamWeatherRegistryTest::RunTest(const FString& Parameters)
{
	using namespace DreamWeather;
	TestTrue(TEXT("every built-in channel is registered"), FDreamWeatherChannels::Num() >= FDreamWeatherChannels::NumBuiltIn);
	TestEqual(TEXT("precipitation sits at its fixed index"), FDreamWeatherChannels::IndexOf(Tags::Channel_Precipitation), int32(FDreamWeatherChannels::Precipitation));
	TestEqual(TEXT("an unknown tag has no index"), FDreamWeatherChannels::IndexOf(FGameplayTag()), int32(INDEX_NONE));
	TestEqual(TEXT("defaults cover every channel"), FDreamWeatherChannels::Defaults().Values.Num(), FDreamWeatherChannels::Num());
	TestTrue(TEXT("clouds gather before a change"), FDreamWeatherChannels::Get()[FDreamWeatherChannels::CloudCoverage].LeadMinutes > 0.0f);
	TestTrue(TEXT("rain follows a change"), FDreamWeatherChannels::Get()[FDreamWeatherChannels::Precipitation].LeadMinutes < 0.0f);
	TestEqual(TEXT("override blend"), FDreamWeatherChannels::Combine(EDreamWeatherBlend::Override, 0.2f, 1.0f, 0.5f), 0.6f);
	TestEqual(TEXT("max blend keeps the higher"), FDreamWeatherChannels::Combine(EDreamWeatherBlend::Max, 0.8f, 0.3f, 1.0f), 0.8f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDreamWeatherHailSleetTest, "DreamWeather.Channels.HailAndSleet", DreamWeather::Tests::Flags)
bool FDreamWeatherHailSleetTest::RunTest(const FString& Parameters)
{
	using namespace DreamWeather::Tests;
	TestEqual(TEXT("hail at its fixed index"), FDreamWeatherChannels::IndexOf(DreamWeather::Tags::Channel_Hail), int32(FDreamWeatherChannels::Hail));
	TestEqual(TEXT("sleet at its fixed index"), FDreamWeatherChannels::IndexOf(DreamWeather::Tags::Channel_Sleet), int32(FDreamWeatherChannels::Sleet));
	TestEqual(TEXT("none by default"), FDreamWeatherChannels::Defaults().Get(FDreamWeatherChannels::Hail) + FDreamWeatherChannels::Defaults().Get(FDreamWeatherChannels::Sleet), 0.0f);
	UDreamWeatherSelector_Fixed* Fixed = NewObject<UDreamWeatherSelector_Fixed>();
	Fixed->Preset = Rain();
	FWeatherWorld W(Fixed);
	if (!TestNotNull(TEXT("weather"), W.Weather))
	{
		return false;
	}
	W.Clock->SetWorldSeconds(Hours(12.0));
	W.Clock->Tick(0.0f);
	// The precipitation's kinds: hail its share first, the sleet's of what is left, the rest rain and snow.
	auto At = [&](float Hail, float Sleet, float Temperature)
	{
		FDreamWeatherValues V = W.Weather->GetGlobalValues();
		V.Values[FDreamWeatherChannels::Precipitation] = 0.8f;
		V.Values[FDreamWeatherChannels::CloudCoverage] = 1.0f;
		V.Values[FDreamWeatherChannels::Temperature] = Temperature;
		V.Values[FDreamWeatherChannels::Hail] = Hail;
		V.Values[FDreamWeatherChannels::Sleet] = Sleet;
		return W.Weather->SampleWeatherAt(FVector::ZeroVector, V, W.Clock->GetWorldSeconds());
	};
	const FDreamWeatherSample Storm = At(0.5f, 0.0f, 10.0f);
	TestNearlyEqual(TEXT("half of it hail"), Storm.Hail, 0.5f * Storm.Precipitation, 1.0e-4f);
	TestNearlyEqual(TEXT("the rest rain on a warm day (hail at any temperature)"), Storm.Rain, 0.5f * Storm.Precipitation, 1.0e-3f);
	TestTrue(TEXT("tagged as hail"), Storm.Tags.HasTag(DreamWeather::Tags::State_Hail));
	const FDreamWeatherSample Mixed = At(0.5f, 0.5f, 10.0f);
	TestNearlyEqual(TEXT("sleet takes its share of what the hail leaves"), Mixed.Sleet, 0.25f * Mixed.Precipitation, 1.0e-4f);
	TestNearlyEqual(TEXT("all of it accounted for"), Mixed.Hail + Mixed.Sleet + Mixed.Rain + Mixed.Snow, Mixed.Precipitation, 1.0e-4f);
	const FDreamWeatherSample Sleet = At(0.0f, 1.0f, 1.0f);
	TestNearlyEqual(TEXT("all sleet: no rain, no snow"), Sleet.Rain + Sleet.Snow, 0.0f, 1.0e-4f);
	TestTrue(TEXT("tagged as sleet"), Sleet.Tags.HasTag(DreamWeather::Tags::State_Sleet));
	TestNearlyEqual(TEXT("landing in the open as it falls"), Sleet.SleetHere, Sleet.Sleet * Sleet.Exposure, 1.0e-5f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDreamWeatherFreezingRainTest, "DreamWeather.Channels.FreezingRain", DreamWeather::Tests::Flags)
bool FDreamWeatherFreezingRainTest::RunTest(const FString& Parameters)
{
	using namespace DreamWeather::Tests;
	TestEqual(TEXT("the channel sits at its fixed index"), FDreamWeatherChannels::IndexOf(DreamWeather::Tags::Channel_FreezingRain), int32(FDreamWeatherChannels::FreezingRain));
	TestEqual(TEXT("none by default"), FDreamWeatherChannels::Defaults().Get(FDreamWeatherChannels::FreezingRain), 0.0f);
	UDreamWeatherSelector_Fixed* Fixed = NewObject<UDreamWeatherSelector_Fixed>();
	Fixed->Preset = Rain();
	FWeatherWorld W(Fixed);
	if (!TestNotNull(TEXT("weather"), W.Weather))
	{
		return false;
	}
	W.Clock->SetWorldSeconds(Hours(12.0));
	W.Clock->Tick(0.0f);
	// The same rain at a point well below freezing: ordinary rain turns to snow there, supercooled rain stays rain, half
	// of it half.
	const FVector P(0, 0, 0);
	auto At = [&](float Freezing)
	{
		FDreamWeatherValues V = W.Weather->GetGlobalValues();
		V.Values[FDreamWeatherChannels::Temperature] = -30.0f;
		V.Values[FDreamWeatherChannels::FreezingRain] = Freezing;
		return W.Weather->SampleWeatherAt(P, V, W.Clock->GetWorldSeconds());
	};
	const FDreamWeatherSample Ordinary = At(0.0f);
	const FDreamWeatherSample Freezing = At(1.0f);
	const FDreamWeatherSample Half = At(0.5f);
	AddInfo(FString::Printf(TEXT("at %.1f C: snow ratio %.2f ordinary, %.2f freezing, %.2f half"), Ordinary.Temperature, Ordinary.SnowRatio, Freezing.SnowRatio, Half.SnowRatio));
	TestTrue(TEXT("it is freezing there"), Ordinary.Temperature < -1.0f);
	TestTrue(TEXT("ordinary rain turns to snow"), Ordinary.SnowRatio > 0.99f && Ordinary.Rain < 0.01f);
	TestTrue(TEXT("freezing rain stays rain"), Freezing.SnowRatio < 0.01f && FMath::IsNearlyEqual(Freezing.Rain, Freezing.Precipitation, 1.0e-4f));
	TestNearlyEqual(TEXT("the sample carries it"), Freezing.FreezingRain, 1.0f, 1.0e-4f);
	TestNearlyEqual(TEXT("half of it, half"), Half.SnowRatio, 0.5f, 1.0e-3f);
	// The snow ratio channel itself still wins: a weather that says snow snows.
	FDreamWeatherValues SaysSnow = W.Weather->GetGlobalValues();
	SaysSnow.Values[FDreamWeatherChannels::FreezingRain] = 1.0f;
	SaysSnow.Values[FDreamWeatherChannels::SnowRatio] = 1.0f;
	TestTrue(TEXT("a weather that says snow snows"), W.Weather->SampleWeatherAt(P, SaysSnow, W.Clock->GetWorldSeconds()).SnowRatio > 0.99f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDreamWeatherSelectorTest, "DreamWeather.Selector.Deterministic", DreamWeather::Tests::Flags)
bool FDreamWeatherSelectorTest::RunTest(const FString& Parameters)
{
	using namespace DreamWeather::Tests;
	UDreamWeatherPreset* A = Clear();
	UDreamWeatherPreset* B = Rain();
	UDreamWeatherSelector_Random* Random = NewObject<UDreamWeatherSelector_Random>();
	Random->Entries.Add({ A, 3.0f });
	Random->Entries.Add({ B, 1.0f });
	Random->SlotHours = 2.0f;
	Random->Persistence = 0.3f;

	int32 Same = 0, CountA = 0, Repeats = 0, Differ = 0;
	const UDreamWeatherPreset* Prev = nullptr;
	constexpr int32 Slots = 4000;
	for (int32 i = 0; i < Slots; ++i)
	{
		const double T = Hours(i * 2.0 + 0.5);
		const FDreamWeatherSlot S1 = Random->GetSlot(T, 7);
		const FDreamWeatherSlot S2 = Random->GetSlot(T, 7);
		Same += S1.Current == S2.Current ? 1 : 0;
		Differ += Random->GetSlot(T, 8).Current != S1.Current ? 1 : 0;
		CountA += S1.Current == A ? 1 : 0;
		Repeats += S1.Current == Prev ? 1 : 0;
		TestTrue(TEXT("the previous slot's weather is reported"), i == 0 || S1.Previous == Prev);
		Prev = S1.Current;
	}
	TestEqual(TEXT("the same time and seed give the same weather"), Same, Slots);
	TestTrue(FString::Printf(TEXT("another seed gives other weather (%d of %d slots differ)"), Differ, Slots), Differ > Slots / 5);
	TestNearlyEqual(TEXT("weights are honoured (3:1)"), float(CountA) / Slots, 0.75f, 0.05f);
	TestTrue(FString::Printf(TEXT("weather persists (%d repeats)"), Repeats), Repeats > Slots * 0.6);

	UDreamWeatherSelector_Seasonal* Seasonal = NewObject<UDreamWeatherSelector_Seasonal>();
	Seasonal->SeasonalEntries.Add({ A, 1.0f, 1.0f, 1.0f, 0.0f });
	Seasonal->SeasonalEntries.Add({ B, 0.0f, 0.0f, 0.0f, 1.0f });
	Seasonal->Persistence = 0.0f;
	bool bWinterOnlyB = true, bSummerOnlyA = true;
	for (int32 i = 0; i < 200; ++i)
	{
		bWinterOnlyB &= Seasonal->GetSlot(DreamWorld::Celestial::FromDateTime(FDateTime(2026, 1, 10)) + i * 3600.0 * 3.0, 3).Current == B;
		bSummerOnlyA &= Seasonal->GetSlot(DreamWorld::Celestial::FromDateTime(FDateTime(2026, 7, 10)) + i * 3600.0 * 3.0, 3).Current == A;
	}
	TestTrue(TEXT("winter draws the winter weather"), bWinterOnlyB);
	TestTrue(TEXT("summer never draws it"), bSummerOnlyA);
	Seasonal->bSouthernHemisphere = true;
	TestTrue(TEXT("the southern hemisphere's January is summer"), Seasonal->GetSlot(DreamWorld::Celestial::FromDateTime(FDateTime(2026, 1, 10)), 3).Current == A);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDreamWeatherScheduleTest, "DreamWeather.Schedule.ContinuousWithOmens", DreamWeather::Tests::Flags)
bool FDreamWeatherScheduleTest::RunTest(const FString& Parameters)
{
	using namespace DreamWeather::Tests;
	FWeatherWorld W(ClearThenRain());
	if (!TestNotNull(TEXT("weather"), W.Weather) || !TestTrue(TEXT("active with a weather actor"), W.Weather->IsActive()))
	{
		return false;
	}

	// Continuity: no channel moves faster than its transition allows (smoothstep peaks at 1.5x the mean slope).
	const TArray<FDreamWeatherChannelDef>& Defs = FDreamWeatherChannels::Get();
	FDreamWeatherValues Prev = W.Weather->EvaluateSchedule(Hours(0.0));
	bool bSmooth = true;
	FString Worst;
	for (int32 Minute = 1; Minute <= 48 * 60; ++Minute)
	{
		const FDreamWeatherValues V = W.Weather->EvaluateSchedule(Hours(Minute / 60.0));
		for (int32 i = 0; i < Defs.Num(); ++i)
		{
			const float Limit = (Defs[i].Max - Defs[i].Min) * 1.6f / FMath::Max(Defs[i].TransitionMinutes, 1.0f) + 1e-4f;
			if (FMath::Abs(V.Get(i) - Prev.Get(i)) > Limit)
			{
				bSmooth = false;
				Worst = FString::Printf(TEXT("%s jumps %.3f at minute %d"), *Defs[i].Channel.ToString(), V.Get(i) - Prev.Get(i), Minute);
			}
		}
		Prev = V;
	}
	TestTrue(Worst.IsEmpty() ? TEXT("every channel eases") : *Worst, bSmooth);

	// The omen: around the change to rain at 03:00, clouds gather first and the rain comes after.
	const FDreamWeatherValues Before = W.Weather->EvaluateSchedule(Hours(3.0 - 5.0 / 60.0));
	const FDreamWeatherValues JustAfter = W.Weather->EvaluateSchedule(Hours(3.0 + 5.0 / 60.0));
	const FDreamWeatherValues Later = W.Weather->EvaluateSchedule(Hours(3.0 + 25.0 / 60.0));
	TestTrue(FString::Printf(TEXT("clouds gather before the change (%.3f)"), Before.Get(C::CloudCoverage)), Before.Get(C::CloudCoverage) > 0.15f);
	TestEqual(TEXT("no rain yet just after the change"), JustAfter.Get(C::Precipitation), 0.0f);
	TestTrue(FString::Printf(TEXT("rain arrives after (%.3f)"), Later.Get(C::Precipitation)), Later.Get(C::Precipitation) > 0.2f);
	TestNearlyEqual(TEXT("full rain by mid-slot"), W.Weather->EvaluateSchedule(Hours(4.5)).Get(C::Precipitation), 0.8f, 1e-3f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDreamWeatherForcedPresetAliveTest, "DreamWeather.Modifiers.KeepTheirPresets", DreamWeather::Tests::Flags)
bool FDreamWeatherForcedPresetAliveTest::RunTest(const FString& Parameters)
{
	using namespace DreamWeather::Tests;
	using namespace DreamWeather::Tags;
	UDreamWeatherSelector_Fixed* Fixed = NewObject<UDreamWeatherSelector_Fixed>();
	Fixed->Preset = Clear();
	FWeatherWorld W(Fixed);
	if (!TestNotNull(TEXT("weather"), W.Weather))
	{
		return false;
	}
	W.Clock->SetPaused(true);
	W.Clock->Tick(0.0f);
	// A preset only the forcing holds (as DreamWeather.Set loads one): the garbage collector must leave it.
	TWeakObjectPtr<UDreamWeatherPreset> Forced = Rain();
	W.Weather->ForceWeather(Forced.Get(), 0.0f);
	W.Clock->Tick(0.1f);
	CollectGarbage(GARBAGE_COLLECTION_KEEPFLAGS);
	TestTrue(TEXT("a forced weather's preset outlives a garbage collection"), Forced.IsValid());
	W.Clock->Tick(0.1f);
	TestNearlyEqual(TEXT("and still rains"), W.Weather->GetGlobalValues().Get(C::Precipitation), 0.8f, 1e-3f);
	W.Weather->ClearForcedWeather(0.0f);
	W.Clock->Tick(5.0f);
	CollectGarbage(GARBAGE_COLLECTION_KEEPFLAGS);
	TestFalse(TEXT("once no modifier holds it, it may go"), Forced.IsValid());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDreamWeatherModifierTest, "DreamWeather.Modifiers", DreamWeather::Tests::Flags)
bool FDreamWeatherModifierTest::RunTest(const FString& Parameters)
{
	using namespace DreamWeather::Tests;
	using namespace DreamWeather::Tags;
	UDreamWeatherSelector_Fixed* Fixed = NewObject<UDreamWeatherSelector_Fixed>();
	Fixed->Preset = Clear();
	FWeatherWorld W(Fixed);
	if (!TestNotNull(TEXT("weather"), W.Weather))
	{
		return false;
	}
	W.Clock->SetPaused(true);
	W.Clock->Tick(0.0f);
	TestEqual(TEXT("dry"), W.Weather->GetGlobalValues().Get(C::Precipitation), 0.0f);

	FDreamWeatherModifierDesc Rainmaker;
	Rainmaker.Source = TEXT("Test");
	Rainmaker.Channels.Add(Channel_Precipitation, 1.0f);
	Rainmaker.BlendInSeconds = 2.0f;
	const FDreamWeatherModifierHandle H = W.Weather->PushModifier(Rainmaker);
	W.Clock->Tick(1.0f);
	TestNearlyEqual(TEXT("half way in after half the blend"), W.Weather->GetGlobalValues().Get(C::Precipitation), 0.5f, 1e-3f);
	W.Clock->Tick(2.0f);
	TestNearlyEqual(TEXT("fully in"), W.Weather->GetGlobalValues().Get(C::Precipitation), 1.0f, 1e-3f);

	// A higher priority wins whatever the push order.
	FDreamWeatherModifierDesc Drought = Rainmaker;
	Drought.Channels.Add(Channel_Precipitation, 0.0f);
	Drought.BlendInSeconds = 0.0f;
	Drought.Priority = 5;
	const FDreamWeatherModifierHandle D = W.Weather->PushModifier(Drought);
	FDreamWeatherModifierDesc Lower = Rainmaker;
	Lower.BlendInSeconds = 0.0f;
	Lower.Priority = 1;
	W.Weather->PushModifier(Lower);
	W.Clock->Tick(0.1f);
	TestNearlyEqual(TEXT("the higher priority applies last"), W.Weather->GetGlobalValues().Get(C::Precipitation), 0.0f, 1e-3f);

	W.Weather->RemoveModifier(D, 1.0f);
	W.Weather->RemoveModifier(H, 1.0f);
	W.Clock->Tick(2.0f);
	TestEqual(TEXT("removed modifiers are dropped once faded"), W.Weather->GetNumModifiers(), 1);
	TestNearlyEqual(TEXT("the remaining one rules"), W.Weather->GetGlobalValues().Get(C::Precipitation), 1.0f, 1e-3f);

	// Forcing a preset.
	W.Weather->ForceWeather(Rain(), 0.0f);
	W.Clock->Tick(0.1f);
	TestNearlyEqual(TEXT("forced weather overrides everything"), W.Weather->GetGlobalValues().Get(C::CloudCoverage), 0.95f, 1e-3f);
	W.Weather->ClearForcedWeather(0.0f);
	W.Clock->Tick(0.1f);
	TestNearlyEqual(TEXT("cleared"), W.Weather->GetGlobalValues().Get(C::CloudCoverage), 0.1f, 1e-3f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDreamWeatherForceHandOverTest, "DreamWeather.ForceHandOver", DreamWeather::Tests::Flags)
bool FDreamWeatherForceHandOverTest::RunTest(const FString& Parameters)
{
	using namespace DreamWeather::Tests;
	using namespace DreamWeather::Tags;
	// The schedule rains; forced weather replaces forced weather. Between the two the schedule must not show through.
	UDreamWeatherSelector_Fixed* Fixed = NewObject<UDreamWeatherSelector_Fixed>();
	Fixed->Preset = Rain();
	FWeatherWorld W(Fixed);
	if (!TestNotNull(TEXT("weather"), W.Weather))
	{
		return false;
	}
	W.Clock->SetPaused(true);
	W.Weather->ForceWeather(Clear(), 0.0f);
	W.Clock->Tick(0.1f);
	TestNearlyEqual(TEXT("forced clear is dry"), W.Weather->GetGlobalValues().Get(C::Precipitation), 0.0f, 1e-5f);

	UDreamWeatherPreset* Overcast = MakePreset(TEXT("Overcast"), { { Channel_CloudCoverage, 0.9f } });
	W.Weather->ForceWeather(Overcast, 10.0f);
	float Wettest = 0.0f;
	float Cover = 0.0f;
	for (int32 Step = 0; Step < 24; ++Step)
	{
		W.Clock->Tick(0.5f);
		Wettest = FMath::Max(Wettest, W.Weather->GetGlobalValues().Get(C::Precipitation));
		Cover = W.Weather->GetGlobalValues().Get(C::CloudCoverage);
	}
	TestNearlyEqual(TEXT("no rain from the schedule while one forced weather hands over to the next"), Wettest, 0.0f, 1e-5f);
	TestNearlyEqual(TEXT("the new weather is in"), Cover, 0.9f, 1e-4f);
	TestEqual(TEXT("the replaced one is gone once the new one is in"), W.Weather->GetNumModifiers(), 1);

	// Taken away while a hand-over is still under way: both go, back to the schedule.
	W.Weather->ForceWeather(Clear(), 10.0f);
	W.Clock->Tick(1.0f);
	W.Weather->ClearForcedWeather(2.0f);
	for (int32 Step = 0; Step < 10; ++Step)
	{
		W.Clock->Tick(0.5f);
	}
	TestEqual(TEXT("cleared mid hand-over: no modifier left"), W.Weather->GetNumModifiers(), 0);
	TestNearlyEqual(TEXT("cleared mid hand-over: the schedule's rain again"), W.Weather->GetGlobalValues().Get(C::Precipitation), 0.8f, 1e-4f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDreamWeatherCloudGatesTest, "DreamWeather.CloudGates", DreamWeather::Tests::Flags)
bool FDreamWeatherCloudGatesTest::RunTest(const FString& Parameters)
{
	using namespace DreamWeather::Tests;
	using namespace DreamWeather::Tags;
	const UDreamWeatherSettings* S = UDreamWeatherSettings::Get();

	// On their own: rain needs a closed cover, lightning a dark one.
	auto Gated = [](float Cover, float Dark, float Precip, float Bolts)
	{
		FDreamWeatherValues V = FDreamWeatherChannels::Defaults();
		V.Values[C::CloudCoverage] = Cover;
		V.Values[C::CloudDarkness] = Dark;
		V.Values[C::Precipitation] = Precip;
		V.Values[C::Lightning] = Bolts;
		FDreamWeatherChannels::ApplyCloudGates(V);
		return V;
	};
	TestNearlyEqual(TEXT("no rain from a broken sky"), Gated(0.5f, 0.5f, 1.0f, 0.0f).Get(C::Precipitation), 0.0f, 1e-5f);
	TestNearlyEqual(TEXT("all of it under a closed cover"), Gated(0.95f, 0.45f, 0.6f, 0.0f).Get(C::Precipitation), 0.6f, 1e-5f);
	TestNearlyEqual(TEXT("snow's cover is closed enough"), Gated(0.9f, 0.2f, 0.5f, 0.0f).Get(C::Precipitation), 0.5f, 1e-5f);
	TestNearlyEqual(TEXT("no lightning from a pale cover"), Gated(1.0f, 0.25f, 0.0f, 1.0f).Get(C::Lightning), 0.0f, 1e-5f);
	TestNearlyEqual(TEXT("a storm's lightning in full"), Gated(1.0f, 0.85f, 0.95f, 0.8f).Get(C::Lightning), 0.8f, 1e-5f);
	TestNearlyEqual(TEXT("coverage itself is left alone"), Gated(0.5f, 0.5f, 1.0f, 1.0f).Get(C::CloudCoverage), 0.5f, 1e-5f);

	// A change of weather that blends every channel at once: clear -> storm -> clear. Whenever it rains the cover is
	// closing or still closed; whenever it may strike the cloud is a storm's.
	UDreamWeatherSelector_Fixed* Fixed = NewObject<UDreamWeatherSelector_Fixed>();
	Fixed->Preset = Clear();
	FWeatherWorld W(Fixed);
	if (!TestNotNull(TEXT("weather"), W.Weather))
	{
		return false;
	}
	W.Clock->SetPaused(true);
	W.Clock->Tick(0.0f);
	UDreamWeatherPreset* Storm = MakePreset(TEXT("Storm"), { { Channel_CloudCoverage, 1.0f }, { Channel_CloudDarkness, 0.85f }, { Channel_Precipitation, 0.95f }, { Channel_Lightning, 0.8f } });
	for (UDreamWeatherPreset* To : { Storm, Clear() })
	{
		W.Weather->ForceWeather(To, 20.0f);
		bool bRained = false, bBroken = false, bFlashedPale = false, bStruck = false;
		float RainFrom = -1.0f;
		for (float T = 0.5f; T <= 21.0f; T += 0.5f)
		{
			W.Clock->Tick(0.5f);
			const FDreamWeatherSample V = W.Weather->SampleWeather(FVector::ZeroVector);
			if (V.Precipitation > 0.01f)
			{
				bRained = true;
				RainFrom = RainFrom < 0.0f ? T : RainFrom;
				bBroken |= V.CloudCoverage < S->PrecipitationCloudCover.X;
			}
			if (V.Lightning > 0.01f)
			{
				bStruck = true;
				bFlashedPale |= V.CloudDarkness < S->LightningCloudDarkness.X || V.CloudCoverage < S->LightningCloudCover.X;
			}
		}
		const FString Name = To == Storm ? TEXT("into the storm") : TEXT("out of it");
		TestFalse(*(Name + TEXT(": no rain while the cover is broken")), bBroken);
		TestFalse(*(Name + TEXT(": no lightning from a pale or broken cover")), bFlashedPale);
		if (To == Storm)
		{
			TestTrue(TEXT("into the storm: it rains and strikes in the end"), bRained && bStruck);
			TestTrue(*FString::Printf(TEXT("into the storm: the rain starts after the clouds have closed in, not with them (at %.1f s of 20)"), RainFrom), RainFrom > 8.0f);
			const FDreamWeatherSample End = W.Weather->SampleWeather(FVector::ZeroVector);
			TestNearlyEqual(TEXT("the storm's own rain in full"), End.Precipitation, 0.95f, 1e-3f);
			TestNearlyEqual(TEXT("the storm's own lightning in full"), End.Lightning, 0.8f, 1e-3f);
		}
		else
		{
			TestNearlyEqual(TEXT("out of it: dry at the end"), W.Weather->SampleWeather(FVector::ZeroVector).Precipitation, 0.0f, 1e-4f);
		}
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDreamWeatherZoneTest, "DreamWeather.Zones", DreamWeather::Tests::Flags)
bool FDreamWeatherZoneTest::RunTest(const FString& Parameters)
{
	using namespace DreamWeather::Tests;
	using namespace DreamWeather::Tags;
	UDreamWeatherSelector_Fixed* Fixed = NewObject<UDreamWeatherSelector_Fixed>();
	Fixed->Preset = Clear();
	FWeatherWorld W(Fixed);
	if (!TestNotNull(TEXT("weather"), W.Weather))
	{
		return false;
	}
	W.AddZone(FVector::ZeroVector, 1000.0f, 1000.0f, 0, { { Channel_Precipitation, 1.0f }, { Channel_CloudCoverage, 1.0f } });
	W.Clock->Tick(0.0f);

	TestNearlyEqual(TEXT("full effect inside"), W.Weather->SampleWeather(FVector(500, 0, 0)).Precipitation, 1.0f, 1e-4f);
	// (The falloff on the coverage: rain halfway across is held back by the half-closed cover, see CloudGates.)
	const float Cover0 = W.Weather->GetGlobalValues().Get(C::CloudCoverage);
	TestNearlyEqual(TEXT("half way across the blend"), W.Weather->SampleWeather(FVector(1500, 0, 0)).CloudCoverage, (Cover0 + 1.0f) * 0.5f, 1e-3f);
	TestNearlyEqual(TEXT("untouched beyond"), W.Weather->SampleWeather(FVector(2500, 0, 0)).Precipitation, 0.0f, 1e-4f);
	TestNearlyEqual(TEXT("the zone stands on the whole height"), W.Weather->SampleWeather(FVector(0, 0, 100000)).Precipitation, 1.0f, 1e-4f);

	// Overlapping zones: the higher priority wins.
	W.AddZone(FVector(200, 0, 0), 1000.0f, 1000.0f, 5, { { Channel_Precipitation, 0.25f } });
	TestNearlyEqual(TEXT("higher priority applies last"), W.Weather->SampleWeather(FVector(300, 0, 0)).Precipitation, 0.25f, 1e-4f);

	// Equal priorities apply in the order of their stable ids, never in the order they happened to register.
	ADreamWeatherZone* P = W.AddZone(FVector(8000, 0, 0), 1000.0f, 1000.0f, 0, { { Channel_Precipitation, 0.6f }, { Channel_CloudCoverage, 1.0f } });
	ADreamWeatherZone* Q = W.AddZone(FVector(8100, 0, 0), 1000.0f, 1000.0f, 0, { { Channel_Precipitation, 0.3f }, { Channel_CloudCoverage, 1.0f } });
	W.Clock->Tick(0.0f);
	const float Expected = P->Zone->GetWeatherInfluenceStableId() > Q->Zone->GetWeatherInfluenceStableId() ? 0.6f : 0.3f;
	TestNearlyEqual(TEXT("equal priorities: the larger stable id applies last"), W.Weather->SampleWeather(FVector(8050, 0, 0)).Precipitation, Expected, 1e-4f);

	// The map's records, run through the shared code on the CPU, give the query's answer: what the sky shows
	// is what gameplay reads.
	const TArray<FVector4f>& Records = W.Weather->GetMapZones();
	TestEqual(TEXT("four zones in the map"), W.Weather->GetMapZoneCount(), 4);
	TestEqual(TEXT("and no polygon points after them"), Records.Num(), 12);
	using namespace DreamWind::Hlsl;
	const FDreamWeatherValues& G = W.Weather->GetGlobalValues();
	for (const FVector& At : { FVector(-300, 0, 0), FVector(1400, 300, 0), FVector(5000, 0, 0), FVector(8050, 40, 0) })
	{
		float3 V(G.Get(C::CloudCoverage), G.Get(C::CloudDarkness), G.Get(C::Precipitation));
		for (int32 i = 0; i < W.Weather->GetMapZoneCount() * 3; i += 3)
		{
			V = DWX_ApplyZone(V, float2(float(At.X), float(At.Y)), ToHlsl(Records[i]), ToHlsl(Records[i + 1]), ToHlsl(Records[i + 2]), FDreamZonePoints(Records));
		}
		V = DWX_GatePrecipitation(V, float2(float(UDreamWeatherSettings::Get()->PrecipitationCloudCover.X), float(UDreamWeatherSettings::Get()->PrecipitationCloudCover.Y)));
		const FDreamWeatherSample S = W.Weather->SampleWeather(At);
		TestNearlyEqual(*FString::Printf(TEXT("map precipitation matches the query at %s"), *At.ToString()), V.z, S.Precipitation, 1e-4f);
		TestNearlyEqual(*FString::Printf(TEXT("map coverage matches the query at %s"), *At.ToString()), V.x, S.CloudCoverage, 1e-4f);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDreamWeatherPolygonZoneTest, "DreamWeather.Zones.Polygon", DreamWeather::Tests::Flags)
bool FDreamWeatherPolygonZoneTest::RunTest(const FString& Parameters)
{
	using namespace DreamWeather::Tests;
	using namespace DreamWeather::Tags;
	using namespace DreamWind::Hlsl;

	// The shared distance: a square outline is the box of the same size, whichever way round it goes.
	{
		const TArray<FVector4f> Square = { FVector4f(-1000, -1000, 1414.3f, 0), FVector4f(1000, -1000, 0, 0), FVector4f(1000, 1000, 0, 0), FVector4f(-1000, 1000, 0, 0) };
		const TArray<FVector4f> Reversed = { FVector4f(-1000, 1000, 1414.3f, 0), FVector4f(1000, 1000, 0, 0), FVector4f(1000, -1000, 0, 0), FVector4f(-1000, -1000, 0, 0) };
		const float4 Z0(300.0f, -200.0f, 1.0f, 0.0f);
		const float4 Box(1000.0f, 1000.0f, 5000.0f, float(DWX_SHAPE_BOX));
		const float4 Polygon(0.0f, 4.0f, 5000.0f, float(DWX_SHAPE_POLYGON));
		for (const FVector2D& P : { FVector2D(300, -200), FVector2D(1000, 0), FVector2D(1900, 300), FVector2D(-2500, -2600), FVector2D(-650, 700) })
		{
			const float2 At(float(P.X), float(P.Y));
			const float Expected = DWX_ZoneSignedDistance(At, Z0, Box, FDreamZonePoints());
			TestNearlyEqual(*FString::Printf(TEXT("a square outline is its box at %s"), *P.ToString()), DWX_ZoneSignedDistance(At, Z0, Polygon, FDreamZonePoints(Square)), Expected, 0.05f);
			TestNearlyEqual(*FString::Printf(TEXT("either way round at %s"), *P.ToString()), DWX_ZoneSignedDistance(At, Z0, Polygon, FDreamZonePoints(Reversed)), Expected, 0.05f);
		}
	}

	UDreamWeatherSelector_Fixed* Fixed = NewObject<UDreamWeatherSelector_Fixed>();
	Fixed->Preset = Clear();
	FWeatherWorld W(Fixed);
	if (!TestNotNull(TEXT("weather"), W.Weather))
	{
		return false;
	}
	// An L: the quarter north-east of its centre is cut out.
	const TArray<FVector2D> L = { FVector2D(-2000, -2000), FVector2D(2000, -2000), FVector2D(2000, 0), FVector2D(0, 0), FVector2D(0, 2000), FVector2D(-2000, 2000) };
	ADreamWeatherZone* Zone = W.AddZone(FVector::ZeroVector, 0.0f, 1000.0f, 0, { { Channel_Precipitation, 1.0f }, { Channel_CloudCoverage, 1.0f } });
	Zone->Zone->SetPolygonPoints(L);
	W.Clock->Tick(0.0f);

	auto Rain = [&W](double X, double Y) { return W.Weather->SampleWeather(FVector(X, Y, 0.0)).Precipitation; };
	TestNearlyEqual(TEXT("full effect in an arm"), Rain(1000, -1000), 1.0f, 1e-4f);
	TestNearlyEqual(TEXT("and in the other"), Rain(-1000, 1500), 1.0f, 1e-4f);
	TestNearlyEqual(TEXT("nothing deep in the cut-out quarter"), Rain(1500, 1500), 0.0f, 1e-4f);
	const float Cover0 = W.Weather->GetGlobalValues().Get(C::CloudCoverage);
	const float Edge = 1.0f - FMath::SmoothStep(0.0f, 1000.0f, 300.0f);
	TestNearlyEqual(TEXT("the blend runs from the cut's edge"), W.Weather->SampleWeather(FVector(1200, 300, 0)).CloudCoverage, Cover0 + (1.0f - Cover0) * Edge, 1e-3f);

	// Turned with the zone's yaw: a quarter turn puts the cut-out north-west.
	Zone->Zone->SetWorldRotation(FRotator(0.0, 90.0, 0.0));
	TestNearlyEqual(TEXT("turned: the cut is north-west"), Rain(-1500, 1500), 0.0f, 1e-4f);
	TestNearlyEqual(TEXT("turned: the north-east is an arm"), Rain(1500, 1500), 1.0f, 1e-4f);
	Zone->Zone->SetWorldRotation(FRotator::ZeroRotator);

	// The weather map's records, run through the shared code on the CPU, give the query's answer around the outline.
	W.Clock->Tick(0.0f);
	const TArray<FVector4f>& Records = W.Weather->GetMapZones();
	TestEqual(TEXT("one zone in the map"), W.Weather->GetMapZoneCount(), 1);
	TestEqual(TEXT("its six points after it"), Records.Num(), 3 + 6);
	const FDreamWeatherValues& G = W.Weather->GetGlobalValues();
	for (const FVector2D& At : { FVector2D(1000, -1000), FVector2D(1500, 1500), FVector2D(1200, 300), FVector2D(300, 1200), FVector2D(-2400, 0), FVector2D(2600, 2600) })
	{
		float3 V(G.Get(C::CloudCoverage), G.Get(C::CloudDarkness), G.Get(C::Precipitation));
		V = DWX_ApplyZone(V, float2(float(At.X), float(At.Y)), ToHlsl(Records[0]), ToHlsl(Records[1]), ToHlsl(Records[2]), FDreamZonePoints(Records));
		const FDreamWeatherSample S = W.Weather->SampleWeather(FVector(At, 0.0));
		TestNearlyEqual(*FString::Printf(TEXT("map coverage matches the query at %s"), *At.ToString()), V.x, S.CloudCoverage, 1e-4f);
	}
	// Without a buffer for the points a record falls back to the circle around them.
	FVector4f Z0, Z1, Z2;
	TestTrue(TEXT("a record without points"), Zone->Zone->MakeMapRecord(Z0, Z1, Z2) && Z1.W == float(DWX_SHAPE_CIRCLE) && FMath::IsNearlyEqual(Z1.X, float(FVector2D(2000, 2000).Size()), 1.0f));

	// A long outline is thinned to what the shared code reads.
	TArray<FVector2D> Round;
	for (int32 i = 0; i < 200; ++i)
	{
		const double A = UE_TWO_PI * i / 200.0;
		Round.Add(FVector2D(FMath::Cos(A), FMath::Sin(A)) * 1500.0);
	}
	Zone->Zone->SetPolygonPoints(Round);
	TArray<FVector4f> Thinned;
	Zone->Zone->MakeFootprintRecord(0.0, Z0, Z1, Thinned);
	TestEqual(TEXT("thinned to 64 points"), Thinned.Num(), int32(DWX_MAX_POLYGON_POINTS));
	TestNearlyEqual(TEXT("and still round"), Rain(0, 1450), 1.0f, 1e-4f);

	// A drawn outline: the actor's closed spline, a point every OutlineSpacing along it.
	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	ADreamWeatherOutlineZone* Drawn = W.World->SpawnActor<ADreamWeatherOutlineZone>(FVector(20000, 0, 0), FRotator::ZeroRotator, Params);
	Drawn->Zone->BlendDistance = 1000.0f;
	Drawn->Zone->Channels.Add(Channel_Precipitation, 1.0f);
	Drawn->Zone->Channels.Add(Channel_CloudCoverage, 1.0f);
	Drawn->Outline->ClearSplinePoints(false);
	for (const FVector2D& P : L)
	{
		Drawn->Outline->AddSplinePoint(FVector(P, 0.0), ESplineCoordinateSpace::Local, false);
	}
	for (int32 i = 0; i < L.Num(); ++i)
	{
		Drawn->Outline->SetSplinePointType(i, ESplinePointType::Linear, false);
	}
	Drawn->Outline->UpdateSpline();
	Drawn->OutlineSpacing = 500.0f;
	Drawn->UpdateFootprint();
	TestTrue(TEXT("an outline zone is a polygon"), Drawn->Zone->Shape == EDreamWeatherZoneShape::Polygon && Drawn->Zone->Points.Num() == 32);
	TestNearlyEqual(TEXT("drawn: an arm"), Rain(21000, -1000), 1.0f, 1e-4f);
	TestNearlyEqual(TEXT("drawn: the cut"), Rain(21500, 1500), 0.0f, 1e-4f);
	// Its outline is no path: a drifting outline zone that follows a path stays where it is without one.
	Drawn->Zone->Motion.bEnabled = true;
	Drawn->Zone->Motion.bFollowPath = true;
	Drawn->Zone->Motion.Velocity = FVector2D::ZeroVector;
	Drawn->Zone->Motion.LifetimeHours = 0.0f;
	Drawn->Zone->Motion.StartSeconds = 0.0;
	TestTrue(TEXT("the outline is not a path"), Drawn->Zone->GetFootprintCenterAt(7200.0).Equals(FVector(20000, 0, 0), 1.0));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDreamWeatherEventsTest, "DreamWeather.EventsAndEnvironment", DreamWeather::Tests::Flags)
bool FDreamWeatherEventsTest::RunTest(const FString& Parameters)
{
	using namespace DreamWeather::Tests;
	FWeatherWorld W(ClearThenRain());
	if (!TestNotNull(TEXT("weather"), W.Weather))
	{
		return false;
	}
	UDreamWeatherTestListener* Listener = NewObject<UDreamWeatherTestListener>();
	Listener->Bind(W.Weather);

	W.Clock->SetTimeOfDay(2.5f);
	W.Clock->Tick(0.0f); // primes
	W.Clock->SetDayLengthMinutes(24.0f); // a game minute per real second
	for (int32 i = 0; i < 90; ++i)
	{
		W.Clock->Tick(1.0f);
	}
	TestEqual(TEXT("one change of weather"), Listener->Changes, 1);
	TestEqual(TEXT("to the rain"), Listener->LastCurrent ? Listener->LastCurrent->GetValue(C::Precipitation) : -1.0f, 0.8f);
	TestEqual(TEXT("rain started once"), Listener->Starts, 1);
	TestTrue(TEXT("the tags followed"), Listener->TagChanges > 0);
	TestTrue(TEXT("the view's tags say rain"), W.Weather->GetViewSample().Tags.HasTag(DreamWeather::Tags::State_Rain) || W.Weather->GetViewSample().Tags.HasTag(DreamWeather::Tags::State_Rain_Heavy));

	const FDreamWorldEnvironment& Env = W.Clock->GetEnvironment();
	TestTrue(TEXT("the environment is live"), Env.bActive);
	TestNearlyEqual(TEXT("the environment carries the view's rain"), Env.Precipitation, W.Weather->GetViewSample().Precipitation, 1e-5f);

	// The wind follows the weather.
	if (UDreamWindSubsystem* Wind = W.World->GetSubsystem<UDreamWindSubsystem>())
	{
		const UDreamWeatherSettings* S = UDreamWeatherSettings::Get();
		const float Expected = FMath::Lerp(float(S->WindSpeedRange.X), float(S->WindSpeedRange.Y), W.Weather->GetViewSample().Wind);
		TestNearlyEqual(TEXT("the wind blows as hard as the weather says"), Wind->GetGlobalWind().Speed, Expected, FMath::Max(1.0f, Expected * 0.011f));
	}

	// Into the dry slot: the rain stops once, with hysteresis.
	W.Clock->SetTimeOfDay(5.9f);
	W.Clock->Tick(0.0f);
	for (int32 i = 0; i < 40; ++i)
	{
		W.Clock->Tick(1.0f);
	}
	TestEqual(TEXT("rain stopped once"), Listener->Stops, 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDreamWeatherTemperatureTest, "DreamWeather.Temperature", DreamWeather::Tests::Flags)
bool FDreamWeatherTemperatureTest::RunTest(const FString& Parameters)
{
	using namespace DreamWeather::Tests;
	using namespace DreamWeather::Tags;
	UDreamWeatherSelector_Fixed* Fixed = NewObject<UDreamWeatherSelector_Fixed>();
	Fixed->Preset = MakePreset(TEXT("Drizzle"), { { Channel_Precipitation, 0.5f }, { Channel_CloudCoverage, 1.0f } });
	FWeatherWorld W(Fixed);
	if (!TestNotNull(TEXT("weather"), W.Weather))
	{
		return false;
	}
	W.Clock->SetTimeOfDay(15.0f);
	W.Clock->Tick(0.0f);
	const FDreamWeatherSample Low = W.Weather->SampleWeather(FVector(0, 0, 0));
	const FDreamWeatherSample High = W.Weather->SampleWeather(FVector(0, 0, 300000));
	const float Lapse = UDreamWeatherSettings::Get()->LapseRatePer100m * 30.0f;
	TestNearlyEqual(TEXT("3 km up is colder by the lapse rate"), Low.Temperature - High.Temperature, Lapse, 1e-3f);
	TestTrue(FString::Printf(TEXT("summer afternoon rain is rain (%.1f C)"), Low.Temperature), Low.Rain > 0.45f && Low.Snow < 0.05f);
	const FDreamWeatherSample Peak = W.Weather->SampleWeather(FVector(0, 0, 600000));
	TestTrue(FString::Printf(TEXT("at 6 km it snows (%.1f C)"), Peak.Temperature), Peak.Snow > 0.45f && Peak.Tags.HasTag(State_Snow));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDreamWeatherSnapshotTest, "DreamWeather.Snapshot", DreamWeather::Tests::Flags)
bool FDreamWeatherSnapshotTest::RunTest(const FString& Parameters)
{
	using namespace DreamWeather::Tests;
	using namespace DreamWeather::Tags;
	UDreamWeatherSelector_Fixed* FixedA = NewObject<UDreamWeatherSelector_Fixed>();
	FixedA->Preset = Clear();
	UDreamWeatherSelector_Fixed* FixedB = NewObject<UDreamWeatherSelector_Fixed>();
	FixedB->Preset = FixedA->Preset;
	FWeatherWorld A(FixedA);
	FWeatherWorld B(FixedB);
	if (!TestNotNull(TEXT("weather A"), A.Weather) || !TestNotNull(TEXT("weather B"), B.Weather))
	{
		return false;
	}
	FDreamWeatherModifierDesc Fog;
	Fog.Source = TEXT("Quest");
	Fog.Channels.Add(Channel_Fog, 0.9f);
	Fog.BlendInSeconds = 4.0f;
	A.Weather->PushModifier(Fog);
	A.Clock->Tick(1.0f);

	const FDreamWorldSnapshot Snapshot = A.Clock->CaptureSnapshot();
	TestTrue(TEXT("the weather wrote its part"), Snapshot.Extensions.Contains(TEXT("DreamWeather")));
	B.Clock->ApplySnapshot(Snapshot);
	B.Clock->Tick(0.0f);
	TestEqual(TEXT("the modifier came back"), B.Weather->GetNumModifiers(), 1);
	TestNearlyEqual(TEXT("at the same strength"), B.Weather->GetGlobalValues().Get(C::Fog), A.Weather->GetGlobalValues().Get(C::Fog), 1e-4f);
	A.Clock->Tick(3.0f);
	B.Clock->Tick(3.0f);
	TestNearlyEqual(TEXT("and carries on the same way"), B.Weather->GetGlobalValues().Get(C::Fog), A.Weather->GetGlobalValues().Get(C::Fog), 1e-4f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDreamWeatherShelterTest, "DreamWeather.Shelter", DreamWeather::Tests::Flags)
bool FDreamWeatherShelterTest::RunTest(const FString& Parameters)
{
	using namespace DreamWeather::Tests;
	UDreamWeatherSelector_Fixed* Fixed = NewObject<UDreamWeatherSelector_Fixed>();
	Fixed->Preset = Rain();
	FWeatherWorld W(Fixed);
	if (!TestNotNull(TEXT("weather"), W.Weather))
	{
		return false;
	}
	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	ADreamWeatherShelter* Hut = W.World->SpawnActor<ADreamWeatherShelter>(FVector(0, 0, 100), FRotator(0, 45, 0), Params);
	Hut->Shelter->Extent = FVector(300, 300, 200);
	Hut->Shelter->FadeDistance = 100.0f;
	W.Clock->Tick(0.0f);

	const FDreamWeatherSample Inside = W.Weather->SampleWeather(FVector(0, 0, 100));
	const FDreamWeatherSample Outside = W.Weather->SampleWeather(FVector(2000, 0, 100));
	TestTrue(TEXT("it rains"), Inside.Rain > 0.5f);
	TestEqual(TEXT("no sky inside"), Inside.Exposure, 0.0f);
	TestTrue(TEXT("sheltered inside"), Inside.bSheltered && W.Weather->IsSheltered(FVector(0, 0, 100)));
	TestEqual(TEXT("no rain lands inside"), Inside.RainHere, 0.0f);
	TestEqual(TEXT("open sky outside"), Outside.Exposure, 1.0f);
	TestNearlyEqual(TEXT("all the rain lands outside"), Outside.RainHere, Outside.Rain, 1e-6f);
	// The box is turned 45 degrees: a point 350 cm along its own X axis is 50 cm outside, half way across the fade.
	const FVector Edge = FVector(0, 0, 100) + FRotator(0, 45, 0).RotateVector(FVector(350, 0, 0));
	TestNearlyEqual(TEXT("the shelter fades out around the box"), W.Weather->GetExposureAt(Edge), 0.5f, 1e-3f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDreamWeatherLightningTest, "DreamWeather.Lightning.Strikes", DreamWeather::Tests::Flags)
bool FDreamWeatherLightningTest::RunTest(const FString& Parameters)
{
	using namespace DreamWeather::Tests;
	using namespace DreamWeather::Tags;
	auto Storm = [] { UDreamWeatherSelector_Fixed* F = NewObject<UDreamWeatherSelector_Fixed>(); F->Preset = MakePreset(TEXT("Storm"), { { Channel_Lightning, 1.0f }, { Channel_CloudCoverage, 1.0f }, { Channel_CloudDarkness, 1.0f } }); return F; };
	FWeatherWorld A(Storm());
	FWeatherWorld B(Storm());
	if (!TestNotNull(TEXT("weather A"), A.Weather) || !TestNotNull(TEXT("weather B"), B.Weather))
	{
		return false;
	}
	UDreamWeatherTestListener* Listener = NewObject<UDreamWeatherTestListener>();
	Listener->Bind(A.Weather);
	bool bFlashed = false;
	for (int32 i = 0; i < 480; ++i) // two real minutes
	{
		A.Clock->Tick(0.25f);
		B.Clock->Tick(0.25f);
		bFlashed |= A.Clock->GetEnvironment().LightningFlash > 0.0f;
	}
	const float Expected = UDreamWeatherSettings::Get()->StrikesPerMinute * 2.0f;
	TestTrue(FString::Printf(TEXT("about %.0f strikes in two minutes (%d)"), Expected, A.Weather->GetStrikeCount()), A.Weather->GetStrikeCount() >= Expected * 0.4f && A.Weather->GetStrikeCount() <= Expected * 2.0f);
	TestEqual(TEXT("every strike was reported"), Listener->Strikes, A.Weather->GetStrikeCount());
	TestEqual(TEXT("the same seed and frames give the same storm"), B.Weather->GetStrikeCount(), A.Weather->GetStrikeCount());
	TestEqual(TEXT("the same last strike"), B.Weather->GetLastStrike().Location, A.Weather->GetLastStrike().Location);
	TestTrue(TEXT("strikes flash"), bFlashed);
	const FDreamLightningStrike& Last = A.Weather->GetLastStrike();
	const FVector2D Range = UDreamWeatherSettings::Get()->StrikeRange;
	TestTrue(FString::Printf(TEXT("strikes land within range (%.0f cm)"), Last.Distance), Last.Distance >= Range.X - 1.0 && Last.Distance <= Range.Y + 1.0);
	TestNearlyEqual(TEXT("thunder follows by distance"), Last.ThunderDelay, Last.Distance / 100000.0f * UDreamWeatherSettings::Get()->ThunderSecondsPerKm, 1e-4f);

	// A lightning rod right here, reaching everywhere, draws every strike.
	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	AActor* Mast = A.World->SpawnActor<AActor>(Params);
	UDreamLightningRodComponent* Rod = NewObject<UDreamLightningRodComponent>(Mast);
	Mast->SetRootComponent(Rod);
	Rod->AttractRadius = 1.0e7f;
	Rod->AttractChance = 1.0f;
	Rod->RegisterComponent();
	Rod->SetWorldLocation(FVector(100, 200, 3000));
	Rod->OnStruck.AddDynamic(Listener, &UDreamWeatherTestListener::HandleRodHit);
	const int32 Before = A.Weather->GetStrikeCount();
	for (int32 i = 0; i < 480; ++i)
	{
		A.Clock->Tick(0.25f);
	}
	const int32 During = A.Weather->GetStrikeCount() - Before;
	TestTrue(TEXT("it kept striking"), During > 0);
	TestEqual(TEXT("the rod drew them all"), Listener->RodHits, During);
	TestEqual(TEXT("onto the rod"), A.Weather->GetLastStrike().Location, Rod->GetComponentLocation());

	// No lightning, no strikes.
	UDreamWeatherSelector_Fixed* Calm = NewObject<UDreamWeatherSelector_Fixed>();
	Calm->Preset = Clear();
	FWeatherWorld C(Calm);
	for (int32 i = 0; i < 480; ++i)
	{
		C.Clock->Tick(0.25f);
	}
	TestEqual(TEXT("clear skies never strike"), C.Weather->GetStrikeCount(), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDreamWeatherLightningInFrontTest, "DreamWeather.Lightning.InFront", DreamWeather::Tests::Flags)
bool FDreamWeatherLightningInFrontTest::RunTest(const FString& Parameters)
{
	using namespace DreamWeather::Tests;
	using namespace DreamWeather::Tags;
	UDreamWeatherSettings* Settings = GetMutableDefault<UDreamWeatherSettings>();
	const float SavedBias = Settings->StrikeViewBias;
	ON_SCOPE_EXIT { Settings->StrikeViewBias = SavedBias; };

	// Strikes during four minutes of storm, looking along +Y: how many landed in the half of the sky in front.
	auto Count = [](float Bias, int32& InFront, int32& Behind)
	{
		GetMutableDefault<UDreamWeatherSettings>()->StrikeViewBias = Bias;
		UDreamWeatherSelector_Fixed* Storm = NewObject<UDreamWeatherSelector_Fixed>();
		Storm->Preset = MakePreset(TEXT("Storm"), { { Channel_Lightning, 1.0f }, { Channel_CloudCoverage, 1.0f }, { Channel_CloudDarkness, 1.0f } });
		FWeatherWorld W(Storm);
		W.Clock->ReportView(FVector(0, 0, 170), FRotator(0, 90, 0), true);
		InFront = 0;
		Behind = 0;
		int32 Seen = 0;
		for (int32 i = 0; i < 960; ++i)
		{
			W.Clock->Tick(0.25f);
			if (W.Weather->GetStrikeCount() != Seen)
			{
				Seen = W.Weather->GetStrikeCount();
				(W.Weather->GetLastStrike().Location.Y >= 0.0 ? InFront : Behind) += 1;
			}
		}
	};
	int32 InFront = 0;
	int32 Behind = 0;
	Count(1.0f, InFront, Behind);
	TestTrue(FString::Printf(TEXT("it struck (%d)"), InFront + Behind), InFront + Behind > 5);
	TestEqual(TEXT("fully biased: every strike lands in front of the view"), Behind, 0);
	Count(0.0f, InFront, Behind);
	TestTrue(FString::Printf(TEXT("unbiased: strikes all around (%d in front, %d behind)"), InFront, Behind), Behind > 0 && InFront > 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDreamWeatherFlashEnvelopeTest, "DreamWeather.Lightning.FlashEnvelope", DreamWeather::Tests::Flags)
bool FDreamWeatherFlashEnvelopeTest::RunTest(const FString& Parameters)
{
	// Three strokes at 0, 0.15 and 0.3 s in a 1.2 s flash.
	const float Times[4] = { 0.0f, 0.15f, 0.3f, 0.0f };
	const float Levels[4] = { 1.0f, 0.6f, 0.5f, 0.0f };
	auto E = [&](float T) { return UDreamWeatherSubsystem::FlashEnvelope(T, 1.2f, 3, Times, Levels); };
	TestEqual(TEXT("full at the first stroke"), E(0.0f), 1.0f);
	TestTrue(TEXT("dies between strokes"), E(0.14f) < 0.25f);
	TestTrue(TEXT("flares again at the second stroke"), E(0.15f) > E(0.14f) + 0.3f);
	TestTrue(TEXT("and the third"), E(0.3f) > E(0.29f) + 0.2f);
	TestNearlyEqual(TEXT("then the afterglow"), E(0.6f), 0.25f * 0.25f, 0.01f);
	TestTrue(TEXT("which fades"), E(1.1f) > 0.0f && E(1.1f) < E(0.8f));
	TestEqual(TEXT("over at the end"), E(1.2f), 0.0f);
	TestEqual(TEXT("nothing before"), E(-0.01f), 0.0f);

	// A storm's flashes stutter: the flash rises again after falling, within one flash.
	using namespace DreamWeather::Tests;
	using namespace DreamWeather::Tags;
	UDreamWeatherSelector_Fixed* Storm = NewObject<UDreamWeatherSelector_Fixed>();
	Storm->Preset = MakePreset(TEXT("Storm"), { { Channel_Lightning, 1.0f }, { Channel_CloudCoverage, 1.0f }, { Channel_CloudDarkness, 1.0f } });
	FWeatherWorld W(Storm);
	if (!TestNotNull(TEXT("weather"), W.Weather))
	{
		return false;
	}
	int32 Rises = 0;
	float Prev = 0.0f;
	bool bFalling = false;
	for (int32 i = 0; i < 60 * 60; ++i) // a minute at 60 fps
	{
		W.Clock->Tick(1.0f / 60.0f);
		const float F = W.Weather->GetLightningFlash();
		if (F < Prev - 1e-4f)
		{
			bFalling = true;
		}
		else if (F > Prev + 0.05f && bFalling && Prev > 0.0f)
		{
			++Rises;
			bFalling = false;
		}
		Prev = F;
	}
	TestTrue(FString::Printf(TEXT("flashes flicker with several strokes (%d re-flares)"), Rises), Rises > 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDreamWeatherFallPathsTest, "DreamWeather.FallPaths", DreamWeather::Tests::Flags)
bool FDreamWeatherFallPathsTest::RunTest(const FString& Parameters)
{
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
	if (!TestNotNull(TEXT("world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	// A roof 4 m above the ground over everything west of x = 0 (its top at z = 410).
	FActorSpawnParameters Spawn;
	Spawn.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	AActor* RoofActor = World->SpawnActor<AActor>(Spawn);
	UBoxComponent* Roof = NewObject<UBoxComponent>(RoofActor);
	Roof->SetBoxExtent(FVector(5000, 10000, 10), false);
	Roof->SetCollisionProfileName(UCollisionProfile::BlockAll_ProfileName);
	RoofActor->SetRootComponent(Roof);
	Roof->RegisterComponent();
	Roof->SetWorldLocation(FVector(-5000, 0, 400));

	const FVector View(0, 0, 170);
	const FVector Fall(0, 0, -900);
	FDreamWeatherFallPaths::FParams Params;
	Params.Capacity = 512;
	Params.Radius = 2000.0f;
	Params.ForwardBias = 0.0f;
	Params.StartHeight = 2500.0f;
	Params.MaxLength = 6000.0f;
	auto Open = [](const FVector&) { return 0.0f; };

	FDreamWeatherFallPaths Paths;
	Paths.Fill(World, View, FVector::ForwardVector, Fall, Params, Open);
	TestEqual(TEXT("a full buffer"), Paths.GetEnds().Num(), Params.Capacity);
	TestEqual(TEXT("one trace per path"), Paths.GetTracesCast(), int64(Params.Capacity));
	int32 OnRoof = 0, InAir = 0, Wrong = 0;
	for (int32 i = 0; i < Paths.GetEnds().Num(); ++i)
	{
		const FVector4& End = Paths.GetEnds()[i];
		const FVector& N = Paths.GetNormals()[i];
		if (End.X < -1.0)
		{
			// Under the roof: the drop stops on it, after falling from 2670 to 410.
			OnRoof += FMath::IsNearlyEqual(End.Z, 410.0, 1.0) && N.Z > 0.99 && FMath::IsNearlyEqual(End.W, (2670.0 - 410.0) / 900.0, 0.01) ? 1 : 0;
		}
		else if (End.X > 1.0)
		{
			// Out in the open with no ground in this world: the path runs out in the air.
			InAir += N.IsNearlyZero() && FMath::IsNearlyEqual(End.Z, 2670.0 - Params.MaxLength, 1.0) ? 1 : 0;
		}
		Wrong += FVector2D(End.X - View.X, End.Y - View.Y).Size() > Params.Radius + 1.0 ? 1 : 0;
	}
	TestTrue(FString::Printf(TEXT("paths west land on the roof (%d)"), OnRoof), OnRoof > Params.Capacity / 3);
	TestTrue(FString::Printf(TEXT("paths east end in the air (%d)"), InAir), InAir > Params.Capacity / 3);
	TestEqual(TEXT("every path lands within the radius"), Wrong, 0);
	TestEqual(TEXT("landed = the roof"), Paths.CountLanded(), OnRoof);

	// A shelter without geometry (east of x = 0, below 1500) stops the paths at its top.
	auto ShelterEast = [](const FVector& P) { return P.X > 0.0 && P.Z < 1500.0 ? 1.0f : 0.0f; };
	Paths.Fill(World, View, FVector::ForwardVector, Fall, Params, ShelterEast);
	int32 Sheltered = 0;
	for (const FVector4& End : Paths.GetEnds())
	{
		Sheltered += End.X > 1.0 && FMath::IsNearlyEqual(End.Z, 1500.0, 10.0) ? 1 : 0;
	}
	TestTrue(FString::Printf(TEXT("a shelter stops the rain at its top (%d)"), Sheltered), Sheltered > Params.Capacity / 3);

	// Wind: with ground at the view's height, blown rain still lands around the view (the paths start upwind),
	// short of it only where the roof catches the drops first.
	AActor* GroundActor = World->SpawnActor<AActor>(Spawn);
	UBoxComponent* Ground = NewObject<UBoxComponent>(GroundActor);
	Ground->SetBoxExtent(FVector(20000, 20000, 10), false);
	Ground->SetCollisionProfileName(UCollisionProfile::BlockAll_ProfileName);
	GroundActor->SetRootComponent(Ground);
	Ground->RegisterComponent();
	Ground->SetWorldLocation(FVector(0, 0, 160));
	const FVector Windy(400, 0, -900);
	Paths.Fill(World, View, FVector::ForwardVector, Windy, Params, Open);
	int32 Strayed = 0;
	for (const FVector4& End : Paths.GetEnds())
	{
		Strayed += FVector2D(End.X - View.X, End.Y - View.Y).Size() > Params.Radius + 150.0 ? 1 : 0;
	}
	TestEqual(TEXT("blown rain still lands around the view"), Strayed, 0);
	TestEqual(TEXT("and all of it lands on something"), Paths.CountLanded(), Params.Capacity);

	// Forward bias: the paths land ahead of the view.
	Params.ForwardBias = 1.0f;
	Paths.Fill(World, View, FVector(0, 1, 0), Fall, Params, Open);
	int32 Behind = 0;
	for (const FVector4& End : Paths.GetEnds())
	{
		Behind += End.Y < View.Y - 1.0 ? 1 : 0;
	}
	TestEqual(TEXT("biased paths all land ahead"), Behind, 0);

	// Updates spread the traces: a quarter of the refresh time re-traces about a quarter of the buffer.
	const int64 Before = Paths.GetTracesCast();
	const uint32 Generation = Paths.GetGeneration();
	Paths.Update(World, View, FVector(0, 1, 0), Fall, Params.RefreshSeconds * 0.25f, Params, Open);
	const int64 Traced = Paths.GetTracesCast() - Before;
	TestTrue(FString::Printf(TEXT("a quarter of the buffer (%lld)"), Traced), FMath::Abs(Traced - Params.Capacity / 4) <= 1);
	TestTrue(TEXT("and the effects hear of it"), Paths.GetGeneration() != Generation);

	// A drop is its own path's segment. The wind turns (a gust) after half the buffer was traced: those paths keep the
	// start they were traced from, and a drop runs from it to the end at (end - start) / w. Sent instead from the end
	// less the new wind times its fall time, it would start kilometres off and slant through whatever stands beside
	// its landing point (rain cutting through a hut's roof).
	Params.ForwardBias = 0.0f;
	Paths.Fill(World, View, FVector::ForwardVector, Windy, Params, Open);
	const TArray<FVector> StartsBefore = Paths.GetStarts();
	const TArray<FVector4> EndsBefore = Paths.GetEnds();
	const FVector Gust(-600, 300, -900);
	Paths.Update(World, View, FVector::ForwardVector, Gust, Params.RefreshSeconds * 0.5f, Params, Open);
	const int32 Count = Paths.GetEnds().Num();
	TestEqual(TEXT("a start for every path"), Paths.GetStarts().Num(), Count);
	int32 Kept = 0, Along = 0, OwnSpeed = 0, Clear = 0, NotFromTheGust = 0, FromTheGust = 0;
	for (int32 i = 0; i < FMath::Min(Count, Paths.GetStarts().Num()); ++i)
	{
		const FVector Start = Paths.GetStarts()[i];
		const FVector4& E = Paths.GetEnds()[i];
		const FVector End(E.X, E.Y, E.Z);
		const bool bKept = Start.Equals(StartsBefore[i], 0.01) && E.Equals(EndsBefore[i], 0.01);
		const FVector TracedWith = bKept ? Windy : Gust;
		const FVector DropVelocity = (End - Start) / FMath::Max(E.W, 1.0e-6);
		Kept += bKept ? 1 : 0;
		Along += (DropVelocity.GetSafeNormal() | TracedWith.GetSafeNormal()) > 0.9999 ? 1 : 0;
		OwnSpeed += FMath::IsNearlyEqual(DropVelocity.Size(), TracedWith.Size(), 1.0) ? 1 : 0;
		FHitResult Hit;
		Clear += World->LineTraceSingleByChannel(Hit, Start, End - (End - Start).GetSafeNormal() * 2.0, ECC_Visibility) ? 0 : 1;
		const double FromGust = FVector::Dist(Start, End - Gust * E.W);
		NotFromTheGust += bKept && FromGust > 100.0 ? 1 : 0;
		FromTheGust += !bKept && FromGust < 1.0 ? 1 : 0;
	}
	TestTrue(FString::Printf(TEXT("half the paths still from before the gust (%d)"), Kept), FMath::Abs(Kept - Count / 2) <= 1);
	TestEqual(TEXT("every drop runs along the wind its path was traced in"), Along, Count);
	TestEqual(TEXT("at that wind's speed"), OwnSpeed, Count);
	TestEqual(TEXT("through nothing until its end (the roof, the ground)"), Clear, Count);
	TestEqual(TEXT("a path from before the gust does not start where the gust would put it"), NotFromTheGust, Kept);
	TestEqual(TEXT("one traced in the gust does"), FromTheGust, Count - Kept);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDreamWeatherSkyModifiersTest, "DreamWeather.SkyModifiers", DreamWeather::Tests::Flags)
bool FDreamWeatherSkyModifiersTest::RunTest(const FString& Parameters)
{
	using namespace DreamWeather::Tests;
	using namespace DreamWeather::Tags;
	auto Weight = [](const FDreamWorldEnvironment& Env, const UObject* Modifier)
	{
		float W = 0.0f;
		for (const FDreamWorldWeightedObject& E : Env.SkyModifiers)
		{
			W += E.Object.Get() == Modifier ? E.Weight : 0.0f;
		}
		return W;
	};
	UDreamWeatherPreset* Fair = Clear();
	UDreamWeatherTestSkyModifier* FairSky = NewObject<UDreamWeatherTestSkyModifier>();
	Fair->SkyModifier = FairSky;
	UDreamWeatherPreset* Wet = Rain();
	UDreamWeatherTestSkyModifier* WetSky = NewObject<UDreamWeatherTestSkyModifier>();
	Wet->SkyModifier = WetSky;
	UDreamWeatherPreset* Storm = Rain();
	UDreamWeatherTestSkyModifier* StormSky = NewObject<UDreamWeatherTestSkyModifier>();
	Storm->SkyModifier = StormSky;

	UDreamWeatherSelector_Fixed* Fixed = NewObject<UDreamWeatherSelector_Fixed>();
	Fixed->Preset = Fair;
	FWeatherWorld W(Fixed);
	if (!TestNotNull(TEXT("weather"), W.Weather))
	{
		return false;
	}
	W.Clock->Tick(0.0f);
	TestNearlyEqual(TEXT("the scheduled weather's sky modifier in full"), Weight(W.Clock->GetEnvironment(), FairSky), 1.0f, 1e-4f);
	TestEqual(TEXT("and nothing else"), W.Clock->GetEnvironment().SkyModifiers.Num(), 1);

	// Forced weather blends in over the schedule as its channels do: half way, half each.
	W.Weather->ForceWeather(Wet, 10.0f);
	W.Clock->Tick(5.0f);
	TestNearlyEqual(TEXT("forced half way in"), Weight(W.Clock->GetEnvironment(), WetSky), 0.5f, 1e-3f);
	TestNearlyEqual(TEXT("the schedule's half way out"), Weight(W.Clock->GetEnvironment(), FairSky), 0.5f, 1e-3f);
	W.Clock->Tick(6.0f);
	TestNearlyEqual(TEXT("forced in full"), Weight(W.Clock->GetEnvironment(), WetSky), 1.0f, 1e-4f);
	TestEqual(TEXT("the schedule's gone"), Weight(W.Clock->GetEnvironment(), FairSky), 0.0f);

	// A pushed set of channels brings no preset, and takes nothing away.
	FDreamWeatherModifierDesc Wind;
	Wind.Channels.Add(Channel_Wind, 1.0f);
	W.Weather->PushModifier(Wind);
	W.Clock->Tick(0.0f);
	TestNearlyEqual(TEXT("channels alone leave the sky modifiers"), Weight(W.Clock->GetEnvironment(), WetSky), 1.0f, 1e-4f);

	// A zone the view stands in, with a preset: its sky modifier by the zone's weight at the view, over the rest.
	ADreamWeatherZone* Zone = W.AddZone(FVector(500.0f, 0.0f, 0.0f), 1000.0f, 1000.0f, 0, {});
	Zone->Zone->Preset = Storm;
	W.Clock->Tick(0.0f);
	TestNearlyEqual(TEXT("in the storm zone, its sky"), Weight(W.Clock->GetEnvironment(), StormSky), 1.0f, 1e-4f);
	TestEqual(TEXT("the forced weather's under it"), Weight(W.Clock->GetEnvironment(), WetSky), 0.0f);
	Zone->Zone->Weight = 0.25f;
	W.Clock->Tick(0.0f);
	TestNearlyEqual(TEXT("a quarter-weight zone, a quarter of its sky"), Weight(W.Clock->GetEnvironment(), StormSky), 0.25f, 1e-4f);
	TestNearlyEqual(TEXT("and three quarters of the forced weather's"), Weight(W.Clock->GetEnvironment(), WetSky), 0.75f, 1e-4f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDreamWeatherCloudPaintTest, "DreamWeather.CloudPaint", DreamWeather::Tests::Flags)
bool FDreamWeatherCloudPaintTest::RunTest(const FString& Parameters)
{
	using namespace DreamWeather::Tests;
	// The painting: a brush stroke pulls the middle all the way, nothing past its rim; erasing gives it back.
	UDreamCloudPainting* Paint = NewObject<UDreamCloudPainting>();
	Paint->Initialize(64);
	TestTrue(TEXT("a blank painting"), Paint->IsValidPainting() && Paint->Sample(FVector2D(0.5, 0.5)).Equals(FVector2f::ZeroVector));
	TestTrue(TEXT("a stroke changes it"), Paint->PaintAt(FVector2D(0.5, 0.5), 0.1f, 1.0f, 1.0f));
	TestTrue(TEXT("overcast and pulled all the way in the middle"), Paint->Sample(FVector2D(0.5, 0.5)).Equals(FVector2f(1.0f, 1.0f), 0.04f));
	TestNearlyEqual(TEXT("nothing past the brush's rim"), Paint->Sample(FVector2D(0.8, 0.5)).Y, 0.0f, 1e-4f);
	TestNearlyEqual(TEXT("nothing outside the footprint"), Paint->Sample(FVector2D(1.2, 0.5)).Y, 0.0f, 1e-6f);
	Paint->PaintAt(FVector2D(0.5, 0.5), 0.1f, 0.0f, 1.0f, true);
	TestTrue(TEXT("erased in the middle"), Paint->Sample(FVector2D(0.5, 0.5)).Y < 0.05f);
	// Paint over paint: a bare texel takes the colour outright, a half painted one mixes in the share added.
	Paint->Fill(0.0f, 0.0f);
	Paint->PaintAt(FVector2D(0.5, 0.5), 0.2f, 0.0f, 0.5f);
	TestTrue(TEXT("a bare texel: clear sky at half"), Paint->Sample(FVector2D(0.5, 0.5)).Equals(FVector2f(0.0f, 0.5f), 0.02f));
	Paint->PaintAt(FVector2D(0.5, 0.5), 0.2f, 1.0f, 0.5f);
	TestTrue(TEXT("clouds over it: three quarters pulled, two thirds overcast"), Paint->Sample(FVector2D(0.5, 0.5)).Equals(FVector2f(2.0f / 3.0f, 0.75f), 0.02f));
	Paint->Resize(128);
	TestTrue(TEXT("resized, the painting kept"), Paint->Resolution == 128 && Paint->Sample(FVector2D(0.5, 0.5)).Equals(FVector2f(2.0f / 3.0f, 0.75f), 0.03f));
	const uint32 Before = Paint->GetRevision();
	Paint->Fill(1.0f, 1.0f);
	TestTrue(TEXT("every change a new revision"), Paint->GetRevision() != Before);
	TestTrue(TEXT("its texture"), !FApp::CanEverRender() || (Paint->GetTexture() && Paint->GetTexture()->GetSizeX() == 128));

	// In the world: the painting over a clear sky, inside its footprint only, scaled by its strength.
	UDreamWeatherSelector_Fixed* Fixed = NewObject<UDreamWeatherSelector_Fixed>();
	Fixed->Preset = Clear();
	FWeatherWorld W(Fixed);
	if (!TestNotNull(TEXT("weather"), W.Weather))
	{
		return false;
	}
	W.Clock->Tick(0.0f);
	const float Own = W.Weather->SampleWeather(FVector::ZeroVector).CloudCoverage;
	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	ADreamCloudPaint* Actor = W.World->SpawnActor<ADreamCloudPaint>(FVector(10000.0, 0.0, 0.0), FRotator::ZeroRotator, Params);
	if (!TestNotNull(TEXT("the painting's actor"), Actor))
	{
		return false;
	}
	TestNull(TEXT("no painting yet: none in use"), W.Weather->GetActiveCloudPaint());
	Actor->CloudPaint->Paint = Paint;
	Actor->CloudPaint->Size = 100000.0;
	TestTrue(TEXT("in use"), W.Weather->GetActiveCloudPaint() == Actor->CloudPaint);
	TestNearlyEqual(TEXT("overcast where it is painted"), W.Weather->SampleWeather(FVector(10000.0, 0.0, 0.0)).CloudCoverage, 1.0f, 1e-3f);
	TestNearlyEqual(TEXT("the weather's own outside its footprint"), W.Weather->SampleWeather(FVector(200000.0, 0.0, 0.0)).CloudCoverage, Own, 1e-4f);
	TestTrue(TEXT("its footprint's record"), Actor->CloudPaint->GetFootprintRecord().Equals(FVector4f(-40000.0f, -50000.0f, 1.0e-5f, 1.0e-5f), 1e-3f));
	Actor->CloudPaint->Strength = 0.5f;
	TestNearlyEqual(TEXT("half strength: half way"), W.Weather->SampleWeather(FVector(10000.0, 0.0, 0.0)).CloudCoverage, FMath::Lerp(Own, 1.0f, 0.5f), 1e-3f);
	Paint->Fill(0.0f, 1.0f);
	Actor->CloudPaint->Strength = 1.0f;
	W.AddZone(FVector(10000.0, 0.0, 0.0), 1000.0f, 1000.0f, 100, { { DreamWeather::Tags::Channel_CloudCoverage, 1.0f }, { DreamWeather::Tags::Channel_Precipitation, 1.0f } });
	W.Clock->Tick(0.0f);
	const FDreamWeatherSample Under = W.Weather->SampleWeather(FVector(10000.0, 0.0, 0.0));
	TestNearlyEqual(TEXT("over the zones: clear sky painted over a storm zone"), Under.CloudCoverage, 0.0f, 1e-3f);
	TestNearlyEqual(TEXT("and no rain under the clear sky"), Under.Precipitation, 0.0f, 1e-4f);
	IConsoleVariable* Switch = IConsoleManager::Get().FindConsoleVariable(TEXT("DreamWeather.CloudPaint"));
	if (TestNotNull(TEXT("the painting's switch"), Switch))
	{
		Switch->Set(0, ECVF_SetByCode);
		TestNull(TEXT("switched off: none in use"), W.Weather->GetActiveCloudPaint());
		Switch->Set(1, ECVF_SetByCode);
	}
	Actor->CloudPaint->bEnabled = false;
	TestNull(TEXT("turned off: none in use"), W.Weather->GetActiveCloudPaint());

	// The weather showing: a forced one over the schedule's (what the widgets name, what a config keeps).
	UDreamWeatherPreset* Wet = Rain();
	TestTrue(TEXT("the schedule's when nothing is forced"), W.Weather->GetForcedWeather() == nullptr && W.Weather->GetWeatherShown() == W.Weather->GetCurrentWeather());
	W.Weather->ForceWeather(Wet, 0.0f);
	TestTrue(TEXT("the forced one showing"), W.Weather->GetForcedWeather() == Wet && W.Weather->GetWeatherShown() == Wet);
	W.Weather->ClearForcedWeather(0.0f);
	TestNull(TEXT("none forced once cleared"), W.Weather->GetForcedWeather());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDreamWorldConfigTest, "DreamWeather.WorldConfig", DreamWeather::Tests::Flags)
bool FDreamWorldConfigTest::RunTest(const FString& Parameters)
{
	using namespace DreamWeather::Tests;
	// Every settings object of the plugin found, the weather's among them.
	const TArray<UClass*> Classes = UDreamWorldConfig::GetSettingsClasses();
	TestTrue(TEXT("the weather's settings found"), Classes.Contains(UDreamWeatherSettings::StaticClass()));
	TestTrue(TEXT("seven or more settings objects"), Classes.Num() >= 7);

	UDreamWeatherSettings* Settings = GetMutableDefault<UDreamWeatherSettings>();
	const float Saved = Settings->MeanTemperature;
	IConsoleVariable* Meteors = IConsoleManager::Get().FindConsoleVariable(TEXT("DreamSky.Meteors"));
	const FString SavedMeteors = Meteors ? Meteors->GetString() : FString();
	ON_SCOPE_EXIT
	{
		Settings->MeanTemperature = Saved;
		if (Meteors)
		{
			Meteors->Set(*SavedMeteors, ECVF_SetByConsole);
		}
	};

	UDreamWeatherSelector_Fixed* Fixed = NewObject<UDreamWeatherSelector_Fixed>();
	Fixed->Preset = Clear();
	FWeatherWorld W(Fixed);
	if (!TestNotNull(TEXT("weather"), W.Weather))
	{
		return false;
	}
	W.Clock->SetTimeOfDay(9.5f);
	W.Clock->Tick(0.0f);
	Settings->MeanTemperature = 21.5f;
	if (Meteors)
	{
		Meteors->Set(Meteors->GetInt() != 0 ? 0 : 1, ECVF_SetByConsole);
	}
	const FString Shown = Meteors ? Meteors->GetString() : FString();

	UDreamWorldConfig* Config = NewObject<UDreamWorldConfig>();
	Config->Capture(W.World);
	TestTrue(TEXT("the settings captured"), Config->Settings.Num() == Classes.Num());
	TestTrue(TEXT("the time of day captured"), Config->WorldState.Contains(TEXT("TimeOfDay")));
	TestEqual(TEXT("nothing differs right after"), Config->DescribeDifferences(W.World).Num(), 0);
	// (A console variable at its default value is not set by hand: only one moved off it is kept.)
	const bool bMeteorsKept = Meteors && Config->ConsoleVariables.Contains(TEXT("DreamSky.Meteors"));
	TestTrue(TEXT("a console variable set off its default by hand captured"), !Meteors || Shown == Meteors->GetDefaultValue() || bMeteorsKept);

	// Changed afterwards: the differences listed, and Apply puts it all back.
	Settings->MeanTemperature = 3.0f;
	if (bMeteorsKept)
	{
		Meteors->Set(Shown == TEXT("0") ? 1 : 0, ECVF_SetByConsole);
	}
	W.Clock->SetTimeOfDay(20.0f);
	TestTrue(TEXT("the differences listed"), Config->DescribeDifferences(W.World).Num() >= 2);
	const int32 Changed = Config->Apply(W.World, false);
	TestTrue(TEXT("the values changed back"), Changed >= 2);
	TestNearlyEqual(TEXT("the setting back"), Settings->MeanTemperature, 21.5f, 1e-4f);
	TestTrue(TEXT("the console variable back"), !bMeteorsKept || Meteors->GetString() == Shown);
	TestNearlyEqual(TEXT("the time of day back"), W.Clock->GetTimeOfDay(), 9.5f, 0.01f);
	TestTrue(TEXT("the config applied last"), UDreamWorldConfig::GetLastApplied() == Config);
	TestEqual(TEXT("nothing differs after"), Config->DescribeDifferences(W.World).Num(), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDreamWorldWidgetsTest, "DreamWeather.Widgets", DreamWeather::Tests::Flags)
bool FDreamWorldWidgetsTest::RunTest(const FString& Parameters)
{
	TestEqual(TEXT("24 hour time"), UDreamClockWidget::FormatTime(7.0833f, true).ToString(), FString(TEXT("07:04")));
	TestEqual(TEXT("12 hour time"), UDreamClockWidget::FormatTime(19.5f, false).ToString(), FString(TEXT("7:30 PM")));
	TestEqual(TEXT("midnight on a 12 hour clock"), UDreamClockWidget::FormatTime(0.25f, false).ToString(), FString(TEXT("12:15 AM")));
	TestEqual(TEXT("wraps past midnight"), UDreamClockWidget::FormatTime(25.0f, true).ToString(), FString(TEXT("01:00")));
	TestEqual(TEXT("Celsius"), UDreamWeatherWidget::FormatTemperature(17.6f, false).ToString(), FString(TEXT("18°C")));
	TestEqual(TEXT("Fahrenheit"), UDreamWeatherWidget::FormatTemperature(20.0f, true).ToString(), FString(TEXT("68°F")));
	FDreamWeatherSample S;
	S.CloudCoverage = 0.1f;
	TestTrue(TEXT("clear"), UDreamWeatherWidget::IconFor(S) == EDreamWeatherIcon::Clear);
	S.CloudCoverage = 0.5f;
	TestTrue(TEXT("partly cloudy"), UDreamWeatherWidget::IconFor(S) == EDreamWeatherIcon::PartlyCloudy);
	S.CloudCoverage = 0.9f;
	TestTrue(TEXT("cloudy"), UDreamWeatherWidget::IconFor(S) == EDreamWeatherIcon::Cloudy);
	S.Fog = 0.8f;
	TestTrue(TEXT("fog"), UDreamWeatherWidget::IconFor(S) == EDreamWeatherIcon::Fog);
	S.Rain = 0.5f;
	S.Precipitation = 0.5f;
	TestTrue(TEXT("rain over fog"), UDreamWeatherWidget::IconFor(S) == EDreamWeatherIcon::Rain);
	S.Snow = 0.3f;
	TestTrue(TEXT("snow over rain"), UDreamWeatherWidget::IconFor(S) == EDreamWeatherIcon::Snow);
	S.Hail = 0.3f;
	TestTrue(TEXT("hail over snow"), UDreamWeatherWidget::IconFor(S) == EDreamWeatherIcon::Hail);
	S.Lightning = 0.8f;
	TestTrue(TEXT("a storm over all"), UDreamWeatherWidget::IconFor(S) == EDreamWeatherIcon::Storm);
	// The widgets build their Slate (with no world: the designer's placeholders).
	UDreamClockWidget* Clock = NewObject<UDreamClockWidget>();
	UDreamWeatherWidget* Weather = NewObject<UDreamWeatherWidget>();
	if (FSlateApplication::IsInitialized())
	{
		TestTrue(TEXT("the clock builds"), Clock->TakeWidget() != SNullWidget::NullWidget);
		TestTrue(TEXT("the weather badge builds"), Weather->TakeWidget() != SNullWidget::NullWidget);
	}
	EDreamWeatherIcon Icon;
	bool bDay;
	FText Name;
	float Celsius;
	Weather->GetShown(Icon, bDay, Name, Celsius);
	TestTrue(TEXT("without a world: a fair day"), Icon == EDreamWeatherIcon::PartlyCloudy && bDay);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
