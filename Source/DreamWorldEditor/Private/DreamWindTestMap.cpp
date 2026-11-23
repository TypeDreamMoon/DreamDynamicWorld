// Copyright DreamMoon. All Rights Reserved.

#include "DreamWindTestMap.h"
#include "DreamDynamicWorldModule.h"
#include "DreamSky.h"
#include "DreamSkyStyle.h"
#include "DreamSurfaceSetup.h"
#include "DreamWindDemoActors.h"
#include "DreamWindEditorLibrary.h"
#include "DreamWindGlobalActor.h"
#include "DreamWindImpulseEmitter.h"

#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/World.h"
#include "Misc/PackageName.h"
#include "UObject/Package.h"

// The level's helpers have a namespace of their own (unity builds put this file beside the other setup files).
namespace DreamWorld::WindTestMapImpl
{
	/** A box of the engine's 1 m cube, Size cm, standing at Center. */
	AStaticMeshActor* AddBox(UWorld& World, UStaticMesh* Cube, const FVector& Center, const FVector& Size, const TCHAR* Label)
	{
		return SurfaceSetup::AddMesh(World, Cube, nullptr, Center, Size / 100.0, FRotator::ZeroRotator, Label);
	}

	FBox Footprint(const FVector& Center, const FVector& Size, double Margin)
	{
		return FBox(Center - Size * 0.5 - FVector(Margin, Margin, 1000.0), Center + Size * 0.5 + FVector(Margin, Margin, 1000.0));
	}
}

