// Copyright DreamMoon. All Rights Reserved.

#include "DreamSurfaceSetup.h"
#include "DreamDynamicWorldModule.h"
#include "DreamSky.h"
#include "DreamSkyStyle.h"
#include "DreamSurfaceMask.h"
#include "DreamSurfaceRoofCapture.h"
#include "DreamSurfaceSettings.h"
#include "DreamWeather.h"
#include "DreamWeatherModule.h"
#include "DreamWeatherPreset.h"
#include "DreamWeatherSelector.h"
#include "DreamWeatherShelter.h"
#include "DreamWeatherZone.h"
#include "Components/SplineComponent.h"
#include "DreamWindEditorLibrary.h"
#include "DreamWindGlobalActor.h"
#include "DreamWindPassThroughComponent.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Materials/Material.h"
#include "Materials/MaterialInstanceConstant.h"
#include "MeshDescription.h"
#include "PhysicsEngine/BodySetup.h"
#include "Misc/PackageName.h"
#include "StaticMeshAttributes.h"
#include "StaticMeshOperations.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"

namespace DreamWorld::SurfaceSetup
{
	bool SaveAsset(UObject* Asset)
	{
		UPackage* Package = Asset->GetOutermost();
		Package->MarkPackageDirty();
		const FString& Extension = Asset->IsA<UWorld>() ? FPackageName::GetMapPackageExtension() : FPackageName::GetAssetPackageExtension();
		const FString Filename = FPackageName::LongPackageNameToFilename(Package->GetName(), Extension);
		FSavePackageArgs Args;
		Args.TopLevelFlags = RF_Public | RF_Standalone;
		Args.SaveFlags = SAVE_None;
		Args.Error = GWarn;
		const bool bSaved = UPackage::SavePackage(Package, Asset, *Filename, Args);
		UE_LOG(LogDreamWorld, Display, TEXT("%s %s"), bSaved ? TEXT("Saved") : TEXT("Could not save"), *Filename);
		return bSaved;
	}

	/** A render target a compute pass writes, of this size and format; made or brought up to date. */
	bool EnsureTarget(const FSoftObjectPath& Path, int32 Resolution, EPixelFormat Format, const FLinearColor& Clear)
	{
		UTextureRenderTarget2D* RT = Cast<UTextureRenderTarget2D>(Path.TryLoad());
		const bool bCreated = RT == nullptr;
		if (!RT)
		{
			UPackage* Package = CreatePackage(*Path.GetLongPackageName());
			Package->FullyLoad();
			RT = NewObject<UTextureRenderTarget2D>(Package, *Path.GetAssetName(), RF_Public | RF_Standalone);
		}
		if (!bCreated && RT->SizeX == Resolution && RT->bSupportsUAV && RT->GetFormat() == Format)
		{
			UE_LOG(LogDreamWorld, Display, TEXT("Surface map %s: up to date"), *RT->GetPathName());
			return true;
		}
		RT->PreEditChange(nullptr);
		RT->ClearColor = Clear;
		RT->AddressX = TA_Clamp;
		RT->AddressY = TA_Clamp;
		RT->bSupportsUAV = true;
		RT->InitCustomFormat(Resolution, Resolution, Format, true);
		RT->PostEditChange();
		if (bCreated)
		{
			FAssetRegistryModule::AssetCreated(RT);
		}
		return SaveAsset(RT);
	}
}

