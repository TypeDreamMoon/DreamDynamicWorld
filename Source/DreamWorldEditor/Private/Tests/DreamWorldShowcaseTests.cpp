// Copyright DreamMoon. All Rights Reserved.

#include "DreamWorldShowcaseSetup.h"

#include "DreamSky.h"
#include "DreamSkyConditions.h"
#include "DreamSkyModifier.h"
#include "DreamSurfaceRoofCapture.h"
#include "DreamWeather.h"
#include "DreamWeatherFallPaths.h"
#include "DreamWeatherModule.h"
#include "DreamWeatherPreset.h"
#include "DreamWeatherSelector.h"
#include "DreamWeatherTemperature.h"
#include "DreamWeatherZone.h"
#include "DreamWindForceComponent.h"
#include "DreamWindGlobalActor.h"
#include "DreamWorldTracks.h"

#include "Components/StaticMeshComponent.h"
#include "Engine/Level.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/World.h"
#include "LevelSequence.h"
#include "LevelSequenceActor.h"
#include "Misc/AutomationTest.h"
#include "Misc/PackageName.h"
#include "MovieScene.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace DreamWorld::ShowcaseTests
{
	constexpr EAutomationTestFlags Flags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter;

	UWorld* LoadLevel(const TCHAR* Package)
	{
		const FString Path = FString::Printf(TEXT("%s.%s"), Package, *FPackageName::GetShortName(Package));
		return LoadObject<UWorld>(nullptr, *Path);
	}

	template <typename T>
	TArray<T*> ActorsOf(UWorld* World)
	{
		TArray<T*> Found;
		if (World && World->PersistentLevel)
		{
			for (AActor* Actor : World->PersistentLevel->Actors)
			{
				if (T* Typed = Cast<T>(Actor))
				{
					Found.Add(Typed);
				}
			}
		}
		return Found;
	}

	int32 CountLabelled(UWorld* World, const TCHAR* Prefix)
	{
		int32 Count = 0;
		for (AActor* Actor : ActorsOf<AActor>(World))
		{
			Count += Actor->GetActorLabel().StartsWith(Prefix) ? 1 : 0;
		}
		return Count;
	}

	int32 CountTagged(UWorld* World, const FName& Tag)
	{
		int32 Count = 0;
		for (AActor* Actor : ActorsOf<AActor>(World))
		{
			Count += Actor->Tags.Contains(Tag) ? 1 : 0;
		}
		return Count;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDreamWorldShowcaseContentsTest, "DreamWorld.Showcase.Contents", DreamWorld::ShowcaseTests::Flags)
bool FDreamWorldShowcaseContentsTest::RunTest(const FString& Parameters)
{
	using namespace DreamWorld::ShowcaseTests;
	UWorld* World = LoadLevel(DreamWorld::Setup::ShowcaseMapPackage);
	if (!TestNotNull(TEXT("the showcase level (run Setup -WorldShowcase)"), World))
	{
		return false;
	}

	// The short day and the night's aurora.
	const TArray<ADreamSky*> Skies = ActorsOf<ADreamSky>(World);
	if (TestEqual(TEXT("one sky"), Skies.Num(), 1))
	{
		const ADreamSky* Sky = Skies[0];
		TestTrue(TEXT("a short day"), Sky->Clock.DayLengthMinutes > 0.0f && Sky->Clock.DayLengthMinutes <= 15.0f);
		bool bAurora = false;
		for (const FDreamSkyConditionalModifier& Entry : Sky->ConditionalModifiers)
		{
			bAurora |= Entry.Modifier && Entry.Modifier->Settings.bOverride_Aurora && Entry.Modifier->Settings.Aurora > 0.5f
				&& Entry.Condition.bHours && Entry.Condition.StartHour >= 18.0f && Entry.Condition.EndHour <= 6.0f;
		}
		TestTrue(TEXT("an aurora modifier for the night"), bAurora);
	}

	// The day's weather in order, a cycle of a day.
	const TArray<ADreamWeather*> Weathers = ActorsOf<ADreamWeather>(World);
	const UDreamWeatherSelector_Sequence* Schedule = Weathers.Num() == 1 ? Cast<UDreamWeatherSelector_Sequence>(Weathers[0]->Selector) : nullptr;
	if (TestNotNull(TEXT("a weather schedule"), Schedule))
	{
		const TCHAR* Expected[] = { TEXT("WP_Clear"), TEXT("WP_Cloudy"), TEXT("WP_Rain"), TEXT("WP_Storm"), TEXT("WP_Overcast"), TEXT("WP_Snow"), TEXT("WP_Dust"), TEXT("WP_Clear") };
		if (TestEqual(TEXT("eight steps"), Schedule->Entries.Num(), int32(UE_ARRAY_COUNT(Expected))))
		{
			float Hours = 0.0f;
			for (int32 i = 0; i < Schedule->Entries.Num(); ++i)
			{
				const UDreamWeatherPreset* Preset = Schedule->Entries[i].Preset;
				TestEqual(*FString::Printf(TEXT("step %d"), i), Preset ? Preset->GetName() : FString(), FString(Expected[i]));
				Hours += Schedule->Entries[i].Hours;
			}
			TestEqual(TEXT("a cycle of 24 hours"), Hours, 24.0f, 0.01f);
		}
	}

	// The set.
	TestTrue(TEXT("a hut with walls, a doorway and a window"), CountLabelled(World, TEXT("Hut_Wall")) >= 6 && CountLabelled(World, TEXT("Hut_WindowPane")) == 1);
	TestTrue(TEXT("roofs the rain knows (hut, porch)"), CountTagged(World, FDreamWeatherFallPaths::RoofTag) >= 2);
	TestEqual(TEXT("a porch roof capture box"), ActorsOf<ADreamSurfaceRoofCapture>(World).Num(), 1);
	TestTrue(TEXT("a pond and a window in the open"), CountLabelled(World, TEXT("Pond")) == 1 && CountLabelled(World, TEXT("Window_Pane")) == 1);
	TestTrue(TEXT("a grove of leaves"), CountTagged(World, FDreamWeatherFallPaths::LeavesTag) >= 3);

	bool bSnowField = false;
	ADreamWeatherStorm* Storm = nullptr;
	for (ADreamWeatherZone* Zone : ActorsOf<ADreamWeatherZone>(World))
	{
		if (ADreamWeatherStorm* AsStorm = Cast<ADreamWeatherStorm>(Zone))
		{
			Storm = AsStorm;
			continue;
		}
		bSnowField |= Zone->Zone->Shape == EDreamWeatherZoneShape::Polygon && Zone->Zone->Points.Num() >= 8
			&& Zone->Zone->Channels.FindRef(DreamWeather::Tags::Channel_SnowRatio) > 0.5f;
	}
	TestTrue(TEXT("an irregular snow field"), bSnowField);
	TestTrue(TEXT("a storm that drifts through"), Storm && Storm->Zone->Motion.bEnabled && Storm->Zone->Motion.StartSeconds >= 0.0);

	bool bCampfire = false;
	for (ADreamTemperatureVolume* Volume : ActorsOf<ADreamTemperatureVolume>(World))
	{
		bCampfire |= Volume->Volume->Temperature > 10.0f;
	}
	TestTrue(TEXT("a campfire's warm air"), bCampfire);

	int32 Crates = 0;
	for (AStaticMeshActor* Actor : ActorsOf<AStaticMeshActor>(World))
	{
		Crates += Actor->FindComponentByClass<UDreamWindForceComponent>() && Actor->GetStaticMeshComponent()->BodyInstance.bSimulatePhysics ? 1 : 0;
	}
	TestTrue(TEXT("crates the wind pushes"), Crates >= 3);
	const TArray<ADreamWindGlobal*> Winds = ActorsOf<ADreamWindGlobal>(World);
	TestTrue(TEXT("wind with the level's heights baked"), Winds.Num() == 1 && Winds[0]->TerrainMap != nullptr);
	TestEqual(TEXT("no sequence playing in the day level"), ActorsOf<ALevelSequenceActor>(World).Num(), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDreamWorldShowcaseDuskTest, "DreamWorld.Showcase.DuskSequence", DreamWorld::ShowcaseTests::Flags)
bool FDreamWorldShowcaseDuskTest::RunTest(const FString& Parameters)
{
	using namespace DreamWorld::ShowcaseTests;
	UWorld* World = LoadLevel(DreamWorld::Setup::ShowcaseDuskMapPackage);
	if (!TestNotNull(TEXT("the dusk level (run Setup -WorldShowcase)"), World))
	{
		return false;
	}
	const TArray<ALevelSequenceActor*> Players = ActorsOf<ALevelSequenceActor>(World);
	if (!TestEqual(TEXT("one sequence player"), Players.Num(), 1))
	{
		return false;
	}
	const ALevelSequenceActor* Player = Players[0];
	TestTrue(TEXT("it plays at once, in cinematic mode"), Player->PlaybackSettings.bAutoPlay && Player->PlaybackSettings.bDisableMovementInput
		&& Player->PlaybackSettings.bHideHud);
	const ULevelSequence* Sequence = Cast<ULevelSequence>(Player->GetSequence());
	const UMovieScene* Scene = Sequence ? Sequence->GetMovieScene() : nullptr;
	if (!TestNotNull(TEXT("its sequence"), Scene))
	{
		return false;
	}
	const UDreamWorldTimeTrack* TimeTrack = Scene->FindTrack<UDreamWorldTimeTrack>();
	const UDreamWeatherTrack* WeatherTrack = Scene->FindTrack<UDreamWeatherTrack>();
	if (TestNotNull(TEXT("a time track"), TimeTrack) && TestEqual(TEXT("one time section"), TimeTrack->GetAllSections().Num(), 1))
	{
		const UDreamWorldTimeSection* Time = Cast<UDreamWorldTimeSection>(TimeTrack->GetAllSections()[0]);
		const TArrayView<const FMovieSceneFloatValue> Keys = Time ? Time->Hours.GetValues() : TArrayView<const FMovieSceneFloatValue>();
		TestTrue(TEXT("from dusk to night"), Keys.Num() == 2 && FMath::IsNearlyEqual(Keys[0].Value, 17.0f) && FMath::IsNearlyEqual(Keys[1].Value, 23.0f));
	}
	if (TestNotNull(TEXT("a weather track"), WeatherTrack) && TestEqual(TEXT("two weather sections"), WeatherTrack->GetAllSections().Num(), 2))
	{
		TArray<FString> Presets;
		for (UMovieSceneSection* Section : WeatherTrack->GetAllSections())
		{
			const UDreamWeatherPresetSection* AsPreset = Cast<UDreamWeatherPresetSection>(Section);
			Presets.Add(AsPreset && AsPreset->Preset ? AsPreset->Preset->GetName() : FString());
		}
		TestTrue(TEXT("clear, then rain"), Presets.Contains(TEXT("WP_Clear")) && Presets.Contains(TEXT("WP_Rain")));
	}
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
