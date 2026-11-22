// Copyright DreamMoon. All Rights Reserved.

#include "DreamWorldShowcaseSetup.h"
#include "DreamDynamicWorldModule.h"
#include "DreamSky.h"
#include "DreamSkyConditions.h"
#include "DreamSkyModifier.h"
#include "DreamSkyStyle.h"
#include "DreamSurfaceMask.h"
#include "DreamSurfaceRoofCapture.h"
#include "DreamSurfaceSetup.h"
#include "DreamWeather.h"
#include "DreamWeatherFallPaths.h"
#include "DreamWeatherModule.h"
#include "DreamWeatherPreset.h"
#include "DreamWeatherSelector.h"
#include "DreamWeatherTemperature.h"
#include "DreamWeatherZone.h"
#include "DreamWindEditorLibrary.h"
#include "DreamWindForceComponent.h"
#include "DreamWindGlobalActor.h"
#include "DreamWindPassThroughComponent.h"
#include "DreamWorldCelestial.h"
#include "DreamWorldTracks.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "PhysicsEngine/BodySetup.h"
#include "GameFramework/PlayerStart.h"
#include "LevelSequence.h"
#include "LevelSequenceActor.h"
#include "Materials/MaterialInterface.h"
#include "MovieScene.h"
#include "Misc/PackageName.h"
#include "UObject/Package.h"

namespace DreamWorld::ShowcaseSetup
{
	/** The showcase's day: in January, so the snow settles; the weather cycle starts at midnight. */
	const FDateTime Day(2026, 1, 20, 0, 0, 0);

	UDreamWeatherPreset* Preset(const TCHAR* Name)
	{
		return LoadObject<UDreamWeatherPreset>(nullptr, *FString::Printf(TEXT("/DreamDynamicWorld/Weather/Presets/%s.%s"), Name, Name));
	}

	/** An asset object made afresh in its own package (what is there is replaced). */
	template <typename T>
	T* FreshAsset(const TCHAR* PackagePath)
	{
		UPackage* Package = SurfaceSetup::FreshPackage(PackagePath);
		T* Asset = NewObject<T>(Package, *FPackageName::GetShortName(PackagePath), RF_Public | RF_Standalone);
		FAssetRegistryModule::AssetCreated(Asset);
		return Asset;
	}

	/** The night's aurora: a sky modifier that lights it fully (the sky shows it only once it is dark). */
	UDreamSkyModifier* EnsureAurora()
	{
		UDreamSkyModifier* Aurora = FreshAsset<UDreamSkyModifier>(Setup::ShowcaseAuroraPackage);
		Aurora->Settings.bOverride_Aurora = true;
		Aurora->Settings.Aurora = 1.0f;
		Aurora->FadeInSeconds = 6.0f;
		Aurora->FadeOutSeconds = 6.0f;
		return SurfaceSetup::SaveAsset(Aurora) ? Aurora : nullptr;
	}

	/**
	 * Dusk into night as the clear sky turns to rain, twenty seconds: the time track from five in the afternoon to eleven
	 * at night, the weather track clear for the first eight seconds, then rain eased in over four.
	 */
	ULevelSequence* EnsureDuskSequence()
	{
		ULevelSequence* Sequence = FreshAsset<ULevelSequence>(Setup::ShowcaseDuskSequencePackage);
		Sequence->Initialize();
		UMovieScene* Scene = Sequence->GetMovieScene();
		const FFrameRate Tick = Scene->GetTickResolution();
		auto At = [&Tick](double Seconds) { return (Seconds * Tick).FloorToFrame(); };
		const FFrameNumber End = At(20.0);
		Scene->SetPlaybackRange(TRange<FFrameNumber>(0, End));

		UDreamWorldTimeTrack* TimeTrack = Scene->AddTrack<UDreamWorldTimeTrack>();
		UDreamWorldTimeSection* Time = Cast<UDreamWorldTimeSection>(TimeTrack->CreateNewSection());
		Time->SetRange(TRange<FFrameNumber>(0, End));
		Time->Hours.AddLinearKey(0, 17.0f);
		Time->Hours.AddLinearKey(End, 23.0f);
		TimeTrack->AddSection(*Time);

		UDreamWeatherTrack* WeatherTrack = Scene->AddTrack<UDreamWeatherTrack>();
		UDreamWeatherPresetSection* Clear = Cast<UDreamWeatherPresetSection>(WeatherTrack->CreateNewSection());
		Clear->Preset = Preset(TEXT("WP_Clear"));
		Clear->SetRange(TRange<FFrameNumber>(0, At(12.0)));
		WeatherTrack->AddSection(*Clear);
		UDreamWeatherPresetSection* Rain = Cast<UDreamWeatherPresetSection>(WeatherTrack->CreateNewSection());
		Rain->Preset = Preset(TEXT("WP_Rain"));
		Rain->SetRange(TRange<FFrameNumber>(At(8.0), End));
		Rain->Easing.bManualEaseIn = true;
		Rain->Easing.ManualEaseInDuration = At(4.0).Value;
		Rain->SetRowIndex(1);
		WeatherTrack->AddSection(*Rain);
		return SurfaceSetup::SaveAsset(Sequence) ? Sequence : nullptr;
	}