bool DreamWorld::Setup::BuildWindTestMap(UDreamSkyStyle* Style)
{
	using namespace DreamWorld::Setup::WindTest;
	UStaticMesh* Cube = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
	UStaticMesh* Cone = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cone.Cone"));
	if (!Cube || !Cone)
	{
		UE_LOG(LogDreamWorld, Error, TEXT("Wind test level: the engine's basic shapes are missing."));
		return false;
	}

	UPackage* Package = SurfaceSetup::FreshPackage(WindTestMapPackage);
	UWorld::InitializationValues Values;
	Values.RequiresHitProxies(false);
	Values.ShouldSimulatePhysics(false);
	Values.EnableTraceCollision(true);
	Values.CreateNavigation(false);
	Values.CreateAISystem(false);
	Values.AllowAudioPlayback(false);
	Values.CreatePhysicsScene(true);
	UWorld* World = UWorld::CreateWorld(EWorldType::Editor, false, FName(FPackageName::GetShortName(WindTestMapPackage)), Package, true, ERHIFeatureLevel::Num, &Values);
	if (!World)
	{
		return false;
	}
	World->SetFlags(RF_Public | RF_Standalone);

	ADreamSky* Sky = World->SpawnActor<ADreamSky>(FVector::ZeroVector, FRotator::ZeroRotator);
	Sky->Style = Style;
	Sky->TimeOfDay = 11.0f;
	Sky->SetActorLabel(TEXT("DreamSky"));

	// The ground (300 m), and what the wind meets on it.
	// (a deep slab, not a plane: the global distance field has no inside for a sheet, and the fluid's walls would miss it,
	// the far grid's 4 m cells under the ground too)
	WindTestMapImpl::AddBox(*World, Cube, FVector(0.0, 0.0, -1000.0), FVector(30000.0, 30000.0, 2000.0), TEXT("Ground"));
	WindTestMapImpl::AddBox(*World, Cube, HouseCenter, HouseSize, TEXT("House"));
	WindTestMapImpl::AddBox(*World, Cube, WallCenter, WallSize, TEXT("Wall"));
	const FVector ArmAlongX(1200.0, 40.0, 300.0);
	const FVector ArmAlongY(40.0, 1200.0, 300.0);
	const FVector ArmXCenter = CornerPoint + FVector(ArmAlongX.X * 0.5, 0.0, ArmAlongX.Z * 0.5);
	const FVector ArmYCenter = CornerPoint + FVector(0.0, -ArmAlongY.Y * 0.5, ArmAlongY.Z * 0.5);
	WindTestMapImpl::AddBox(*World, Cube, ArmXCenter, ArmAlongX, TEXT("CornerAlongX"));
	WindTestMapImpl::AddBox(*World, Cube, ArmYCenter, ArmAlongY, TEXT("CornerAlongY"));
	// (The engine's cone stands on its middle: 1 m tall, 1 m across at the base.)
	SurfaceSetup::AddMesh(*World, Cone, nullptr, HillCenter + FVector(0.0, 0.0, 400.0), FVector(30.0, 30.0, 8.0), FRotator::ZeroRotator, TEXT("Hill"));

	// The grass, kept off the solids.
	FActorSpawnParameters Deferred;
	Deferred.bDeferConstruction = true;
	if (ADreamWindGrassField* Grass = World->SpawnActor<ADreamWindGrassField>(FVector::ZeroVector, FRotator::ZeroRotator, Deferred))
	{
		Grass->Count = 40000;
		Grass->Extent = GrassExtent;
		Grass->Exclusions = {
			WindTestMapImpl::Footprint(HouseCenter, HouseSize, 20.0),
			WindTestMapImpl::Footprint(WallCenter, WallSize, 20.0),
			WindTestMapImpl::Footprint(ArmXCenter, ArmAlongX, 20.0),
			WindTestMapImpl::Footprint(ArmYCenter, ArmAlongY, 20.0),
		};
		Grass->FinishSpawning(FTransform::Identity);
		Grass->SetActorLabel(TEXT("Grass"));
	}

	// The clearing's impulse: a radial burst every four seconds, outward over the grass.
	if (ADreamWindImpulseEmitter* Burst = World->SpawnActor<ADreamWindImpulseEmitter>(ClearingCenter, FRotator::ZeroRotator))
	{
		Burst->Interval = 4.0f;
		Burst->Impulse.Type = EDreamWindSourceType::Radial;
		Burst->Impulse.Radius = 800.0f;
		Burst->Impulse.Strength = 2000.0f;
		Burst->Duration = 0.4f;
		Burst->FadeOutTime = 0.25f;
		Burst->SetActorLabel(TEXT("ClearingBurst"));
	}

	// The wind: steady along +x, the terrain map baked over the set (its collision first, a moment-old world may lack it).
	SurfaceSetup::ReadyCollision(*World);
	ADreamWindGlobal* Wind = World->SpawnActor<ADreamWindGlobal>(FVector::ZeroVector, FRotator::ZeroRotator);
	Wind->SetActorLabel(TEXT("Wind"));
	Wind->Wind.Direction = WindDirection;
	Wind->Wind.Speed = WindSpeed;
	Wind->TerrainBakeBounds = FBox(FVector(-6000.0, -6000.0, -500.0), FVector(6000.0, 6000.0, 2000.0));
	Wind->TerrainBakeSettings.Resolution = 512;
	if (!UDreamWindEditorLibrary::BakeTerrainMapForActor(Wind))
	{
		UE_LOG(LogDreamWorld, Warning, TEXT("Wind test level: the terrain map could not be baked; the wind casts no shadow there."));
	}

	ACameraActor* Camera = World->SpawnActor<ACameraActor>(CameraLocation, CameraRotation);
	if (FProperty* AutoActivate = ACameraActor::StaticClass()->FindPropertyByName(TEXT("AutoActivateForPlayer")))
	{
		*AutoActivate->ContainerPtrToValuePtr<uint8>(Camera) = uint8(EAutoReceiveInput::Player0);
	}
	Camera->GetCameraComponent()->SetFieldOfView(90.0f);
	Camera->GetCameraComponent()->bConstrainAspectRatio = false;
	Camera->SetActorLabel(TEXT("ShotCamera"));

	const bool bSaved = SurfaceSetup::SaveAsset(World);
	World->DestroyWorld(false);
	return bSaved;
}
