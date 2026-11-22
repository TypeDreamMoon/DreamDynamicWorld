// Copyright DreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "DreamAtmosphereTypes.h"
#include "UObject/Object.h"
#include "DreamAtmosphereTables.generated.h"

class UTextureRenderTarget2D;
class UTextureRenderTargetVolume;

/**
 * The atmosphere's lookup tables on the GPU, built from the shared model (Shaders/Private/DreamAtmosphereLuts.usf):
 *   Transmittance     the light's transmittance to the top of the atmosphere, by height and zenith angle
 *   MultiScattering   the light scattered many times, by height and light angle
 *   Sky               the sky seen from near the ground, by view elevation, angle round from the light and light
 *                     elevation, per unit of the light's illuminance; any light (the sun, the moon) reads it
 * Render targets, so materials (the dome) can sample them as textures; the coordinates are the shared model's
 * (DSA_TransmittanceUv, DSA_MultiScatteringUv, DSA_SkyUvw, read at DSA_TableTexcoord).
 */
UCLASS(Transient)
class DREAMATMOSPHERE_API UDreamAtmosphereTables : public UObject
{
	GENERATED_BODY()

public:
	/** (Re)build all three for this air, at the project settings' sizes. Returns at once; the GPU builds them. */
	void Build(const FDreamAtmosphereParameters& Parameters);

	/** Whether they were built for this air. */
	bool IsBuiltFor(const FDreamAtmosphereParameters& Parameters) const { return bBuilt && Built.Equals(Parameters); }

	bool IsBuilt() const { return bBuilt; }

	/** The air they were last built for. */
	const FDreamAtmosphereParameters& GetBuiltParameters() const { return Built; }

	UTextureRenderTarget2D* GetTransmittance() const { return Transmittance; }
	UTextureRenderTarget2D* GetMultiScattering() const { return MultiScattering; }
	UTextureRenderTargetVolume* GetSky() const { return Sky; }

	/** The sky table's depth slice Z (a light elevation), read back (flushes rendering): for tests and inspection. */
	bool ReadSkySlice(int32 Z, TArray<FLinearColor>& OutPixels);

	/** Whether the CPU holds a copy of the sky table (read back once after each build). */
	bool HasCpuSky() const { return CpuSky.Num() > 0; }

	/**
	 * The sky along View with a light along Light (world unit vectors, z up), per unit of the light's illuminance outside
	 * the air: the CPU's copy of the sky table, read as the GPU reads it (trilinear at the table's coordinates). Black
	 * without a copy.
	 */
	FLinearColor SampleSky(const FVector& View, const FVector& Light) const;

	/** The light's transmittance to the ground for its zenith cosine (none from under the horizon), from the built air. */
	FLinearColor LightTransmittance(float LightZ) const;

private:
	void EnsureTargets();

	UPROPERTY()
	TObjectPtr<UTextureRenderTarget2D> Transmittance;

	UPROPERTY()
	TObjectPtr<UTextureRenderTarget2D> MultiScattering;

	UPROPERTY()
	TObjectPtr<UTextureRenderTargetVolume> Sky;

	UPROPERTY()
	TObjectPtr<UTextureRenderTarget2D> SkySlice;

	UPROPERTY()
	TObjectPtr<UTextureRenderTarget2D> SkyAtlas;

	/** The sky table on the CPU: X fastest, then Y, then Z. */
	TArray<FLinearColor> CpuSky;
	FIntVector CpuSkySize = FIntVector::ZeroValue;

	void ReadCpuSky();

	FDreamAtmosphereParameters Built;
	bool bBuilt = false;
};