bool DreamWorld::Setup::EnsureSurfaceMaps()
{
	const UDreamSurfaceSettings* S = UDreamSurfaceSettings::Get();
	bool bOk = SurfaceSetup::EnsureTarget(S->NearMapTexture, 256, PF_FloatRGBA, FLinearColor(0.0f, 0.0f, 0.0f, 0.0f));
	bOk &= SurfaceSetup::EnsureTarget(S->FarMapTexture, 128, PF_FloatRGBA, FLinearColor(0.0f, 0.0f, 0.0f, 0.0f));
	// The ice, frost, rime and frozen water beside them, the same sizes.
	bOk &= SurfaceSetup::EnsureTarget(S->NearIceMapTexture, 256, PF_FloatRGBA, FLinearColor(0.0f, 0.0f, 0.0f, 0.0f));
	bOk &= SurfaceSetup::EnsureTarget(S->FarIceMapTexture, 128, PF_FloatRGBA, FLinearColor(0.0f, 0.0f, 0.0f, 0.0f));
	// The cover map clears to "nothing overhead": (cover height, how fully, the ground's low, -).
	bOk &= SurfaceSetup::EnsureTarget(S->CoverMapTexture, 256, PF_A32B32G32R32F, FLinearColor(-1.0e9f, 0.0f, 0.0f, 0.0f));
	// The mask map clears to "no brush" (every multiplier 1).
	bOk &= SurfaceSetup::EnsureTarget(S->MaskMapTexture, 256, PF_FloatRGBA, FLinearColor(1.0f, 1.0f, 1.0f, 1.0f));
	// The trail map: 5 cm a texel over its 51 m.
	bOk &= SurfaceSetup::EnsureTarget(S->TrailMapTexture, 1024, PF_FloatRGBA, FLinearColor(0.0f, 0.0f, 0.0f, 0.0f));
	return bOk;
}

// ---------------------------------------------------------------------------------------------------------------------
// The surface test level

namespace DreamWorld::SurfaceSetup
{
	const TCHAR* GroundPackage = TEXT("/DreamDynamicWorld/Surface/Meshes/SM_DreamSurfaceGround");
	const TCHAR* FigurePackage = TEXT("/DreamDynamicWorld/Surface/Meshes/SM_DreamSurfaceFigure");

	/** A package to build an asset in afresh: a copy already in memory lets go of its file and moves aside. */
	UPackage* FreshPackage(const TCHAR* Path)
	{
		if (UPackage* Existing = FindPackage(nullptr, Path))
		{
			ResetLoaders(Existing);
			Existing->Rename(*MakeUniqueObjectName(nullptr, UPackage::StaticClass(), TEXT("/Temp/DreamSurfaceOld")).ToString(), nullptr, REN_DontCreateRedirectors | REN_NonTransactional);
		}
		UPackage* Package = CreatePackage(Path);
		Package->MarkAsFullyLoaded();
		return Package;
	}

	UStaticMesh* SaveMesh(const TCHAR* Path, FMeshDescription&& Description, const TCHAR* Slot, float DistanceField)
	{
		UPackage* Package = FreshPackage(Path);
		UStaticMesh* Mesh = NewObject<UStaticMesh>(Package, *FPackageName::GetShortName(Path), RF_Public | RF_Standalone);
		Mesh->GetStaticMaterials().Add(FStaticMaterial(UMaterial::GetDefaultMaterial(MD_Surface), Slot, Slot));
		FStaticMeshSourceModel& Source = Mesh->AddSourceModel();
		Source.BuildSettings.bRecomputeNormals = false;
		Source.BuildSettings.bRecomputeTangents = true;
		Source.BuildSettings.DistanceFieldResolutionScale = DistanceField;
		FMeshDescription* Target = Mesh->CreateMeshDescription(0);
		*Target = MoveTemp(Description);
		Mesh->CommitMeshDescription(0);
		Mesh->Build(true);
		Mesh->PostEditChange();
		FAssetRegistryModule::AssetCreated(Mesh);
		return SaveAsset(Mesh) ? Mesh : nullptr;
	}

	/** The ground's height (cm) at a point: flat around the pavement, rolling further out, a dip to the left. */
	double GroundHeight(double X, double Y)
	{
		const double FromPavement = FVector2D(X - 1300.0, Y - 400.0).Size();
		const double Rolling = 45.0 * FMath::Sin(X / 900.0) * FMath::Cos(Y / 700.0) + 25.0 * FMath::Sin((X + Y) / 430.0);
		const double Dip = -90.0 * FMath::Exp(-FVector2D(X - 900.0, Y + 1100.0).SizeSquared() / FMath::Square(450.0));
		return -3.0 + Rolling * FMath::SmoothStep(1400.0, 2600.0, FromPavement) + Dip;
	}

