// Copyright DreamMoon. All Rights Reserved.

#include "DreamAtmosphereBake.h"
#include "DreamAtmosphereTables.h"
#include "DreamDynamicWorldModule.h"
#include "DreamSkyStyle.h"
#include "DreamSurfaceSetup.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "Engine/VolumeTexture.h"
#include "HAL/IConsoleManager.h"
#include "Misc/App.h"
#include "Misc/PackageName.h"
#include "RHIGlobals.h"
#include "UObject/Package.h"

namespace DreamAtmosphereBakeImpl
{
	constexpr float Scale = 1.0e4f;
	const TCHAR* DefaultStylePath = TEXT("/DreamDynamicWorld/Sky/DS_DreamSkyStylized.DS_DreamSkyStylized");
}

bool DreamWorld::Setup::BakeStyleSky(UDreamSkyStyle* Style, bool bSave)
{
	using namespace DreamAtmosphereBakeImpl;
	if (!Style)
	{
		return false;
	}
	if (!FApp::CanEverRender() || GUsingNullRHI || GMaxRHIFeatureLevel < ERHIFeatureLevel::SM5)
	{
		UE_LOG(LogDreamWorld, Display, TEXT("Sky table for %s: not baked (no renderer: run Setup with -AllowCommandletRendering and without -nullrhi, or DreamSky.BakeSky in the editor)"), *Style->GetName());
		return true;
	}
	UDreamAtmosphereTables* Tables = NewObject<UDreamAtmosphereTables>(GetTransientPackage());
	Tables->Build(Style->Atmosphere);
	if (!Tables->HasCpuSky())
	{
		UE_LOG(LogDreamWorld, Warning, TEXT("Sky table for %s: the tables could not be read back"), *Style->GetName());
		return false;
	}
	// Beside the style: <its package>_SkyTable.
	const FString PackagePath = Style->GetOutermost()->GetName() + TEXT("_SkyTable");
	UPackage* Package = CreatePackage(*PackagePath);
	Package->FullyLoad();
	const bool bExisted = FindObject<UVolumeTexture>(Package, *FPackageName::GetShortName(PackagePath)) != nullptr;
	FDreamAtmosphereBakedSky Baked;
	UVolumeTexture* Texture = Tables->BakeSky(Package, FName(*FPackageName::GetShortName(PackagePath)), Scale, Baked);
	if (!Texture)
	{
		UE_LOG(LogDreamWorld, Warning, TEXT("Sky table for %s: the bake failed"), *Style->GetName());
		return false;
	}
	if (!bExisted)
	{
		FAssetRegistryModule::AssetCreated(Texture);
	}
	Style->Modify();
	Style->BakedSky = Baked;
	Style->MarkPackageDirty();
	UE_LOG(LogDreamWorld, Display, TEXT("Sky table for %s: baked %s (%d light elevations)"), *Style->GetName(), *Texture->GetPathName(), Baked.Zenith.Num());
	return !bSave || (DreamWorld::SurfaceSetup::SaveAsset(Texture) && DreamWorld::SurfaceSetup::SaveAsset(Style));
}

static FAutoConsoleCommand GDreamSkyBakeSkyCommand(
	TEXT("DreamSky.BakeSky"),
	TEXT("DreamSky.BakeSky [style object path]: bake a sky style's sky table for mobile (no Shader Model 5) beside it, and save both. Default: the plugin's default style."),
	FConsoleCommandWithArgsDelegate::CreateLambda([](const TArray<FString>& Args)
	{
		const FString Path = Args.Num() > 0 ? Args[0] : FString(DreamAtmosphereBakeImpl::DefaultStylePath);
		UDreamSkyStyle* Style = LoadObject<UDreamSkyStyle>(nullptr, *Path);
		if (!Style)
		{
			UE_LOG(LogDreamWorld, Warning, TEXT("DreamSky.BakeSky: no sky style at %s"), *Path);
			return;
		}
		DreamWorld::Setup::BakeStyleSky(Style, true);
	}));
