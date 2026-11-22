// Copyright DreamMoon. All Rights Reserved.
//
// DreamAtmosphereShared.h - the air's scattering: how much of the sun's (or the moon's) light the atmosphere lets through
// and how much it scatters toward the eye, compiled BOTH as HLSL and as C++.
//
//   HLSL:  #include "/Plugin/DreamDynamicWorld/Shared/DreamAtmosphereShared.h"   (the lookup tables, the aerial perspective)
//   C++:   #include "DreamAtmosphereHlslShim.h"  ->  DreamWind::Hlsl::DSA_*          (the light's colour, tests)
//
// The model is the one modern skies use (Bruneton's tables, Hillaire's multiple scattering): a planet with a thin shell
// of air, Rayleigh scattering from the gas (blue, falling off with an 8 km scale height), Mie scattering and absorption
// from the haze (white, strongly forward, 1.2 km), an ozone layer that absorbs (some 25 km up, turning the twilight sky
// blue), and the ground's albedo. Multiple scattering is folded into one table (the light scattered more than once
// arrives from all around, so an isotropic term per height and sun angle carries it).
//
// Units: kilometres, distances from the planet's centre (r) or along rays (t); mu is the cosine of a ray's angle from the
// zenith. Luminance comes out per unit of the light's illuminance outside the atmosphere: multiply by the sun's (or the
// moon's) illuminance and colour. Directions are unit vectors in a frame whose z is up at the observer.
//
// The rules of DreamWindShared.h apply (no swizzles, no out parameters, f suffixes, only the shim's intrinsics).
//
// The functions that walk rays need two tables, which the includer provides BEFORE including this file's second half
// (DreamAtmosphereScattering.h): the transmittance to the top of the atmosphere and the multiple scattering. On the GPU
// they are textures; in C++ they are the direct integrals below (slow, for the few values the CPU wants).

#ifndef DREAMATMOSPHERE_SHARED_H
#define DREAMATMOSPHERE_SHARED_H

// DSA_FN prefixes every function: empty in HLSL, `inline` in C++ (DreamAtmosphereHlslShim.h defines it).
#ifndef DSA_FN
#define DSA_FN
#endif

#define DSA_PI 3.14159265f

// The atmosphere, unpacked from five float4s (FDreamAtmosphereParameters::Pack):
//   A0 = (planet radius, top of the atmosphere (radius), Rayleigh scale height, Mie scale height)        km
//   A1 = (Rayleigh scattering rgb, Mie anisotropy g)                                                       1/km
//   A2 = (Mie scattering, Mie absorption, ozone layer centre altitude, ozone layer width)                  1/km, km
//   A3 = (ozone absorption rgb, multiple scattering factor)                                                1/km
//   A4 = (ground albedo rgb, -)
struct FDSA_Atmosphere
{
	float BottomRadius;
	float TopRadius;
	float RayleighScaleHeight;
	float MieScaleHeight;
	float3 RayleighScattering;
	float MieG;
	float MieScattering;
	float MieAbsorption;
	float OzoneCentre;
	float OzoneWidth;
	float3 OzoneAbsorption;
	float MultiScatteringFactor;
	float3 GroundAlbedo;
};

DSA_FN FDSA_Atmosphere DSA_Unpack(float4 A0, float4 A1, float4 A2, float4 A3, float4 A4)
{
	FDSA_Atmosphere A;
	A.BottomRadius = A0.x;
	A.TopRadius = max(A0.y, A0.x + 1.0f);
	A.RayleighScaleHeight = max(A0.z, 0.01f);
	A.MieScaleHeight = max(A0.w, 0.01f);
	A.RayleighScattering = float3(A1.x, A1.y, A1.z);
	A.MieG = clamp(A1.w, -0.99f, 0.99f);
	A.MieScattering = A2.x;
	A.MieAbsorption = A2.y;
	A.OzoneCentre = A2.z;
	A.OzoneWidth = max(A2.w, 0.01f);
	A.OzoneAbsorption = float3(A3.x, A3.y, A3.z);
	A.MultiScatteringFactor = A3.w;
	A.GroundAlbedo = float3(A4.x, A4.y, A4.z);
	return A;
}

DSA_FN float3 DSA_Exp3(float3 V)
{
	return float3(exp(V.x), exp(V.y), exp(V.z));
}

// ---- the shell's geometry -----------------------------------------------------------------------------------------

// The distance along a ray from radius r at zenith cosine mu to the sphere of radius R ahead of it (the far crossing
// when inside it or on it, the near one when outside); -1 when the ray misses the sphere or the sphere lies behind.
// (On it counts as inside to a part in a million: a radius of thousands of km carries that much rounding.)
DSA_FN float DSA_RaySphere(float r, float mu, float R)
{
	float B = r * mu;
	float D = B * B - r * r + R * R;
	if (D < 0.0f)
	{
		return -1.0f;
	}
	float S = sqrt(D);
	float Far = -B + S;
	float Near = -B - S;
	if (r > R * (1.0f + 1.0e-6f) && Near > 0.0f)
	{
		return Near;
	}
	return Far > 0.0f ? Far : -1.0f;
}

