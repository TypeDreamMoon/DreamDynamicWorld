// Copyright DreamMoon. All Rights Reserved.

#include "DreamWorldPlaySetupCommandlet.h"
#include "DreamWeatherAudioAssets.h"
#include "DreamSky.h"
#include "DreamSkyStyle.h"
#include "DreamWeather.h"
#include "DreamWeatherFallPaths.h"
#include "DreamWeatherModule.h"
#include "DreamWeatherPreset.h"
#include "DreamWeatherSelector.h"
#include "DreamWindGlobalActor.h"
#include "DreamWeatherStormSpawner.h"
#include "DreamWeatherTemperature.h"
#include "DreamWindForceComponent.h"
#include "DreamWeatherStatusComponent.h"
#include "DreamWeatherZone.h"
#include "DreamWorldCelestial.h"

#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Components/SplineComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInterface.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/World.h"
#include "GameFramework/PlayerStart.h"
#include "Misc/PackageName.h"
#include "UObject/Package.h"
#include "UObject/UnrealType.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(DreamWorldPlaySetupCommandlet)

DEFINE_LOG_CATEGORY_STATIC(LogDreamWorldPlaySetup, Log, All);

namespace DreamWorldPlay
{
	static const TCHAR* ShowcasePackage = TEXT("/DreamDynamicWorld/Play/Maps/Lvl_DreamWorldPlay");
	static const TCHAR* Plane = TEXT("/Engine/BasicShapes/Plane.Plane");
	static const TCHAR* Cube = TEXT("/Engine/BasicShapes/Cube.Cube");
	static const TCHAR* Sphere = TEXT("/Engine/BasicShapes/Sphere.Sphere");
	static const TCHAR* Cylinder = TEXT("/Engine/BasicShapes/Cylinder.Cylinder");

	static UDreamWeatherPreset* Preset(const TCHAR* Name)
	{
		return LoadObject<UDreamWeatherPreset>(nullptr, *FString::Printf(TEXT("/DreamDynamicWorld/Weather/Presets/%s.%s"), Name, Name));
	}

	/** A basic shape; the engine's shapes are 100 cm, so Size is in cm. */
	static AStaticMeshActor* AddShape(UWorld& World, const TCHAR* MeshPath, const TCHAR* Label, const FVector& Center, const FVector& Size,
		const FRotator& Rotation = FRotator::ZeroRotator)
	{
		UStaticMesh* Mesh = LoadObject<UStaticMesh>(nullptr, MeshPath);
		if (!Mesh)
		{
			UE_LOG(LogDreamWorldPlaySetup, Error, TEXT("Mesh %s not found."), MeshPath);
			return nullptr;
		}
		AStaticMeshActor* Actor = World.SpawnActor<AStaticMeshActor>(Center, Rotation);
		Actor->GetStaticMeshComponent()->SetStaticMesh(Mesh);
		Actor->GetStaticMeshComponent()->SetMobility(EComponentMobility::Static);
		Actor->SetActorScale3D(Size / 100.0);
		Actor->SetActorLabel(Label);
		return Actor;
	}

