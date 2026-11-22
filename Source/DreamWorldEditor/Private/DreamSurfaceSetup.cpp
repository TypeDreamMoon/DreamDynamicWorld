// Copyright DreamMoon. All Rights Reserved.

#include "DreamSurfaceSetup.h"
#include "DreamDynamicWorldModule.h"
#include "DreamSurfaceSettings.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Misc/PackageName.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"

namespace DreamWorld::SurfaceSetup
{
	bool SaveAsset(UObject* Asset)
	{
		UPackage* Package = Asset->GetOutermost();
		Package->MarkPackageDirty();
		const FString Filename = FPackageName::LongPackageNameToFilename(Package->GetName(), FPackageName::GetAssetPackageExtension());
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
	using namespace DreamWorld::SurfaceSetup;
	const UDreamSurfaceSettings* S = UDreamSurfaceSettings::Get();
	bool bOk = EnsureTarget(S->NearMapTexture, 256, PF_FloatRGBA, FLinearColor(0.0f, 0.0f, 0.0f, 0.0f));
	bOk &= EnsureTarget(S->FarMapTexture, 128, PF_FloatRGBA, FLinearColor(0.0f, 0.0f, 0.0f, 0.0f));
	// The cover map clears to "nothing overhead".
	bOk &= EnsureTarget(S->CoverMapTexture, 256, PF_G32R32F, FLinearColor(-1.0e9f, 0.0f, 0.0f, 0.0f));
	return bOk;
}