// Whether a ray from r at mu meets the ground ahead of it.
DSA_FN bool DSA_HitsGround(FDSA_Atmosphere A, float r, float mu)
{
	return mu < 0.0f && r * r * (mu * mu - 1.0f) + A.BottomRadius * A.BottomRadius >= 0.0f;
}

// How far a ray from r at mu runs through the air: to the ground if it meets it, else to the top (0 above it, rising).
DSA_FN float DSA_RayLength(FDSA_Atmosphere A, float r, float mu)
{
	if (DSA_HitsGround(A, r, mu))
	{
		// The near crossing, always: from on the ground looking down that is 0, never the chord through the planet.
		float B = r * mu;
		float D = B * B - r * r + A.BottomRadius * A.BottomRadius;
		return max(-B - sqrt(max(D, 0.0f)), 0.0f);
	}
	return max(DSA_RaySphere(r, mu, A.TopRadius), 0.0f);
}

// ---- the medium -----------------------------------------------------------------------------------------------------

struct FDSA_Medium
{
	float3 Rayleigh;     // Rayleigh scattering, 1/km
	float Mie;           // Mie scattering, 1/km
	float3 Scattering;   // both, 1/km
	float3 Extinction;   // all that takes light out of the ray (scattering and absorption), 1/km
};

DSA_FN FDSA_Medium DSA_MediumAt(FDSA_Atmosphere A, float Altitude)
{
	float H = max(Altitude, 0.0f);
	float RayleighDensity = exp(-H / A.RayleighScaleHeight);
	float MieDensity = exp(-H / A.MieScaleHeight);
	// The ozone: a tent of the layer's width round its centre.
	float OzoneDensity = max(0.0f, 1.0f - abs(H - A.OzoneCentre) / (0.5f * A.OzoneWidth));
	FDSA_Medium M;
	M.Rayleigh = A.RayleighScattering * RayleighDensity;
	M.Mie = A.MieScattering * MieDensity;
	M.Scattering = M.Rayleigh + float3(M.Mie, M.Mie, M.Mie);
	float MieExtinction = (A.MieScattering + A.MieAbsorption) * MieDensity;
	M.Extinction = M.Rayleigh + float3(MieExtinction, MieExtinction, MieExtinction) + A.OzoneAbsorption * OzoneDensity;
	return M;
}

// ---- phase functions ------------------------------------------------------------------------------------------------

DSA_FN float DSA_RayleighPhase(float CosTheta)
{
	return 3.0f / (16.0f * DSA_PI) * (1.0f + CosTheta * CosTheta);
}

// Cornette-Shanks: Henyey-Greenstein made to behave like Mie scattering in the backward lobe.
DSA_FN float DSA_MiePhase(float CosTheta, float G)
{
	float G2 = G * G;
	float K = 3.0f / (8.0f * DSA_PI) * (1.0f - G2) / (2.0f + G2);
	float Denom = max(1.0f + G2 - 2.0f * G * CosTheta, 1.0e-4f);
	return K * (1.0f + CosTheta * CosTheta) / (Denom * sqrt(Denom));
}

// ---- transmittance ------------------------------------------------------------------------------------------------

// The optical depth along a ray from r at mu over Length km, Steps midpoint samples.
DSA_FN float3 DSA_OpticalDepth(FDSA_Atmosphere A, float r, float mu, float Length, int Steps)
{
	float3 Depth = float3(0.0f, 0.0f, 0.0f);
	float Dt = Length / float(max(Steps, 1));
	for (int i = 0; i < Steps; ++i)
	{
		float T = (float(i) + 0.5f) * Dt;
		float Ri = sqrt(r * r + T * T + 2.0f * r * mu * T);
		Depth = Depth + DSA_MediumAt(A, Ri - A.BottomRadius).Extinction * Dt;
	}
	return Depth;
}

// The transmittance from r toward mu to the top of the atmosphere, integrated (the transmittance table's texels). A ray
// into the ground is taken to the top as if the ground were not there (the callers test the ground themselves).
DSA_FN float3 DSA_TransmittanceToTopDirect(FDSA_Atmosphere A, float r, float mu)
{
	float Length = max(DSA_RaySphere(r, mu, A.TopRadius), 0.0f);
	return DSA_Exp3(-DSA_OpticalDepth(A, r, mu, Length, 40));
}

// The transmittance table's coordinates (Bruneton): u follows the distance to the top between its least and its most for
// that height, v the height as the distance to the horizon. Only rays that see the sky are stored.
DSA_FN float2 DSA_TransmittanceUv(FDSA_Atmosphere A, float r, float mu)
{
	float H = sqrt(max(A.TopRadius * A.TopRadius - A.BottomRadius * A.BottomRadius, 0.0f));
	float Rho = sqrt(max(r * r - A.BottomRadius * A.BottomRadius, 0.0f));
	float D = max(DSA_RaySphere(r, mu, A.TopRadius), 0.0f);
	float DMin = A.TopRadius - r;
	float DMax = Rho + H;
	float U = (D - DMin) / max(DMax - DMin, 1.0e-4f);
	float V = Rho / max(H, 1.0e-4f);
	return float2(saturate(U), saturate(V));
}