	/** A closed hut with a doorway on its -X side: step inside and the rain goes dull, stand in the door and it comes from the door. */
	static void AddHut(UWorld& World, const FVector& Center)
	{
		const double Half = 300.0, Height = 300.0, Wall = 20.0, Door = 120.0;
		AddShape(World, Cube, TEXT("Hut_WallPlusX"), Center + FVector(Half, 0.0, Height * 0.5), FVector(Wall, 2.0 * Half, Height));
		AddShape(World, Cube, TEXT("Hut_WallPlusY"), Center + FVector(0.0, Half, Height * 0.5), FVector(2.0 * Half, Wall, Height));
		AddShape(World, Cube, TEXT("Hut_WallMinusY"), Center + FVector(0.0, -Half, Height * 0.5), FVector(2.0 * Half, Wall, Height));
		// The -X wall, either side of the doorway.
		const double Side = (2.0 * Half - Door) * 0.5;
		AddShape(World, Cube, TEXT("Hut_WallDoorLeft"), Center + FVector(-Half, -(Door * 0.5 + Side * 0.5), Height * 0.5), FVector(Wall, Side, Height));
		AddShape(World, Cube, TEXT("Hut_WallDoorRight"), Center + FVector(-Half, Door * 0.5 + Side * 0.5, Height * 0.5), FVector(Wall, Side, Height));
		AddShape(World, Cube, TEXT("Hut_Lintel"), Center + FVector(-Half, 0.0, Height - 40.0), FVector(Wall, Door, 80.0));
		if (AStaticMeshActor* Roof = AddShape(World, Cube, TEXT("Hut_Roof"), Center + FVector(0.0, 0.0, Height + 10.0), FVector(2.0 * Half + 80.0, 2.0 * Half + 80.0, 20.0)))
		{
			Roof->Tags.Add(FDreamWeatherFallPaths::RoofTag);
		}
	}

	/** A roof on four posts: covered without being indoors (the roof's rain overhead, the open all around). */
	static void AddPorch(UWorld& World, const FVector& Center)
	{
		const double Half = 250.0, Height = 280.0;
		for (int32 i = 0; i < 4; ++i)
		{
			const FVector Corner((i & 1) ? Half : -Half, (i & 2) ? Half : -Half, Height * 0.5);
			AddShape(World, Cylinder, *FString::Printf(TEXT("Porch_Post%d"), i), Center + Corner, FVector(20.0, 20.0, Height));
		}
		if (AStaticMeshActor* Roof = AddShape(World, Cube, TEXT("Porch_Roof"), Center + FVector(0.0, 0.0, Height + 10.0), FVector(2.0 * Half + 60.0, 2.0 * Half + 60.0, 20.0)))
		{
			Roof->Tags.Add(FDreamWeatherFallPaths::RoofTag);
		}
	}

	/** A few trees whose crowns are leaves to the rain. */
	static void AddGrove(UWorld& World, const FVector& Center)
	{
		const FVector2D Spots[] = { { 0, 0 }, { 450, 120 }, { -380, 260 }, { 160, -420 }, { -260, -330 }, { 520, -300 }, { -560, -40 } };
		int32 Index = 0;
		for (const FVector2D& Spot : Spots)
		{
			const double Scale = 0.8 + 0.1 * double(Index % 4);
			const FVector Base = Center + FVector(Spot.X, Spot.Y, 0.0);
			AddShape(World, Cylinder, *FString::Printf(TEXT("Tree%d_Trunk"), Index), Base + FVector(0.0, 0.0, 200.0 * Scale), FVector(40.0, 40.0, 400.0) * Scale);
			if (AStaticMeshActor* Crown = AddShape(World, Sphere, *FString::Printf(TEXT("Tree%d_Crown"), Index), Base + FVector(0.0, 0.0, 520.0 * Scale), FVector(380.0, 380.0, 320.0) * Scale))
			{
				Crown->Tags.Add(FDreamWeatherFallPaths::LeavesTag);
			}
			++Index;
		}
	}

