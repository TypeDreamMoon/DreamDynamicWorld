// Copyright DreamMoon. All Rights Reserved.
//
// DreamWeatherShared.h - how a weather zone weighs a point, compiled BOTH as HLSL and as C++.
//
//   HLSL:  #include "/Plugin/DreamDynamicWorld/Shared/DreamWeatherShared.h"   (the weather map compute pass)
//   C++:   #include "DreamWeatherHlslShim.h"  ->  DreamWind::Hlsl::DWX_*      (point queries, tests)
//
// The weather map the sky reads and a gameplay query at a point use the same falloff, so a storm zone that
// darkens the sky over a valley is exactly where SampleWeather reports the storm.
//
// The rules of DreamWindShared.h apply (no swizzles, no out parameters, f suffixes, only the shim's intrinsics).

#ifndef DREAMWEATHER_SHARED_H
#define DREAMWEATHER_SHARED_H

// DWX_FN prefixes every function: empty in HLSL, `inline` in C++ (DreamWeatherHlslShim.h defines it).
#ifndef DWX_FN
#define DWX_FN
#endif

#define DWX_SHAPE_BOX    0
#define DWX_SHAPE_CIRCLE 1

// A zone on the GPU is three float4s:
//   Z0 = (centre x, centre y, cos yaw, sin yaw)
//   Z1 = (half extent x, half extent y (a circle uses x as its radius), blend distance, shape)
//   Z2 = (cloud coverage, cloud darkness, precipitation, weight) - a channel below -0.5 is one the zone leaves alone
#define DWX_ZONE_FLOAT4S 3

// Signed horizontal distance from a point to the zone's footprint: negative inside, cm.
DWX_FN float DWX_ZoneSignedDistance(float2 P, float4 Z0, float4 Z1)
{
	float dx = P.x - Z0.x;
	float dy = P.y - Z0.y;
	// into the zone's frame: rotate by -yaw
	float lx = dx * Z0.z + dy * Z0.w;
	float ly = -dx * Z0.w + dy * Z0.z;
	if (Z1.w > 0.5f)
	{
		return sqrt(lx * lx + ly * ly) - Z1.x;
	}
	float qx = abs(lx) - Z1.x;
	float qy = abs(ly) - Z1.y;
	float ox = max(qx, 0.0f);
	float oy = max(qy, 0.0f);
	return sqrt(ox * ox + oy * oy) + min(max(qx, qy), 0.0f);
}

// How much the zone has its way at a point: Weight inside, fading to nothing BlendDistance outside.
DWX_FN float DWX_ZoneWeight(float2 P, float4 Z0, float4 Z1, float Weight)
{
	float d = DWX_ZoneSignedDistance(P, Z0, Z1);
	return Weight * (1.0f - smoothstep(0.0f, max(Z1.z, 1.0f), d));
}

// A zone pulls a channel toward its own value by its weight; a value below -0.5 means "not set here".
DWX_FN float DWX_ApplyChannel(float Current, float ZoneValue, float W)
{
	return ZoneValue < -0.5f ? Current : Current + (ZoneValue - Current) * W;
}

// The three channels the weather map carries (coverage, darkness, precipitation) after one zone.
DWX_FN float3 DWX_ApplyZone(float3 V, float2 P, float4 Z0, float4 Z1, float4 Z2)
{
	float w = DWX_ZoneWeight(P, Z0, Z1, Z2.w);
	return float3(DWX_ApplyChannel(V.x, Z2.x, w), DWX_ApplyChannel(V.y, Z2.y, w), DWX_ApplyChannel(V.z, Z2.z, w));
}

#endif // DREAMWEATHER_SHARED_H
