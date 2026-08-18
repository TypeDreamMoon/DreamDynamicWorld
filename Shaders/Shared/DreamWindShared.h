// Copyright DreamMoon. All Rights Reserved.
//
// DreamWindShared.h - the analytic wind model, compiled BOTH as HLSL and as C++.
//
//   HLSL:  #include "/Plugin/DreamWind/Shared/DreamWindShared.h"      (materials, Niagara DI, compute)
//   C++:   #include "DreamWindHlslShim.h"  ->  DreamWind::Hlsl::DW_*   (subsystem, Niagara CPU VM, tests)
//
// The point of the single source is that a cloth mesh (CPU), a groom (GPU Niagara), a grass blade
// (material WPO) and a gameplay query all see the SAME wind at the SAME position, without any GPU
// read-back and without keeping two implementations in step by hand.
//
// RULES for anything written in this file (they keep it valid in both languages, see the shim):
//   * only these types: float, int, uint, bool, float2, float3, float4 - no matrices, no arrays
//   * NO swizzles (v.xy) - use .x/.y/.z/.w and the constructors
//   * NO out/inout parameters - return structs instead
//   * NO HLSL attributes ([loop], [unroll], [branch]) and no static locals
//   * every literal carries an f suffix, and hex constants a u suffix
//   * only the intrinsics provided by DreamWindHlslShim.h: abs floor frac sqrt rsqrt sin cos exp
//     min max clamp saturate lerp smoothstep step dot cross length normalize
//   * comments are // or /* */ only
//
// Coordinate/unit conventions: world space, centimetres, seconds. Velocities are cm/s.

#ifndef DREAMWIND_SHARED_H
#define DREAMWIND_SHARED_H

// ---------------------------------------------------------------------------------------------
// Parameters
// ---------------------------------------------------------------------------------------------

/** The global (L0) wind. Packed into three float4 for MPC / uniform transport, see DW_UnpackParams. */
struct FDreamWindParams
{
	float3 BaseDirection;        // unit vector, world space (mostly horizontal)
	float  BaseSpeed;            // cm/s
	float  GustStrength;         // relative amplitude of gust fronts, 0 = steady wind, 1 = strong gusts
	float  GustWavelength;       // cm, distance between gust fronts along the wind
	float  GustSpeedScale;       // gust fronts travel downwind at BaseSpeed * GustSpeedScale
	float  GustFrequency;        // Hz, global temporal flicker of the gust factor
	float  TurbulenceStrength;   // relative amplitude of the curl-noise turbulence
	float  TurbulenceWavelength; // cm, size of the turbulence eddies
	float  Time;                 // seconds
	float  Seed;                 // integer-valued; decorrelates worlds/instances
};

/** The dynamic volume (L2) window. Packed into two float4, see DW_UnpackVolume. */
struct FDreamWindVolumeParams
{
	float3 Center;     // world-space centre of the window
	float  FadeFrac;   // fraction of the half-extent over which the volume fades out at the border
	float3 InvExtent;  // 1 / (voxels * voxel size) per axis
	float  Enabled;    // 0 or 1
};

FDreamWindParams DW_UnpackParams(float4 P0, float4 P1, float4 P2)
{
	FDreamWindParams P;
	P.BaseDirection        = float3(P0.x, P0.y, P0.z);
	P.BaseSpeed            = P0.w;
	P.GustStrength         = P1.x;
	P.GustWavelength       = P1.y;
	P.GustSpeedScale       = P1.z;
	P.GustFrequency        = P1.w;
	P.TurbulenceStrength   = P2.x;
	P.TurbulenceWavelength = P2.y;
	P.Time                 = P2.z;
	P.Seed                 = P2.w;
	return P;
}

FDreamWindVolumeParams DW_UnpackVolume(float4 V0, float4 V1)
{
	FDreamWindVolumeParams V;
	V.Center    = float3(V0.x, V0.y, V0.z);
	V.FadeFrac  = V0.w;
	V.InvExtent = float3(V1.x, V1.y, V1.z);
	V.Enabled   = V1.w;
	return V;
}

// ---------------------------------------------------------------------------------------------
// Hashing and value noise with analytic derivatives
//   Integer hashing keeps CPU and GPU bit-identical on the lattice; the interpolation is plain
//   float math and continuous, so an FMA-contraction difference at a cell edge cannot show.
// ---------------------------------------------------------------------------------------------

uint DW_HashUInt(uint x)
{
	// lowbias32 (Chris Wellons)
	x = x ^ (x >> 16u);
	x = x * 0x7feb352du;
	x = x ^ (x >> 15u);
	x = x * 0x846ca68bu;
	x = x ^ (x >> 16u);
	return x;
}

/** Hash of a 2D lattice cell to [0,1). */
float DW_LatticeHash2(int cx, int cy, uint seed)
{
	uint h = DW_HashUInt(uint(cx) * 0x9E3779B1u + seed);
	h = DW_HashUInt(h ^ (uint(cy) * 0x85EBCA77u));
	return float(h & 0x00ffffffu) * (1.0f / 16777216.0f);
}