	struct FShowcaseKit
	{
		UStaticMesh* Cube = nullptr;
		UStaticMesh* Cylinder = nullptr;
		UStaticMesh* Sphere = nullptr;
		UStaticMesh* Cone = nullptr;
		UStaticMesh* Plane = nullptr;
		UStaticMesh* Ground = nullptr;
		UStaticMesh* Figure = nullptr;
		UMaterialInterface* Terrain = nullptr;
		UMaterialInterface* Water = nullptr;
		UMaterialInterface* Glass = nullptr;
		UMaterialInterface* Stone = nullptr;
		UMaterialInterface* Paving = nullptr;
		UMaterialInterface* Metal = nullptr;
		UMaterialInterface* Wood = nullptr;
		UMaterialInterface* ToonRed = nullptr;
		UMaterialInterface* ToonBlue = nullptr;
		UMaterialInterface* ToonYellow = nullptr;

		bool Load()
		{
			Cube = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
			Cylinder = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
			Sphere = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Sphere.Sphere"));
			Cone = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cone.Cone"));
			Plane = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Plane.Plane"));
			Ground = SurfaceSetup::EnsureGround();
			Figure = SurfaceSetup::EnsureFigure();
			const TCHAR* Demo = TEXT("/DreamDynamicWorld/Surface/Materials/M_DreamSurfaceDemo.M_DreamSurfaceDemo");
			const TCHAR* Toon = TEXT("/DreamDynamicWorld/Surface/Materials/M_DreamSurfaceToonDemo.M_DreamSurfaceToonDemo");
			Terrain = LoadObject<UMaterialInterface>(nullptr, TEXT("/DreamDynamicWorld/Surface/Materials/M_DreamSurfaceTerrainDemo.M_DreamSurfaceTerrainDemo"));
			Water = LoadObject<UMaterialInterface>(nullptr, TEXT("/DreamDynamicWorld/Surface/Materials/M_DreamSurfaceWaterDemo.M_DreamSurfaceWaterDemo"));
			Glass = LoadObject<UMaterialInterface>(nullptr, TEXT("/DreamDynamicWorld/Surface/Materials/M_DreamGlassDemo.M_DreamGlassDemo"));
			// The surface test level's instances (the same names and values: made afresh, they come out the same).
			Stone = SurfaceSetup::EnsureInstance(TEXT("MI_DreamSurfaceStone"), Demo, FLinearColor(0.42f, 0.4f, 0.37f), 0.8f, 0.0f);
			Paving = SurfaceSetup::EnsureInstance(TEXT("MI_DreamSurfacePaving"), Demo, FLinearColor(0.5f, 0.47f, 0.43f), 0.7f, 0.0f);
			Metal = SurfaceSetup::EnsureInstance(TEXT("MI_DreamSurfaceMetal"), Demo, FLinearColor(0.85f, 0.86f, 0.88f), 0.3f, 1.0f);
			Wood = SurfaceSetup::EnsureInstance(TEXT("MI_DreamSurfaceWood"), Demo, FLinearColor(0.4f, 0.26f, 0.14f), 0.6f, 0.0f);
			ToonRed = SurfaceSetup::EnsureInstance(TEXT("MI_DreamSurfaceToonRed"), Toon, FLinearColor(0.85f, 0.36f, 0.3f), 0.6f, -1.0f);
			ToonBlue = SurfaceSetup::EnsureInstance(TEXT("MI_DreamSurfaceToonBlue"), Toon, FLinearColor(0.3f, 0.46f, 0.82f), 0.6f, -1.0f);
			ToonYellow = SurfaceSetup::EnsureInstance(TEXT("MI_DreamSurfaceToonYellow"), Toon, FLinearColor(0.92f, 0.78f, 0.35f), 0.5f, -1.0f);
			return Cube && Cylinder && Sphere && Cone && Plane && Ground && Figure && Terrain && Water && Glass && Stone && Paving && Metal && Wood
				&& ToonRed && ToonBlue && ToonYellow;
		}
	};

