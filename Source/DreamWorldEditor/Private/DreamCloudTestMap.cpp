// Copyright DreamMoon. All Rights Reserved.

#include "DreamCloudTestMap.h"
#include "DreamCloudVolume.h"
#include "DreamCloudVolumeComponent.h"
#include "DreamDynamicWorldModule.h"
#include "DreamSky.h"
#include "DreamSkyStyle.h"
#include "DreamWeather.h"
#include "DreamWeatherPreset.h"
#include "DreamWeatherSelector.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/World.h"
#include "Materials/Material.h"
#include "MeshDescription.h"
#include "Misc/PackageName.h"
#include "StaticMeshAttributes.h"
#include "StaticMeshOperations.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"

namespace DreamWorld::Setup
{
	namespace
	{
		bool SaveAsset(UPackage* Package, UObject* Asset, const FString& Extension)
		{
			Package->MarkPackageDirty();
			const FString Filename = FPackageName::LongPackageNameToFilename(Package->GetName(), Extension);
			FSavePackageArgs Args;
			Args.TopLevelFlags = RF_Public | RF_Standalone;
			Args.SaveFlags = SAVE_None;
			Args.Error = GWarn;
			const bool bSaved = UPackage::SavePackage(Package, Asset, *Filename, Args);
			UE_LOG(LogDreamWorld, Display, TEXT("%s %s"), bSaved ? TEXT("Saved") : TEXT("Could not save"), *Filename);
			return bSaved;
		}

		/** A package to build an asset in afresh: a copy already in memory lets go of its file and moves aside. */
		UPackage* FreshPackage(const TCHAR* Path)
		{
			if (UPackage* Existing = FindPackage(nullptr, Path))
			{
				ResetLoaders(Existing);
				Existing->Rename(*MakeUniqueObjectName(nullptr, UPackage::StaticClass(), TEXT("/Temp/DreamCloudOld")).ToString(), nullptr, REN_DontCreateRedirectors | REN_NonTransactional);
			}
			UPackage* Package = CreatePackage(Path);
			Package->MarkAsFullyLoaded();
			return Package;
		}

		AStaticMeshActor* AddMesh(UWorld& World, const TCHAR* MeshPath, const FVector& Location, const FVector& Scale)
		{
			UStaticMesh* Mesh = LoadObject<UStaticMesh>(nullptr, MeshPath);
			if (!Mesh)
			{
				return nullptr;
			}
			AStaticMeshActor* Actor = World.SpawnActor<AStaticMeshActor>(Location, FRotator::ZeroRotator);
			Actor->GetStaticMeshComponent()->SetStaticMesh(Mesh);
			Actor->GetStaticMeshComponent()->SetMobility(EComponentMobility::Static);
			Actor->SetActorScale3D(Scale);
			return Actor;
		}

		FDreamCloudPrimitiveShape Primitive(EDreamCloudPrimitive Type, const FVector& Location, const FVector& Size, bool bSubtract = false, float Rounding = 0.0f,
			const FRotator& Rotation = FRotator::ZeroRotator)
		{
			FDreamCloudPrimitiveShape S;
			S.Type = Type;
			S.Transform = FTransform(Rotation, Location);
			S.Size = Size;
			S.bSubtract = bSubtract;
			S.Rounding = Rounding;
			return S;
		}

		/** A cumulus-like heap of four spheres, Size cm across (as DreamCloud.Spawn's blob). */
		TArray<FDreamCloudPrimitiveShape> Heap(double Size)
		{
			const FVector Parts[] = { FVector(0.0, 0.0, -0.1), FVector(-0.35, 0.1, 0.2), FVector(0.3, -0.15, 0.15), FVector(0.05, 0.25, -0.05) };
			const double Radii[] = { 0.55, 0.4, 0.45, 0.4 };
			TArray<FDreamCloudPrimitiveShape> Out;
			for (int32 i = 0; i < UE_ARRAY_COUNT(Parts); ++i)
			{
				Out.Add(Primitive(EDreamCloudPrimitive::Sphere, Parts[i] * Size, FVector(Radii[i] * 0.5 * Size)));
			}
			return Out;
		}