	/** A grid of the ground's heights, 80 m square around (2500, 0), a vertex every 50 cm (dense enough for its snow). */
	UStaticMesh* EnsureGround()
	{
		constexpr int32 Cells = 160;
		constexpr double Size = 8000.0;
		const FVector2D Origin(2500.0 - Size * 0.5, -Size * 0.5);
		const double Step = Size / Cells;
		auto Build = [&](bool bFlip)
		{
			FMeshDescription Description;
			FStaticMeshAttributes Attributes(Description);
			Attributes.Register();
			TVertexAttributesRef<FVector3f> Positions = Attributes.GetVertexPositions();
			TVertexInstanceAttributesRef<FVector2f> UVs = Attributes.GetVertexInstanceUVs();
			const FPolygonGroupID Group = Description.CreatePolygonGroup();
			Attributes.GetPolygonGroupMaterialSlotNames()[Group] = TEXT("Ground");
			TArray<FVertexInstanceID> Instances;
			Instances.Reserve((Cells + 1) * (Cells + 1));
			for (int32 j = 0; j <= Cells; ++j)
			{
				for (int32 i = 0; i <= Cells; ++i)
				{
					const double X = Origin.X + i * Step;
					const double Y = Origin.Y + j * Step;
					const FVertexID Vertex = Description.CreateVertex();
					Positions[Vertex] = FVector3f(float(X), float(Y), float(GroundHeight(X, Y)));
					const FVertexInstanceID Instance = Description.CreateVertexInstance(Vertex);
					UVs.Set(Instance, 0, FVector2f(float(i) / Cells, float(j) / Cells) * 40.0f);
					Instances.Add(Instance);
				}
			}
			for (int32 j = 0; j < Cells; ++j)
			{
				for (int32 i = 0; i < Cells; ++i)
				{
					const FVertexInstanceID A = Instances[j * (Cells + 1) + i];
					const FVertexInstanceID B = Instances[j * (Cells + 1) + i + 1];
					const FVertexInstanceID C = Instances[(j + 1) * (Cells + 1) + i + 1];
					const FVertexInstanceID D = Instances[(j + 1) * (Cells + 1) + i];
					if (bFlip)
					{
						Description.CreateTriangle(Group, { A, C, B });
						Description.CreateTriangle(Group, { A, D, C });
					}
					else
					{
						Description.CreateTriangle(Group, { A, B, C });
						Description.CreateTriangle(Group, { A, C, D });
					}
				}
			}
			FStaticMeshOperations::ComputeTriangleTangentsAndNormals(Description);
			FStaticMeshOperations::ComputeTangentsAndNormals(Description, EComputeNTBsFlags::Normals | EComputeNTBsFlags::Tangents | EComputeNTBsFlags::WeightedNTBs);
			return Description;
		};
		// Whichever winding faces up.
		FMeshDescription Description = Build(false);
		bool bDown = false;
		{
			FStaticMeshAttributes Attributes(Description);
			bDown = Attributes.GetVertexInstanceNormals()[FVertexInstanceID(0)].Z < 0.0f;
		}
		if (bDown)
		{
			Description = Build(true);
		}
		return SaveMesh(GroundPackage, MoveTemp(Description), TEXT("Ground"));
	}