/**
 * 2D value noise with quintic interpolation. Returns (value, d/dx, d/dy) with value in [-1,1] and
 * the derivatives taken with respect to the (already scaled) input coordinates.
 */
float3 DW_NoiseD2(float2 p, uint seed)
{
	float2 i = floor(p);
	float2 f = p - i;

	// quintic fade and its derivative
	float2 u  = f * f * f * (f * (f * 6.0f - 15.0f) + 10.0f);
	float2 du = f * f * (f * (f - 2.0f) + 1.0f) * 30.0f;

	int cx = int(i.x);
	int cy = int(i.y);
	float a = DW_LatticeHash2(cx,     cy,     seed);
	float b = DW_LatticeHash2(cx + 1, cy,     seed);
	float c = DW_LatticeHash2(cx,     cy + 1, seed);
	float d = DW_LatticeHash2(cx + 1, cy + 1, seed);

	float k1 = b - a;
	float k2 = c - a;
	float k3 = a - b - c + d;

	float value = a + k1 * u.x + k2 * u.y + k3 * u.x * u.y;
	float dx = du.x * (k1 + k3 * u.y);
	float dy = du.y * (k2 + k3 * u.x);

	// remap [0,1] -> [-1,1]; the derivative scales by 2 as well
	return float3(value * 2.0f - 1.0f, dx * 2.0f, dy * 2.0f);
}

/** Divergence-free 2D flow: the curl of the scalar noise, (dN/dy, -dN/dx). */
float2 DW_Curl2(float2 p, uint seed)
{
	float3 n = DW_NoiseD2(p, seed);
	return float2(n.z, -n.y);
}

// ---------------------------------------------------------------------------------------------
// The analytic sample (L0)
// ---------------------------------------------------------------------------------------------

struct FDreamWindSample
{
	float3 Velocity;   // cm/s, world space
	float  Gust;       // speed multiplier actually applied (1 = nominal), handy for flutter/rustle
};

/** Unit horizontal direction of the wind, falling back to +X when the wind is vertical. */
float2 DW_HorizontalDir(float3 dir)
{
	float2 along = float2(dir.x, dir.y);
	float l = length(along);
	return l > 0.0001f ? along * (1.0f / l) : float2(1.0f, 0.0f);
}

/**
 * Full-quality analytic wind: base wind x travelling gust fronts (2 octaves + temporal flicker)
 * + two octaves of curl turbulence + a little vertical turbulence. About 6 noise evaluations.
 */
FDreamWindSample DW_SampleAnalytic(FDreamWindParams P, float3 WorldPos)
{
	float3 dir    = P.BaseDirection;
	float2 along  = DW_HorizontalDir(dir);
	float2 across = float2(-along.y, along.x);
	float2 pxy    = float2(WorldPos.x, WorldPos.y);
	uint   seed   = uint(int(P.Seed));

	// --- gust fronts: bands elongated across the wind, travelling downwind
	float travel = P.Time * P.BaseSpeed * P.GustSpeedScale;
	float pa = dot(pxy, along) - travel;
	float pc = dot(pxy, across);
	float invGL = 1.0f / max(P.GustWavelength, 1.0f);
	float2 gp = float2(pa * invGL, pc * invGL * 0.35f);
	float3 g1 = DW_NoiseD2(gp, seed + 17u);
	float3 g2 = DW_NoiseD2(gp * 2.7f + float2(31.3f, 7.1f), seed + 29u);
	float3 gt = DW_NoiseD2(float2(P.Time * P.GustFrequency, 0.37f), seed + 101u);
	float g = g1.x * 0.7f + g2.x * 0.3f;
	float gust = max(1.0f + P.GustStrength * (0.9f * g + 0.35f * gt.x), 0.0f);

	// --- turbulence: curl noise (divergence free), drifting downwind at half speed
	float invTL = 1.0f / max(P.TurbulenceWavelength, 1.0f);
	float2 tp = (pxy - along * (travel * 0.5f)) * invTL;
	float2 c1 = DW_Curl2(tp, seed + 7u);
	float2 c2 = DW_Curl2(tp * 2.1f + float2(5.2f, 1.3f), seed + 13u) * 0.5f;
	float2 turb = (c1 + c2) * (P.TurbulenceStrength * 0.5f);
	float3 vz = DW_NoiseD2(tp * 0.7f + float2(100.0f, 50.0f), seed + 41u);

	FDreamWindSample S;
	S.Velocity = dir * (P.BaseSpeed * gust)
	           + float3(turb.x, turb.y, vz.x * P.TurbulenceStrength * 0.15f) * P.BaseSpeed;
	S.Gust = gust;
	return S;
}

