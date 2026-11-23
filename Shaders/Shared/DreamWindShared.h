// Copyright DreamMoon. All Rights Reserved.
//
// DreamWindShared.h - the analytic wind model, compiled BOTH as HLSL and as C++.
//
//   HLSL:  #include "/Plugin/DreamDynamicWorld/Shared/DreamWindShared.h"      (materials, Niagara DI, compute)
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
//   * only the intrinsics provided by DreamWindHlslShim.h: abs floor frac sqrt rsqrt sin cos exp exp2
//     min max clamp saturate lerp smoothstep step dot cross length normalize
//   * comments are // or /* */ only
//
// Coordinate/unit conventions: world space, centimetres, seconds. Velocities are cm/s.

#ifndef DREAMWIND_SHARED_H
#define DREAMWIND_SHARED_H

// DW_FN prefixes every function below: empty in HLSL, `inline` in C++ (DreamWindHlslShim.h defines it),
// so the C++ translation units that all include this header never define the same function twice.
#ifndef DW_FN
#define DW_FN
#endif

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
	float  Enabled;    // 0 off .. 1 (a fade while the quality level changes the grids: consumers take this much of the volume)
};

DW_FN FDreamWindParams DW_UnpackParams(float4 P0, float4 P1, float4 P2)
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

DW_FN FDreamWindVolumeParams DW_UnpackVolume(float4 V0, float4 V1)
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

DW_FN uint DW_HashUInt(uint x)
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
DW_FN float DW_LatticeHash2(int cx, int cy, uint seed)
{
	uint h = DW_HashUInt(uint(cx) * 0x9E3779B1u + seed);
	h = DW_HashUInt(h ^ (uint(cy) * 0x85EBCA77u));
	return float(h & 0x00ffffffu) * (1.0f / 16777216.0f);
}

/**
 * 2D value noise with quintic interpolation. Returns (value, d/dx, d/dy) with value in [-1,1] and
 * the derivatives taken with respect to the (already scaled) input coordinates.
 */
DW_FN float3 DW_NoiseD2(float2 p, uint seed)
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
DW_FN float2 DW_Curl2(float2 p, uint seed)
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
DW_FN float2 DW_HorizontalDir(float3 dir)
{
	float2 along = float2(dir.x, dir.y);
	float l = length(along);
	return l > 0.0001f ? along * (1.0f / l) : float2(1.0f, 0.0f);
}

/**
 * Full-quality analytic wind: base wind x travelling gust fronts (2 octaves + temporal flicker)
 * + two octaves of curl turbulence + a little vertical turbulence. About 6 noise evaluations.
 */
DW_FN FDreamWindSample DW_SampleAnalytic(FDreamWindParams P, float3 WorldPos)
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
DW_FN FDreamWindSample DW_SampleAnalyticCheap(FDreamWindParams P, float3 WorldPos)
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
//   Every source is a velocity field with a footprint: a sphere for the three "pushing" types, an
//   oriented box for a shelter. Consumers BLEND toward it with the footprint weight - never add - so
//   a source can never accumulate beyond its own strength and the result is frame-rate independent.
// ---------------------------------------------------------------------------------------------

#define DW_SOURCE_DIRECTIONAL 0   // pushes along Direction inside the sphere (fans, wakes, spells)
#define DW_SOURCE_RADIAL      1   // pushes away from the centre (explosions, landings)
#define DW_SOURCE_VORTEX      2   // swirls around Direction as an axis (tornado, spin attacks)
#define DW_SOURCE_SHELTER     3   // oriented box: the wind inside becomes Direction * Strength;
                                  // Strength 0 = a sheltered interior (house, cave, behind a wall),
                                  // Strength > 0 = a wind zone (corridor draft, wind tunnel)
