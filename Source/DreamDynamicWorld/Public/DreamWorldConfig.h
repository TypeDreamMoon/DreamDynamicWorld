// Copyright DreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "DreamWorldConfig.generated.h"

class UDreamWorldConfig;
class UWorld;

/** One settings object's values in a config: its class and each config property's value as text. */
USTRUCT(BlueprintType)
struct DREAMDYNAMICWORLD_API FDreamWorldConfigSettings
{
	GENERATED_BODY()

	/** The settings class, e.g. /Script/DreamWeather.DreamWeatherSettings. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Config")
	FSoftClassPath Class;

	/** Each config property's value as text (as an .ini holds it), by name. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Config")
	TMap<FName, FString> Values;
};

DECLARE_MULTICAST_DELEGATE_OneParam(FOnDreamWorldConfigApplied, const UDreamWorldConfig*);

/**
 * A whole set of Dream Dynamic World settings, saved as an asset and put back in one go: every project settings object
 * of the plugin (Dream World, Weather, Wind, Sky, Surfaces, Audio, Effects -- any added later too, found by reflection),
 * the plugin's console variables set by hand, and what the world shows (the time of day, the weather).
 *
 * Capture fills it from what is in effect now; Apply puts it back, for this session or written to the project settings
 * (editor). Dream World > Configs on the level editor's toolbar lists them.
 */
UCLASS(BlueprintType, meta = (DisplayName = "Dream World Config"))
class DREAMDYNAMICWORLD_API UDreamWorldConfig : public UDataAsset
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Config", meta = (MultiLine = true))
	FText Description;

	/** The project settings objects' values. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Config")
	TArray<FDreamWorldConfigSettings> Settings;

	/** The plugin's console variables set above their scalability level (by hand, by code, from an .ini), by name. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Config")
	TMap<FString, FString> ConsoleVariables;

	/** What the world showed, by provider (TimeOfDay, Weather, ... -- see RegisterWorldState). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Config")
	TMap<FName, FString> WorldState;

	/** Put the console variables back on Apply. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Apply")
	bool bApplyConsoleVariables = true;

	/** Put the time of day and the weather back on Apply. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Apply")
	bool bApplyWorldState = true;

	/** Fills the config from what is in effect now (the world's state from WorldContext's world, when given). */
	UFUNCTION(BlueprintCallable, Category = "Dream World|Config", meta = (WorldContext = "WorldContext"))
	void Capture(const UObject* WorldContext);

	/**
	 * Puts the config back: the settings objects' values (changed ones only, each announced as edited), the console
	 * variables (the plugin's others set from the console go back to what they were under it) and the world's state.
	 * With bWriteProjectSettings (editor) the settings are also written to the project's config files.
	 * Returns how many values changed.
	 */
	UFUNCTION(BlueprintCallable, Category = "Dream World|Config", meta = (WorldContext = "WorldContext"))
	int32 Apply(const UObject* WorldContext, bool bWriteProjectSettings = false);

	/** What differs between the config and what is in effect now, one line each ("Class.Property: now -> config"). */
	UFUNCTION(BlueprintCallable, Category = "Dream World|Config", meta = (WorldContext = "WorldContext"))
	TArray<FString> DescribeDifferences(const UObject* WorldContext) const;

	/** The plugin's project settings classes (every UDeveloperSettings in one of its modules). */
	static TArray<UClass*> GetSettingsClasses();

	/** The console variable prefixes a config holds. */
	static TArrayView<const TCHAR* const> GetConsoleVariablePrefixes();

	/** A piece of the world's state a config holds: Capture returns it as text (empty: none), Apply puts it back. */
	struct FWorldStateProvider
	{
		TFunction<FString(UWorld&)> Capture;
		TFunction<void(UWorld&, const FString&)> Apply;
	};
	static void RegisterWorldState(FName Key, FWorldStateProvider Provider);
	static void UnregisterWorldState(FName Key);

	/** The config applied last (this session), or null. */
	static UDreamWorldConfig* GetLastApplied();

	/** After a config is applied. */
	static FOnDreamWorldConfigApplied OnApplied;

private:
	static TMap<FName, FWorldStateProvider>& Providers();
};