/** Cheap analytic wind for vertex-heavy consumers: 1 gust octave + temporal flicker + 1 curl octave. */
FDreamWindSample DW_SampleAnalyticCheap(FDreamWindParams P, float3 WorldPos)
{
	float3 dir    = P.BaseDirection;
	float2 along  = DW_HorizontalDir(dir);
	float2 across = float2(-along.y, along.x);
	float2 pxy    = float2(WorldPos.x, WorldPos.y);
	uint   seed   = uint(int(P.Seed));

	float travel = P.Time * P.BaseSpeed * P.GustSpeedScale;
	float pa = dot(pxy, along) - travel;
	float pc = dot(pxy, across);
	float invGL = 1.0f / max(P.GustWavelength, 1.0f);
	float3 g1 = DW_NoiseD2(float2(pa * invGL, pc * invGL * 0.35f), seed + 17u);
	float3 gt = DW_NoiseD2(float2(P.Time * P.GustFrequency, 0.37f), seed + 101u);
	float gust = max(1.0f + P.GustStrength * (0.9f * g1.x + 0.35f * gt.x), 0.0f);

	float invTL = 1.0f / max(P.TurbulenceWavelength, 1.0f);
	float2 tp = (pxy - along * (travel * 0.5f)) * invTL;
	float2 turb = DW_Curl2(tp, seed + 7u) * (P.TurbulenceStrength * 0.5f);

	FDreamWindSample S;
	S.Velocity = dir * (P.BaseSpeed * gust) + float3(turb.x, turb.y, 0.0f) * P.BaseSpeed;
	S.Gust = gust;
	return S;
}

// ---------------------------------------------------------------------------------------------
// Sources / impulses (L3)
//   Every source is a velocity field with a spherical footprint. Consumers BLEND toward it with the
//   footprint weight - never add - so a source can never accumulate beyond its own strength and the
//   result is frame-rate independent.
// ---------------------------------------------------------------------------------------------

#define DW_SOURCE_DIRECTIONAL 0   // pushes along Direction inside the sphere (fans, wakes, spells)
#define DW_SOURCE_RADIAL      1   // pushes away from the centre (explosions, landings)
#define DW_SOURCE_VORTEX      2   // swirls around Direction as an axis (tornado, spin attacks)

/** GPU/CPU shared source record: three float4, 48 bytes, no padding. */
struct FDreamWindSourceData
{
	float4 PositionRadius;     // xyz centre, w radius (cm)
	float4 DirectionStrength;  // xyz unit direction / axis, w strength (cm/s)
	float4 Misc;               // x type (DW_SOURCE_*), y inject rate (1/s, volume only), z inner-radius fraction, w unused
};

struct FDreamWindSourceEval
{
	float3 Velocity;   // the velocity the source wants at the point
	float  Weight;     // 0..1 footprint weight
};

FDreamWindSourceEval DW_EvalSource(FDreamWindSourceData S, float3 WorldPos)
{
	float3 c = float3(S.PositionRadius.x, S.PositionRadius.y, S.PositionRadius.z);
	float  R = max(S.PositionRadius.w, 0.001f);
	float3 d = WorldPos - c;
	float dist = length(d);

	float inner = saturate(S.Misc.z);
	float t = saturate((dist / R - inner) / max(1.0f - inner, 0.001f));
	float w = 1.0f - t;
	w = w * w * (3.0f - 2.0f * w);

	int type = int(S.Misc.x);
	float3 dir = float3(S.DirectionStrength.x, S.DirectionStrength.y, S.DirectionStrength.z);
	float strength = S.DirectionStrength.w;

	float3 v = dir * strength;
	if (type == DW_SOURCE_RADIAL)
	{
		v = (dist > 0.001f ? d * (1.0f / dist) : float3(0.0f, 0.0f, 1.0f)) * strength;
	}
	else if (type == DW_SOURCE_VORTEX)
	{
		float3 tangent = cross(dir, d);
		float tl = length(tangent);
		v = (tl > 0.001f ? tangent * (1.0f / tl) : float3(0.0f, 0.0f, 0.0f)) * strength;
	}

	FDreamWindSourceEval E;
	E.Velocity = v;
	E.Weight = w;
	return E;
}

/** Blend a velocity toward a source's velocity by the footprint weight (times an optional rate factor). */
float3 DW_ApplySource(float3 Velocity, FDreamWindSourceData S, float3 WorldPos, float RateFactor)
{
	FDreamWindSourceEval E = DW_EvalSource(S, WorldPos);
	return lerp(Velocity, E.Velocity, saturate(E.Weight * RateFactor));
}

// ---------------------------------------------------------------------------------------------
// Volume window helpers (L2). The window follows the camera and is re-based every frame by the
// update pass, so a texel always maps to the same world position within a frame and consumers
// sample with a plain CLAMP sampler; the border mask hides the edge.
// ---------------------------------------------------------------------------------------------

float3 DW_VolumeUVW(float3 WorldPos, FDreamWindVolumeParams V)
{
	return (WorldPos - V.Center) * V.InvExtent + float3(0.5f, 0.5f, 0.5f);
}

/** 1 inside the window, fading to 0 over the outer FadeFrac of the half extent; 0 when disabled. */
float DW_VolumeMask(float3 WorldPos, FDreamWindVolumeParams V)
{
	float3 d = abs(WorldPos - V.Center) * V.InvExtent * 2.0f;   // 0 at centre, 1 at the border
	float edge = max(d.x, max(d.y, d.z));
	float m = saturate((1.0f - edge) / max(V.FadeFrac, 0.001f));
	return m * V.Enabled;
}

#endif // DREAMWIND_SHARED_H
