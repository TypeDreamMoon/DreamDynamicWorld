// Copyright DreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "DreamAtmosphereTypes.h"
#include "Subsystems/WorldSubsystem.h"
#include "DreamAtmosphereSubsystem.generated.h"

class FDreamAtmosphereViewExtension;
class UDreamAtmosphereTables;
class UTexture;
class UVolumeTexture;

/**
 * The world's atmosphere: the air it is made of (the project settings' until a sky style sets its own), the lookup
 * tables built from it (rebuilt on the GPU whenever the air changes, never every frame), and the aerial perspective
 * laid over every view of the world, lit by the clock's sun and moon. Where the GPU cannot build the tables (mobile, no
 * Shader Model 5) the sky comes from a sky table baked ahead for the air (SetBakedSky), and the aerial perspective is off.
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

	/** The weather the aerial perspective takes from now on (a sky's, every frame; clear air until then). */
	UFUNCTION(BlueprintCallable, Category = "Dream Atmosphere")
	void SetWeather(const FDreamAtmosphereWeather& InWeather) { Weather = InWeather; }

	const FDreamAtmosphereWeather& GetWeather() const { return Weather; }

	/** The sky baked ahead for where the tables cannot be built (a sky style's); used only while it is for the air. */
	void SetBakedSky(const FDreamAtmosphereBakedSky& InBaked);

	/** Whether this GPU builds the tables (Shader Model 5, a renderer; r.DreamAtmosphere.ForceBakedSky 0). */
	static bool CanBuildTables();

	/** The tables, built for GetParameters() once the next tick has run (null where they are not built). */
	UDreamAtmosphereTables* GetTables() const { return Tables; }

	/** How many times the tables were built: they are rebuilt only when the air changes. */
	int32 GetBuildCount() const { return BuildCount; }

	/** The sun outside the air (illuminance times colour, the project settings'), and the moon with its phase. */
	FLinearColor GetSunLight() const;
	FLinearColor GetMoonLight() const;

	/** The moon's light in the sky's own colour (the real moon's share of the sun's; Moon Sky Illuminance), its phase in. */
	FLinearColor GetMoonSkyLight() const;

	/** Whether the physical sky can be drawn and read: the tables built and read back, or a baked sky for the air. */
	bool IsSkyReady() const;

	/** Whether the sky comes from the baked table (not the GPU's). */
	bool IsSkyBaked() const;

	/** The sky table the dome samples (the tables' render target, or the baked texture); null when not ready. */
	UTexture* GetSkyTexture() const;

	/** What the dome multiplies the table's values by: 1 for the GPU's table, 1 / its scale for a baked one, 0 for none. */
	float GetSkyDecode() const;

	/** The zenith with a light along Light (world unit vector), per unit of the light's illuminance. */
	FLinearColor SkyZenith(const FVector& Light) const;

	/** The horizon (a little over it) averaged all round with a light along Light, per unit of its illuminance. */
	FLinearColor SkyHorizon(const FVector& Light) const;

	/** The light's transmittance to the ground for its zenith cosine (none from under the horizon), for the air. */
	FLinearColor LightTransmittance(float LightZ) const;

private:
	UPROPERTY()
	TObjectPtr<UDreamAtmosphereTables> Tables;

	UPROPERTY()
	TObjectPtr<UVolumeTexture> BakedTexture;

	TSharedPtr<FDreamAtmosphereViewExtension, ESPMode::ThreadSafe> ViewExtension;

	FDreamAtmosphereParameters Parameters;
	FDreamAtmosphereBakedSky Baked;
	FDreamAtmosphereWeather Weather;
	bool bOverridden = false;
	int32 BuildCount = 0;

	/** A baked summary row read at a light's elevation (the table's depth coordinate). */
	FLinearColor ReadBaked(const TArray<FLinearColor>& Rows, float LightZ) const;
};