	/** A figure of the engine's basic shapes, 1.8 m tall: legs, body, arms, head (stands in for a toon character). */
	UStaticMesh* EnsureFigure()
	{
		struct FPart
		{
			const TCHAR* Mesh;
			FTransform Transform;
		};
		const TCHAR* Cylinder = TEXT("/Engine/BasicShapes/Cylinder.Cylinder");
		const TCHAR* Sphere = TEXT("/Engine/BasicShapes/Sphere.Sphere");
		const FPart Parts[] = {
			{ Cylinder, FTransform(FRotator::ZeroRotator, FVector(0.0, -14.0, 45.0), FVector(0.24, 0.24, 0.9)) },
			{ Cylinder, FTransform(FRotator::ZeroRotator, FVector(0.0, 14.0, 45.0), FVector(0.24, 0.24, 0.9)) },
			{ Cylinder, FTransform(FRotator::ZeroRotator, FVector(0.0, 0.0, 125.0), FVector(0.42, 0.55, 0.75)) },
			{ Sphere, FTransform(FRotator::ZeroRotator, FVector(0.0, 0.0, 160.0), FVector(0.42, 0.55, 0.25)) },
			{ Cylinder, FTransform(FRotator(0.0, 0.0, 12.0), FVector(0.0, -36.0, 122.0), FVector(0.16, 0.16, 0.7)) },
			{ Cylinder, FTransform(FRotator(0.0, 0.0, -12.0), FVector(0.0, 36.0, 122.0), FVector(0.16, 0.16, 0.7)) },
			{ Sphere, FTransform(FRotator::ZeroRotator, FVector(0.0, 0.0, 186.0), FVector(0.36)) },
		};
		FMeshDescription Merged;
		FStaticMeshAttributes Attributes(Merged);
		Attributes.Register();
		const FPolygonGroupID Group = Merged.CreatePolygonGroup();
		Attributes.GetPolygonGroupMaterialSlotNames()[Group] = TEXT("Figure");
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
		return SaveMesh(FigurePackage, MoveTemp(Merged), TEXT("Figure"));
	}

	/** An instance of a demo material with these parameters (Metallic below 0: the parent's), made afresh. */
	UMaterialInterface* EnsureInstance(const TCHAR* Name, const TCHAR* ParentPath, const FLinearColor& Tint, float Roughness, float Metallic)
	{
		UMaterialInterface* Parent = LoadObject<UMaterialInterface>(nullptr, ParentPath);
		if (!Parent)
		{
			UE_LOG(LogDreamWorld, Error, TEXT("%s is missing (compile the DreamSurface demo materials first)."), ParentPath);
			return nullptr;
		}
		const FString Path = FString(TEXT("/DreamDynamicWorld/Surface/Materials/")) + Name;
		UPackage* Package = FreshPackage(*Path);
		UMaterialInstanceConstant* Instance = NewObject<UMaterialInstanceConstant>(Package, Name, RF_Public | RF_Standalone);
		Instance->SetParentEditorOnly(Parent);
		Instance->SetVectorParameterValueEditorOnly(FMaterialParameterInfo(TEXT("Tint")), Tint);
		Instance->SetScalarParameterValueEditorOnly(FMaterialParameterInfo(TEXT("Roughness")), Roughness);
		if (Metallic >= 0.0f)
		{
			Instance->SetScalarParameterValueEditorOnly(FMaterialParameterInfo(TEXT("Metallic")), Metallic);
		}
		Instance->PostEditChange();
		FAssetRegistryModule::AssetCreated(Instance);
		return SaveAsset(Instance) ? Instance : nullptr;
	}

	void ReadyCollision(UWorld& World)
	{
		for (TActorIterator<AStaticMeshActor> It(&World); It; ++It)
		{
			UStaticMeshComponent* Component = It->GetStaticMeshComponent();
			UStaticMesh* Mesh = Component ? Component->GetStaticMesh() : nullptr;
			if (UBodySetup* Body = Mesh ? Mesh->GetBodySetup() : nullptr)
			{
				Body->CreatePhysicsMeshes();
				Component->RecreatePhysicsState();
			}
		}
	}

	AStaticMeshActor* AddMesh(UWorld& World, UStaticMesh* Mesh, UMaterialInterface* Material, const FVector& Location, const FVector& Scale,
		const FRotator& Rotation, const TCHAR* Label)
	{
		if (!Mesh)
		{
			return nullptr;
		}
		AStaticMeshActor* Actor = World.SpawnActor<AStaticMeshActor>(Location, Rotation);
		UStaticMeshComponent* Component = Actor->GetStaticMeshComponent();
		Component->SetStaticMesh(Mesh);
		Component->SetMobility(EComponentMobility::Static);
		if (Material)
		{
			Component->SetMaterial(0, Material);
		}
		Actor->SetActorScale3D(Scale);
		Actor->SetActorLabel(Label);
		return Actor;
	}
}

