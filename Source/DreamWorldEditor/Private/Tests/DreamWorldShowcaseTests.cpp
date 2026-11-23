// Copyright DreamMoon. All Rights Reserved.

#include "DreamWorldShowcaseSetup.h"
#include "DreamWeatherFog.h"
#include "DreamSurfaceInteraction.h"
#include "DreamSurfaceWalker.h"
#include "DreamSurfaceSetup.h"
#include "DreamWindTerrainMap.h"

#include "DreamSky.h"
#include "DreamSkyConditions.h"
#include "DreamSkyModifier.h"
#include "DreamSkyStyle.h"
#include "DreamSurfaceRoofCapture.h"
#include "DreamWeather.h"
#include "DreamWeatherCloudPaint.h"
#include "DreamWeatherRadar.h"
#include "DreamWorldWidgets.h"
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
#include "Components/PointLightComponent.h"
#include "Engine/PointLight.h"
#include "Engine/StaticMesh.h"
#include "LevelSequence.h"
#include "Materials/MaterialInterface.h"
#include "MeshDescription.h"
#include "StaticMeshAttributes.h"
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
		bool bHalo = false;
		for (const FDreamSkyConditionalModifier& Entry : Sky->ConditionalModifiers)
		{
			bHalo |= Entry.Modifier && Entry.Modifier->Settings.bOverride_HighCloudCover && Entry.Modifier->Settings.HighCloudCover
				>= GetDefault<UDreamSkyStyle>()->Halos.HighCoverFull && Entry.Condition.bHours && Entry.Condition.StartHour < 12.0f;
		}
		TestTrue(TEXT("a sheet of high cloud for the morning's halo"), bHalo);
		bool bShine = false;
		for (const FDreamSkyConditionalModifier& Entry : Sky->ConditionalModifiers)
		{
			bShine |= Entry.Modifier && Entry.Modifier->Settings.bOverride_Noctilucent && Entry.Modifier->Settings.Noctilucent > 0.5f
				&& Entry.Condition.bHours && Entry.Condition.StartHour >= 17.0f && Entry.Condition.EndHour <= 21.0f;
		}
		TestTrue(TEXT("noctilucent clouds for the evening's twilight"), bShine);
		bool bStorm = false;
		for (const FDreamSkyConditionalModifier& Entry : Sky->ConditionalModifiers)
		{
			bStorm |= Entry.Modifier && Entry.Modifier->Settings.bOverride_Meteors && Entry.Modifier->Settings.Meteors > 1.0f && Entry.Condition.bHours
				&& Entry.Condition.StartHour >= 20.0f && Entry.Condition.EndHour <= 6.0f;
		}
		TestTrue(TEXT("a meteor storm for the night"), bStorm);
	}

	TestEqual(TEXT("a weather radar screen"), ActorsOf<ADreamWeatherRadarDisplay>(World).Num(), 1);
	TestEqual(TEXT("a screen of the clock and weather widgets"), ActorsOf<ADreamWorldWidgetsDisplay>(World).Num(), 1);
	{
		const TArray<ADreamCloudPaint*> Paints = ActorsOf<ADreamCloudPaint>(World);
		const UDreamCloudPainting* Painting = Paints.Num() == 1 ? Paints[0]->CloudPaint->Paint.Get() : nullptr;
		TestTrue(TEXT("a cloud painting"), Painting && Painting->IsValidPainting());
		// A bank of cloud in the north (12 km out), nothing painted over the set.
		TestTrue(TEXT("cloud painted in the north"), Painting && Painting->Sample(Paints[0]->CloudPaint->WorldToUV(FVector(1200000.0, 0.0, 0.0))).Y > 0.5f);
		TestTrue(TEXT("nothing painted over the set"), Painting && Painting->Sample(Paints[0]->CloudPaint->WorldToUV(FVector::ZeroVector)).Y < 0.01f);
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

	// Enough light inside to see by: a wide window, and a dim warm lamp under the roof.
	for (AStaticMeshActor* Actor : ActorsOf<AStaticMeshActor>(World))
	{
		if (Actor->GetActorLabel().StartsWith(TEXT("Hut_WindowPane")))
		{
			const FVector Size = Actor->GetActorScale3D();   // of a 1 m cube: across, through, up
			TestTrue(*FString::Printf(TEXT("a wide window (%.1f m2)"), Size.X * Size.Z), Size.X * Size.Z >= 3.0);
		}
	}
	int32 Lamps = 0;
	for (APointLight* Lamp : ActorsOf<APointLight>(World))
	{
		const UPointLightComponent* Light = Cast<UPointLightComponent>(Lamp->GetLightComponent());
		const FVector At = Lamp->GetActorLocation();
		if (!Light || !Lamp->GetActorLabel().StartsWith(TEXT("Hut_Lamp")))
		{
			continue;
		}
		++Lamps;
		TestTrue(TEXT("the lamp is inside the hut, under its roof"), FMath::Abs(At.X - 2450.0) < 280.0 && FMath::Abs(At.Y - 700.0) < 280.0 && At.Z < 300.0);
		TestTrue(TEXT("the lamp is warm"), Light->bUseTemperature && Light->Temperature < 3500.0f);
		TestTrue(TEXT("the lamp is dim"), Light->IntensityUnits == ELightUnits::Candelas && Light->Intensity > 0.0f && Light->Intensity <= 50.0f);
	}
	TestEqual(TEXT("one lamp in the hut"), Lamps, 1);
	TestTrue(TEXT("roofs the rain knows (hut, porch)"), CountTagged(World, FDreamWeatherFallPaths::RoofTag) >= 2);
	TestEqual(TEXT("a porch roof capture box"), ActorsOf<ADreamSurfaceRoofCapture>(World).Num(), 1);
	TestEqual(TEXT("a fog bank"), ActorsOf<ADreamFogBank>(World).Num(), 1);
	TestEqual(TEXT("wind fog"), ActorsOf<ADreamWindFog>(World).Num(), 1);
	const TArray<ADreamSurfaceWalker*> Walkers = ActorsOf<ADreamSurfaceWalker>(World);
	if (TestEqual(TEXT("a walker"), Walkers.Num(), 1))
	{
		const ADreamSurfaceWalker* Walker = Walkers[0];
		TestNotNull(TEXT("with a body"), Walker->Body->GetStaticMesh().Get());
		TestTrue(TEXT("whose feet press prints and stir the puddles"), Walker->LeftFoot->bAffectsPuddles && Walker->RightFoot->bAffectsSnowAndDust);
		TestTrue(TEXT("going round the square"), FVector2D::Distance(Walker->Center, FVector2D(1300.0, 400.0)) < 200.0 && Walker->Radii.GetMin() > 200.0);
	}
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
	bool bHailField = false;
	for (ADreamWeatherZone* Zone : ActorsOf<ADreamWeatherZone>(World))
	{
		bHailField |= Zone->Zone->Channels.FindRef(DreamWeather::Tags::Channel_Hail) > 0.5f;
	}
	TestTrue(TEXT("a patch where it hails"), bHailField);
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
	if (TestTrue(TEXT("wind with the level's heights baked"), Winds.Num() == 1 && Winds[0]->TerrainMap && Winds[0]->TerrainMap->IsValidMap()))
	{
		// The bake saw the set's collision: the hut's roof (its top near 320 cm) is in the heights, the square beside it is low.
		const UDreamWindTerrainMap* Map = Winds[0]->TerrainMap;
		auto HeightAt = [Map](double X, double Y) { return double(Map->Sample(FVector2D(X, Y)).W) + Map->HeightRange.X; };
		TestTrue(*FString::Printf(TEXT("the hut's roof in the heights (%.0f cm)"), HeightAt(2450.0, 700.0)), HeightAt(2450.0, 700.0) > 280.0);
		TestTrue(*FString::Printf(TEXT("the square low (%.0f cm)"), HeightAt(1100.0, 300.0)), HeightAt(1100.0, 300.0) < 50.0);
	}

	// The crowns in a muted green, not a toon colour.
	for (AStaticMeshActor* Actor : ActorsOf<AStaticMeshActor>(World))
	{
		if (Actor->Tags.Contains(FDreamWeatherFallPaths::LeavesTag))
		{
			const UMaterialInterface* Material = Actor->GetStaticMeshComponent()->GetMaterial(0);
			TestEqual(TEXT("a crown's material"), Material ? Material->GetName() : FString(), FString(TEXT("MI_DreamSurfaceLeaves")));
		}
	}

	// The pond's rim lies just inside the bank's waterline all round: under water at the rim, above it a little further out.
	for (AStaticMeshActor* Actor : ActorsOf<AStaticMeshActor>(World))
	{
		if (!Actor->GetActorLabel().StartsWith(TEXT("Pond")))
		{
			continue;
		}
		const UStaticMesh* Mesh = Actor->GetStaticMeshComponent()->GetStaticMesh();
		const FMeshDescription* Description = Mesh ? Mesh->GetMeshDescription(0) : nullptr;
		if (!TestNotNull(TEXT("the pond's own mesh"), Description))
		{
			break;
		}
		TestTrue(TEXT("the pond's level"), FMath::IsNearlyEqual(Actor->GetActorLocation().Z, DreamWorld::Setup::ShowcasePondLevel, 0.1));
		FStaticMeshConstAttributes Attributes(*Description);
		const TVertexAttributesConstRef<FVector3f> Positions = Attributes.GetVertexPositions();
		int32 Rim = 0, Wet = 0, Dry = 0;
		for (const FVertexID Vertex : Description->Vertices().GetElementIDs())
		{
			const FVector2D Local(Positions[Vertex].X, Positions[Vertex].Y);
			if (Local.Size() < 1.0)
			{
				continue;
			}
			const FVector2D At = FVector2D(Actor->GetActorLocation()) + Local;
			const FVector2D Beyond = At + Local.GetSafeNormal() * (2.0 * DreamWorld::Setup::ShowcasePondInset);
			++Rim;
			Wet += DreamWorld::SurfaceSetup::GroundHeight(At.X, At.Y) < DreamWorld::Setup::ShowcasePondLevel ? 1 : 0;
			Dry += DreamWorld::SurfaceSetup::GroundHeight(Beyond.X, Beyond.Y) > DreamWorld::Setup::ShowcasePondLevel ? 1 : 0;
		}
		TestTrue(*FString::Printf(TEXT("a round rim (%d sides)"), Rim), Rim >= 96);
		TestEqual(TEXT("the rim under water all round"), Wet, Rim);
		TestEqual(TEXT("the bank above water just beyond it all round"), Dry, Rim);
	}

	// The land out to the horizon: ranges kilometres out and up to some two kilometres high, under the set's ground where
	// they meet, with no collision.
	int32 FarLands = 0;
	for (AStaticMeshActor* Actor : ActorsOf<AStaticMeshActor>(World))
	{
		if (!Actor->GetActorLabel().StartsWith(TEXT("FarLand")))
		{
			continue;
		}
		++FarLands;
		const UStaticMeshComponent* Component = Actor->GetStaticMeshComponent();
		const FBox Bounds = Component->GetStaticMesh() ? Component->GetStaticMesh()->GetBoundingBox() : FBox(ForceInit);
		TestTrue(*FString::Printf(TEXT("the far land reaches the horizon (%.0f km)"), Bounds.GetExtent().X / 1.0e5), Bounds.GetExtent().X > 3.0e6);
		TestTrue(*FString::Printf(TEXT("its peaks stand 1-2.5 km high (%.2f km)"), Bounds.Max.Z / 1.0e5), Bounds.Max.Z > 1.0e5 && Bounds.Max.Z < 2.5e5);
		TestTrue(TEXT("it has no collision"), Component->GetCollisionEnabled() == ECollisionEnabled::NoCollision);
		TestTrue(TEXT("it wears the terrain's surface material (snow lies on it)"), Component->GetMaterial(0) && Component->GetMaterial(0)->GetName().Contains(TEXT("Terrain")));
	}
	TestEqual(TEXT("one far land"), FarLands, 1);
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
