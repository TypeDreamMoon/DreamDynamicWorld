// Copyright DreamMoon. All Rights Reserved.

#include "DreamWorldShowcaseSetup.h"
#include "DreamDynamicWorldModule.h"
#include "DreamSky.h"
#include "DreamSkyConditions.h"
#include "DreamSkyModifier.h"
#include "DreamSkyStyle.h"
#include "DreamSurfaceMask.h"
#include "DreamSurfaceRoofCapture.h"
#include "DreamSurfaceDripLine.h"
#include "DreamSurfaceSetup.h"
#include "DreamSurfaceVehicleGlass.h"
#include "DreamSurfaceWalker.h"
#include "DreamWeather.h"
#include "DreamWeatherFallPaths.h"
#include "DreamTerrainFog.h"
#include "DreamWeatherFog.h"
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
#include "Components/PointLightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/PointLight.h"
#include "Engine/CollisionProfile.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/World.h"
#include "Materials/MaterialInstanceConstant.h"
#include "EngineUtils.h"
#include "MeshDescription.h"
#include "PhysicsEngine/BodySetup.h"
#include "StaticMeshAttributes.h"
#include "StaticMeshOperations.h"
#include "DreamWeatherRadar.h"
#include "GameFramework/PlayerStart.h"
#include "LevelSequence.h"
#include "LevelSequenceActor.h"
#include "Materials/MaterialInterface.h"
#include "MovieScene.h"
#include "MovieSceneMetaData.h"
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

	/** A sheet of high ice cloud: the halo and the sun dogs round a low sun. */
	UDreamSkyModifier* EnsureHalo()
	{
		UDreamSkyModifier* Halo = FreshAsset<UDreamSkyModifier>(Setup::ShowcaseHaloPackage);
		Halo->Settings.bOverride_HighCloudCover = true;
		Halo->Settings.HighCloudCover = 0.9f;
		Halo->FadeInSeconds = 6.0f;
		Halo->FadeOutSeconds = 6.0f;
		return SurfaceSetup::SaveAsset(Halo) ? Halo : nullptr;
	}

	/** Noctilucent clouds whatever the place and the season (they still show only in the deep twilight). */
	UDreamSkyModifier* EnsureNoctilucent()
	{
		UDreamSkyModifier* Shine = FreshAsset<UDreamSkyModifier>(Setup::ShowcaseNoctilucentPackage);
		Shine->Settings.bOverride_Noctilucent = true;
		Shine->Settings.Noctilucent = 1.0f;
		Shine->FadeInSeconds = 6.0f;
		Shine->FadeOutSeconds = 6.0f;
		return SurfaceSetup::SaveAsset(Shine) ? Shine : nullptr;
	}

	/** A meteor storm for the night (the date's own shower, or the sporadic ones, ten times over). */
	UDreamSkyModifier* EnsureMeteors()
	{
		UDreamSkyModifier* Storm = FreshAsset<UDreamSkyModifier>(Setup::ShowcaseMeteorsPackage);
		Storm->Settings.bOverride_Meteors = true;
		Storm->Settings.Meteors = 10.0f;
		Storm->FadeInSeconds = 6.0f;
		Storm->FadeOutSeconds = 6.0f;
		return SurfaceSetup::SaveAsset(Storm) ? Storm : nullptr;
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
		// Initialize stamps the machine's user name as author; the shipped asset should not carry it.
		Sequence->RemoveMetaData<UMovieSceneMetaData>();
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

	/**
	 * The pond's water: a disc whose rim follows the bank, just inside the line where the ground (SurfaceSetup's
	 * height) comes up out of the water at the pond's level - a smooth shore of 128 sides instead of the ground's grid
	 * cut by a flat plane. The rim is found along each ray from the middle (the first rise above the water), so the
	 * outline is as round or as long as the hollow is. Centred on the pond's middle, at its level.
	 */
	UStaticMesh* EnsurePond()
	{
		constexpr int32 Sides = 128;
		const FVector2D Center = Setup::ShowcasePondCenter;
		const double Level = Setup::ShowcasePondLevel;
		auto Above = [&](const FVector2D& Dir, double R) { const FVector2D P = Center + Dir * R; return SurfaceSetup::GroundHeight(P.X, P.Y) >= Level; };
		TArray<FVector2D> Rim;
		double Reach = 0.0;
		for (int32 i = 0; i < Sides; ++i)
		{
			const double Angle = 2.0 * UE_DOUBLE_PI * i / Sides;
			const FVector2D Dir(FMath::Cos(Angle), FMath::Sin(Angle));
			double Out = 10.0;
			while (Out < 3000.0 && !Above(Dir, Out))
			{
				Out += 10.0;
			}
			double In = Out - 10.0;
			for (int32 Step = 0; Step < 16; ++Step)
			{
				const double Mid = 0.5 * (In + Out);
				(Above(Dir, Mid) ? Out : In) = Mid;
			}
			const double R = FMath::Max(In - Setup::ShowcasePondInset, 10.0);
			Rim.Add(Dir * R);
			Reach = FMath::Max(Reach, R);
		}

		auto Build = [&](bool bFlip)
		{
			FMeshDescription Description;
			FStaticMeshAttributes Attributes(Description);
			Attributes.Register();
			TVertexAttributesRef<FVector3f> Positions = Attributes.GetVertexPositions();
			TVertexInstanceAttributesRef<FVector2f> UVs = Attributes.GetVertexInstanceUVs();
			const FPolygonGroupID Group = Description.CreatePolygonGroup();
			Attributes.GetPolygonGroupMaterialSlotNames()[Group] = TEXT("Water");
			auto Add = [&](const FVector2D& P)
			{
				const FVertexID Vertex = Description.CreateVertex();
				Positions[Vertex] = FVector3f(float(P.X), float(P.Y), 0.0f);
				const FVertexInstanceID Instance = Description.CreateVertexInstance(Vertex);
				UVs.Set(Instance, 0, FVector2f(float(P.X / (2.0 * Reach) + 0.5), float(P.Y / (2.0 * Reach) + 0.5)));
				return Instance;
			};
			const FVertexInstanceID Middle = Add(FVector2D::ZeroVector);
			TArray<FVertexInstanceID> Ring;
			for (const FVector2D& P : Rim)
			{
				Ring.Add(Add(P));
			}
			for (int32 i = 0; i < Sides; ++i)
			{
				const FVertexInstanceID A = Ring[i];
				const FVertexInstanceID B = Ring[(i + 1) % Sides];
				Description.CreateTriangle(Group, bFlip ? TArray<FVertexInstanceID>{ Middle, B, A } : TArray<FVertexInstanceID>{ Middle, A, B });
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
		UStaticMesh* Mesh = SurfaceSetup::SaveMesh(Setup::ShowcasePondPackage, MoveTemp(Description), TEXT("Water"));
		if (!Mesh)
		{
			return nullptr;
		}
		// Its surface is its collision: the rain lands on the water, not on the bed beneath it.
		Mesh->CreateBodySetup();
		Mesh->GetBodySetup()->CollisionTraceFlag = CTF_UseComplexAsSimple;
		Mesh->GetBodySetup()->InvalidatePhysicsData();
		Mesh->GetBodySetup()->CreatePhysicsMeshes();
		return SurfaceSetup::SaveAsset(Mesh) ? Mesh : nullptr;
	}

	/** A range of the far land: a band of distance from the set's middle, a span of azimuth, and how high it rises. */
	struct FFarRange
	{
		double Azimuth0, Azimuth1;   // degrees clockwise from north (+X), the span it stands in
		double Near, Far;            // cm from the middle, the band it stands in
		double Peak;                 // cm, its highest
		double Scale;                // cm, the size of its ridges
	};

	/** The far land's ranges: near hills, mid mountains and a far range in different directions, low hills all round. */
	const FFarRange FarRanges[] = {
		{ 195.0, 265.0, 500000.0, 750000.0, 60000.0, 220000.0 },      // near hills, south-west, 5-7.5 km, to 600 m
		{ 10.0, 110.0, 550000.0, 800000.0, 45000.0, 200000.0 },       // near hills, north-east (behind the wide shot's set)
		{ 240.0, 330.0, 1200000.0, 1600000.0, 130000.0, 400000.0 },   // mid mountains, west, 12-16 km, to 1.3 km
		{ 140.0, 230.0, 2400000.0, 3000000.0, 200000.0, 600000.0 },   // the far range, south, 24-30 km, to 2 km
		{ 0.0, 360.0, 2000000.0, 3300000.0, 60000.0, 500000.0 },      // low ranges all round, closing the horizon
	};

	/** How far the land rises above the set's level at a point (cm): the ranges, each ridged and broken up. */
	double FarHeight(double X, double Y)
	{
		const FVector2D Middle(Setup::ShowcaseFarCenter);
		const FVector2D P = FVector2D(X, Y) - Middle;
		const double R = P.Size();
		const double Azimuth = FMath::Fmod(FMath::RadiansToDegrees(FMath::Atan2(P.Y, P.X)) + 360.0, 360.0);
		double H = 0.0;
		for (const FFarRange& Range : FarRanges)
		{
			// Soft in distance and in azimuth, so a range rises and falls away rather than standing in a box.
			const double Mid = 0.5 * (Range.Near + Range.Far), Half = 0.5 * (Range.Far - Range.Near);
			const double InBand = FMath::Exp(-FMath::Square((R - Mid) / Half) * 1.5);
			const double Span = Range.Azimuth1 - Range.Azimuth0;
			const double Into = FMath::Fmod(Azimuth - Range.Azimuth0 + 360.0, 360.0);
			const double InSpan = Span >= 359.0 ? 1.0 : (Into <= Span ? FMath::Sin(UE_DOUBLE_PI * Into / Span) : 0.0);
			if (InBand * InSpan < 1.0e-3)
			{
				continue;
			}
			// Ridges: two octaves of folded noise, peaks sharp, valleys broad.
			const FVector2D Q = FVector2D(X, Y) / Range.Scale + FVector2D(Range.Azimuth0 * 0.37, Range.Near * 1.0e-6);
			const double Ridge = 1.0 - FMath::Abs(FMath::PerlinNoise2D(Q)) * 1.2;
			const double Detail = 1.0 - FMath::Abs(FMath::PerlinNoise2D(Q * 2.7 + FVector2D(13.1, 7.7)));
			const double Shape = FMath::Clamp(0.65 * Ridge + 0.35 * Detail, 0.0, 1.0);
			H = FMath::Max(H, Range.Peak * InBand * FMath::Pow(InSpan, 0.6) * (0.25 + 0.75 * FMath::Square(Shape)));
		}
		return H;
	}

	/**
	 * The land around the set out to the horizon: rings round the set's middle from just inside the ground's edge to
	 * 35 km, closer near the set. Near the set it follows the set's ground (SurfaceSetup's height) a little below it, so
	 * the ground's edge meets it without a step; from a few hundred metres out it rises into the far ranges. For the
	 * air between the eye and the land (aerial perspective, fog) and the snow on the far peaks.
	 */
	/** The far land's height at a point (cm): the set's ground near it, the ranges beyond, a little under the set's ground where they overlap. */
	double FarLandHeight(double X, double Y)
	{
		const double R = (FVector2D(X, Y) - FVector2D(Setup::ShowcaseFarCenter)).Size();
		return FMath::Lerp(SurfaceSetup::GroundHeight(X, Y) - 20.0, FarHeight(X, Y) - 3.0, FMath::SmoothStep(15000.0, 250000.0, R));
	}

	UStaticMesh* EnsureFarLand()
	{
		constexpr int32 Rings = 150, Sides = 360;
		const double Inner = 3900.0, Outer = 3500000.0;
		const FVector2D Middle(Setup::ShowcaseFarCenter);
		auto HeightAt = [&](double X, double Y, double R) { return FarLandHeight(X, Y); };
		auto Build = [&](bool bFlip)
		{
			FMeshDescription Description;
			FStaticMeshAttributes Attributes(Description);
			Attributes.Register();
			TVertexAttributesRef<FVector3f> Positions = Attributes.GetVertexPositions();
			TVertexInstanceAttributesRef<FVector2f> UVs = Attributes.GetVertexInstanceUVs();
			const FPolygonGroupID Group = Description.CreatePolygonGroup();
			Attributes.GetPolygonGroupMaterialSlotNames()[Group] = TEXT("Land");
			TArray<FVertexInstanceID> Instances;
			Instances.Reserve((Rings + 1) * Sides);
			for (int32 Ring = 0; Ring <= Rings; ++Ring)
			{
				const double R = Inner * FMath::Pow(Outer / Inner, double(Ring) / Rings);
				for (int32 Side = 0; Side < Sides; ++Side)
				{
					const double Angle = 2.0 * UE_DOUBLE_PI * Side / Sides;
					const double X = Middle.X + R * FMath::Cos(Angle), Y = Middle.Y + R * FMath::Sin(Angle);
					const FVertexID Vertex = Description.CreateVertex();
					Positions[Vertex] = FVector3f(float(X - Middle.X), float(Y - Middle.Y), float(HeightAt(X, Y, R)));
					const FVertexInstanceID Instance = Description.CreateVertexInstance(Vertex);
					UVs.Set(Instance, 0, FVector2f(float(X / 2000.0), float(Y / 2000.0)));
					Instances.Add(Instance);
				}
			}
			for (int32 Ring = 0; Ring < Rings; ++Ring)
			{
				for (int32 Side = 0; Side < Sides; ++Side)
				{
					const int32 Next = (Side + 1) % Sides;
					const FVertexInstanceID A = Instances[Ring * Sides + Side];
					const FVertexInstanceID B = Instances[Ring * Sides + Next];
					const FVertexInstanceID C = Instances[(Ring + 1) * Sides + Next];
					const FVertexInstanceID D = Instances[(Ring + 1) * Sides + Side];
					Description.CreateTriangle(Group, bFlip ? TArray<FVertexInstanceID>{ A, C, B } : TArray<FVertexInstanceID>{ A, B, C });
					Description.CreateTriangle(Group, bFlip ? TArray<FVertexInstanceID>{ A, D, C } : TArray<FVertexInstanceID>{ A, C, D });
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
		return SurfaceSetup::SaveMesh(Setup::ShowcaseFarLandPackage, MoveTemp(Description), TEXT("Land"), 0.0f);
	}

	struct FShowcaseKit
	{
		UStaticMesh* Cube = nullptr;
		UStaticMesh* Cylinder = nullptr;
		UStaticMesh* Sphere = nullptr;
		UStaticMesh* Cone = nullptr;
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
		UMaterialInterface* Leaves = nullptr;
		UStaticMesh* Pond = nullptr;
		UStaticMesh* FarLand = nullptr;
		UStaticMesh* Plane = nullptr;
		UMaterialInterface* VehicleGlass = nullptr;

		bool Load()
		{
			Cube = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
			Cylinder = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
			Sphere = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Sphere.Sphere"));
			Cone = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cone.Cone"));
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
			// A muted green for the grove's crowns.
			Leaves = SurfaceSetup::EnsureInstance(TEXT("MI_DreamSurfaceLeaves"), Demo, FLinearColor(0.2f, 0.27f, 0.16f), 0.75f, 0.0f);
			Pond = EnsurePond();
			FarLand = EnsureFarLand();
			Plane = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Plane.Plane"));
			VehicleGlass = EnsureVehicleGlass();
			return Cube && Cylinder && Sphere && Cone && Ground && Figure && Terrain && Water && Glass && Stone && Paving && Metal && Wood
				&& ToonRed && ToonBlue && ToonYellow && Leaves && Pond && FarLand && Plane && VehicleGlass;
		}

		/**
		 * The demo cars' windscreen glass: M_DreamVehicleGlassDemo with two wipers on the windscreen (AddCar's: the pane
		 * 85 cm up its slope and 150 cm across, the mesh's x down the slope, y across).
		 */
		static UMaterialInterface* EnsureVehicleGlass()
		{
			UMaterialInterface* Parent = LoadObject<UMaterialInterface>(nullptr, TEXT("/DreamDynamicWorld/Surface/Materials/M_DreamVehicleGlassDemo.M_DreamVehicleGlassDemo"));
			if (!Parent)
			{
				UE_LOG(LogDreamWorld, Error, TEXT("M_DreamVehicleGlassDemo is missing (compile the DreamSurface demo materials first)."));
				return nullptr;
			}
			const TCHAR* Name = TEXT("MI_DreamVehicleGlassDemo");
			UPackage* Package = SurfaceSetup::FreshPackage(TEXT("/DreamDynamicWorld/Surface/Materials/MI_DreamVehicleGlassDemo"));
			UMaterialInstanceConstant* Instance = NewObject<UMaterialInstanceConstant>(Package, Name, RF_Public | RF_Standalone);
			Instance->SetParentEditorOnly(Parent);
			// Pivots by the windscreen's lower edge (x down the slope), the blades lying along it to the left when parked.
			Instance->SetVectorParameterValueEditorOnly(FMaterialParameterInfo(TEXT("WiperPivot")), FLinearColor(38.0f, -48.0f, 0.0f, 62.0f));
			Instance->SetVectorParameterValueEditorOnly(FMaterialParameterInfo(TEXT("WiperPivot2")), FLinearColor(38.0f, 12.0f, 0.0f, 62.0f));
			Instance->SetVectorParameterValueEditorOnly(FMaterialParameterInfo(TEXT("WiperAngles")), FLinearColor(6.0f, 118.0f, 0.0f, 0.0f));
			Instance->PostEditChange();
			FAssetRegistryModule::AssetCreated(Instance);
			return SurfaceSetup::SaveAsset(Instance) ? Instance : nullptr;
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

	/** Drip lines along the lower edges of a flat roof (HalfX, HalfY: its half sizes; Z: its underside), all four but the
	 *  one numbered Skip (0 -X, 1 +X, 2 -Y, 3 +Y; -1 none): an edge against a wall or another roof does not drip. */
	void AddEaves(UWorld& World, const FVector& Center, double HalfX, double HalfY, double Z, const TCHAR* Label, int32 Skip = -1)
	{
		const struct { FVector At; double Yaw; double Length; } Edges[] = {
			{ FVector(-HalfX, 0.0, 0.0), 90.0, 2.0 * HalfY }, { FVector(HalfX, 0.0, 0.0), 90.0, 2.0 * HalfY },
			{ FVector(0.0, -HalfY, 0.0), 0.0, 2.0 * HalfX }, { FVector(0.0, HalfY, 0.0), 0.0, 2.0 * HalfX },
		};
		for (int32 i = 0; i < 4; ++i)
		{
			if (i == Skip)
			{
				continue;
			}
			const auto& Edge = Edges[i];
			ADreamSurfaceDripLine* Line = World.SpawnActor<ADreamSurfaceDripLine>(FVector(Center.X, Center.Y, Z) + Edge.At, FRotator(0.0, Edge.Yaw, 0.0));
			Line->Drips->Length = float(Edge.Length);
			Line->SetActorLabel(Label);
		}
	}

	/**
	 * A hut to step into: stone walls, a doorway on the -X side, a wide window on the -Y side, a wooden roof (the rain's
	 * roof); a table and a stool inside, and a dim warm lamp over the table. The walls block the view probe's rays, so
	 * inside counts as indoors; the window and the lamp light the room enough to see into it.
	 */
	void AddHut(UWorld& World, const FShowcaseKit& Kit, const FVector& Center)
	{
		const double Half = 300.0, Height = 300.0, Wall = 20.0, Door = 120.0, DoorHeight = 220.0;
		const double WindowWide = 260.0, Sill = 80.0, Head = 240.0;
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
		AddEaves(World, Center, Half + 30.0, Half + 30.0, Center.Z + Height, TEXT("Hut_Eave"));
		PassThrough(AddBox(World, Kit, Kit.Wood, Center + FVector(80.0, 60.0, 40.0), FVector(140.0, 90.0, 80.0), TEXT("Hut_Table")));
		PassThrough(SurfaceSetup::AddMesh(World, Kit.Cylinder, Kit.Wood, Center + FVector(-20.0, 60.0, 22.0), FVector(0.4, 0.4, 0.45), FRotator::ZeroRotator, TEXT("Hut_Stool")));

		// A lamp under the roof over the table: warm (3200 K) and dim (1.5 cd): by day the window lights the room, the lamp
		// only warms its corners; by night it is what the room is seen by.
		APointLight* Lamp = World.SpawnActor<APointLight>(Center + FVector(60.0, 60.0, Height - 50.0), FRotator::ZeroRotator);
		UPointLightComponent* Light = CastChecked<UPointLightComponent>(Lamp->GetLightComponent());
		Light->SetMobility(EComponentMobility::Movable);
		Light->IntensityUnits = ELightUnits::Candelas;
		Light->SetIntensity(1.5f);
		Light->bUseTemperature = true;
		Light->SetTemperature(3200.0f);
		Light->SetAttenuationRadius(800.0f);
		Light->SetSourceRadius(8.0f);
		Lamp->SetActorLabel(TEXT("Hut_Lamp"));
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
		// Its back edge runs under the hut's roof.
		AddEaves(World, Center, HalfX + 30.0, HalfY + 30.0, Center.Z + Height, TEXT("Porch_Eave"), 1);
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
			if (AStaticMeshActor* Crown = SurfaceSetup::AddMesh(World, Kit.Sphere, Kit.Leaves, Base + FVector(0.0, 0.0, 520.0 * Scale), FVector(3.6, 3.6, 3.0) * Scale,
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
	/**
	 * A small car (boxes, wheels, a windscreen and a rear window of vehicle glass) with a DreamSurface Vehicle Glass
	 * component: ExtraVelocity shows it as if it drove (cm/s, forward), the wipers running or parked.
	 */
	void AddCar(UWorld& World, const FShowcaseKit& Kit, const FVector& At, float Yaw, const FVector& ExtraVelocity, bool bWipers, const TCHAR* Label)
	{
		AActor* Car = World.SpawnActor<AActor>(At, FRotator(0.0, Yaw, 0.0));
		USceneComponent* Root = NewObject<USceneComponent>(Car, TEXT("Root"));
		Car->SetRootComponent(Root);
		Car->AddInstanceComponent(Root);
		Root->RegisterComponent();
		Root->SetWorldLocationAndRotation(At, FRotator(0.0, Yaw, 0.0));
		auto Part = [&](UStaticMesh* Mesh, UMaterialInterface* Material, const FVector& Location, const FVector& Scale, const FRotator& Rotation, const TCHAR* Name)
		{
			UStaticMeshComponent* Component = NewObject<UStaticMeshComponent>(Car, Name);
			Component->SetupAttachment(Root);
			Component->SetStaticMesh(Mesh);
			Component->SetMaterial(0, Material);
			Component->SetRelativeLocationAndRotation(Location, Rotation);
			Component->SetRelativeScale3D(Scale);
			Car->AddInstanceComponent(Component);
			Component->RegisterComponent();
			return Component;
		};
		Part(Kit.Cube, Kit.Metal, FVector(0.0, 0.0, 55.0), FVector(4.2, 1.8, 0.7), FRotator::ZeroRotator, TEXT("Body"));
		// The cabin dark behind its glass.
		Part(Kit.Cube, Kit.Stone, FVector(-40.0, 0.0, 111.0), FVector(2.3, 1.6, 0.42), FRotator::ZeroRotator, TEXT("Cabin"));
		for (int32 i = 0; i < 4; ++i)
		{
			const FVector Wheel((i < 2 ? 1.0 : -1.0) * 130.0, (i % 2 ? 1.0 : -1.0) * 88.0, 33.0);
			Part(Kit.Cylinder, Kit.Stone, Wheel, FVector(0.66, 0.66, 0.22), FRotator(0.0, 0.0, 90.0), TEXT("Wheel"));
		}
		// The windscreen raked 30 degrees from the level, its x down the slope; the rear window steeper.
		UStaticMeshComponent* Screen = Part(Kit.Plane, Kit.VehicleGlass, FVector(112.0, 0.0, 112.0), FVector(0.85, 1.5, 1.0), FRotator(-30.0, 0.0, 0.0), TEXT("Windscreen"));
		UStaticMeshComponent* Rear = Part(Kit.Plane, Kit.VehicleGlass, FVector(-176.0, 0.0, 112.0), FVector(0.55, 1.4, 1.0), FRotator(37.0, 0.0, 0.0), TEXT("RearWindow"));
		Screen->ComponentTags.Add(TEXT("DreamGlass"));
		Rear->ComponentTags.Add(TEXT("DreamGlass"));
		UDreamSurfaceVehicleGlassComponent* Glass = NewObject<UDreamSurfaceVehicleGlassComponent>(Car, TEXT("VehicleGlass"));
		Glass->ExtraVelocity = ExtraVelocity;
		Glass->bWipers = bWipers;
		Car->AddInstanceComponent(Glass);
		Glass->RegisterComponent();
		Glass->UpdateGlass();
		Car->SetActorLabel(Label);
	}

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

	bool BuildOne(const TCHAR* PackagePath, UDreamSkyStyle* Style, const FShowcaseKit& Kit, UDreamSkyModifier* Aurora, UDreamSkyModifier* Halo, UDreamSkyModifier* Shine, UDreamSkyModifier* Storm, ULevelSequence* Dusk)
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
		if (Halo)
		{
			// (The clear morning, 8 to 9: the sun low in the south-east.)
			FDreamSkyConditionalModifier& Sheet = Sky->ConditionalModifiers.AddDefaulted_GetRef();
			Sheet.Modifier = Halo;
			Sheet.Condition.bHours = true;
			Sheet.Condition.StartHour = 8.0f;
			Sheet.Condition.EndHour = 9.0f;
			Sheet.Condition.FadeHours = 0.25f;
			Sheet.BlendSeconds = 4.0f;
		}
		if (Shine)
		{
			// (The evening's deep twilight, 17:30 to 20.)
			FDreamSkyConditionalModifier& Evening = Sky->ConditionalModifiers.AddDefaulted_GetRef();
			Evening.Modifier = Shine;
			Evening.Condition.bHours = true;
			Evening.Condition.StartHour = 17.5f;
			Evening.Condition.EndHour = 20.0f;
			Evening.Condition.FadeHours = 0.25f;
			Evening.BlendSeconds = 4.0f;
		}
		if (Storm)
		{
			// (The clear night, 22 to 4.)
			FDreamSkyConditionalModifier& Night = Sky->ConditionalModifiers.AddDefaulted_GetRef();
			Night.Modifier = Storm;
			Night.Condition.bHours = true;
			Night.Condition.StartHour = 22.0f;
			Night.Condition.EndHour = 4.0f;
			Night.Condition.FadeHours = 0.25f;
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
		SurfaceSetup::AddMesh(*World, Kit.Pond, Kit.Water, FVector(Setup::ShowcasePondCenter, Setup::ShowcasePondLevel), FVector(1.0), FRotator::ZeroRotator, TEXT("Pond"));

		// The land out to the horizon, its ranges in the distance: no collision (nothing walks or lands there), left out
		// of the wind's level heights.
		if (AStaticMeshActor* Far = SurfaceSetup::AddMesh(*World, Kit.FarLand, Kit.Terrain, FVector(Setup::ShowcaseFarCenter, 0.0), FVector(1.0), FRotator::ZeroRotator, TEXT("FarLand")))
		{
			Far->GetStaticMeshComponent()->SetCollisionProfileName(UCollisionProfile::NoCollision_ProfileName);
			PassThrough(Far);
		}

		// The grove on the rolling ground to the left, the campfire and the crates to the right, the snow field far ahead.
		AddGrove(*World, Kit, FVector2D(500.0, 2100.0));
		AddCampfire(*World, Kit, FVector(2050.0, -450.0, -3.0));
		AddCrates(*World, Kit, FVector2D(1600.0, -800.0));
		// A bank of ground fog in the dip round the pond, as thick as the weather's fog; and wisps blown low over the set
		// in a foggy wind.
		if (ADreamFogBank* Bank = World->SpawnActor<ADreamFogBank>(FVector(Setup::ShowcasePondCenter, Setup::ShowcasePondLevel), FRotator::ZeroRotator))
		{
			Bank->Extent = FVector(1600.0, 1300.0, 260.0);
			Bank->SetActorLabel(TEXT("FogBank"));
		}
		if (ADreamWindFog* Wisps = World->SpawnActor<ADreamWindFog>(FVector(1300.0, 400.0, -20.0), FRotator::ZeroRotator))
		{
			Wisps->SetActorLabel(TEXT("WindFog"));
		}
		// Terrain fog that comes by itself after a clear, calm night: a morning mist over the set, pooling in the dip and
		// the low ground, and valley fog in the hills to the south-west (seen from the set, lit by the sunrise). Between
		// them they keep within the engine's 32 local fog volumes a view (the fog bank's draw only in a foggy weather).
		if (ADreamTerrainFog* Mist = World->SpawnActor<ADreamTerrainFog>(FVector(Setup::ShowcaseFarCenter, 0.0), FRotator::ZeroRotator))
		{
			Mist->Extent = FVector(3800.0, 3800.0, 600.0);
			Mist->Cells = 48;
			Mist->Humidity = 0.92f;
			Mist->PoolDepth = 400.0f;
			Mist->MistThickness = 200.0f;
			Mist->Density = 2.0f;
			Mist->MistDensity = 1.5f;
			Mist->Patches = 12;
			Mist->MistPatches = 2;
			Mist->PatchCells = 3.0f;
			Mist->SetActorLabel(TEXT("MorningMist"));
		}
		{
			const double Azimuth = FMath::DegreesToRadians(230.0), Out = 625000.0;
			const FVector2D Valley = Setup::ShowcaseFarCenter + FVector2D(FMath::Cos(Azimuth), FMath::Sin(Azimuth)) * Out;
			if (ADreamTerrainFog* Valleys = World->SpawnActor<ADreamTerrainFog>(FVector(Valley, 30000.0), FRotator::ZeroRotator))
			{
				Valleys->Extent = FVector(180000.0, 180000.0, 60000.0);
				Valleys->Cells = 40;
				Valleys->Humidity = 0.9f;
				Valleys->PoolDepth = 8000.0f;
				Valleys->MistThickness = 0.0f;
				Valleys->MistPatches = 0;
				Valleys->Patches = 16;
				Valleys->PatchCells = 3.0f;
				// The far land has no collision: its heights handed in, kept with the level.
				TArray<float> Land;
				for (int32 Y = 0; Y < Valleys->Cells; ++Y)
				{
					for (int32 X = 0; X < Valleys->Cells; ++X)
					{
						const double Step = 2.0 * Valleys->Extent.X / Valleys->Cells;
						Land.Add(float(FarLandHeight(Valley.X - Valleys->Extent.X + (X + 0.5) * Step, Valley.Y - Valleys->Extent.Y + (Y + 0.5) * Step)));
					}
				}
				Valleys->SetTerrainHeights(Land);
				Valleys->SetActorLabel(TEXT("ValleyFog"));
			}
		}
		// A walker going round the square and its puddles, its feet stepping in turn: prints in the snow and the dust,
		// the puddles pushed away and ringing, whatever the weather.
		if (ADreamSurfaceWalker* Walker = World->SpawnActor<ADreamSurfaceWalker>(FVector(1870.0, 430.0, 0.0), FRotator::ZeroRotator))
		{
			Walker->Body->SetStaticMesh(Kit.Figure);
			Walker->Body->SetMaterial(0, Kit.ToonRed);
			Walker->Center = FVector2D(1350.0, 430.0);
			Walker->Radii = FVector2D(520.0, 360.0);
			Walker->SetActorLabel(TEXT("Walker"));
		}
		// Two cars south of the set, side by side facing north: one parked (the snow lies on its windscreen, the drops sit), one shown as if it drove at
		// 60 km/h with its wipers running.
		AddCar(*World, Kit, FVector(200.0, -1000.0, SurfaceSetup::GroundHeight(200.0, -1000.0)), 90.0f, FVector::ZeroVector, false, TEXT("ParkedCar"));
		AddCar(*World, Kit, FVector(-200.0, -1000.0, SurfaceSetup::GroundHeight(-200.0, -1000.0)), 90.0f, FVector(1670.0, 0.0, 0.0), true, TEXT("DrivingCar"));
		AddSnowField(*World, FVector(4800.0, -2300.0, 0.0));
		AddStorm(*World);

		// The wind, with level heights baked over the set: roofs keep the rain off, puddles follow the dips. The bake
		// traces the set's collision, which a world built a moment ago may not have yet: build it now.
		SurfaceSetup::ReadyCollision(*World);
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
		// The weather radar on a screen ahead of the start, facing it: the plane's face toward -X, its texture's across to
		// the viewer's right (+Y), its down down.
		{
			const FVector2D At(1500.0, 120.0);
			const FRotator Facing = FRotationMatrix::MakeFromXZ(FVector(0.0, 1.0, 0.0), FVector(-1.0, 0.0, 0.0)).Rotator();
			ADreamWeatherRadarDisplay* Radar = World->SpawnActor<ADreamWeatherRadarDisplay>(FVector(At, SurfaceSetup::GroundHeight(At.X, At.Y) + 260.0),
				Facing);
			Radar->SetActorScale3D(FVector(2.4, 2.4, 1.0));
			Radar->SetActorLabel(TEXT("WeatherRadar"));
		}
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
	UDreamSkyModifier* Halo = ShowcaseSetup::EnsureHalo();
	UDreamSkyModifier* Shine = ShowcaseSetup::EnsureNoctilucent();
	UDreamSkyModifier* Storm = ShowcaseSetup::EnsureMeteors();
	ULevelSequence* Dusk = ShowcaseSetup::EnsureDuskSequence();
	bool bOk = Aurora && Halo && Shine && Storm && Dusk;
	bOk &= ShowcaseSetup::BuildOne(ShowcaseMapPackage, Style, Kit, Aurora, Halo, Shine, Storm, nullptr);
	bOk &= ShowcaseSetup::BuildOne(ShowcaseDuskMapPackage, Style, Kit, Aurora, Halo, Shine, Storm, Dusk);
	return bOk;
}
