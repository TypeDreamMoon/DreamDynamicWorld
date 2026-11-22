// Copyright DreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "DreamAtmosphereTypes.generated.h"

class UVolumeTexture;

/**
 * The air the sky is made of (Shaders/Shared/DreamAtmosphereShared.h): an earth-like planet's by default, the values
 * physically based skies use. Coefficients are per megametre (1000 km): 5.8 here is 5.8e-3 per km.
 */
USTRUCT(BlueprintType)
struct DREAMATMOSPHERE_API FDreamAtmosphereParameters
{
	GENERATED_BODY()

	/** The planet's radius. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Planet", meta = (ClampMin = "100", Units = "km"))
	float PlanetRadius = 6360.0f;

	/** How high the air reaches. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Planet", meta = (ClampMin = "1", Units = "km"))
	float AtmosphereHeight = 100.0f;

	/** The colour the ground reflects back into the air (it brightens the low sky, most under a low sun). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Planet", meta = (HideAlphaChannel))
	FLinearColor GroundAlbedo = FLinearColor(0.3f, 0.3f, 0.3f);

	/** The gas's scattering per megametre at the ground: what makes the sky blue and the low sun red. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rayleigh", meta = (HideAlphaChannel))
	FLinearColor RayleighScattering = FLinearColor(5.802f, 13.558f, 33.1f);

	/** The height over which the gas thins to a third. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rayleigh", meta = (ClampMin = "0.1", Units = "km"))
	float RayleighScaleHeight = 8.0f;

	/** The haze's scattering per megametre at the ground: the white glow round the sun, the pale horizon. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mie", meta = (ClampMin = "0"))
	float MieScattering = 3.996f;

	/** The haze's absorption per megametre at the ground: it dims and greys the distance. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mie", meta = (ClampMin = "0"))
	float MieAbsorption = 0.444f;

	/** How strongly the haze scatters forward (0 evenly, toward 1 into a tight glow round the sun). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mie", meta = (ClampMin = "0", ClampMax = "0.99"))
	float MieAnisotropy = 0.8f;

	/** The height over which the haze thins to a third. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mie", meta = (ClampMin = "0.1", Units = "km"))
	float MieScaleHeight = 1.2f;

	/** The ozone's absorption per megametre at its densest: it keeps the twilight sky blue overhead. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ozone", meta = (HideAlphaChannel))
	FLinearColor OzoneAbsorption = FLinearColor(0.650f, 1.881f, 0.085f);

	/** Where the ozone layer is densest. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ozone", meta = (ClampMin = "0", Units = "km"))
	float OzoneCentre = 25.0f;

	/** How thick the ozone layer is (it thins linearly to nothing at half this either side of its centre). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ozone", meta = (ClampMin = "0.1", Units = "km"))
	float OzoneWidth = 30.0f;

	/** The light scattered more than once, scaled (1 = physical: it fills the shadows of the sky, lights the twilight). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Scattering", meta = (ClampMin = "0", ClampMax = "2"))
	float MultipleScattering = 1.0f;

	/** The five float4s the shared model unpacks (DSA_Unpack), in km and 1/km. */
	void Pack(FVector4f Out[5]) const;

	/** Whether anything that shapes the lookup tables differs (the tables are rebuilt only then). */
	bool Equals(const FDreamAtmosphereParameters& Other) const;
};

/**
 * A sky table baked ahead (Setup, DreamSky.BakeSky) for where the GPU cannot build one: mobile, no Shader Model 5. The
 * table as a volume texture (half floats, its values times Scale, so the deep twilight keeps its digits), and for the
 * CPU's light, ambient and fog the zenith and the horizon all round for each light elevation the table holds.
 */
USTRUCT(BlueprintType)
struct DREAMATMOSPHERE_API FDreamAtmosphereBakedSky
{
	GENERATED_BODY()

	/** The sky table, its values times Scale. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Baked Sky")
	TSoftObjectPtr<UVolumeTexture> Table;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Baked Sky")
	float Scale = 1.0e4f;

	/** Per light elevation (the table's depth slices): the zenith, per unit of the light's illuminance. */
	UPROPERTY(VisibleAnywhere, Category = "Baked Sky")
	TArray<FLinearColor> Zenith;

	/** Per light elevation: the horizon (a little over it) averaged all round, per unit of the light's illuminance. */
	UPROPERTY(VisibleAnywhere, Category = "Baked Sky")
	TArray<FLinearColor> Horizon;

	/** The air it was baked from: it stands only for that air. */
	UPROPERTY(VisibleAnywhere, Category = "Baked Sky")
	FDreamAtmosphereParameters BakedFor;

	/** Whether it holds a sky for this air. */
	bool IsFor(const FDreamAtmosphereParameters& Parameters) const { return Zenith.Num() > 1 && Zenith.Num() == Horizon.Num() && BakedFor.Equals(Parameters); }
};
