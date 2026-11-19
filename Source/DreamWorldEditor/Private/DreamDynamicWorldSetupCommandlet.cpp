// Copyright DreamMoon. All Rights Reserved.

#include "DreamDynamicWorldSetupCommandlet.h"
#include "DreamCloudNoise.h"
#include "DreamDynamicWorldModule.h"
#include "DreamSky.h"
#include "DreamSkyStyle.h"
#include "DreamWeather.h"
#include "DreamWeatherModule.h"
#include "DreamWeatherPreset.h"
#include "DreamWeatherSelector.h"
#include "DreamWeatherSettings.h"
#include "DreamWeatherZone.h"
#include "DreamWindEditorLibrary.h"
#include "DreamWorldParameters.h"

#include "Engine/VolumeTexture.h"
#include "Engine/TextureRenderTarget2D.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/World.h"
#include "Materials/MaterialInterface.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "UObject/MetaData.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"
#include "UObject/UnrealType.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(DreamDynamicWorldSetupCommandlet)

namespace DreamWorld::Setup
{
	static const TCHAR* StylePackage = TEXT("/DreamDynamicWorld/Sky/DS_DreamSkyStylized");
	static const TCHAR* TestMapPackage = TEXT("/DreamDynamicWorld/Sky/Maps/Lvl_DreamSkyTest");
	static const TCHAR* CloudNoisePackage = TEXT("/DreamDynamicWorld/Sky/T_DreamCloudNoise");
	static const TCHAR* CloudNoiseVersionKey = TEXT("DreamCloudNoiseVersion");
	static constexpr int32 CloudNoiseSize = 128;

	static bool Save(UPackage* Package, UObject* Asset, const FString& Extension)
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

	/**
	 * The default style as an asset projects can duplicate (its values are the class defaults). bReset writes the
	 * class defaults over an existing asset (after the defaults were retuned).
	 */
	static UDreamSkyStyle* EnsureDefaultStyle(bool bReset)
	{
		const FString ObjectPath = FString(StylePackage) + TEXT(".") + FPackageName::GetShortName(StylePackage);
		if (FPackageName::DoesPackageExist(StylePackage))
		{
			if (UDreamSkyStyle* Existing = LoadObject<UDreamSkyStyle>(nullptr, *ObjectPath))
			{
				if (!bReset)
				{
					UE_LOG(LogDreamWorld, Display, TEXT("Sky style %s: up to date"), *ObjectPath);
					return Existing;
				}
				const UDreamSkyStyle* Defaults = GetDefault<UDreamSkyStyle>();
				Existing->PreEditChange(nullptr);
				for (TFieldIterator<FProperty> It(UDreamSkyStyle::StaticClass()); It; ++It)
				{
					if (It->HasAnyPropertyFlags(CPF_Edit))
					{
						It->CopyCompleteValue_InContainer(Existing, Defaults);
					}
				}
				Existing->PostEditChange();
				UE_LOG(LogDreamWorld, Display, TEXT("Sky style %s: reset to the class defaults"), *ObjectPath);
				return Save(Existing->GetOutermost(), Existing, FPackageName::GetAssetPackageExtension()) ? Existing : nullptr;
			}
		}
		UPackage* Package = CreatePackage(StylePackage);
		UDreamSkyStyle* Style = NewObject<UDreamSkyStyle>(Package, *FPackageName::GetShortName(StylePackage), RF_Public | RF_Standalone);
		FAssetRegistryModule::AssetCreated(Style);
		return Save(Package, Style, FPackageName::GetAssetPackageExtension()) ? Style : nullptr;
	}