	/** A box of this size (cm) centred here, a 1 m cube scaled. */
	AStaticMeshActor* AddBox(UWorld& World, const FShowcaseKit& Kit, UMaterialInterface* Material, const FVector& Center, const FVector& Size, const TCHAR* Label)
	{
		return SurfaceSetup::AddMesh(World, Kit.Cube, Material, Center, Size / 100.0, FRotator::ZeroRotator, Label);
	}

	/** Left out of the wind's level heights: its top is no roof over the ground beside it. */
	void PassThrough(AActor* Actor)
	{
		if (Actor)
		{
			Actor->Tags.AddUnique(UDreamWindPassThroughComponent::PassThroughTag);
		}
	}

	/**
	 * A hut to step into: stone walls, a doorway on the -X side, a window on the -Y side, a wooden roof (the rain's
	 * roof); a table and a stool inside. The walls block the view probe's rays, so inside counts as indoors.
	 */
	void AddHut(UWorld& World, const FShowcaseKit& Kit, const FVector& Center)
	{
		const double Half = 300.0, Height = 300.0, Wall = 20.0, Door = 120.0, DoorHeight = 220.0;
		const double WindowWide = 140.0, Sill = 100.0, Head = 210.0;
		AddBox(World, Kit, Kit.Stone, Center + FVector(Half, 0.0, Height * 0.5), FVector(Wall, 2.0 * Half, Height), TEXT("Hut_WallBack"));
		AddBox(World, Kit, Kit.Stone, Center + FVector(0.0, Half, Height * 0.5), FVector(2.0 * Half, Wall, Height), TEXT("Hut_WallLeft"));
		// The -Y wall round the window.
		const double Side = (2.0 * Half - WindowWide) * 0.5;
		AddBox(World, Kit, Kit.Stone, Center + FVector(-(WindowWide * 0.5 + Side * 0.5), -Half, Height * 0.5), FVector(Side, Wall, Height), TEXT("Hut_WallRight"));
		AddBox(World, Kit, Kit.Stone, Center + FVector(WindowWide * 0.5 + Side * 0.5, -Half, Height * 0.5), FVector(Side, Wall, Height), TEXT("Hut_WallRight"));
		AddBox(World, Kit, Kit.Stone, Center + FVector(0.0, -Half, Sill * 0.5), FVector(WindowWide, Wall, Sill), TEXT("Hut_UnderWindow"));
		AddBox(World, Kit, Kit.Stone, Center + FVector(0.0, -Half, (Head + Height) * 0.5), FVector(WindowWide, Wall, Height - Head), TEXT("Hut_OverWindow"));
		SurfaceSetup::AddMesh(World, Kit.Cube, Kit.Glass, Center + FVector(0.0, -Half, (Sill + Head) * 0.5), FVector(WindowWide / 100.0, 0.02, (Head - Sill) / 100.0),
			FRotator::ZeroRotator, TEXT("Hut_WindowPane"));
		// The -X wall either side of the doorway, and above it.
		const double DoorSide = (2.0 * Half - Door) * 0.5;
		AddBox(World, Kit, Kit.Stone, Center + FVector(-Half, -(Door * 0.5 + DoorSide * 0.5), Height * 0.5), FVector(Wall, DoorSide, Height), TEXT("Hut_WallFront"));
		AddBox(World, Kit, Kit.Stone, Center + FVector(-Half, Door * 0.5 + DoorSide * 0.5, Height * 0.5), FVector(Wall, DoorSide, Height), TEXT("Hut_WallFront"));
		AddBox(World, Kit, Kit.Stone, Center + FVector(-Half, 0.0, (DoorHeight + Height) * 0.5), FVector(Wall, Door, Height - DoorHeight), TEXT("Hut_Lintel"));
		if (AStaticMeshActor* Roof = AddBox(World, Kit, Kit.Wood, Center + FVector(0.0, 0.0, Height + 10.0), FVector(2.0 * Half + 60.0, 2.0 * Half + 60.0, 20.0), TEXT("Hut_Roof")))
		{
			Roof->Tags.Add(FDreamWeatherFallPaths::RoofTag);
		}
		PassThrough(AddBox(World, Kit, Kit.Wood, Center + FVector(80.0, 60.0, 40.0), FVector(140.0, 90.0, 80.0), TEXT("Hut_Table")));
		PassThrough(SurfaceSetup::AddMesh(World, Kit.Cylinder, Kit.Wood, Center + FVector(-20.0, 60.0, 22.0), FVector(0.4, 0.4, 0.45), FRotator::ZeroRotator, TEXT("Hut_Stool")));
	}

