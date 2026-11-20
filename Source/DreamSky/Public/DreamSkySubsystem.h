// Copyright DreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "DreamSkyTypes.h"
#include "DreamSkySubsystem.generated.h"

class ADreamSky;
class UDreamSkyStyle;
class UDreamWorldSubsystem;
class FDreamSkyCloudViewExtension;

/**
 * The lighting director of a world's sky.
 *
 * Runs inside the world clock's tick (UDreamWorldSubsystem::OnWorldTick), right after the sun and moon moved:
 * the active ADreamSky's style turns the celestial state into lighting, which is eased while the clock runs
 * (so a toon ramp never flickers across its threshold) and snapped when the clock jumps; the sky actor's
 * lights, sky light, fog and exposure follow; and the look of the dome plus the lighting are written to the
 * shared parameter collection, where the sky material and any other material read them.
 */
UCLASS()
class DREAMSKY_API UDreamSkySubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintPure, Category = "Dream Sky", meta = (WorldContext = "WorldContextObject", DisplayName = "Get Dream Sky"))
	static UDreamSkySubsystem* Get(const UObject* WorldContextObject);

	/** The plugin's default style. */
	static UDreamSkyStyle* GetDefaultStyle();

	//~ USubsystem
	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	void RegisterSky(ADreamSky* Sky);
	void UnregisterSky(ADreamSky* Sky);

	/** The sky actor in charge (the first registered that is still alive), or null. */
	UFUNCTION(BlueprintPure, Category = "Dream Sky")
	ADreamSky* GetSky() const;

	/** The lighting as applied this frame: main light, ambient, fog, exposure, day phase. */
	UFUNCTION(BlueprintPure, Category = "Dream Sky")
	const FDreamSkyLighting& GetLighting() const { return Applied; }

	/** The dome's look this frame. */
	UFUNCTION(BlueprintPure, Category = "Dream Sky")
	const FDreamSkyLook& GetLook() const { return Look; }

	/** True once a sky has been applied in this world. */
	UFUNCTION(BlueprintPure, Category = "Dream Sky")
	bool HasLighting() const { return bHasApplied; }

	/** Eases From toward To (colours and intensities; directions and the day phase are taken from To). */
	static FDreamSkyLighting BlendLighting(const FDreamSkyLighting& From, const FDreamSkyLighting& To, float Alpha);

private:
	void Update(UDreamWorldSubsystem& Clock, float DeltaTime);
	void WriteParameters(UDreamWorldSubsystem& Clock, const UDreamSkyStyle& Style, bool bVolumetricClouds);
	void LogLighting(UDreamWorldSubsystem& Clock);
	bool RecaptureIfChanged();

	TArray<TWeakObjectPtr<ADreamSky>> Skies;
	FDreamSkyLighting Applied;
	FDreamSkyLook Look;
	bool bHasApplied = false;
	uint32 LastJumpSerial = 0;
	FDelegateHandle TickHandle;
	TWeakObjectPtr<UDreamWorldSubsystem> ClockSubsystem;
	TSharedPtr<FDreamSkyCloudViewExtension, ESPMode::ThreadSafe> CloudViewExtension;

	int32 RecaptureTask = 0;
	FLinearColor CapturedZenith = FLinearColor::Transparent;
	FLinearColor CapturedHorizon = FLinearColor::Transparent;
	float CapturedHaze = 0.0f;
	double LastLightingLog = -1.0e9;
};