	/** The weather map render target the weather draws and the sky reads (RGBA16F, UAV). */
	static bool EnsureWeatherMap()
	{
		const UDreamWeatherSettings* Settings = UDreamWeatherSettings::Get();
		const FString PackageName = Settings->WeatherMapTexture.GetLongPackageName();
		const FString AssetName = Settings->WeatherMapTexture.GetAssetName();
		UTextureRenderTarget2D* RT = Cast<UTextureRenderTarget2D>(Settings->WeatherMapTexture.TryLoad());
		const bool bCreated = RT == nullptr;
		if (!RT)
		{
			UPackage* Package = CreatePackage(*PackageName);
			Package->FullyLoad();
			RT = NewObject<UTextureRenderTarget2D>(Package, *AssetName, RF_Public | RF_Standalone);
		}
		const int32 Resolution = FMath::Clamp(Settings->WeatherMapResolution, 16, 1024);
		if (!bCreated)
		{
			UE_LOG(LogDreamWorld, Display, TEXT("Weather map %s: %dx%d, UAV %d, format %s"), *RT->GetPathName(), RT->SizeX, RT->SizeY, RT->bSupportsUAV ? 1 : 0, GetPixelFormatString(RT->GetFormat()));
		}
		if (bCreated || RT->SizeX != Resolution || !RT->bSupportsUAV || RT->GetFormat() != PF_FloatRGBA)
		{
			RT->PreEditChange(nullptr);
			RT->ClearColor = FLinearColor(0.3f, 0.0f, 0.0f, 0.0f);
			RT->AddressX = TA_Clamp;
			RT->AddressY = TA_Clamp;
			RT->bSupportsUAV = true;
			RT->InitCustomFormat(Resolution, Resolution, PF_FloatRGBA, true);
			RT->PostEditChange();
			if (bCreated)
			{
				FAssetRegistryModule::AssetCreated(RT);
			}
			return Save(RT->GetOutermost(), RT, FPackageName::GetAssetPackageExtension());
		}
		UE_LOG(LogDreamWorld, Display, TEXT("Weather map %s: up to date"), *RT->GetPathName());
		return true;
	}

	/**
	 * The volumetric clouds' 3D noise (see DreamCloudNoise.h), generated here: 128^3 voxels, uncompressed (the
	 * billows' fine detail survives), tiling. Built again when the generator's version changed, or with bForce.
	 */
	static bool EnsureCloudNoise(bool bForce)
	{
		const FString ObjectPath = FString(CloudNoisePackage) + TEXT(".") + FPackageName::GetShortName(CloudNoisePackage);
		UVolumeTexture* Texture = FPackageName::DoesPackageExist(CloudNoisePackage) ? LoadObject<UVolumeTexture>(nullptr, *ObjectPath) : nullptr;
		const FString Version = FString::FromInt(DreamCloudNoise::Version);
		if (Texture && !bForce && Texture->GetOutermost()->GetMetaData().GetValue(Texture, CloudNoiseVersionKey) == Version)
		{
			UE_LOG(LogDreamWorld, Display, TEXT("Cloud noise %s: up to date"), *ObjectPath);
			return true;
		}
		const bool bCreated = Texture == nullptr;
		if (!Texture)
		{
			UPackage* Package = CreatePackage(CloudNoisePackage);
			Package->FullyLoad();
			Texture = NewObject<UVolumeTexture>(Package, *FPackageName::GetShortName(CloudNoisePackage), RF_Public | RF_Standalone);
		}

		const double Start = FPlatformTime::Seconds();
		TArray<FColor> Voxels;
		DreamCloudNoise::Build(CloudNoiseSize, Voxels);
		const int32 Size = CloudNoiseSize;
		Texture->UpdateSourceFromFunction([&Voxels, Size](int32 X, int32 Y, int32 Z, void* Out)
		{
			*static_cast<FColor*>(Out) = Voxels[(Z * Size + Y) * Size + X];
		}, Size, Size, Size, TSF_BGRA8);
		Texture->PreEditChange(nullptr);
		Texture->SRGB = false;
		Texture->CompressionSettings = TC_VectorDisplacementmap;
		Texture->LODGroup = TEXTUREGROUP_World;
		Texture->MipGenSettings = TMGS_FromTextureGroup;
		Texture->NeverStream = true;
		Texture->AddressMode = TA_Wrap;
		Texture->PostEditChange();
		Texture->GetOutermost()->GetMetaData().SetValue(Texture, CloudNoiseVersionKey, *Version);
		if (bCreated)
		{
			FAssetRegistryModule::AssetCreated(Texture);
		}
		UE_LOG(LogDreamWorld, Display, TEXT("Cloud noise %s: built %d^3 in %.1f s"), *ObjectPath, Size, FPlatformTime::Seconds() - Start);
		return Save(Texture->GetOutermost(), Texture, FPackageName::GetAssetPackageExtension());
	}

	struct FPresetSpec
	{
		const TCHAR* Name;
		const TCHAR* Display;
		TArray<TPair<FGameplayTag, float>> Channels;
	};

