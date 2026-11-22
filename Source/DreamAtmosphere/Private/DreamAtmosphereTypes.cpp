// Copyright DreamMoon. All Rights Reserved.

#include "DreamAtmosphereTypes.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(DreamAtmosphereTypes)

void FDreamAtmosphereParameters::Pack(FVector4f Out[5]) const
{
	constexpr float PerMegametre = 1.0e-3f;
	const float Bottom = FMath::Max(PlanetRadius, 100.0f);
	Out[0] = FVector4f(Bottom, Bottom + FMath::Max(AtmosphereHeight, 1.0f), FMath::Max(RayleighScaleHeight, 0.1f), FMath::Max(MieScaleHeight, 0.1f));
	Out[1] = FVector4f(RayleighScattering.R * PerMegametre, RayleighScattering.G * PerMegametre, RayleighScattering.B * PerMegametre,
		FMath::Clamp(MieAnisotropy, 0.0f, 0.99f));
	Out[2] = FVector4f(FMath::Max(MieScattering, 0.0f) * PerMegametre, FMath::Max(MieAbsorption, 0.0f) * PerMegametre, FMath::Max(OzoneCentre, 0.0f),
		FMath::Max(OzoneWidth, 0.1f));
	Out[3] = FVector4f(OzoneAbsorption.R * PerMegametre, OzoneAbsorption.G * PerMegametre, OzoneAbsorption.B * PerMegametre,
		FMath::Clamp(MultipleScattering, 0.0f, 2.0f));
	Out[4] = FVector4f(GroundAlbedo.R, GroundAlbedo.G, GroundAlbedo.B, 0.0f);
}

bool FDreamAtmosphereParameters::Equals(const FDreamAtmosphereParameters& Other) const
{
	FVector4f A[5], B[5];
	Pack(A);
	Other.Pack(B);
	for (int32 i = 0; i < 5; ++i)
	{
		if (!A[i].Equals(B[i], 1.0e-7f))
		{
			return false;
		}
	}
	return true;
}