	/** A porch before the hut's door: a roof on four posts that only a roof capture box tells the weather about. */
	void AddPorch(UWorld& World, const FShowcaseKit& Kit, const FVector& Center)
	{
		const double HalfX = 170.0, HalfY = 220.0, Height = 260.0;
		for (int32 i = 0; i < 4; ++i)
		{
			const FVector Corner((i & 1) ? HalfX : -HalfX, (i & 2) ? HalfY : -HalfY, Height * 0.5);
			PassThrough(SurfaceSetup::AddMesh(World, Kit.Cylinder, Kit.Wood, Center + Corner, FVector(0.2, 0.2, Height / 100.0), FRotator::ZeroRotator, TEXT("Porch_Post")));
		}
		AStaticMeshActor* Roof = AddBox(World, Kit, Kit.Wood, Center + FVector(0.0, 0.0, Height + 10.0), FVector(2.0 * HalfX + 60.0, 2.0 * HalfY + 60.0, 20.0), TEXT("Porch_Roof"));
		PassThrough(Roof);
		if (Roof)
		{
			Roof->Tags.Add(FDreamWeatherFallPaths::RoofTag);
		}
		ADreamSurfaceRoofCapture* Capture = World.SpawnActor<ADreamSurfaceRoofCapture>(Center + FVector(0.0, 0.0, 180.0), FRotator::ZeroRotator);
		Capture->Roof->Extent = FVector(HalfX + 60.0, HalfY + 60.0, 180.0);
		Capture->SetActorLabel(TEXT("Porch_RoofCapture"));
	}

	/** A window standing in the open, the rain on its glass. */
	void AddWindow(UWorld& World, const FShowcaseKit& Kit, const FVector& Window)
	{
		SurfaceSetup::AddMesh(World, Kit.Cube, Kit.Glass, Window, FVector(0.01, 2.0, 1.5), FRotator::ZeroRotator, TEXT("Window_Pane"));
		AddBox(World, Kit, Kit.Wood, Window + FVector(0.0, 0.0, 80.0), FVector(10.0, 220.0, 10.0), TEXT("Window_Head"));
		AddBox(World, Kit, Kit.Wood, Window - FVector(0.0, 0.0, 80.0), FVector(14.0, 220.0, 10.0), TEXT("Window_Sill"));
		AddBox(World, Kit, Kit.Wood, Window + FVector(0.0, 105.0, 0.0), FVector(10.0, 10.0, 170.0), TEXT("Window_Jamb"));
		AddBox(World, Kit, Kit.Wood, Window - FVector(0.0, 105.0, 0.0), FVector(10.0, 10.0, 170.0), TEXT("Window_Jamb"));
		AddBox(World, Kit, Kit.Wood, Window - FVector(0.0, 0.0, Window.Z * 0.5 + 42.5), FVector(10.0, 40.0, Window.Z - 85.0), TEXT("Window_Stand"));
	}