	/** The plugin's weather presets; existing ones are left alone unless bForce. */
	static TMap<FString, UDreamWeatherPreset*> EnsureWeatherPresets(bool bForce)
	{
		using namespace DreamWeather::Tags;
		const FGameplayTag Coverage = Channel_CloudCoverage, Dark = Channel_CloudDarkness, Precip = Channel_Precipitation, Snow = Channel_SnowRatio,
			Fog = Channel_Fog, Dust = Channel_Dust, Wind = Channel_Wind, Lightning = Channel_Lightning, Temperature = Channel_Temperature, Wet = Channel_Wetness,
			SnowCover = Channel_SnowCover;
		const TArray<FPresetSpec> Specs = {
			{ TEXT("WP_Clear"), TEXT("Clear"), { { Coverage, 0.15f }, { Wind, 0.2f }, { Temperature, 1.0f } } },
			{ TEXT("WP_Cloudy"), TEXT("Cloudy"), { { Coverage, 0.55f }, { Dark, 0.05f }, { Wind, 0.35f } } },
			{ TEXT("WP_Overcast"), TEXT("Overcast"), { { Coverage, 0.9f }, { Dark, 0.25f }, { Wind, 0.4f }, { Fog, 0.1f }, { Temperature, -1.0f } } },
			{ TEXT("WP_Rain"), TEXT("Rain"), { { Coverage, 0.95f }, { Dark, 0.45f }, { Precip, 0.6f }, { Wind, 0.5f }, { Fog, 0.25f }, { Temperature, -3.0f }, { Wet, 1.0f } } },
			{ TEXT("WP_Storm"), TEXT("Thunderstorm"), { { Coverage, 1.0f }, { Dark, 0.85f }, { Precip, 0.95f }, { Wind, 0.9f }, { Lightning, 0.8f }, { Fog, 0.2f }, { Temperature, -5.0f }, { Wet, 1.0f } } },
			{ TEXT("WP_Snow"), TEXT("Snow"), { { Coverage, 0.9f }, { Dark, 0.2f }, { Precip, 0.5f }, { Snow, 1.0f }, { Wind, 0.3f }, { Fog, 0.3f }, { Temperature, -12.0f }, { SnowCover, 1.0f } } },
			{ TEXT("WP_Fog"), TEXT("Fog"), { { Coverage, 0.6f }, { Dark, 0.1f }, { Fog, 0.9f }, { Wind, 0.05f }, { Temperature, -2.0f }, { Wet, 0.3f } } },
			{ TEXT("WP_Dust"), TEXT("Dust Storm"), { { Coverage, 0.35f }, { Dust, 0.9f }, { Wind, 0.85f }, { Temperature, 4.0f } } },
		};
		TMap<FString, UDreamWeatherPreset*> Out;
		for (const FPresetSpec& Spec : Specs)
		{
			const FString PackageName = FString(TEXT("/DreamDynamicWorld/Weather/Presets/")) + Spec.Name;
			UDreamWeatherPreset* Preset = FPackageName::DoesPackageExist(PackageName) ? LoadObject<UDreamWeatherPreset>(nullptr, *(PackageName + TEXT(".") + Spec.Name)) : nullptr;
			if (Preset && !bForce)
			{
				Out.Add(Spec.Name, Preset);
				continue;
			}
			const bool bCreated = Preset == nullptr;
			if (!Preset)
			{
				Preset = NewObject<UDreamWeatherPreset>(CreatePackage(*PackageName), Spec.Name, RF_Public | RF_Standalone);
			}
			Preset->DisplayName = FText::FromString(Spec.Display);
			Preset->Channels.Reset();
			for (const TPair<FGameplayTag, float>& Channel : Spec.Channels)
			{
				Preset->Channels.Add(Channel.Key, Channel.Value);
			}
			if (bCreated)
			{
				FAssetRegistryModule::AssetCreated(Preset);
			}
			Save(Preset->GetOutermost(), Preset, FPackageName::GetAssetPackageExtension());
			Out.Add(Spec.Name, Preset);
		}
		return Out;
	}

	static AStaticMeshActor* AddMesh(UWorld& World, const TCHAR* MeshPath, const FVector& Location, const FVector& Scale, const FRotator& Rotation = FRotator::ZeroRotator)
	{
		UStaticMesh* Mesh = LoadObject<UStaticMesh>(nullptr, MeshPath);
		if (!Mesh)
		{
			UE_LOG(LogDreamWorld, Error, TEXT("Mesh %s not found."), MeshPath);
			return nullptr;
		}
		AStaticMeshActor* Actor = World.SpawnActor<AStaticMeshActor>(Location, Rotation);
		Actor->GetStaticMeshComponent()->SetStaticMesh(Mesh);
		Actor->GetStaticMeshComponent()->SetMobility(EComponentMobility::Static);
		Actor->SetActorScale3D(Scale);
		return Actor;
	}

