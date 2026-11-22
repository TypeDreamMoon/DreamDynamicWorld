// Copyright DreamMoon. All Rights Reserved.
//
// DreamAtmosphereScattering.h - the second half of the atmosphere model (DreamAtmosphereShared.h): walking rays through
// the air and gathering the light it scatters. Compiled BOTH as HLSL and as C++, under the same rules.
//
// The includer defines, before including this file:
//   float3 DSA_LookupTransmittance(FDSA_Atmosphere A, float r, float mu)      the transmittance to the top (no ground)
//   float3 DSA_LookupMultiScattering(FDSA_Atmosphere A, float r, float MuS)   the multiple scattering per unit scattering
// On the GPU they read the tables; in C++ (DreamAtmosphereHlslShim.h) they are the direct integrals.

#ifndef DREAMATMOSPHERE_SCATTERING_H
#define DREAMATMOSPHERE_SCATTERING_H

// The light's transmittance to a point at r, the light at zenith cosine MuS: none in the earth's shadow.
DSA_FN float3 DSA_LightTransmittance(FDSA_Atmosphere A, float r, float MuS)
{
	if (DSA_HitsGround(A, r, MuS))
	{
		return float3(0.0f, 0.0f, 0.0f);
	}
	return DSA_LookupTransmittance(A, r, MuS);
}

struct FDSA_Ray
{
	float3 Luminance;      // the light scattered toward the ray's start, per unit of the light's illuminance
	float3 Transmittance;  // from the ray's start to its end
	float3 ScatterShare;   // the share of light a scattering event along the ray sends on to its start (the
	                       // multiple-scattering series' ratio, before the isotropic phase)
};

// Walks a ray from Origin (km, from the planet's centre) along Dir for TMax km in Steps segments (denser near the start,
// where the air is densest for a ray from the ground) and gathers the light scattered toward its start from a light
// along LightDir: once, with the phase functions, and many times, from the table. bSecondOrder: the multiple-scattering
// table's own walk, with the isotropic phase and no table.
DSA_FN FDSA_Ray DSA_WalkRay(FDSA_Atmosphere A, float3 Origin, float3 Dir, float3 LightDir, float TMax, int Steps, bool bSecondOrder)
{
	FDSA_Ray R;
	R.Luminance = float3(0.0f, 0.0f, 0.0f);
	R.Transmittance = float3(1.0f, 1.0f, 1.0f);
	R.ScatterShare = float3(0.0f, 0.0f, 0.0f);
	float CosTheta = dot(Dir, LightDir);
	float Isotropic = 1.0f / (4.0f * DSA_PI);
	float PhaseR = bSecondOrder ? Isotropic : DSA_RayleighPhase(CosTheta);
	float PhaseM = bSecondOrder ? Isotropic : DSA_MiePhase(CosTheta, A.MieG);
	float N = float(max(Steps, 1));
	float Previous = 0.0f;
	for (int i = 0; i < Steps; ++i)
	{
		float Edge = (float(i) + 1.0f) / N;
		float End = TMax * Edge * Edge;
		float Dt = End - Previous;
		float T = Previous + 0.3f * Dt;
		Previous = End;

		float3 P = Origin + Dir * T;
		float r = length(P);
		float MuS = dot(P, LightDir) / max(r, 1.0e-3f);
		FDSA_Medium M = DSA_MediumAt(A, r - A.BottomRadius);
		float3 Light = DSA_LightTransmittance(A, r, MuS);
		float3 Single = (M.Rayleigh * PhaseR + float3(M.Mie, M.Mie, M.Mie) * PhaseM) * Light;
		float3 Multi = bSecondOrder ? float3(0.0f, 0.0f, 0.0f) : M.Scattering * DSA_LookupMultiScattering(A, r, MuS) * A.MultiScatteringFactor;
		float3 S = Single + Multi;

		// The light scattered in over the segment, as it fades along it (exact for a constant medium over the step).
		float3 Extinction = max(M.Extinction, float3(1.0e-6f, 1.0e-6f, 1.0e-6f));
		float3 StepTransmittance = DSA_Exp3(-M.Extinction * Dt);
		R.Luminance += R.Transmittance * (S - S * StepTransmittance) / Extinction;
		R.ScatterShare += R.Transmittance * (M.Scattering - M.Scattering * StepTransmittance) / Extinction;
		R.Transmittance = R.Transmittance * StepTransmittance;
	}
	return R;
}

// The light scattered many times, arriving at a point at r from all round with the light at zenith cosine MuS, per unit
// of scattering there (Hillaire 2020): the second order gathered from 64 directions, with what the ground reflects, then
// the whole series of higher orders as a geometric sum, 1 / (1 - the share each event sends on).
DSA_FN float3 DSA_MultiScatteringDirect(FDSA_Atmosphere A, float r, float MuS)
{
	float3 Origin = float3(0.0f, 0.0f, r);
	float3 Light = float3(sqrt(max(1.0f - MuS * MuS, 0.0f)), 0.0f, MuS);
	float3 L = float3(0.0f, 0.0f, 0.0f);
	float3 F = float3(0.0f, 0.0f, 0.0f);
	for (int j = 0; j < 8; ++j)
	{
		float CosT = 1.0f - 2.0f * (float(j) + 0.5f) / 8.0f;
		float SinT = sqrt(max(1.0f - CosT * CosT, 0.0f));
		for (int i = 0; i < 8; ++i)
		{
			float Phi = 2.0f * DSA_PI * (float(i) + 0.5f) / 8.0f;
			float3 Dir = float3(SinT * cos(Phi), SinT * sin(Phi), CosT);
			float TMax = DSA_RayLength(A, r, CosT);
			FDSA_Ray Ray = DSA_WalkRay(A, Origin, Dir, Light, TMax, 20, true);
			float3 Li = Ray.Luminance;
			if (DSA_HitsGround(A, r, CosT))
			{
				float3 Ground = Origin + Dir * TMax;
				float GroundMuS = dot(normalize(Ground), Light);
				Li += Ray.Transmittance * DSA_LightTransmittance(A, A.BottomRadius, GroundMuS) * saturate(GroundMuS) * A.GroundAlbedo * (1.0f / DSA_PI);
			}
			L += Li;
			F += Ray.ScatterShare;
		}
	}
	// (The isotropic phase times the sphere's 4 pi over 64 samples: the plain average.)
	L = L * (1.0f / 64.0f);
	F = F * (1.0f / 64.0f);
	float3 One = float3(1.0f, 1.0f, 1.0f);
	return L / max(One - F, float3(1.0e-3f, 1.0e-3f, 1.0e-3f));
}

// The sky's luminance an observer AltitudeKm over the ground sees along View with the light along Light (unit vectors,
// z up at the observer), per unit of the light's illuminance; the sun's disk is not in it.
DSA_FN float3 DSA_SkyLuminance(FDSA_Atmosphere A, float AltitudeKm, float3 View, float3 Light, int Steps)
{
	float r = A.BottomRadius + max(AltitudeKm, 0.0f);
	float TMax = DSA_RayLength(A, r, View.z);
	return DSA_WalkRay(A, float3(0.0f, 0.0f, r), View, Light, TMax, Steps, false).Luminance;
}

#endif // DREAMATMOSPHERE_SCATTERING_H