	/** A few trees on the rolling ground whose crowns are leaves to the rain (the rain sounds on them, drips under them). */
	void AddGrove(UWorld& World, const FShowcaseKit& Kit, const FVector2D& Center)
	{
		const FVector2D Spots[] = { { 0, 0 }, { 420, 160 }, { -380, 240 }, { 180, -400 }, { -300, -320 }, { 560, -260 } };
		int32 Index = 0;
		for (const FVector2D& Spot : Spots)
		{
			const double Scale = 0.85 + 0.1 * double(Index % 3);
			const FVector2D At = Center + Spot;
			const FVector Base(At, SurfaceSetup::GroundHeight(At.X, At.Y));
			SurfaceSetup::AddMesh(World, Kit.Cylinder, Kit.Wood, Base + FVector(0.0, 0.0, 200.0 * Scale), FVector(0.4, 0.4, 4.0) * Scale, FRotator::ZeroRotator,
				*FString::Printf(TEXT("Grove_Trunk%d"), Index));
			if (AStaticMeshActor* Crown = SurfaceSetup::AddMesh(World, Kit.Sphere, Kit.ToonYellow, Base + FVector(0.0, 0.0, 520.0 * Scale), FVector(3.6, 3.6, 3.0) * Scale,
				FRotator::ZeroRotator, *FString::Printf(TEXT("Grove_Crown%d"), Index)))
			{
				Crown->Tags.Add(FDreamWeatherFallPaths::LeavesTag);
			}
			++Index;
		}
	}

	/** A campfire: warm air (a temperature volume), no snow or puddles round it (a mask brush), and the fire itself. */
	void AddCampfire(UWorld& World, const FShowcaseKit& Kit, const FVector& At)
	{
		ADreamTemperatureVolume* Warmth = World.SpawnActor<ADreamTemperatureVolume>(At + FVector(0.0, 0.0, 50.0), FRotator::ZeroRotator);
		Warmth->Volume->Temperature = 20.0f;
		Warmth->Volume->Extent = FVector(150.0);
		Warmth->Volume->Falloff = 350.0f;
		Warmth->SetActorLabel(TEXT("Campfire_Warmth"));
		ADreamSurfaceMask* Melted = World.SpawnActor<ADreamSurfaceMask>(At, FRotator::ZeroRotator);
		Melted->Mask->Shape = EDreamSurfaceMaskShape::Circle;
		Melted->Mask->Extent = FVector2D(220.0, 220.0);
		Melted->Mask->FadeDistance = 150.0f;
		Melted->Mask->Wetness = 0.3f;
		Melted->Mask->Water = 0.0f;
		Melted->Mask->Snow = 0.0f;
		Melted->Mask->Dust = 1.0f;
		Melted->SetActorLabel(TEXT("Campfire_Melted"));
		PassThrough(SurfaceSetup::AddMesh(World, Kit.Cone, Kit.ToonRed, At + FVector(0.0, 0.0, 30.0), FVector(0.6, 0.6, 0.6), FRotator::ZeroRotator, TEXT("Campfire")));
		for (int32 i = 0; i < 6; ++i)
		{
			const FVector Ring(FMath::Cos(i * PI / 3.0) * 55.0, FMath::Sin(i * PI / 3.0) * 55.0, 8.0);
			PassThrough(SurfaceSetup::AddMesh(World, Kit.Sphere, Kit.Stone, At + Ring, FVector(0.22, 0.22, 0.16), FRotator::ZeroRotator, TEXT("Campfire_Stone")));
		}
	}

	/** Wooden crates of 15-25 kg the wind slides and tips. */
	void AddCrates(UWorld& World, const FShowcaseKit& Kit, const FVector2D& At)
	{
		for (int32 i = 0; i < 5; ++i)
		{
			const double Size = 60.0 - 6.0 * i;
			AStaticMeshActor* Crate = AddBox(World, Kit, Kit.Wood, FVector(At.X + 90.0 * i, At.Y + 60.0 * (i % 2), Size * 0.5), FVector(Size),
				*FString::Printf(TEXT("WindCrate%d"), i));
			if (!Crate)
			{
				continue;
			}
			PassThrough(Crate);
			UStaticMeshComponent* Mesh = Crate->GetStaticMeshComponent();
			Mesh->SetMobility(EComponentMobility::Movable);
			Mesh->SetSimulatePhysics(true);
			Mesh->SetMassOverrideInKg(NAME_None, 25.0f - 2.5f * i, true);
			UDreamWindForceComponent* Force = NewObject<UDreamWindForceComponent>(Crate, TEXT("WindForce"));
			Force->ForceScale = 1.0f;
			Crate->AddInstanceComponent(Force);
		}
	}

