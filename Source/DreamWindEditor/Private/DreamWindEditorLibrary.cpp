// Copyright DreamMoon. All Rights Reserved.

#include "DreamWindEditorLibrary.h"
#include "DreamWindModule.h"
#include "DreamWindSettings.h"

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
	using namespace DreamWind::EditorSetup;
	const UDreamWindSettings* Settings = UDreamWindSettings::Get();

	FString PackageName, AssetName;
	if (!SplitAssetPath(Settings->ParameterCollection, PackageName, AssetName))
	{
		return nullptr;
	}

	UMaterialParameterCollection* MPC = Cast<UMaterialParameterCollection>(Settings->ParameterCollection.TryLoad());
	bool bCreated = false;
	if (!MPC)
	{
		UPackage* Package = CreatePackage(*PackageName);
		Package->FullyLoad();
		MPC = NewObject<UMaterialParameterCollection>(Package, *AssetName, RF_Public | RF_Standalone);
		bCreated = true;
	}

	static const FName Names[] = { DreamWind::Names::MPC_P0, DreamWind::Names::MPC_P1, DreamWind::Names::MPC_P2, DreamWind::Names::MPC_V0, DreamWind::Names::MPC_V1, DreamWind::Names::MPC_V2, DreamWind::Names::MPC_V3, DreamWind::Names::MPC_T0, DreamWind::Names::MPC_T1 };
	bool bChanged = false;
	MPC->PreEditChange(nullptr);
	for (const FName& Name : Names)
	{
		const bool bExists = MPC->VectorParameters.ContainsByPredicate([&Name](const FCollectionVectorParameter& P) { return P.ParameterName == Name; });
		if (!bExists)
		{
			FCollectionVectorParameter& P = MPC->VectorParameters.AddDefaulted_GetRef();
			P.ParameterName = Name;
			P.Id = FGuid::NewGuid();
			P.DefaultValue = (Name == DreamWind::Names::MPC_P0) ? FLinearColor(1, 0, 0, 0) : FLinearColor(0, 0, 0, 0);
			bChanged = true;
		}
	}
	MPC->PostEditChange();

	if (bCreated)
	{
		FAssetRegistryModule::AssetCreated(MPC);
	}
	if ((bCreated || bChanged) && bSaveAsset)
	{
		SavePackageFor(MPC);
	}
	UE_LOG(LogDreamWind, Display, TEXT("Parameter collection %s: %s"), *MPC->GetPathName(), bCreated ? TEXT("created") : (bChanged ? TEXT("parameters added") : TEXT("up to date")));
	return MPC;
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
	return bMPC && bRT && bTerrain;
}