#define DW_SOURCE_TWISTER     4   // a whirlwind standing on the ground at the centre, Direction its axis (a dust
                                  // devil, a tornado): the air turns round it, Strength cm/s at its core's edge
                                  // (Misc.z: the core's share of Radius; + counter-clockwise seen from above),
                                  // rises in its core (BoxExtent.x cm/s) and is drawn in along the ground
                                  // (BoxExtent.y cm/s); Misc.w: its height over Radius. Added to the wind (it is
                                  // carried along with it), not blended toward.

/** Number of float4 per source record in the flat GPU buffers (see DW_LoadSource in the consumers). */
#define DW_SOURCE_FLOAT4S 5

/** GPU/CPU shared source record: five float4, 80 bytes, no padding. */
struct FDreamWindSourceData
{
	float4 PositionRadius;     // xyz centre, w radius (cm) - for a box the bounding-sphere radius
	float4 DirectionStrength;  // xyz unit direction / axis / zone wind direction, w strength (cm/s)
	float4 Misc;               // x type (DW_SOURCE_*), y inject rate (1/s, volume only), z inner fraction,
	                           // w stretch: forward axial radius / Radius (>= 1; 1 = sphere, 4 = a jet four
	                           //   radii long along +Direction; radial sources ignore it)
	float4 BoxExtent;          // xyz half extents of a shelter box (cm), w unused - spheres leave it 0
	float4 BoxRotation;        // xyzw unit quaternion, box local -> world - spheres leave it (0,0,0,1)
};

struct FDreamWindSourceEval
{
	float3 Velocity;   // the velocity the source wants at the point
	float  Weight;     // 0..1 footprint weight
};

/** Rotate v by the INVERSE of unit quaternion q (world -> box local). */
DW_FN float3 DW_RotateInverse(float4 q, float3 v)
{
	// v' = v + 2 * cross(qv, cross(qv, v) + qw * v) with qv negated for the conjugate
	float3 qv = float3(-q.x, -q.y, -q.z);
	float3 t = cross(qv, cross(qv, v) + v * q.w);
	return v + t * 2.0f;
}