	/**
	 * The gameplay showcase: a floor with the hut (indoors), the porch (a roof overhead) and the grove (leaves) in a
	 * row, weather that runs clear - rain - storm - clear every few hours, and a camera on all three.
	 */
	static bool BuildShowcaseMap()
	{
		if (UPackage* Existing = FindPackage(nullptr, ShowcasePackage))
		{
			ResetLoaders(Existing);
			Existing->Rename(*MakeUniqueObjectName(nullptr, UPackage::StaticClass(), TEXT("/Temp/DreamWorldPlayOld")).ToString(), nullptr, REN_DontCreateRedirectors | REN_NonTransactional);
		}
		UPackage* Package = CreatePackage(ShowcasePackage);
		Package->MarkAsFullyLoaded();
		UWorld::InitializationValues Values;
		Values.RequiresHitProxies(false);
		Values.ShouldSimulatePhysics(false);
		Values.EnableTraceCollision(false);
		Values.CreateNavigation(false);
		Values.CreateAISystem(false);
		Values.AllowAudioPlayback(false);
		Values.CreatePhysicsScene(true);
		UWorld* World = UWorld::CreateWorld(EWorldType::Editor, false, FName(FPackageName::GetShortName(ShowcasePackage)), Package, true, ERHIFeatureLevel::Num, &Values);
		if (!World)
		{
			return false;
		}
		World->SetFlags(RF_Public | RF_Standalone);

		ADreamSky* Sky = World->SpawnActor<ADreamSky>(FVector::ZeroVector, FRotator::ZeroRotator);
		Sky->Style = LoadObject<UDreamSkyStyle>(nullptr, TEXT("/DreamDynamicWorld/Sky/DS_DreamSkyStylized.DS_DreamSkyStylized"));
		Sky->TimeOfDay = 10.0f;
		Sky->Clock.StartTime = FDateTime(2026, 6, 21, 10, 0, 0);
		Sky->Clock.StartTime = FDateTime(2026, 6, 21, 10, 0, 0);
		Sky->SetActorLabel(TEXT("DreamSky"));
		World->SpawnActor<ADreamWindGlobal>(FVector(0.0, 0.0, 300.0), FRotator::ZeroRotator)->SetActorLabel(TEXT("DreamWindGlobal"));

		ADreamWeather* Weather = World->SpawnActor<ADreamWeather>(FVector(0.0, 0.0, 200.0), FRotator::ZeroRotator);
		Weather->SetActorLabel(TEXT("DreamWeather"));
		UDreamWeatherSelector_Sequence* Sequence = NewObject<UDreamWeatherSelector_Sequence>(Weather, TEXT("Selector_Sequence"));
		for (const TPair<const TCHAR*, float>& Step : { TPair<const TCHAR*, float>(TEXT("WP_Clear"), 3.0f), TPair<const TCHAR*, float>(TEXT("WP_Rain"), 3.0f),
			TPair<const TCHAR*, float>(TEXT("WP_Storm"), 3.0f), TPair<const TCHAR*, float>(TEXT("WP_Cloudy"), 3.0f) })
		{
			FDreamWeatherSequenceEntry& Entry = Sequence->Entries.AddDefaulted_GetRef();
			Entry.Preset = Preset(Step.Key);
			Entry.Hours = Step.Value;
		}
		Weather->Selector = Sequence;

		AddShape(*World, Plane, TEXT("Floor"), FVector::ZeroVector, FVector(8000.0, 8000.0, 100.0));
		AddHut(*World, FVector(1200.0, 0.0, 0.0));
		AddPorch(*World, FVector(0.0, 0.0, 0.0));
		AddGrove(*World, FVector(-1400.0, 0.0, 0.0));

		// A campfire by the porch: warm air (a temperature volume) and a marker.
		ADreamTemperatureVolume* Campfire = World->SpawnActor<ADreamTemperatureVolume>(FVector(0.0, -700.0, 50.0), FRotator::ZeroRotator);
		Campfire->SetActorLabel(TEXT("Campfire_Warmth"));
		Campfire->Volume->Temperature = 20.0f;
		Campfire->Volume->Extent = FVector(150.0);
		Campfire->Volume->Falloff = 350.0f;
		AddShape(*World, Cylinder, TEXT("Campfire"), FVector(0.0, -700.0, 15.0), FVector(80.0, 80.0, 30.0));

		// Someone standing in a cold spot (a temperature volume at -6 degrees), breathing out mist that the wind takes.
		if (AStaticMeshActor* Breather = AddShape(*World, Cylinder, TEXT("Breather"), FVector(300.0, -1500.0, 85.0), FVector(40.0, 40.0, 170.0)))
		{
			Breather->GetStaticMeshComponent()->SetMobility(EComponentMobility::Movable);
			UDreamWeatherStatusComponent* Status = NewObject<UDreamWeatherStatusComponent>(Breather, TEXT("WeatherStatus"));
			Status->bFreezingBreath = true;
			Status->bWriteMaterialParameters = false;
			Status->BreathOffset = FVector(25.0, 0.0, 70.0);
			Breather->AddInstanceComponent(Status);
		}
		ADreamTemperatureVolume* ColdSpot = World->SpawnActor<ADreamTemperatureVolume>(FVector(300.0, -1500.0, 100.0), FRotator::ZeroRotator);
		ColdSpot->SetActorLabel(TEXT("ColdSpot"));
		ColdSpot->Volume->Mode = EDreamTemperatureMode::Replace;
		ColdSpot->Volume->Temperature = -6.0f;
		ColdSpot->Volume->Extent = FVector(250.0);
		ColdSpot->Volume->Falloff = 200.0f;

		// Crates the wind pushes about, in the open before the hut.
		for (int32 i = 0; i < 5; ++i)
		{
			const double Size = 60.0 - 6.0 * i;
			if (AStaticMeshActor* Crate = AddShape(*World, Cube, *FString::Printf(TEXT("WindCrate%d"), i), FVector(700.0 + 90.0 * i, -900.0 + 60.0 * (i % 2), Size * 0.5),
				FVector(Size)))
			{
				UStaticMeshComponent* Mesh = Crate->GetStaticMeshComponent();
				Mesh->SetMobility(EComponentMobility::Movable);
				Mesh->SetSimulatePhysics(true);
				// Wooden crates of 15-25 kg: a gale slides and tips them, it does not throw them.
				Mesh->SetMassOverrideInKg(NAME_None, 25.0f - 2.5f * i, true);
				UDreamWindForceComponent* Force = NewObject<UDreamWindForceComponent>(Crate, TEXT("WindForce"));
				Force->ForceScale = 1.0f;
				Crate->AddInstanceComponent(Force);
			}
		}

		// A storm that drifts in from the west on the morning of the showcase's day: over the area around ten, gone by
		// half past twelve.
		ADreamWeatherStorm* Storm = World->SpawnActor<ADreamWeatherStorm>(FVector(-3500000.0, 0.0, 0.0), FRotator::ZeroRotator);
		Storm->SetActorLabel(TEXT("DriftingStorm"));
		Storm->Zone->Extent = FVector2D(800000.0, 800000.0);
		Storm->Zone->BlendDistance = 600000.0f;
		Storm->Zone->Motion.Velocity = FVector2D(25.0, 0.0);
		Storm->Zone->Motion.LifetimeHours = 4.0f;
		Storm->Zone->Motion.bDestroyWhenDone = false;
		Storm->Zone->Motion.StartSeconds = DreamWorld::Celestial::FromDateTime(FDateTime(2026, 6, 21, 8, 30, 0));

		// Far to the north, a snowfield of no regular shape (an outline zone, a bay cut into its south side): its clouds,
		// the snow on the land and the point queries share one edge. The land around the floor reads the surface maps.
		if (AStaticMeshActor* Land = AddShape(*World, Plane, TEXT("Land"), FVector(0.0, 0.0, -5.0), FVector(4000000.0, 4000000.0, 100.0)))
		{
			Land->GetStaticMeshComponent()->SetMaterial(0, LoadObject<UMaterialInterface>(nullptr, TEXT("/DreamDynamicWorld/Surface/Materials/M_DreamSurfaceTerrainDemo.M_DreamSurfaceTerrainDemo")));
		}
		ADreamWeatherOutlineZone* Snowfield = World->SpawnActor<ADreamWeatherOutlineZone>(FVector(0.0, 700000.0, 0.0), FRotator::ZeroRotator);
		Snowfield->SetActorLabel(TEXT("Snowfield"));
		Snowfield->Outline->ClearSplinePoints(false);
		for (const FVector& P : { FVector(380000.0, 60000.0, 0.0), FVector(250000.0, 300000.0, 0.0), FVector(-60000.0, 340000.0, 0.0), FVector(-330000.0, 180000.0, 0.0),
			FVector(-300000.0, -200000.0, 0.0), FVector(-120000.0, -330000.0, 0.0), FVector(-20000.0, -110000.0, 0.0), FVector(120000.0, -320000.0, 0.0),
			FVector(330000.0, -230000.0, 0.0) })
		{
			Snowfield->Outline->AddSplinePoint(P, ESplineCoordinateSpace::Local, false);
		}
		Snowfield->Outline->UpdateSpline();
		Snowfield->OutlineSpacing = 50000.0f;
		Snowfield->UpdateFootprint();
		Snowfield->Zone->BlendDistance = 40000.0f;
		Snowfield->Zone->Priority = 5;
		Snowfield->Zone->Channels.Add(DreamWeather::Tags::Channel_CloudCoverage, 1.0f);
		Snowfield->Zone->Channels.Add(DreamWeather::Tags::Channel_CloudDarkness, 0.5f);
		Snowfield->Zone->Channels.Add(DreamWeather::Tags::Channel_Precipitation, 0.7f);
		Snowfield->Zone->Channels.Add(DreamWeather::Tags::Channel_SnowRatio, 1.0f);
		Snowfield->Zone->Channels.Add(DreamWeather::Tags::Channel_Temperature, -30.0f);   // an offset: the level stands in midsummer

		// And storms that come by themselves with the season.
		ADreamWeatherStormSpawner* Spawner = World->SpawnActor<ADreamWeatherStormSpawner>(FVector::ZeroVector, FRotator::ZeroRotator);
		Spawner->SetActorLabel(TEXT("StormSpawner"));
		FDreamStormKind& Thunder = Spawner->Kinds.AddDefaulted_GetRef();
		Thunder.Preset = Preset(TEXT("WP_Storm"));
		Thunder.Winter = 0.2f;
		FDreamStormKind& Squall = Spawner->Kinds.AddDefaulted_GetRef();
		Squall.Preset = Preset(TEXT("WP_Rain"));


		World->SpawnActor<APlayerStart>(FVector(0.0, -1200.0, 100.0), FRotator(0.0, 90.0, 0.0));
		ACameraActor* Camera = World->SpawnActor<ACameraActor>(FVector(0.0, -2600.0, 420.0), FRotator(-6.0, 90.0, 0.0));
		if (FProperty* AutoActivate = ACameraActor::StaticClass()->FindPropertyByName(TEXT("AutoActivateForPlayer")))
		{
			*AutoActivate->ContainerPtrToValuePtr<uint8>(Camera) = uint8(EAutoReceiveInput::Player0);
		}
		Camera->GetCameraComponent()->SetFieldOfView(80.0f);
		Camera->GetCameraComponent()->bConstrainAspectRatio = false;
		Camera->SetActorLabel(TEXT("ShotCamera"));

		const bool bSaved = SaveAsset(World);
		World->DestroyWorld(false);
		return bSaved;
	}
}

UDreamWorldPlaySetupCommandlet::UDreamWorldPlaySetupCommandlet()
{
	IsClient = false;
	IsEditor = true;
	IsServer = false;
	LogToConsole = true;
}

int32 UDreamWorldPlaySetupCommandlet::Main(const FString& Params)
{
	using namespace DreamWorldPlay;
	bool bOk = EnsureAudioAssets(FParse::Param(*Params, TEXT("ForceAudio")));
	if (FParse::Param(*Params, TEXT("ShowcaseMap")))
	{
		if (FPackageName::DoesPackageExist(ShowcasePackage) && !FParse::Param(*Params, TEXT("Force")))
		{
			UE_LOG(LogDreamWorldPlaySetup, Display, TEXT("%s exists; pass -Force to write it again."), ShowcasePackage);
		}
		else
		{
			bOk &= BuildShowcaseMap();
		}
	}
	UE_LOG(LogDreamWorldPlaySetup, Display, TEXT("DreamWorldPlaySetup: %s"), bOk ? TEXT("done") : TEXT("FAILED"));
	return bOk ? 0 : 1;
}
