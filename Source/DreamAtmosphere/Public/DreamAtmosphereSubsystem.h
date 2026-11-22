// Copyright DreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "DreamAtmosphereTypes.h"
#include "Subsystems/WorldSubsystem.h"
#include "DreamAtmosphereSubsystem.generated.h"

class FDreamAtmosphereViewExtension;
class UDreamAtmosphereTables;

/**
 * The world's atmosphere: the air it is made of (the project settings' until a sky style sets its own), the lookup
 * tables built from it (rebuilt on the GPU whenever the air changes, never every frame), and the aerial perspective
 * laid over every view of the world, lit by the clock's sun and moon.
 */
UCLASS()
class DREAMATMOSPHERE_API UDreamAtmosphereSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	static UDreamAtmosphereSubsystem* Get(const UWorld* World);

	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;
	virtual bool IsTickableInEditor() const override { return true; }

	/** The air from now on (a sky style's); the tables follow on the next tick if it differs. */
	UFUNCTION(BlueprintCallable, Category = "Dream Atmosphere")
	void SetParameters(const FDreamAtmosphereParameters& InParameters);

	/** Back to the project settings' air. */
	UFUNCTION(BlueprintCallable, Category = "Dream Atmosphere")
	void ResetParameters();

	const FDreamAtmosphereParameters& GetParameters() const { return Parameters; }

	/** The tables, built for GetParameters() once the next tick has run (null where nothing renders). */
	UDreamAtmosphereTables* GetTables() const { return Tables; }

	/** How many times the tables were built: they are rebuilt only when the air changes. */
	int32 GetBuildCount() const { return BuildCount; }

	/** The sun outside the air (illuminance times colour, the project settings'), and the moon with its phase. */
	FLinearColor GetSunLight() const;
	FLinearColor GetMoonLight() const;

	/** The moon's light in the sky's own colour (the real moon's share of the sun's; Moon Sky Illuminance), its phase in. */
	FLinearColor GetMoonSkyLight() const;

	/** Whether the physical sky can be drawn and read: the tables built, their copy on the CPU. */
	bool IsSkyReady() const;

private:
	UPROPERTY()
	TObjectPtr<UDreamAtmosphereTables> Tables;

	TSharedPtr<FDreamAtmosphereViewExtension, ESPMode::ThreadSafe> ViewExtension;

	FDreamAtmosphereParameters Parameters;
	bool bOverridden = false;
	int32 BuildCount = 0;
};