	/**
	 * A plain level to judge a sky in: a large grey floor, a few primitives of different heights for shadows and
	 * shading, the sky, and a fixed camera looking south-west so the setting sun and the evening glow are in frame.
	 */
	static bool BuildTestMap(UDreamSkyStyle* Style, const TMap<FString, UDreamWeatherPreset*>& Presets)
	{
		// A copy already in memory (loaded by something earlier in the run) holds the file open and owns the
		// name: let go of the file and move the old package aside before building the new one.
		if (UPackage* Existing = FindPackage(nullptr, TestMapPackage))
		{
			ResetLoaders(Existing);
			Existing->Rename(*MakeUniqueObjectName(nullptr, UPackage::StaticClass(), TEXT("/Temp/DreamSkyTestOld")).ToString(), nullptr, REN_DontCreateRedirectors | REN_NonTransactional);
		}
		UPackage* Package = CreatePackage(TestMapPackage);
		// A new package whose file already exists counts as partly loaded, which saving refuses.
		Package->MarkAsFullyLoaded();
		UWorld::InitializationValues Values;
		Values.RequiresHitProxies(false);
		Values.ShouldSimulatePhysics(false);
		Values.EnableTraceCollision(false);
		Values.CreateNavigation(false);
		Values.CreateAISystem(false);
		Values.AllowAudioPlayback(false);
		Values.CreatePhysicsScene(true);
		UWorld* World = UWorld::CreateWorld(EWorldType::Editor, false, FName(FPackageName::GetShortName(TestMapPackage)), Package, true, ERHIFeatureLevel::Num, &Values);
		if (!World)
		{
			return false;
		}
		World->SetFlags(RF_Public | RF_Standalone);

		ADreamSky* Sky = World->SpawnActor<ADreamSky>(FVector::ZeroVector, FRotator::ZeroRotator);
		Sky->Style = Style;
		Sky->TimeOfDay = 9.0f;
		Sky->SetActorLabel(TEXT("DreamSky"));

		const TCHAR* Plane = TEXT("/Engine/BasicShapes/Plane.Plane");
		const TCHAR* Cube = TEXT("/Engine/BasicShapes/Cube.Cube");
		const TCHAR* Sphere = TEXT("/Engine/BasicShapes/Sphere.Sphere");
		const TCHAR* Cylinder = TEXT("/Engine/BasicShapes/Cylinder.Cylinder");
		const TCHAR* Cone = TEXT("/Engine/BasicShapes/Cone.Cone");

		AddMesh(*World, Plane, FVector::ZeroVector, FVector(600.0f, 600.0f, 1.0f))->SetActorLabel(TEXT("Floor"));
		// A ring of shapes around the camera's view, at several heights.
		AddMesh(*World, Cube, FVector(-1200.0f, -300.0f, 150.0f), FVector(3.0f, 3.0f, 3.0f))->SetActorLabel(TEXT("Cube"));
		AddMesh(*World, Cylinder, FVector(-1600.0f, -1100.0f, 500.0f), FVector(1.5f, 1.5f, 10.0f))->SetActorLabel(TEXT("Pillar"));
		AddMesh(*World, Sphere, FVector(-900.0f, -900.0f, 100.0f), FVector(2.0f, 2.0f, 2.0f))->SetActorLabel(TEXT("Sphere"));
		AddMesh(*World, Cone, FVector(-2000.0f, 200.0f, 200.0f), FVector(4.0f, 4.0f, 4.0f))->SetActorLabel(TEXT("Cone"));
		AddMesh(*World, Cube, FVector(-2600.0f, -1800.0f, 400.0f), FVector(8.0f, 4.0f, 8.0f), FRotator(0.0f, 25.0f, 0.0f))->SetActorLabel(TEXT("Block"));
		for (int32 i = 0; i < 5; ++i)
		{
			AddMesh(*World, Sphere, FVector(-700.0f - i * 250.0f, 400.0f + i * 120.0f, 50.0f), FVector(1.0f))->SetActorLabel(*FString::Printf(TEXT("Ball%d"), i));
		}

		// Weather that changes every few hours, and a thunderstorm parked 35 kilometres south-west, in front of the
		// camera: its towers stand on the horizon of the clear test area, drawn through the weather map.
		ADreamWeather* Weather = World->SpawnActor<ADreamWeather>(FVector(0.0f, 0.0f, 200.0f), FRotator::ZeroRotator);
		Weather->SetActorLabel(TEXT("DreamWeather"));
		if (UDreamWeatherSelector_Random* Random = Cast<UDreamWeatherSelector_Random>(Weather->Selector))
		{
			const TPair<const TCHAR*, float> Odds[] = { { TEXT("WP_Clear"), 3.0f }, { TEXT("WP_Cloudy"), 3.0f }, { TEXT("WP_Overcast"), 1.5f },
				{ TEXT("WP_Rain"), 1.0f }, { TEXT("WP_Storm"), 0.5f }, { TEXT("WP_Fog"), 0.5f } };
			for (const TPair<const TCHAR*, float>& Entry : Odds)
			{
				if (UDreamWeatherPreset* const* Preset = Presets.Find(Entry.Key))
				{
					FDreamWeatherWeightedEntry& E = Random->Entries.AddDefaulted_GetRef();
					E.Preset = *Preset;
					E.Weight = Entry.Value;
				}
			}
		}
		ADreamWeatherZone* Storm = World->SpawnActor<ADreamWeatherZone>(FVector(-2680000.0f, -2250000.0f, 0.0f), FRotator::ZeroRotator);
		Storm->SetActorLabel(TEXT("StormZone"));
		Storm->Zone->Shape = EDreamWeatherZoneShape::Circle;
		Storm->Zone->Extent = FVector2D(400000.0, 400000.0);
		Storm->Zone->BlendDistance = 150000.0f;
		Storm->Zone->Priority = 10;
		if (UDreamWeatherPreset* const* StormPreset = Presets.Find(TEXT("WP_Storm")))
		{
			Storm->Zone->Preset = *StormPreset;
		}

		// Looking south-west (sunset on the right of frame), slightly up so the sky fills the upper half.
		ACameraActor* Camera = World->SpawnActor<ACameraActor>(FVector(600.0f, 600.0f, 260.0f), FRotator(8.0f, 220.0f, 0.0f));
		// The level view of player 0 (the property has no setter; it is what the details panel edits).
		if (FProperty* AutoActivate = ACameraActor::StaticClass()->FindPropertyByName(TEXT("AutoActivateForPlayer")))
		{
			*AutoActivate->ContainerPtrToValuePtr<uint8>(Camera) = uint8(EAutoReceiveInput::Player0);
		}
		Camera->GetCameraComponent()->SetFieldOfView(90.0f);
		Camera->GetCameraComponent()->bConstrainAspectRatio = false;
		Camera->SetActorLabel(TEXT("ShotCamera"));

		const bool bSaved = Save(Package, World, FPackageName::GetMapPackageExtension());
		World->DestroyWorld(false);
		return bSaved;
	}
}

