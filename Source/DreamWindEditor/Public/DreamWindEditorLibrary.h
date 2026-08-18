// Copyright DreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "DreamWindEditorLibrary.generated.h"

class UMaterialParameterCollection;
class UTextureRenderTargetVolume;

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
	 * Create (or repair) MPC_DreamWind and RT_DreamWindVolume. Existing assets keep their other
	 * parameters; the wind parameters are added when missing. Returns false on any failure.
	 */
	UFUNCTION(BlueprintCallable, Category = "DreamWind|Editor")
	static bool EnsureAssets(bool bSaveAssets = true);

	/** The collection at the configured path, created when missing. */
	UFUNCTION(BlueprintCallable, Category = "DreamWind|Editor")
	static UMaterialParameterCollection* EnsureParameterCollection(bool bSaveAsset = true);

	/** The volume render target at the configured path, created or re-initialised to the configured size. */
	UFUNCTION(BlueprintCallable, Category = "DreamWind|Editor")
	static UTextureRenderTargetVolume* EnsureVolumeTexture(bool bSaveAsset = true);
};