DW_FN FDreamWindSourceEval DW_EvalSource(FDreamWindSourceData S, float3 WorldPos)
{
	float3 c = float3(S.PositionRadius.x, S.PositionRadius.y, S.PositionRadius.z);
	float  R = max(S.PositionRadius.w, 0.001f);
	float3 d = WorldPos - c;
	float dist = length(d);
	int type = int(S.Misc.x);

	float3 dir = float3(S.DirectionStrength.x, S.DirectionStrength.y, S.DirectionStrength.z);
	float stretch = max(S.Misc.w, 1.0f);

	// footprint distance, 0 at the centre and 1 at the edge: |d| / R for spheres; for a stretched
	// directional / vortex source an egg whose axial radius is R * stretch ahead of the centre (along
	// +Direction) and R behind it; the largest normalised axis distance (Chebyshev) inside the
	// oriented box for shelters
	if (type == DW_SOURCE_TWISTER)
	{
		// Rankine: turning as a solid body inside the core, falling off as 1/r outside it; the core rising, fastest on the
		// axis and from a core's height up; the air drawn in along the ground toward the core's edge.
		float height = R * stretch;
		float along = dot(d, dir);
		float3 radialVec = d - dir * along;
		float r = length(radialVec);
		float core = max(R * saturate(S.Misc.z), 1.0f);
		float rc = r / core;
		float across = r / R;
		float up = along < 0.0f ? -along / (0.25f * R) : along / max(height, 1.0f);   // (below the ground it ends at once)
		float edge = max(across, up);
		FDreamWindSourceEval T;
		T.Weight = 1.0f - smoothstep(0.7f, 1.0f, edge);
		float3 tangent = r > 0.001f ? cross(dir, radialVec) * (1.0f / r) : float3(0.0f, 0.0f, 0.0f);
		float3 inward = r > 0.001f ? radialVec * (-1.0f / r) : float3(0.0f, 0.0f, 0.0f);
		float spin = rc < 1.0f ? rc : 1.0f / rc;
		float rise = exp(-rc * rc) * saturate(along / core + 0.2f);
		float low = exp(-max(along, 0.0f) / core);
		float draw = rc < 1.0f ? rc : 1.0f / rc;
		T.Velocity = tangent * (S.DirectionStrength.w * spin) + dir * (S.BoxExtent.x * rise) + inward * (S.BoxExtent.y * draw * low);
		return T;
	}

	float footprint = dist / R;
	if (type == DW_SOURCE_SHELTER)
	{
		float3 local = DW_RotateInverse(S.BoxRotation, d);
		float3 ext = max(float3(S.BoxExtent.x, S.BoxExtent.y, S.BoxExtent.z), float3(0.001f, 0.001f, 0.001f));
		float3 a = abs(local) / ext;
		footprint = max(a.x, max(a.y, a.z));
	}
	else if (stretch > 1.0f && type != DW_SOURCE_RADIAL)
	{
		float axial = dot(d, dir);
		float3 radialVec = d - dir * axial;
		float radial = length(radialVec) / R;
		float axialR = axial >= 0.0f ? R * stretch : R;
		float axialN = axial / axialR;
		footprint = sqrt(radial * radial + axialN * axialN);
	}

	float inner = saturate(S.Misc.z);
	float t = saturate((footprint - inner) / max(1.0f - inner, 0.001f));
	float w = 1.0f - t;
	w = w * w * (3.0f - 2.0f * w);

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
DW_FN float3 DW_ApplySource(float3 Velocity, FDreamWindSourceData S, float3 WorldPos, float RateFactor)
{
	FDreamWindSourceEval E = DW_EvalSource(S, WorldPos);
	if (int(S.Misc.x) == DW_SOURCE_TWISTER)
	{
		return Velocity + E.Velocity * saturate(E.Weight * RateFactor);   // carried along by the wind it stands in
	}
	return lerp(Velocity, E.Velocity, saturate(E.Weight * RateFactor));
}

// ---------------------------------------------------------------------------------------------
// Volume window helpers (L2). The window follows the camera and is re-based every frame by the
// update pass, so a texel always maps to the same world position within a frame and consumers
// sample with a plain CLAMP sampler; the border mask hides the edge.
// ---------------------------------------------------------------------------------------------

DW_FN float3 DW_VolumeUVW(float3 WorldPos, FDreamWindVolumeParams V)
{
	return (WorldPos - V.Center) * V.InvExtent + float3(0.5f, 0.5f, 0.5f);
}

/** 1 inside the window, fading to 0 over the outer FadeFrac of the half extent; 0 when disabled. */
DW_FN float DW_VolumeMask(float3 WorldPos, FDreamWindVolumeParams V)
{
	float3 d = abs(WorldPos - V.Center) * V.InvExtent * 2.0f;   // 0 at centre, 1 at the border
	float edge = max(d.x, max(d.y, d.z));
	float m = saturate((1.0f - edge) / max(V.FadeFrac, 0.001f));
	return m * V.Enabled;
}

// ---------------------------------------------------------------------------------------------
// Terrain modulation (L1). A baked 2D map over the level: R = speed multiplier (ridges > 1,
// hollows < 1), GB = horizontal steering offset added to the wind direction (toward valleys),
// A = normalised height for debugging. Consumers fetch the texel (GPU: texture, CPU: cached array)
// and call DW_ApplyTerrain; outside the map or when disabled the wind passes through unchanged.
// ---------------------------------------------------------------------------------------------

struct FDreamWindTerrainParams
{
	float2 Min;               // world XY of texel (0,0)
	float2 InvSize;           // 1 / world size of the map per axis
	float  Enabled;           // 0 or 1
	float  SteerScale;        // multiplies the baked steering offset
	float  SpeedScale;        // 0 = ignore the baked multiplier, 1 = apply it fully
	float  TexelSize;         // world size (cm) of one base-level texel
	float  OcclusionStrength; // 0 = geometry does not block the wind, 1 = full wind shadow behind it
	float  OcclusionRange;    // cm, how far upwind a sample looks for something that shades it
	float  OcclusionSlope;    // rise / run of the wind-shadow wedge: an obstacle H above the sample
	                          //   shades it out to H / slope downwind (0.2 = five heights)
	float  HeightBase;        // world Z (cm) that texel height 0 stands for (the map's lowest ground);
	                          //   heights are stored relative to it so a 16-bit texture keeps cm precision
};

DW_FN FDreamWindTerrainParams DW_UnpackTerrain(float4 T0, float4 T1, float4 T2)
{
	FDreamWindTerrainParams T;
	T.Min               = float2(T0.x, T0.y);
	T.InvSize           = float2(T0.z, T0.w);
	T.Enabled           = T1.x;
	T.SteerScale        = T1.y;
	T.SpeedScale        = T1.z;
	T.TexelSize         = T1.w;
	T.OcclusionStrength = T2.x;
	T.OcclusionRange    = T2.y;
	T.OcclusionSlope    = T2.z;
	T.HeightBase        = T2.w;
	return T;
}

/** World Z (cm) of the ground / obstacle top stored in a terrain texel. */
DW_FN float DW_TerrainHeight(float4 Texel, FDreamWindTerrainParams T)
{
	return Texel.w + T.HeightBase;
}

// The terrain texture is an ATLAS: the baked map (level 0) in the top half, and below it a chain of
// MAX-filtered copies of its height (levels 1..DW_TERRAIN_MAX_LEVEL, each half the size of the one
// before, .x = 1 / .yz = 0). Level L lives at v = 1 - 2^-L .. 1 - 2^-(L+1), u = 0 .. 2^-L. The
// occlusion march samples coarser levels further away so a thin wall is never stepped over.
#define DW_TERRAIN_MAX_LEVEL 4

/**
 * Texture UV of a logical map UV (0..1) at level Level of the atlas, clamped inside that level, for
 * a BILINEAR fetch (the terrain modulation reads level 0 this way so multiplier and steering stay smooth).
 */
DW_FN float2 DW_TerrainAtlasUV(float2 UV, int Level, FDreamWindTerrainParams T)
{
	float scale = exp2(-float(Level));
	// texels of every level are 1 / W wide and 1 / (2 H) tall in atlas space
	float halfTexelU = 0.5f * T.InvSize.x * T.TexelSize;
	float halfTexelV = 0.25f * T.InvSize.y * T.TexelSize;
	float u = clamp(UV.x * scale, halfTexelU, scale - halfTexelU);
	float v = (1.0f - scale) + clamp(UV.y * scale * 0.5f, halfTexelV, scale * 0.5f - halfTexelV);
	return float2(u, v);
}

/**
 * Texture UV of the CENTRE of the level-Level texel that contains a logical map UV: a bilinear fetch
 * there returns that one texel, i.e. a nearest fetch (debug / tests; the march itself is bilinear).
 */
DW_FN float2 DW_TerrainAtlasTexelUV(float2 UV, int Level, FDreamWindTerrainParams T)
{
	float scale = exp2(-float(Level));
	float texelU = T.InvSize.x * T.TexelSize;          // 1 / W
	float texelV = 0.5f * T.InvSize.y * T.TexelSize;   // 1 / (2 H)
	float widthL = scale / texelU;                     // texels of this level per axis (W >> Level)
	float heightL = scale / (2.0f * texelV);
	float ix = clamp(floor(saturate(UV.x) * widthL), 0.0f, widthL - 1.0f);
	float iy = clamp(floor(saturate(UV.y) * heightL), 0.0f, heightL - 1.0f);
	return float2((ix + 0.5f) * texelU, (1.0f - scale) + (iy + 0.5f) * texelV);
}

/** UV of a world position in the terrain map; outside [0,1] means outside the map. */
DW_FN float2 DW_TerrainUV(float3 WorldPos, FDreamWindTerrainParams T)
{
	return (float2(WorldPos.x, WorldPos.y) - T.Min) * T.InvSize;
}

/** 1 when the UV lies inside the map (and the map is enabled), 0 otherwise. */
DW_FN float DW_TerrainInside(float2 UV, FDreamWindTerrainParams T)
{
	float inside = (UV.x >= 0.0f && UV.x <= 1.0f && UV.y >= 0.0f && UV.y <= 1.0f) ? 1.0f : 0.0f;
	return inside * T.Enabled;
}

/**
 * Applies a fetched terrain texel to an analytic sample. Texel = (speed mult, steer.x, steer.y,
 * ground/obstacle height in cm above HeightBase - see DW_TerrainHeight).
 */
DW_FN FDreamWindSample DW_ApplyTerrain(FDreamWindSample S, float4 Texel, FDreamWindTerrainParams T, float Inside)
{
	if (Inside <= 0.0f)
	{
		return S;
	}
	float speedMul = lerp(1.0f, max(Texel.x, 0.0f), saturate(T.SpeedScale));
	float2 steer = float2(Texel.y, Texel.z) * T.SteerScale;
	float2 v2 = float2(S.Velocity.x, S.Velocity.y);
	float sp = length(v2);
	if (sp > 0.001f)
	{
		float2 d = v2 * (1.0f / sp) + steer;
		float dl = length(d);
		if (dl > 0.001f)
		{
			v2 = d * (sp / dl);
		}
	}
	S.Velocity = float3(v2.x * speedMul, v2.y * speedMul, S.Velocity.z * speedMul);
	return S;
}

// ---------------------------------------------------------------------------------------------
// Occlusion (still L1): the wind shadow of level geometry, read from the same terrain map.
//   A sample marches upwind through the baked heights; anything that rises above it casts a wedge
//   of shadow downwind (H above the sample shades out to H / OcclusionSlope). The taps are spaced
//   quadratically (dense near the sample, far-reaching at the end) and each tap reads the max-height
//   level whose texel is at least as wide as the step, at the tap AND at the midpoint of the step -
//   two bilinear fetches half a texel apart, so a one-texel wall always shows at least 3/4 of its
//   height and the shadow stays smooth. Every consumer runs the same loop with its own texture
//   fetch: tap positions, accumulation and the final damping live here so the CPU, the materials,
//   Niagara and the volume agree.
// ---------------------------------------------------------------------------------------------

#define DW_OCCLUSION_TAPS 12

/** True when occlusion is worth evaluating for this sample. */
DW_FN bool DW_OcclusionActive(FDreamWindTerrainParams T, float Speed)
{
	return T.Enabled > 0.0f && T.OcclusionStrength > 0.0f && T.OcclusionRange > 0.0f && Speed > 0.001f;
}

/** Upwind distance of tap k (1..DW_OCCLUSION_TAPS), cm. */
DW_FN float DW_OcclusionTapDistance(int k, FDreamWindTerrainParams T)
{
	float f = float(k) / float(DW_OCCLUSION_TAPS);
	return T.OcclusionRange * f * f;
}

/** World XY at Dist cm upwind of a sample at WorldPos; UpDir2 is the unit horizontal direction pointing UPWIND. */
DW_FN float2 DW_OcclusionXYAt(float3 WorldPos, float2 UpDir2, float Dist)
{
	return float2(WorldPos.x, WorldPos.y) + UpDir2 * Dist;
}

/** World XY of tap k. */
DW_FN float2 DW_OcclusionTapXY(float3 WorldPos, float2 UpDir2, int k, FDreamWindTerrainParams T)
{
	return DW_OcclusionXYAt(WorldPos, UpDir2, DW_OcclusionTapDistance(k, T));
}

/** Upwind distance of the midpoint between tap k-1 and tap k, cm. */
DW_FN float DW_OcclusionMidDistance(int k, FDreamWindTerrainParams T)
{
	return 0.5f * (DW_OcclusionTapDistance(k - 1, T) + DW_OcclusionTapDistance(k, T));
}

/**
 * Atlas level to read for tap k: the coarsest-needed max level whose texel is AT LEAST as wide as
 * the gap to the previous tap, so consecutive taps read the same or adjacent texels and no column
 * in between is skipped.
 */
DW_FN int DW_OcclusionTapLevel(int k, FDreamWindTerrainParams T)
{
	float gap = DW_OcclusionTapDistance(k, T) - DW_OcclusionTapDistance(k - 1, T);
	float ratio = gap / max(T.TexelSize, 1.0f);
	int level = 0;
	// ceil(log2(ratio)) without log2 (the shim has none): 2, 4, 8, 16 texels
	if (ratio > 1.0f) { level = 1; }
	if (ratio > 2.0f) { level = 2; }
	if (ratio > 4.0f) { level = 3; }
	if (ratio > 8.0f) { level = 4; }
	return min(level, DW_TERRAIN_MAX_LEVEL);
}

/**
 * Folds one tap into the running shadow (0..1): an obstacle Rise cm above the sample at Dist cm
 * upwind shades it by 1 - Dist * slope / Rise, i.e. fully right behind, nothing beyond Rise / slope.
 */
DW_FN float DW_OcclusionAccumulate(float Shadow, float TapHeight, float SampleZ, float Dist, FDreamWindTerrainParams T)
{
	float rise = TapHeight - SampleZ;
	if (rise <= 0.0f)
	{
		return Shadow;
	}
	float reach = rise / max(T.OcclusionSlope, 0.01f);
	float s = saturate(1.0f - Dist / max(reach, 1.0f));
	return max(Shadow, s);
}

/**
 * Shadow of the sample's own column: inside solid geometry (the ground / an obstacle top is above the
 * sample) = full, fading in over the first 45 cm so the edge of a solid is not a hard step. The 5 cm
 * tolerance keeps a blade sitting exactly on the baked ground from being "buried".
 */
DW_FN float DW_OcclusionSelf(float TexelHeight, float SampleZ)
{
	return saturate((TexelHeight - SampleZ - 5.0f) / 45.0f);
}

/** Damps the wind by the accumulated shadow. Only the magnitude changes; the direction stays. */
DW_FN FDreamWindSample DW_ApplyOcclusion(FDreamWindSample S, float Shadow, FDreamWindTerrainParams T)
{
	float k = 1.0f - saturate(Shadow) * saturate(T.OcclusionStrength);
	S.Velocity = S.Velocity * k;
	return S;
}

// ---------------------------------------------------------------------------------------------
// The runtime terrain wind map (L1, computed around the camera instead of baked). Two levels, each a
// square ring buffer over the world (wrap addressing: a world position's texel is its cell index modulo
// the map's size, so following the camera never moves what is already there):
//   derived texel = (speed multiplier, steer.x, steer.y, wind shadow height in cm)
//   height texel  = the top surface's height in cm (the roofs; not needed for the wind itself)
// The wind shadow height S of a texel is the highest any upwind obstacle's wedge reaches over it:
// max over r >= 0 of (H(x - WindDir * r) - Slope * r), the texel's own height included (r = 0), so a point
// below S is in the lee of something (or inside it). The speed multiplier and steering come from the far
// level (the terrain's large shapes); the shadow from the near level inside its window, the far beyond.
// A level's mode: 0 off, 1 runtime ring buffer, 2 a baked map (clamped, centred on xy) for where nothing
// is computed (mobile, low settings).
// ---------------------------------------------------------------------------------------------

struct FDreamWindL1Level
{
	float2 Center;     // world XY of the window's middle (ring buffer) or of the baked map
	float  InvExtent;  // 1 / the window's (map's) width, 1/cm
	float  Mode;       // 0 off, 1 ring buffer, 2 baked
};

struct FDreamWindL1Params
{
	float ShadowStrength;  // 0 = obstacles do not shelter, 1 = a full lee behind them
	float FadeBand;        // cm over which a point goes from the open into the lee
	float SteerScale;      // multiplies the steering
	float SpeedScale;      // 0 = ignore the speed multiplier, 1 = apply it fully
};

DW_FN FDreamWindL1Level DW_UnpackL1Level(float4 L)
{
	FDreamWindL1Level Level;
	Level.Center    = float2(L.x, L.y);
	Level.InvExtent = L.z;
	Level.Mode      = L.w;
	return Level;
}

DW_FN FDreamWindL1Params DW_UnpackL1Params(float4 L)
{
	FDreamWindL1Params P;
	P.ShadowStrength = L.x;
	P.FadeBand       = L.y;
	P.SteerScale     = L.z;
	P.SpeedScale     = L.w;
	return P;
}

/** Texture UV of a world position in a level: wrapped (ring buffer) or clamped into the baked map. */
DW_FN float2 DW_L1UV(float3 WorldPos, FDreamWindL1Level Level)
{
	if (Level.Mode > 1.5f)
	{
		return float2((WorldPos.x - Level.Center.x) * Level.InvExtent + 0.5f, (WorldPos.y - Level.Center.y) * Level.InvExtent + 0.5f);
	}
	return float2(WorldPos.x * Level.InvExtent, WorldPos.y * Level.InvExtent);
}

/** 1 well inside a level's window, fading to 0 over its outer eighth; 0 when the level is off. */
DW_FN float DW_L1Weight(float3 WorldPos, FDreamWindL1Level Level)
{
	if (Level.Mode < 0.5f)
	{
		return 0.0f;
	}
	float dx = abs(WorldPos.x - Level.Center.x) * Level.InvExtent * 2.0f;
	float dy = abs(WorldPos.y - Level.Center.y) * Level.InvExtent * 2.0f;
	float edge = max(dx, dy);
	return saturate((1.0f - edge) * 8.0f);
}

/** How much of the wind a point at height Z keeps under a wind shadow height S (1 open .. 1 - strength in the lee). */
DW_FN float DW_L1Open(float ShadowHeight, float Z, FDreamWindL1Params P)
{
	float lee = saturate((ShadowHeight - Z) / max(P.FadeBand, 1.0f));
	return 1.0f - lee * saturate(P.ShadowStrength);
}

/**
 * Everything L1 does to an analytic sample: the far texel's speed multiplier and steering (weighted by the far
 * level's coverage), then the wind shadow of the near texel inside the near window, the far one beyond it.
 */
DW_FN FDreamWindSample DW_ApplyL1(FDreamWindSample S, float3 WorldPos, float4 NearTexel, float NearWeight, float4 FarTexel, float FarWeight, FDreamWindL1Params P)
{
	if (FarWeight > 0.0f)
	{
		float speedMul = lerp(1.0f, max(FarTexel.x, 0.0f), saturate(P.SpeedScale) * FarWeight);
		float2 steer = float2(FarTexel.y, FarTexel.z) * (P.SteerScale * FarWeight);
		float2 v2 = float2(S.Velocity.x, S.Velocity.y);
		float sp = length(v2);
		if (sp > 0.001f)
		{
			float2 d = v2 * (1.0f / sp) + steer;
			float dl = length(d);
			if (dl > 0.001f)
			{
				v2 = d * (sp / dl);
			}
		}
		S.Velocity = float3(v2.x * speedMul, v2.y * speedMul, S.Velocity.z * speedMul);
	}
	float open = 1.0f;
	if (NearWeight > 0.0f || FarWeight > 0.0f)
	{
		float openFar = FarWeight > 0.0f ? lerp(1.0f, DW_L1Open(FarTexel.w, WorldPos.z, P), FarWeight) : 1.0f;
		float openNear = DW_L1Open(NearTexel.w, WorldPos.z, P);
		open = lerp(openFar, openNear, NearWeight);
	}
	S.Velocity = S.Velocity * open;
	return S;
}

/** One step of the wind shadow's sweep: a texel's S against the S of the texel Dist cm upwind, lowered by the wedge. */
DW_FN float DW_L1ShadowStep(float Here, float Upwind, float Dist, float Slope)
{
	return max(Here, Upwind - Slope * Dist);
}

#endif // DREAMWIND_SHARED_H