		ADreamCloudVolume* AddCloud(UWorld& World, const TCHAR* Label, const FVector& Location, EDreamCloudLook Look, TFunctionRef<void(UDreamCloudVolumeComponent&)> Shape,
			const FVector& Scale = FVector::OneVector, const FRotator& Rotation = FRotator::ZeroRotator)
		{
			ADreamCloudVolume* Volume = World.SpawnActor<ADreamCloudVolume>(Location, Rotation);
			UDreamCloudVolumeComponent* Cloud = Volume->GetCloud();
			Cloud->Look = Look;
			// Coarse fields keep the level small (a 96^3 field is some 1.7 MB): the test clouds need no finer.
			Cloud->Resolution = 64;
			Shape(*Cloud);
			Volume->SetActorScale3D(Scale);
			Cloud->Rebake();
			Volume->SetActorLabel(Label);
			return Volume;
		}
	}

	UStaticMesh* EnsureCloudSculptMesh(bool bForce)
	{
		if (!bForce && FPackageName::DoesPackageExist(CloudSculptPackage))
		{
			if (UStaticMesh* Existing = LoadObject<UStaticMesh>(nullptr, *(FString(CloudSculptPackage) + TEXT(".SM_DreamCloudSculpt"))))
			{
				return Existing;
			}
		}
		struct FPart
		{
			const TCHAR* Mesh;
			FTransform Transform;
		};
		const FPart Parts[] = {
			{ TEXT("/Engine/BasicShapes/Cube.Cube"), FTransform(FRotator(0.0, 15.0, 0.0), FVector(0.0, 0.0, 0.0), FVector(2.2, 1.0, 0.9)) },
			{ TEXT("/Engine/BasicShapes/Sphere.Sphere"), FTransform(FRotator::ZeroRotator, FVector(55.0, 10.0, 55.0), FVector(1.5)) },
			{ TEXT("/Engine/BasicShapes/Cylinder.Cylinder"), FTransform(FRotator::ZeroRotator, FVector(-70.0, -10.0, 60.0), FVector(0.8, 0.8, 1.7)) },
			{ TEXT("/Engine/BasicShapes/Sphere.Sphere"), FTransform(FRotator::ZeroRotator, FVector(-70.0, -10.0, 145.0), FVector(0.95)) },
		};
		FMeshDescription Merged;
		FStaticMeshAttributes Attributes(Merged);
		Attributes.Register();
		const FPolygonGroupID Group = Merged.CreatePolygonGroup();
		Attributes.GetPolygonGroupMaterialSlotNames()[Group] = TEXT("Cloud");
		FStaticMeshOperations::FAppendSettings Settings;
		Settings.PolygonGroupsDelegate = FAppendPolygonGroupsDelegate::CreateLambda([Group](const FMeshDescription& Source, FMeshDescription&, PolygonGroupMap& Map)
		{
			for (const FPolygonGroupID Id : Source.PolygonGroups().GetElementIDs())
			{
				Map.Add(Id, Group);
			}
		});
		for (const FPart& Part : Parts)
		{
			const UStaticMesh* Mesh = LoadObject<UStaticMesh>(nullptr, Part.Mesh);
			const FMeshDescription* Source = Mesh ? Mesh->GetMeshDescription(0) : nullptr;
			if (!Source)
			{
				UE_LOG(LogDreamWorld, Error, TEXT("%s has no mesh description."), Part.Mesh);
				return nullptr;
			}
			Settings.MeshTransform = Part.Transform;
			FStaticMeshOperations::AppendMeshDescription(*Source, Merged, Settings);
		}

		UPackage* Package = FreshPackage(CloudSculptPackage);
		UStaticMesh* Mesh = NewObject<UStaticMesh>(Package, TEXT("SM_DreamCloudSculpt"), RF_Public | RF_Standalone);
		Mesh->GetStaticMaterials().Add(FStaticMaterial(UMaterial::GetDefaultMaterial(MD_Surface), TEXT("Cloud"), TEXT("Cloud")));
		Mesh->AddSourceModel();
		FMeshDescription* Description = Mesh->CreateMeshDescription(0);
		*Description = MoveTemp(Merged);
		Mesh->CommitMeshDescription(0);
		Mesh->bAllowCPUAccess = true;
		Mesh->Build(true);
		Mesh->PostEditChange();
		FAssetRegistryModule::AssetCreated(Mesh);
		return SaveAsset(Package, Mesh, FPackageName::GetAssetPackageExtension()) ? Mesh : nullptr;
	}

