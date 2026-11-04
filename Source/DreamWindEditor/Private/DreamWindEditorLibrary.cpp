// Copyright DreamMoon. All Rights Reserved.

#include "DreamWindEditorLibrary.h"
#include "DreamWindModule.h"
#include "DreamWindSettings.h"
#include "DreamWindPreset.h"
#include "DreamWorldParameters.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Engine/TextureRenderTargetVolume.h"
#include "Materials/MaterialParameterCollection.h"
#include "Misc/PackageName.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(DreamWindEditorLibrary)

namespace DreamWind::EditorSetup
{
	static bool SplitAssetPath(const FSoftObjectPath& Path, FString& OutPackageName, FString& OutAssetName)
	{
		const FString Long = Path.GetLongPackageName();
		if (Long.IsEmpty() || !FPackageName::IsValidLongPackageName(Long))
		{
			UE_LOG(LogDreamWind, Error, TEXT("'%s' is not a valid asset path."), *Path.ToString());
			return false;
		}
		OutPackageName = Long;
		OutAssetName = Path.GetAssetName();
		if (OutAssetName.IsEmpty())
		{
			OutAssetName = FPackageName::GetLongPackageAssetName(Long);
		}
		return true;
	}

	static bool SavePackageFor(UObject* Asset)
	{
		UPackage* Package = Asset->GetOutermost();
		Package->MarkPackageDirty();
		const FString Filename = FPackageName::LongPackageNameToFilename(Package->GetName(), FPackageName::GetAssetPackageExtension());
		FSavePackageArgs Args;
		Args.TopLevelFlags = RF_Public | RF_Standalone;
		Args.SaveFlags = SAVE_NoError;
		const bool bOk = UPackage::SavePackage(Package, Asset, *Filename, Args);
		if (!bOk)
		{
			UE_LOG(LogDreamWind, Error, TEXT("Failed to save '%s'."), *Filename);
		}
		else
		{
			UE_LOG(LogDreamWind, Display, TEXT("Saved %s"), *Filename);
		}
		return bOk;
	}
}

UMaterialParameterCollection* UDreamWindEditorLibrary::EnsureParameterCollection(bool bSaveAsset)
{
	// The wind writes into the plugin's shared collection; the core module creates it from every
	// registered parameter (the wind's included).
	return FDreamWorldParameters::EnsureParameterCollection(bSaveAsset);
}

UTextureRenderTargetVolume* UDreamWindEditorLibrary::EnsureVolumeTexture(bool bSaveAsset)
{
	const UDreamWindSettings* Settings = UDreamWindSettings::Get();
	UTextureRenderTargetVolume* Near = EnsureVolumeTextureAt(Settings->VolumeTexture, bSaveAsset);
	if (Settings->bVolumeFarLevel)
	{
		EnsureVolumeTextureAt(Settings->VolumeFarTexture, bSaveAsset);
	}
	return Near;
}

UTextureRenderTargetVolume* UDreamWindEditorLibrary::EnsureVolumeTextureAt(const FSoftObjectPath& Path, bool bSaveAsset)
{
	using namespace DreamWind::EditorSetup;
	const UDreamWindSettings* Settings = UDreamWindSettings::Get();

	FString PackageName, AssetName;
	if (!SplitAssetPath(Path, PackageName, AssetName))
	{
		return nullptr;
	}

	UTextureRenderTargetVolume* RT = Cast<UTextureRenderTargetVolume>(Path.TryLoad());
	bool bCreated = false;
	if (!RT)
	{
		UPackage* Package = CreatePackage(*PackageName);
		Package->FullyLoad();
		RT = NewObject<UTextureRenderTargetVolume>(Package, *AssetName, RF_Public | RF_Standalone);
		bCreated = true;
	}

	const FIntVector Res = Settings->VolumeResolution;
	const bool bMismatch = RT->SizeX != Res.X || RT->SizeY != Res.Y || RT->SizeZ != Res.Z || RT->GetFormat() != PF_FloatRGBA || !RT->bSupportsUAV;
	if (bCreated || bMismatch)
	{
		RT->PreEditChange(nullptr);
		RT->bSupportsUAV = true;
		RT->bHDR = true;
		RT->OverrideFormat = PF_FloatRGBA;
		RT->ClearColor = FLinearColor::Transparent;
		RT->Init(Res.X, Res.Y, Res.Z, PF_FloatRGBA);
		RT->PostEditChange();
		RT->UpdateResourceImmediate(true);
	}

	if (bCreated)
	{
		FAssetRegistryModule::AssetCreated(RT);
	}
	if ((bCreated || bMismatch) && bSaveAsset)
	{
		SavePackageFor(RT);
	}
	UE_LOG(LogDreamWind, Display, TEXT("Volume render target %s: %s (%dx%dx%d RGBA16F, UAV)"), *RT->GetPathName(), bCreated ? TEXT("created") : (bMismatch ? TEXT("re-initialised") : TEXT("up to date")), Res.X, Res.Y, Res.Z);
	return RT;
}