	/** A snow field of an irregular outline (a polygon zone): cold, snowing, its edge breaking up on the ground. */
	void AddSnowField(UWorld& World, const FVector& Center)
	{
		ADreamWeatherZone* Field = World.SpawnActor<ADreamWeatherZone>(Center, FRotator::ZeroRotator);
		const double Radii[] = { 1500, 1150, 1650, 1300, 900, 1250, 1700, 1400, 1000, 1350, 1600, 1100 };
		TArray<FVector2D> Outline;
		for (int32 i = 0; i < UE_ARRAY_COUNT(Radii); ++i)
		{
			const double Angle = 2.0 * PI * i / UE_ARRAY_COUNT(Radii);
			Outline.Add(FVector2D(FMath::Cos(Angle), FMath::Sin(Angle)) * Radii[i]);
		}
		Field->Zone->Shape = EDreamWeatherZoneShape::Polygon;
		Field->Zone->SetPolygonPoints(Outline);
		Field->Zone->BlendDistance = 400.0f;
		Field->Zone->Channels.Add(DreamWeather::Tags::Channel_CloudCoverage, 1.0f);
		Field->Zone->Channels.Add(DreamWeather::Tags::Channel_Precipitation, 0.6f);
		Field->Zone->Channels.Add(DreamWeather::Tags::Channel_SnowRatio, 1.0f);
		Field->Zone->Channels.Add(DreamWeather::Tags::Channel_Temperature, -20.0f);
		Field->SetActorLabel(TEXT("SnowField"));
	}

	/** A storm that drifts in from the west in the morning: over the area around eleven, gone by early afternoon. */
	void AddStorm(UWorld& World)
	{
		ADreamWeatherStorm* Storm = World.SpawnActor<ADreamWeatherStorm>(FVector(-3500000.0, 0.0, 0.0), FRotator::ZeroRotator);
		Storm->Zone->Preset = Preset(TEXT("WP_Storm"));
		Storm->Zone->Extent = FVector2D(800000.0, 800000.0);
		Storm->Zone->BlendDistance = 600000.0f;
		Storm->Zone->Motion.bEnabled = true;
		Storm->Zone->Motion.Velocity = FVector2D(25.0, 0.0);
		Storm->Zone->Motion.LifetimeHours = 4.0f;
		Storm->Zone->Motion.bDestroyWhenDone = false;
		Storm->Zone->Motion.StartSeconds = DreamWorld::Celestial::FromDateTime(Day + FTimespan(9, 30, 0));
		Storm->SetActorLabel(TEXT("DriftingStorm"));
	}

	/** The weather of the showcase's day, a cycle of 24 hours from midnight. */
	UDreamWeatherSelector* MakeSchedule(UObject* Outer)
	{
		UDreamWeatherSelector_Sequence* Schedule = NewObject<UDreamWeatherSelector_Sequence>(Outer, TEXT("Selector_Showcase"));
		const TPair<const TCHAR*, float> Steps[] = {
			{ TEXT("WP_Clear"), 9.0f },     // the night before, the morning: clear (an aurora until five)
			{ TEXT("WP_Cloudy"), 2.0f },    // 9-11
			{ TEXT("WP_Rain"), 2.0f },      // 11-13
			{ TEXT("WP_Storm"), 2.0f },     // 13-15: thunder
			{ TEXT("WP_Overcast"), 2.0f },  // 15-17: after the rain, still wet
			{ TEXT("WP_Snow"), 3.0f },      // 17-20
			{ TEXT("WP_Dust"), 2.0f },      // 20-22
			{ TEXT("WP_Clear"), 2.0f },     // 22-24: a clear night, the aurora
		};
		for (const TPair<const TCHAR*, float>& Step : Steps)
		{
			FDreamWeatherSequenceEntry& Entry = Schedule->Entries.AddDefaulted_GetRef();
			Entry.Preset = Preset(Step.Key);
			Entry.Hours = Step.Value;
		}
		return Schedule;
	}