// And back: the (r, mu) a texel stands for.
DSA_FN float2 DSA_TransmittanceRMu(FDSA_Atmosphere A, float2 Uv)
{
	float H = sqrt(max(A.TopRadius * A.TopRadius - A.BottomRadius * A.BottomRadius, 0.0f));
	float Rho = H * Uv.y;
	float r = sqrt(Rho * Rho + A.BottomRadius * A.BottomRadius);
	float DMin = A.TopRadius - r;
	float DMax = Rho + H;
	float D = DMin + Uv.x * (DMax - DMin);
	float Mu = D <= 0.0f ? 1.0f : (H * H - Rho * Rho - D * D) / (2.0f * r * D);
	return float2(r, clamp(Mu, -1.0f, 1.0f));
}

// ---- the multiple scattering table's coordinates --------------------------------------------------------------------

// u: the light's zenith cosine, -1..1, dense about the horizon (where the twilight's light changes fastest: the earth's
// shadow sweeps up the air); v: the altitude over the shell.
DSA_FN float DSA_SignedSqrt(float X)
{
	return X >= 0.0f ? sqrt(X) : -sqrt(-X);
}

DSA_FN float DSA_SignedSquare(float X)
{
	return X >= 0.0f ? X * X : -X * X;
}

DSA_FN float2 DSA_MultiScatteringUv(FDSA_Atmosphere A, float r, float MuS)
{
	return float2(saturate(0.5f + 0.5f * DSA_SignedSqrt(clamp(MuS, -1.0f, 1.0f))), saturate((r - A.BottomRadius) / (A.TopRadius - A.BottomRadius)));
}

DSA_FN float2 DSA_MultiScatteringRMu(FDSA_Atmosphere A, float2 Uv)
{
	return float2(A.BottomRadius + Uv.y * (A.TopRadius - A.BottomRadius), DSA_SignedSquare(Uv.x * 2.0f - 1.0f));
}

// ---- the sky table's coordinates ------------------------------------------------------------------------------------
// The sky as an observer near the ground sees it, for any light: u the view's elevation (dense at the horizon, where the
// colours change fastest), v the angle round from the light (0 toward it, 1 away; the sky is symmetric about the
// light's vertical plane), w the light's elevation (dense about the horizon, from 20 degrees under it: below that the sky
// is dark but for the stars).

#define DSA_SKY_LOWEST_LIGHT (-0.342f)

// ViewZ, LightZ: the view's and the light's z (the sine of the elevation); CosAround: the cosine of the angle between
// their horizontal directions.
DSA_FN float3 DSA_SkyUvw(float ViewZ, float CosAround, float LightZ)
{
	float U = 0.5f + 0.5f * DSA_SignedSqrt(clamp(ViewZ, -1.0f, 1.0f));
	float V = 0.5f - 0.5f * clamp(CosAround, -1.0f, 1.0f);
	float Lowest = DSA_SignedSqrt(DSA_SKY_LOWEST_LIGHT);
	float W = (DSA_SignedSqrt(clamp(LightZ, DSA_SKY_LOWEST_LIGHT, 1.0f)) - Lowest) / (1.0f - Lowest);
	return float3(U, V, saturate(W));
}

// And back: (ViewZ, CosAround, LightZ) for a texel's coordinates.
DSA_FN float3 DSA_SkyDirections(float3 Uvw)
{
	float ViewZ = DSA_SignedSquare(Uvw.x * 2.0f - 1.0f);
	float CosAround = 1.0f - 2.0f * Uvw.y;
	float Lowest = DSA_SignedSqrt(DSA_SKY_LOWEST_LIGHT);
	float LightZ = DSA_SignedSquare(Lowest + Uvw.z * (1.0f - Lowest));
	return float3(ViewZ, CosAround, LightZ);
}

// The unit view and light vectors for those: the light in the x-z plane, the view turned round from it.
DSA_FN float3 DSA_SkyViewVector(float3 Directions)
{
	float Z = Directions.x;
	float Horizontal = sqrt(max(1.0f - Z * Z, 0.0f));
	float C = Directions.y;
	float S = sqrt(max(1.0f - C * C, 0.0f));
	return float3(Horizontal * C, Horizontal * S, Z);
}

DSA_FN float3 DSA_SkyLightVector(float3 Directions)
{
	float Z = Directions.z;
	return float3(sqrt(max(1.0f - Z * Z, 0.0f)), 0.0f, Z);
}

// ---- the tables' texels -----------------------------------------------------------------------------------------------
// A table of Size texels along an axis stores its coordinate 0..1 edge to edge: texel i holds Index / (Size - 1), and a
// coordinate is read at the texel centre that holds it (so 0 and 1 are stored exactly, never extrapolated).

DSA_FN float DSA_TableParam(float Index, float Size)
{
	return Index / max(Size - 1.0f, 1.0f);
}

DSA_FN float DSA_TableTexcoord(float Param, float Size)
{
	return (saturate(Param) * (Size - 1.0f) + 0.5f) / max(Size, 1.0f);
}

#endif // DREAMATMOSPHERE_SHARED_H