	bool BuildCloudTestMap(UDreamSkyStyle* Style, UDreamWeatherPreset* Weather)
	{
		UStaticMesh* Sculpt = EnsureCloudSculptMesh(false);
		UPackage* Package = FreshPackage(CloudTestMapPackage);
		UWorld::InitializationValues Values;
		Values.RequiresHitProxies(false);
		Values.ShouldSimulatePhysics(false);
		Values.EnableTraceCollision(false);
		Values.CreateNavigation(false);
		Values.CreateAISystem(false);
		Values.AllowAudioPlayback(false);
		Values.CreatePhysicsScene(true);
		UWorld* World = UWorld::CreateWorld(EWorldType::Editor, false, FName(FPackageName::GetShortName(CloudTestMapPackage)), Package, true, ERHIFeatureLevel::Num, &Values);
		if (!World)
		{
			return false;
		}
		World->SetFlags(RF_Public | RF_Standalone);

		ADreamSky* Sky = World->SpawnActor<ADreamSky>(FVector::ZeroVector, FRotator::ZeroRotator);
		Sky->Style = Style;
		Sky->TimeOfDay = 15.0f;
		Sky->SetActorLabel(TEXT("DreamSky"));
		if (Weather)
		{
			ADreamWeather* WeatherActor = World->SpawnActor<ADreamWeather>(FVector(0.0f, 0.0f, 200.0f), FRotator::ZeroRotator);
			WeatherActor->SetActorLabel(TEXT("DreamWeather"));
			if (UDreamWeatherSelector_Random* Random = Cast<UDreamWeatherSelector_Random>(WeatherActor->Selector))
			{
				FDreamWeatherWeightedEntry& Entry = Random->Entries.AddDefaulted_GetRef();
				Entry.Preset = Weather;
				Entry.Weight = 1.0f;
			}
		}

		const TCHAR* Plane = TEXT("/Engine/BasicShapes/Plane.Plane");
		AddMesh(*World, Plane, FVector::ZeroVector, FVector(600.0f, 600.0f, 1.0f))->SetActorLabel(TEXT("Floor"));
		if (AStaticMeshActor* Ground = AddMesh(*World, Plane, FVector(0.0f, 0.0f, -5.0f), FVector(1000000.0f, 1000000.0f, 1.0f)))
		{
			Ground->SetActorLabel(TEXT("Ground"));
			Ground->GetStaticMeshComponent()->SetCastShadow(false);
			Ground->GetStaticMeshComponent()->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		}
		// Something to stand in the clouds' soft contact: a pillar through the realistic heap.
		if (AStaticMeshActor* Pillar = AddMesh(*World, TEXT("/Engine/BasicShapes/Cylinder.Cylinder"), FVector(7000.0f, -3400.0f, 1000.0f), FVector(2.0f, 2.0f, 20.0f)))
		{
			Pillar->SetActorLabel(TEXT("Pillar"));
		}

		// Ahead: the three looks side by side.
		const TPair<const TCHAR*, EDreamCloudLook> Looks[] = { { TEXT("Look_Realistic"), EDreamCloudLook::Realistic },
			{ TEXT("Look_Toon"), EDreamCloudLook::Toon }, { TEXT("Look_Painted"), EDreamCloudLook::Painted } };
		for (int32 i = 0; i < UE_ARRAY_COUNT(Looks); ++i)
		{
			AddCloud(*World, Looks[i].Key, FVector(7000.0, -3800.0 + 3800.0 * i, 1500.0), Looks[i].Value, [](UDreamCloudVolumeComponent& C)
			{
				C.Primitives = Heap(3000.0);
				C.PrimitiveBlend = 400.0f;
			});
		}

		// Right: meshes as clouds.
		AddCloud(*World, TEXT("Mesh_Cube"), FVector(-3000.0, 6000.0, 1200.0), EDreamCloudLook::Realistic, [](UDreamCloudVolumeComponent& C)
		{
			C.ShapeSource = EDreamCloudShapeSource::Mesh;
			C.ShapeMesh = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
		}, FVector(14.0));
		AddCloud(*World, TEXT("Mesh_Sphere"), FVector(0.0, 6000.0, 1200.0), EDreamCloudLook::Toon, [](UDreamCloudVolumeComponent& C)
		{
			C.ShapeSource = EDreamCloudShapeSource::Mesh;
			C.ShapeMesh = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Sphere.Sphere"));
		}, FVector(16.0));
		AddCloud(*World, TEXT("Mesh_Sculpt"), FVector(3200.0, 6000.0, 1100.0), EDreamCloudLook::Painted, [Sculpt](UDreamCloudVolumeComponent& C)
		{
			C.ShapeSource = EDreamCloudShapeSource::Mesh;
			C.ShapeMesh = Sculpt;
			C.Resolution = 96;
		}, FVector(7.0));

		// Behind: a cloud with a tunnel through it at 1200 cm, and one to fly through beside it.
		AddCloud(*World, TEXT("Tunnel"), FVector(-7000.0, 0.0, 1200.0), EDreamCloudLook::Toon, [](UDreamCloudVolumeComponent& C)
		{
			C.Primitives = {
				Primitive(EDreamCloudPrimitive::Box, FVector::ZeroVector, FVector(2500.0, 1500.0, 1100.0), false, 600.0f),
				Primitive(EDreamCloudPrimitive::Sphere, FVector(800.0, 300.0, 1000.0), FVector(1100.0)),
				Primitive(EDreamCloudPrimitive::Capsule, FVector::ZeroVector, FVector(450.0, 0.0, 3500.0), true, 0.0f, FRotator(90.0, 0.0, 0.0)),
			};
			C.PrimitiveBlend = 300.0f;
			C.Resolution = 96;
		});
		AddCloud(*World, TEXT("FlyThrough"), FVector(-7000.0, -5200.0, 1200.0), EDreamCloudLook::Realistic, [](UDreamCloudVolumeComponent& C)
		{
			C.Primitives = Heap(4500.0);
			C.PrimitiveBlend = 500.0f;
		});

		// Left, high up: a castle in the sky.
		AddCloud(*World, TEXT("Sky_Castle"), FVector(2000.0, -26000.0, 14000.0), EDreamCloudLook::Painted, [](UDreamCloudVolumeComponent& C)
		{
			C.Primitives = {
				Primitive(EDreamCloudPrimitive::Box, FVector::ZeroVector, FVector(4000.0, 3000.0, 900.0), false, 700.0f),
				Primitive(EDreamCloudPrimitive::Capsule, FVector(2800.0, 2000.0, 1400.0), FVector(800.0, 0.0, 1300.0)),
				Primitive(EDreamCloudPrimitive::Capsule, FVector(-2800.0, 2000.0, 1400.0), FVector(800.0, 0.0, 1300.0)),
				Primitive(EDreamCloudPrimitive::Capsule, FVector(2800.0, -2000.0, 1400.0), FVector(800.0, 0.0, 1300.0)),
				Primitive(EDreamCloudPrimitive::Capsule, FVector(-2800.0, -2000.0, 1400.0), FVector(800.0, 0.0, 1300.0)),
				Primitive(EDreamCloudPrimitive::Capsule, FVector(0.0, 0.0, 2600.0), FVector(1400.0, 0.0, 1800.0)),
				Primitive(EDreamCloudPrimitive::Sphere, FVector(0.0, 0.0, 5000.0), FVector(1300.0)),
				Primitive(EDreamCloudPrimitive::Sphere, FVector(2800.0, 2000.0, 3000.0), FVector(950.0)),
				Primitive(EDreamCloudPrimitive::Sphere, FVector(-2800.0, -2000.0, 3000.0), FVector(950.0)),
			};
			C.PrimitiveBlend = 700.0f;
			C.NoiseScale = 4000.0f;
			C.NoiseErosion = 500.0f;
			C.EdgeSoftness = 150.0f;
			C.Extinction = 0.5f;
			C.LobeScale = 600.0f;
		}, FVector::OneVector, FRotator(0.0, 30.0, 0.0));

		ACameraActor* Camera = World->SpawnActor<ACameraActor>(FVector(0.0f, 0.0f, 300.0f), FRotator(5.0f, 0.0f, 0.0f));
		if (FProperty* AutoActivate = ACameraActor::StaticClass()->FindPropertyByName(TEXT("AutoActivateForPlayer")))
		{
			*AutoActivate->ContainerPtrToValuePtr<uint8>(Camera) = uint8(EAutoReceiveInput::Player0);
		}
		Camera->GetCameraComponent()->SetFieldOfView(90.0f);
		Camera->GetCameraComponent()->bConstrainAspectRatio = false;
		Camera->SetActorLabel(TEXT("ShotCamera"));

		const bool bSaved = SaveAsset(Package, World, FPackageName::GetMapPackageExtension());
		World->DestroyWorld(false);
		return bSaved;
	}
}