UTextureRenderTarget2D* UDreamWindEditorLibrary::EnsureTerrainTexture(bool bSaveAsset)
{
	using namespace DreamWind::EditorSetup;
	const UDreamWindSettings* Settings = UDreamWindSettings::Get();

	FString PackageName, AssetName;
	if (!SplitAssetPath(Settings->TerrainTexture, PackageName, AssetName))
	{
		return nullptr;
	}

	UTextureRenderTarget2D* RT = Cast<UTextureRenderTarget2D>(Settings->TerrainTexture.TryLoad());
	bool bCreated = false;
	if (!RT)
	{
		UPackage* Package = CreatePackage(*PackageName);
		Package->FullyLoad();
		RT = NewObject<UTextureRenderTarget2D>(Package, *AssetName, RF_Public | RF_Standalone);
		bCreated = true;
	}
	if (bCreated || RT->GetFormat() != PF_FloatRGBA)
	{
		RT->PreEditChange(nullptr);
		RT->ClearColor = FLinearColor(1, 0, 0, 0);   // multiplier 1, no steering
		RT->AddressX = TA_Clamp;
		RT->AddressY = TA_Clamp;
		RT->InitCustomFormat(4, 4, PF_FloatRGBA, true);
		RT->PostEditChange();
		RT->UpdateResourceImmediate(true);
	}
	if (bCreated)
	{
		FAssetRegistryModule::AssetCreated(RT);
		if (bSaveAsset)
		{
			SavePackageFor(RT);
		}
	}
	UE_LOG(LogDreamWind, Display, TEXT("Terrain render target %s: %s"), *RT->GetPathName(), bCreated ? TEXT("created") : TEXT("up to date"));
	return RT;
}

bool UDreamWindEditorLibrary::EnsureAssets(bool bSaveAssets)
{
	const bool bMPC = EnsureParameterCollection(bSaveAssets) != nullptr;
	const bool bRT = EnsureVolumeTexture(bSaveAssets) != nullptr;
	const bool bTerrain = EnsureTerrainTexture(bSaveAssets) != nullptr;
	const bool bPresets = EnsurePresets(bSaveAssets);
	return bMPC && bRT && bTerrain && bPresets;
}

bool UDreamWindEditorLibrary::EnsurePresets(bool bSaveAssets)
{
	using namespace DreamWind::EditorSetup;

	struct FPresetSpec
	{
		const TCHAR* Name;
		const TCHAR* Description;
		float Speed, GustStrength, GustWavelength, GustSpeedScale, GustFrequency, TurbulenceStrength, TurbulenceWavelength;
	};
	// Speeds in cm/s: 150 = leaves rustle, 600 = a fresh breeze, 2200 = a gale (Beaufort 8-9)
	static const FPresetSpec Specs[] = {
		{ TEXT("DWP_Calm"),   TEXT("Barely there: leaves rustle, grass tips tremble. Interiors, dawn, cutscenes."),        150.0f, 0.25f, 3000.0f, 0.8f, 0.10f, 0.20f,  700.0f },
		{ TEXT("DWP_Breeze"), TEXT("A fresh breeze with visible gust fronts - the everyday outdoor wind."),                600.0f, 0.50f, 4000.0f, 0.8f, 0.15f, 0.35f,  900.0f },
		{ TEXT("DWP_Storm"),  TEXT("Gale: everything leans, long violent gust fronts, strong lateral turbulence."),      2200.0f, 0.85f, 7000.0f, 0.9f, 0.25f, 0.60f, 1400.0f },
	};

	bool bAllOk = true;
	for (const FPresetSpec& Spec : Specs)
	{
		const FString PackageName = FString::Printf(TEXT("/DreamDynamicWorld/Wind/Presets/%s"), Spec.Name);
		if (UDreamWindPreset* Existing = LoadObject<UDreamWindPreset>(nullptr, *(PackageName + TEXT(".") + Spec.Name)))
		{
			continue; // designers may have tuned it
		}
		UPackage* Package = CreatePackage(*PackageName);
		if (!Package)
		{
			UE_LOG(LogDreamWind, Error, TEXT("Cannot create package '%s'."), *PackageName);
			bAllOk = false;
			continue;
		}
		Package->FullyLoad();
		UDreamWindPreset* Preset = NewObject<UDreamWindPreset>(Package, Spec.Name, RF_Public | RF_Standalone);
		Preset->Description = FText::FromString(Spec.Description);
		Preset->Wind.Direction = FVector(1.0, 0.0, 0.0);
		Preset->Wind.Speed = Spec.Speed;
		Preset->Wind.GustStrength = Spec.GustStrength;
		Preset->Wind.GustWavelength = Spec.GustWavelength;
		Preset->Wind.GustSpeedScale = Spec.GustSpeedScale;
		Preset->Wind.GustFrequency = Spec.GustFrequency;
		Preset->Wind.TurbulenceStrength = Spec.TurbulenceStrength;
		Preset->Wind.TurbulenceWavelength = Spec.TurbulenceWavelength;
		FAssetRegistryModule::AssetCreated(Preset);
		if (bSaveAssets && !SavePackageFor(Preset))
		{
			bAllOk = false;
		}
	}
	return bAllOk;
}