	bool BuildOne(const TCHAR* PackagePath, UDreamSkyStyle* Style, const FShowcaseKit& Kit, UDreamSkyModifier* Aurora, ULevelSequence* Dusk)
	{
		UPackage* Package = SurfaceSetup::FreshPackage(PackagePath);
		UWorld::InitializationValues Values;
		Values.RequiresHitProxies(false);
		Values.ShouldSimulatePhysics(false);
		Values.EnableTraceCollision(true);   // the wind terrain bake traces the level
		Values.CreateNavigation(false);
		Values.CreateAISystem(false);
		Values.AllowAudioPlayback(false);
		Values.CreatePhysicsScene(true);
		UWorld* World = UWorld::CreateWorld(EWorldType::Editor, false, FName(FPackageName::GetShortName(PackagePath)), Package, true, ERHIFeatureLevel::Num, &Values);
		if (!World)
		{
			return false;
		}
		World->SetFlags(RF_Public | RF_Standalone);

		// The sky and the short day: twelve minutes a day, from eight in the morning of a January day.
		ADreamSky* Sky = World->SpawnActor<ADreamSky>(FVector::ZeroVector, FRotator::ZeroRotator);
		Sky->Style = Style;
		Sky->Clock.StartTime = Day + FTimespan(8, 0, 0);
		Sky->Clock.DayLengthMinutes = 12.0f;
		Sky->TimeOfDay = 8.0f;
		if (Aurora)
		{
			FDreamSkyConditionalModifier& Night = Sky->ConditionalModifiers.AddDefaulted_GetRef();
			Night.Modifier = Aurora;
			Night.Condition.bHours = true;
			Night.Condition.StartHour = 20.0f;
			Night.Condition.EndHour = 5.0f;
			Night.Condition.FadeHours = 1.0f;
			Night.BlendSeconds = 4.0f;
		}
		Sky->SetActorLabel(TEXT("DreamSky"));

		ADreamWeather* Weather = World->SpawnActor<ADreamWeather>(FVector(0.0, 0.0, 200.0), FRotator::ZeroRotator);
		Weather->Selector = MakeSchedule(Weather);
		Weather->SetActorLabel(TEXT("DreamWeather"));

		// The ground and the paved square.
		SurfaceSetup::AddMesh(*World, Kit.Ground, Kit.Terrain, FVector::ZeroVector, FVector(1.0), FRotator::ZeroRotator, TEXT("Ground"));
		SurfaceSetup::AddMesh(*World, Kit.Cube, Kit.Paving, FVector(1300.0, 400.0, 0.0), FVector(14.0, 10.0, 0.1), FRotator::ZeroRotator, TEXT("Square"));

		// Realistic props, and the toon group in the square's far corner.
		PassThrough(SurfaceSetup::AddMesh(*World, Kit.Cube, Kit.Stone, FVector(1050.0, -50.0, 60.0), FVector(1.0), FRotator(0.0, 20.0, 0.0), TEXT("StoneBlock")));
		PassThrough(SurfaceSetup::AddMesh(*World, Kit.Sphere, Kit.Metal, FVector(1180.0, 160.0, 60.0), FVector(1.0), FRotator::ZeroRotator, TEXT("MetalBall")));
		PassThrough(SurfaceSetup::AddMesh(*World, Kit.Cylinder, Kit.Wood, FVector(1380.0, -40.0, 50.0), FVector(0.8), FRotator::ZeroRotator, TEXT("WoodPost")));
		PassThrough(SurfaceSetup::AddMesh(*World, Kit.Cube, Kit.ToonRed, FVector(1550.0, 720.0, 50.0), FVector(0.9), FRotator(0.0, 35.0, 0.0), TEXT("ToonBox")));
		PassThrough(SurfaceSetup::AddMesh(*World, Kit.Sphere, Kit.ToonYellow, FVector(1380.0, 600.0, 50.0), FVector(0.9), FRotator::ZeroRotator, TEXT("ToonBall")));
		PassThrough(SurfaceSetup::AddMesh(*World, Kit.Cone, Kit.ToonRed, FVector(1720.0, 520.0, 50.0), FVector(0.8), FRotator::ZeroRotator, TEXT("ToonCone")));
		PassThrough(SurfaceSetup::AddMesh(*World, Kit.Figure, Kit.ToonBlue, FVector(1300.0, 380.0, 5.0), FVector(1.0), FRotator(0.0, 200.0, 0.0), TEXT("ToonFigure")));

		// The hut beyond the square, its porch before the door; a window in the open; the pond in the dip.
		AddHut(*World, Kit, FVector(2450.0, 700.0, -3.0));
		AddPorch(*World, Kit, FVector(1960.0, 700.0, -3.0));
		AddWindow(*World, Kit, FVector(850.0, 820.0, 120.0));
		SurfaceSetup::AddMesh(*World, Kit.Plane, Kit.Water, FVector(900.0, -1100.0, -45.0), FVector(9.0, 9.0, 1.0), FRotator::ZeroRotator, TEXT("Pond"));

		// The grove on the rolling ground to the left, the campfire and the crates to the right, the snow field far ahead.
		AddGrove(*World, Kit, FVector2D(500.0, 2100.0));
		AddCampfire(*World, Kit, FVector(2050.0, -450.0, -3.0));
		AddCrates(*World, Kit, FVector2D(1600.0, -800.0));
		AddSnowField(*World, FVector(4800.0, -2300.0, 0.0));
		AddStorm(*World);

		// The wind, with level heights baked over the set: roofs keep the rain off, puddles follow the dips. The bake
		// traces the set's collision, which a world built a moment ago may not have yet: build it now.
		for (TActorIterator<AStaticMeshActor> It(World); It; ++It)
		{
			UStaticMeshComponent* Component = It->GetStaticMeshComponent();
			UStaticMesh* Mesh = Component ? Component->GetStaticMesh() : nullptr;
			if (UBodySetup* Body = Mesh ? Mesh->GetBodySetup() : nullptr)
			{
				Body->CreatePhysicsMeshes();
				Component->RecreatePhysicsState();
			}
		}
		ADreamWindGlobal* Wind = World->SpawnActor<ADreamWindGlobal>(FVector::ZeroVector, FRotator::ZeroRotator);
		Wind->SetActorLabel(TEXT("Wind"));
		Wind->TerrainBakeBounds = FBox(FVector(-1500.0, -4000.0, -500.0), FVector(6500.0, 4000.0, 1500.0));
		Wind->TerrainBakeSettings.Resolution = 512;
		if (!UDreamWindEditorLibrary::BakeTerrainMapForActor(Wind))
		{
			UE_LOG(LogDreamWorld, Warning, TEXT("Showcase: the wind terrain map could not be baked; puddles follow the hollow pattern only."));
		}

		// The dusk level plays its sequence at once, in cinematic mode.
		if (Dusk)
		{
			ALevelSequenceActor* Player = World->SpawnActor<ALevelSequenceActor>(FVector::ZeroVector, FRotator::ZeroRotator);
			Player->SetSequence(Dusk);
			Player->PlaybackSettings.bAutoPlay = true;
			Player->PlaybackSettings.bDisableMovementInput = true;
			Player->PlaybackSettings.bDisableLookAtInput = true;
			Player->PlaybackSettings.bHidePlayer = true;
			Player->PlaybackSettings.bHideHud = true;
			Player->SetActorLabel(TEXT("DuskSequence"));
		}

		World->SpawnActor<APlayerStart>(FVector(600.0, 300.0, 100.0), FRotator(0.0, 10.0, 0.0));
		ACameraActor* Camera = World->SpawnActor<ACameraActor>(FVector(200.0, -300.0, 320.0), FRotator(-10.0, 18.0, 0.0));
		if (FProperty* AutoActivate = ACameraActor::StaticClass()->FindPropertyByName(TEXT("AutoActivateForPlayer")))
		{
			*AutoActivate->ContainerPtrToValuePtr<uint8>(Camera) = uint8(EAutoReceiveInput::Player0);
		}
		Camera->GetCameraComponent()->SetFieldOfView(80.0f);
		Camera->GetCameraComponent()->bConstrainAspectRatio = false;
		Camera->SetActorLabel(TEXT("ShotCamera"));

		const bool bSaved = SurfaceSetup::SaveAsset(World);
		World->DestroyWorld(false);
		return bSaved;
	}
}

bool DreamWorld::Setup::BuildShowcaseMaps(UDreamSkyStyle* Style)
{
	ShowcaseSetup::FShowcaseKit Kit;
	if (!Kit.Load())
	{
		UE_LOG(LogDreamWorld, Error, TEXT("Showcase: a mesh or material is missing (compile the DreamSurface demo materials first)."));
		return false;
	}
	UDreamSkyModifier* Aurora = ShowcaseSetup::EnsureAurora();
	ULevelSequence* Dusk = ShowcaseSetup::EnsureDuskSequence();
	bool bOk = Aurora && Dusk;
	bOk &= ShowcaseSetup::BuildOne(ShowcaseMapPackage, Style, Kit, Aurora, nullptr);
	bOk &= ShowcaseSetup::BuildOne(ShowcaseDuskMapPackage, Style, Kit, Aurora, Dusk);
	return bOk;
}