bool DreamWorld::Setup::BuildSurfaceTestMap(UDreamSkyStyle* Style)
{
	UStaticMesh* Ground = SurfaceSetup::EnsureGround();
	UStaticMesh* Figure = SurfaceSetup::EnsureFigure();
	const TCHAR* Demo = TEXT("/DreamDynamicWorld/Surface/Materials/M_DreamSurfaceDemo.M_DreamSurfaceDemo");
	const TCHAR* Toon = TEXT("/DreamDynamicWorld/Surface/Materials/M_DreamSurfaceToonDemo.M_DreamSurfaceToonDemo");
	UMaterialInterface* Terrain = LoadObject<UMaterialInterface>(nullptr, TEXT("/DreamDynamicWorld/Surface/Materials/M_DreamSurfaceTerrainDemo.M_DreamSurfaceTerrainDemo"));
	UMaterialInterface* Stone = SurfaceSetup::EnsureInstance(TEXT("MI_DreamSurfaceStone"), Demo, FLinearColor(0.42f, 0.4f, 0.37f), 0.8f, 0.0f);
	UMaterialInterface* Paving = SurfaceSetup::EnsureInstance(TEXT("MI_DreamSurfacePaving"), Demo, FLinearColor(0.5f, 0.47f, 0.43f), 0.7f, 0.0f);
	UMaterialInterface* Metal = SurfaceSetup::EnsureInstance(TEXT("MI_DreamSurfaceMetal"), Demo, FLinearColor(0.85f, 0.86f, 0.88f), 0.3f, 1.0f);
	UMaterialInterface* Wood = SurfaceSetup::EnsureInstance(TEXT("MI_DreamSurfaceWood"), Demo, FLinearColor(0.4f, 0.26f, 0.14f), 0.6f, 0.0f);
	UMaterialInterface* ToonRed = SurfaceSetup::EnsureInstance(TEXT("MI_DreamSurfaceToonRed"), Toon, FLinearColor(0.85f, 0.36f, 0.3f), 0.6f, -1.0f);
	UMaterialInterface* ToonBlue = SurfaceSetup::EnsureInstance(TEXT("MI_DreamSurfaceToonBlue"), Toon, FLinearColor(0.3f, 0.46f, 0.82f), 0.6f, -1.0f);
	UMaterialInterface* ToonYellow = SurfaceSetup::EnsureInstance(TEXT("MI_DreamSurfaceToonYellow"), Toon, FLinearColor(0.92f, 0.78f, 0.35f), 0.5f, -1.0f);
	if (!Ground || !Figure || !Terrain || !Stone || !Paving || !Metal || !Wood || !ToonRed || !ToonBlue || !ToonYellow)
	{
		return false;
	}

	UPackage* Package = SurfaceSetup::FreshPackage(SurfaceTestMapPackage);
	UWorld::InitializationValues Values;
	Values.RequiresHitProxies(false);
	Values.ShouldSimulatePhysics(false);
	Values.EnableTraceCollision(true);   // the wind terrain bake traces the level
	Values.CreateNavigation(false);
	Values.CreateAISystem(false);
	Values.AllowAudioPlayback(false);
	Values.CreatePhysicsScene(true);
	UWorld* World = UWorld::CreateWorld(EWorldType::Editor, false, FName(FPackageName::GetShortName(SurfaceTestMapPackage)), Package, true, ERHIFeatureLevel::Num, &Values);
	if (!World)
	{
		return false;
	}
	World->SetFlags(RF_Public | RF_Standalone);

	ADreamSky* Sky = World->SpawnActor<ADreamSky>(FVector::ZeroVector, FRotator::ZeroRotator);
	Sky->Style = Style;
	Sky->TimeOfDay = 11.0f;
	Sky->SetActorLabel(TEXT("DreamSky"));
	// A clear schedule: force the weather (DreamWeather.Set) to watch the surfaces change.
	ADreamWeather* Weather = World->SpawnActor<ADreamWeather>(FVector(0.0f, 0.0f, 200.0f), FRotator::ZeroRotator);
	Weather->SetActorLabel(TEXT("DreamWeather"));
	UDreamWeatherSelector_Fixed* Fixed = NewObject<UDreamWeatherSelector_Fixed>(Weather);
	Fixed->Preset = LoadObject<UDreamWeatherPreset>(nullptr, TEXT("/DreamDynamicWorld/Weather/Presets/WP_Clear.WP_Clear"));
	Weather->Selector = Fixed;

	const FVector Unit(1.0);
	SurfaceSetup::AddMesh(*World, Ground, Terrain, FVector::ZeroVector, Unit, FRotator::ZeroRotator, TEXT("Ground"));

	// The pavement, a canopy over its far end with a shelter box below the roof.
	UStaticMesh* Cube = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
	UStaticMesh* Cylinder = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
	UStaticMesh* Sphere = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Sphere.Sphere"));
	UStaticMesh* Cone = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cone.Cone"));
	SurfaceSetup::AddMesh(*World, Cube, Paving, FVector(1300.0, 400.0, 0.0), FVector(14.0, 10.0, 0.1), FRotator::ZeroRotator, TEXT("Pavement"));
	const FVector Canopy(1700.0, 700.0, 0.0);
	const FVector2D Corners[] = { FVector2D(-260.0, -170.0), FVector2D(260.0, -170.0), FVector2D(-260.0, 170.0), FVector2D(260.0, 170.0) };
	for (const FVector2D& Corner : Corners)
	{
		SurfaceSetup::AddMesh(*World, Cylinder, Wood, Canopy + FVector(Corner, 150.0), FVector(0.25, 0.25, 3.0), FRotator::ZeroRotator, TEXT("CanopyPillar"));
	}
	SurfaceSetup::AddMesh(*World, Cube, Wood, Canopy + FVector(0.0, 0.0, 310.0), FVector(6.4, 4.4, 0.2), FRotator::ZeroRotator, TEXT("CanopyRoof"));
	ADreamWeatherShelter* Shelter = World->SpawnActor<ADreamWeatherShelter>(Canopy + FVector(0.0, 0.0, 150.0), FRotator::ZeroRotator);
	Shelter->Shelter->Extent = FVector(310.0, 210.0, 150.0);
	Shelter->Shelter->FadeDistance = 40.0f;
	Shelter->SetActorLabel(TEXT("CanopyShelter"));

	// Realistic props: stone, metal, wood; one under the canopy.
	SurfaceSetup::AddMesh(*World, Cube, Stone, FVector(1050.0, -150.0, 60.0), FVector(1.0), FRotator(0.0, 20.0, 0.0), TEXT("StoneBlock"));
	SurfaceSetup::AddMesh(*World, Sphere, Metal, FVector(1150.0, 150.0, 60.0), FVector(1.0), FRotator::ZeroRotator, TEXT("MetalBall"));
	SurfaceSetup::AddMesh(*World, Cylinder, Wood, FVector(1350.0, -50.0, 50.0), FVector(0.8, 0.8, 0.8), FRotator::ZeroRotator, TEXT("WoodPost"));
	SurfaceSetup::AddMesh(*World, Cube, Stone, Canopy + FVector(-80.0, 0.0, 50.0), FVector(1.0), FRotator(0.0, -15.0, 0.0), TEXT("StoneUnderCanopy"));

	// Toon props and the toon figure.
	SurfaceSetup::AddMesh(*World, Cube, ToonRed, FVector(1250.0, 650.0, 50.0), FVector(0.9), FRotator(0.0, 35.0, 0.0), TEXT("ToonBox"));
	SurfaceSetup::AddMesh(*World, Sphere, ToonYellow, FVector(1050.0, 500.0, 50.0), FVector(0.9), FRotator::ZeroRotator, TEXT("ToonBall"));
	SurfaceSetup::AddMesh(*World, Cone, ToonRed, FVector(1450.0, 350.0, 50.0), FVector(0.8), FRotator::ZeroRotator, TEXT("ToonCone"));
	SurfaceSetup::AddMesh(*World, Figure, ToonBlue, FVector(950.0, 250.0, 5.0), FVector(1.0), FRotator(0.0, 200.0, 0.0), TEXT("ToonFigure"));

	// Ahead left: a heated circle (a mask brush: no water or snow on it, a soft edge), and a pergola with no shelter box,
	// only a roof capture box that finds its roof from above.
	ADreamSurfaceMask* Heated = World->SpawnActor<ADreamSurfaceMask>(FVector(450.0, -350.0, 0.0), FRotator::ZeroRotator);
	Heated->Mask->Shape = EDreamSurfaceMaskShape::Circle;
	Heated->Mask->Extent = FVector2D(250.0, 250.0);
	Heated->Mask->FadeDistance = 120.0f;
	Heated->Mask->Wetness = 0.0f;
	Heated->Mask->Water = 0.0f;
	Heated->Mask->Snow = 0.0f;
	Heated->Mask->Dust = 1.0f;
	Heated->SetActorLabel(TEXT("HeatedCircle"));
	const FVector Pergola(2500.0, -500.0, 0.0);
	for (const FVector2D& Corner : { FVector2D(-220.0, -170.0), FVector2D(220.0, -170.0), FVector2D(-220.0, 170.0), FVector2D(220.0, 170.0) })
	{
		SurfaceSetup::AddMesh(*World, Cylinder, Wood, Pergola + FVector(Corner, 140.0), FVector(0.2, 0.2, 2.8), FRotator::ZeroRotator, TEXT("PergolaPillar"));
	}
	SurfaceSetup::AddMesh(*World, Cube, Stone, Pergola + FVector(0.0, 0.0, 290.0), FVector(5.4, 4.4, 0.2), FRotator::ZeroRotator, TEXT("PergolaRoof"));
	ADreamSurfaceRoofCapture* RoofBox = World->SpawnActor<ADreamSurfaceRoofCapture>(Pergola + FVector(0.0, 0.0, 200.0), FRotator::ZeroRotator);
	RoofBox->Roof->Extent = FVector(320.0, 270.0, 200.0);
	RoofBox->SetActorLabel(TEXT("PergolaRoofCapture"));

	// A pond in the dip to the left, its edges buried in the slopes: its own water material, rippled while it rains
	// (MF_DreamWaterRipples).
	UMaterialInterface* Water = LoadObject<UMaterialInterface>(nullptr, TEXT("/DreamDynamicWorld/Surface/Materials/M_DreamSurfaceWaterDemo.M_DreamSurfaceWaterDemo"));
	SurfaceSetup::AddMesh(*World, LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Plane.Plane")), Water, FVector(900.0, -1100.0, -45.0), FVector(9.0, 9.0, 1.0),
		FRotator::ZeroRotator, TEXT("Pond"));

	// A window standing on the pavement, facing the start: a wooden frame round a pane of glass with the rain on it
	// (MF_DreamGlassRain), the toon box seen through it.
	if (UMaterialInterface* Glass = LoadObject<UMaterialInterface>(nullptr, TEXT("/DreamDynamicWorld/Surface/Materials/M_DreamGlassDemo.M_DreamGlassDemo")))
	{
		const FVector Window(800.0, 750.0, 120.0);
		SurfaceSetup::AddMesh(*World, Cube, Glass, Window, FVector(0.01, 2.0, 1.5), FRotator::ZeroRotator, TEXT("WindowPane"));
		SurfaceSetup::AddMesh(*World, Cube, Wood, Window + FVector(0.0, 0.0, 80.0), FVector(0.1, 2.2, 0.1), FRotator::ZeroRotator, TEXT("WindowHead"));
		SurfaceSetup::AddMesh(*World, Cube, Wood, Window - FVector(0.0, 0.0, 80.0), FVector(0.14, 2.2, 0.1), FRotator::ZeroRotator, TEXT("WindowSill"));
		SurfaceSetup::AddMesh(*World, Cube, Wood, Window + FVector(0.0, 105.0, 0.0), FVector(0.1, 0.1, 1.7), FRotator::ZeroRotator, TEXT("WindowJamb"));
		SurfaceSetup::AddMesh(*World, Cube, Wood, Window - FVector(0.0, 105.0, 0.0), FVector(0.1, 0.1, 1.7), FRotator::ZeroRotator, TEXT("WindowJamb"));
		SurfaceSetup::AddMesh(*World, Cube, Wood, Window - FVector(0.0, 0.0, 102.5), FVector(0.1, 0.4, 0.35), FRotator::ZeroRotator, TEXT("WindowStand"));
	}

	// Far ahead right: a snow zone of no regular shape, a bay cut into the side facing the camera; its border lies on
	// the ground.
	ADreamWeatherOutlineZone* SnowZone = World->SpawnActor<ADreamWeatherOutlineZone>(FVector(5200.0, -2600.0, 0.0), FRotator::ZeroRotator);
	SnowZone->Outline->ClearSplinePoints(false);
	for (const FVector& P : { FVector(1900.0, -300.0, 0.0), FVector(1300.0, 1100.0, 0.0), FVector(200.0, 1400.0, 0.0), FVector(-150.0, 450.0, 0.0),
		FVector(-1000.0, 1500.0, 0.0), FVector(-1800.0, 400.0, 0.0), FVector(-1300.0, -1300.0, 0.0), FVector(300.0, -1800.0, 0.0) })
	{
		SnowZone->Outline->AddSplinePoint(P, ESplineCoordinateSpace::Local, false);
	}
	SnowZone->Outline->UpdateSpline();
	SnowZone->OutlineSpacing = 200.0f;
	SnowZone->UpdateFootprint();
	SnowZone->Zone->BlendDistance = 500.0f;
	SnowZone->Zone->Channels.Add(DreamWeather::Tags::Channel_CloudCoverage, 1.0f);
	SnowZone->Zone->Channels.Add(DreamWeather::Tags::Channel_Precipitation, 0.6f);
	SnowZone->Zone->Channels.Add(DreamWeather::Tags::Channel_SnowRatio, 1.0f);
	SnowZone->Zone->Channels.Add(DreamWeather::Tags::Channel_Temperature, -20.0f);
	SnowZone->SetActorLabel(TEXT("SnowZone"));

	// The wind's baked level heights over the test area: roofs keep the rain off, and the puddles follow the ground's
	// dips (DSF_GroundLow) - the pond's hollow, the rolling ground's valleys.
	// Props, pillars and the window are left out of it (pass-through): their tops are no roof over the ground beside them.
	static const TSet<FString> Props = { TEXT("StoneBlock"), TEXT("MetalBall"), TEXT("WoodPost"), TEXT("StoneUnderCanopy"), TEXT("ToonBox"),
		TEXT("ToonBall"), TEXT("ToonCone"), TEXT("ToonFigure"), TEXT("CanopyPillar"), TEXT("PergolaPillar"), TEXT("WindowPane"),
		TEXT("WindowHead"), TEXT("WindowSill"), TEXT("WindowJamb"), TEXT("WindowStand") };
	for (TActorIterator<AStaticMeshActor> It(World); It; ++It)
	{
		FString Label = It->GetActorLabel();
		int32 Suffix = INDEX_NONE;
		if (Label.FindLastChar(TEXT('_'), Suffix))
		{
			Label.LeftInline(Suffix);   // a repeated label comes back as Name_1, Name_2
		}
		if (Props.Contains(Label))
		{
			It->Tags.AddUnique(UDreamWindPassThroughComponent::PassThroughTag);
		}
	}
	ADreamWindGlobal* Wind = World->SpawnActor<ADreamWindGlobal>(FVector(0.0, 0.0, 0.0), FRotator::ZeroRotator);
	Wind->SetActorLabel(TEXT("Wind"));
	Wind->TerrainBakeBounds = FBox(FVector(-2000.0, -4000.0, -500.0), FVector(7000.0, 3000.0, 1500.0));
	Wind->TerrainBakeSettings.Resolution = 512;
	SurfaceSetup::ReadyCollision(*World);
	if (!UDreamWindEditorLibrary::BakeTerrainMapForActor(Wind))
	{
		UE_LOG(LogDreamWorld, Warning, TEXT("Surface test level: the wind terrain map could not be baked; puddles follow the hollow pattern only."));
	}

	ACameraActor* Camera = World->SpawnActor<ACameraActor>(FVector(0.0, 0.0, 260.0), FRotator(-12.0, 15.0, 0.0));
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
