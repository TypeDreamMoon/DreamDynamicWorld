// Copyright DreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "DreamWindEditorLibrary.generated.h"

class UMaterialParameterCollection;
class UTextureRenderTargetVolume;
class UTextureRenderTarget2D;
class UDreamWindTerrainMap;
class ADreamWindGlobal;
struct FDreamWindTerrainBakeSettings;

/**
 * Editor-side helpers: creating the two assets the runtime needs (the material parameter collection
 * and the volume render target) at the paths configured in Project Settings > Dream Wind.
 * Reachable from Tools > DreamWind, from Python/Blueprint, and from the DreamWindSetup commandlet.
 */
UCLASS()
class DREAMWINDEDITOR_API UDreamWindEditorLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/**
	 * Create (or repair) the plugin's shared parameter collection, the wind render targets and the wind
	 * presets. Existing assets keep their other parameters; missing ones are added. Returns false on any failure.
	 */
	UFUNCTION(BlueprintCallable, Category = "DreamWind|Editor")
	static bool EnsureAssets(bool bSaveAssets = true);

	/** The plugin's shared collection (Project Settings > Dream Dynamic World), created when missing. */
	UFUNCTION(BlueprintCallable, Category = "DreamWind|Editor")
	static UMaterialParameterCollection* EnsureParameterCollection(bool bSaveAsset = true);

	/** The volume render targets (near, and far when enabled) at the configured paths, created or re-initialised to the configured size. Returns the near one. */
	UFUNCTION(BlueprintCallable, Category = "DreamWind|Editor")
	static UTextureRenderTargetVolume* EnsureVolumeTexture(bool bSaveAsset = true);

	/** One volume render target at an explicit path. */
	static UTextureRenderTargetVolume* EnsureVolumeTextureAt(const FSoftObjectPath& Path, bool bSaveAsset);

	/** The 2D terrain render target at the configured path (materials read the active terrain map through it). */
	UFUNCTION(BlueprintCallable, Category = "DreamWind|Editor")
	static UTextureRenderTarget2D* EnsureTerrainTexture(bool bSaveAsset = true);

	/** The shipped wind presets (/DreamDynamicWorld/Wind/Presets/DWP_Calm, DWP_Breeze, DWP_Storm); existing ones are left alone. */
	UFUNCTION(BlueprintCallable, Category = "DreamWind|Editor")
	static bool EnsurePresets(bool bSaveAssets = true);

	/**
	 * Bake a terrain map from the world's collision into Target. Bounds left invalid = the bounds of the
	 * level's static geometry. Marks the target dirty; does not save.
	 */
	UFUNCTION(BlueprintCallable, Category = "DreamWind|Editor")
	static bool BakeTerrainMap(UWorld* World, const FBox& Bounds, const FDreamWindTerrainBakeSettings& Settings, UDreamWindTerrainMap* Target);

	/** Bake (creating <Level>_WindTerrain when the actor has no map), assign, apply and save. What the actor's button calls. */
	UFUNCTION(BlueprintCallable, Category = "DreamWind|Editor")
	static bool BakeTerrainMapForActor(ADreamWindGlobal* Actor);
};