UDreamDynamicWorldSetupCommandlet::UDreamDynamicWorldSetupCommandlet()
{
	IsClient = false;
	IsEditor = true;
	IsServer = false;
	LogToConsole = true;
}

int32 UDreamDynamicWorldSetupCommandlet::Main(const FString& Params)
{
	using namespace DreamWorld::Setup;
	const bool bTestMap = FParse::Param(*Params, TEXT("TestMap"));
	const bool bForce = FParse::Param(*Params, TEXT("Force"));

	bool bOk = FDreamWorldParameters::EnsureParameterCollection(true) != nullptr;
	bOk &= UDreamWindEditorLibrary::EnsureAssets(true);
	UDreamSkyStyle* Style = EnsureDefaultStyle(FParse::Param(*Params, TEXT("ForceStyle")));
	bOk &= Style != nullptr;
	bOk &= EnsureWeatherMap();
	bOk &= EnsureCloudNoise(FParse::Param(*Params, TEXT("ForceCloudNoise")));
	const TMap<FString, UDreamWeatherPreset*> Presets = EnsureWeatherPresets(FParse::Param(*Params, TEXT("ForcePresets")));
	bOk &= Presets.Num() > 0;

	if (bTestMap)
	{
		if (FPackageName::DoesPackageExist(TestMapPackage) && !bForce)
		{
			UE_LOG(LogDreamWorld, Display, TEXT("%s exists; pass -Force to write it again."), TestMapPackage);
		}
		else
		{
			bOk &= BuildTestMap(Style, Presets);
		}
	}
	UE_LOG(LogDreamWorld, Display, TEXT("DreamDynamicWorldSetup: %s"), bOk ? TEXT("done") : TEXT("FAILED"));
	return bOk ? 0 : 1;
}
