// Copyright DreamMoon. All Rights Reserved.
//
// DreamWeatherShared.h - how a weather zone weighs a point, compiled BOTH as HLSL and as C++.
//
//   HLSL:  #include "/Plugin/DreamDynamicWorld/Shared/DreamWeatherShared.h"   (the weather and surface map passes)
//   C++:   #include "DreamWeatherHlslShim.h"  ->  DreamWind::Hlsl::DWX_*      (point queries, tests)
//
// The weather map the sky reads, the surface map the ground reads and a gameplay query at a point use the same
// footprint and falloff, so a storm zone that darkens the sky over a valley is exactly where SampleWeather reports the
// storm and where the ground gets wet.
//
// The rules of DreamWindShared.h apply (no swizzles, no out parameters, f suffixes, only the shim's intrinsics).

#ifndef DREAMWEATHER_SHARED_H
#define DREAMWEATHER_SHARED_H

// DWX_FN prefixes every function: empty in HLSL, `inline` in C++ (DreamWeatherHlslShim.h defines it).
#ifndef DWX_FN
#define DWX_FN
#endif

#define DWX_SHAPE_BOX     0
#define DWX_SHAPE_CIRCLE  1
#define DWX_SHAPE_POLYGON 2

// The most points a polygon footprint has (a longer outline is thinned to this many).
#define DWX_MAX_POLYGON_POINTS 64

// Where a polygon's points are read from: the zones' own buffer in HLSL (the points follow the records), a pointer to
// FVector4f in C++ (DreamWeatherHlslShim.h defines it).
#ifndef DWX_POINTS
#define DWX_POINTS StructuredBuffer<float4>
#endif

// A zone on the GPU is three float4s:
//   Z0 = (centre x, centre y, cos yaw, sin yaw)
//   Z1 = (half extent x, half extent y (a circle uses x as its radius), blend distance, shape)
//        a polygon: (index of its first point, number of points, blend distance, shape)
//   Z2 = (cloud coverage, cloud darkness, precipitation, weight) - a channel below -0.5 is one the zone leaves alone
// A polygon's points are float4s (x, y in the zone's frame, cm; the first point's z is the distance of the farthest
// point from the centre), in order around the outline (either way round), the last joined back to the first.
#define DWX_ZONE_FLOAT4S 3

// Signed distance from a point in the zone's frame to a polygon outline: negative inside, cm. Each edge counts the
// crossings of a ray from the point (the sign) and the nearest point on it (the distance).
DWX_FN float DWX_PolygonSignedDistance(float lx, float ly, DWX_POINTS Points, int First, int Count)
{
	float4 Last = Points[First + Count - 1];
	float jx = Last.x;
	float jy = Last.y;
	float d = 1.0e30f;
	float s = 1.0f;
	for (int k = 0; k < Count; ++k)
	{
		float4 V = Points[First + k];
		float ex = jx - V.x;
		float ey = jy - V.y;
		float wx = lx - V.x;
		float wy = ly - V.y;
		float t = clamp((wx * ex + wy * ey) / max(ex * ex + ey * ey, 1.0e-6f), 0.0f, 1.0f);
		float bx = wx - ex * t;
		float by = wy - ey * t;
		d = min(d, bx * bx + by * by);
		bool c0 = ly >= V.y;
		bool c1 = ly < jy;
		bool c2 = ex * wy > ey * wx;
		if ((c0 && c1 && c2) || (!c0 && !c1 && !c2))
		{
			s = -s;
		}
		jx = V.x;
		jy = V.y;
	}
	return s * sqrt(d);
}

// Signed horizontal distance from a point to the zone's footprint: negative inside, cm. Points: where a polygon's
// points are (box and circle do not read it). Farther than the blend distance from a polygon's farthest point it
// gives that bound instead (the weight there is nothing either way).
DWX_FN float DWX_ZoneSignedDistance(float2 P, float4 Z0, float4 Z1, DWX_POINTS Points)
{
	float dx = P.x - Z0.x;
	float dy = P.y - Z0.y;
	// into the zone's frame: rotate by -yaw
	float lx = dx * Z0.z + dy * Z0.w;
	float ly = -dx * Z0.w + dy * Z0.z;
	float r = sqrt(lx * lx + ly * ly);
	if (Z1.w > 1.5f)
	{
		int First = (int)Z1.x;
		int Count = min((int)Z1.y, DWX_MAX_POLYGON_POINTS);
		if (Count < 3)
		{
			return 1.0e30f;
		}
		float Bound = r - Points[First].z;
		if (Bound >= max(Z1.z, 1.0f))
		{
			return Bound;
		}
		return DWX_PolygonSignedDistance(lx, ly, Points, First, Count);
	}
	if (Z1.w > 0.5f)
	{
		return r - Z1.x;
	}
	float qx = abs(lx) - Z1.x;
	float qy = abs(ly) - Z1.y;
	float ox = max(qx, 0.0f);
	float oy = max(qy, 0.0f);
	return sqrt(ox * ox + oy * oy) + min(max(qx, qy), 0.0f);
}

// How much the zone has its way at a point: Weight inside, fading to nothing BlendDistance outside.
DWX_FN float DWX_ZoneWeight(float2 P, float4 Z0, float4 Z1, float Weight, DWX_POINTS Points)
{
	float d = DWX_ZoneSignedDistance(P, Z0, Z1, Points);
	return Weight * (1.0f - smoothstep(0.0f, max(Z1.z, 1.0f), d));
}

// A zone pulls a channel toward its own value by its weight; a value below -0.5 means "not set here".
DWX_FN float DWX_ApplyChannel(float Current, float ZoneValue, float W)
{
	return ZoneValue < -0.5f ? Current : Current + (ZoneValue - Current) * W;
}

// The three channels the weather map carries (coverage, darkness, precipitation) after a zone that has weight W here.
DWX_FN float3 DWX_ApplyZoneWeight(float3 V, float4 Z2, float W)
{
	return float3(DWX_ApplyChannel(V.x, Z2.x, W), DWX_ApplyChannel(V.y, Z2.y, W), DWX_ApplyChannel(V.z, Z2.z, W));
}

// ...after one zone.
DWX_FN float3 DWX_ApplyZone(float3 V, float2 P, float4 Z0, float4 Z1, float4 Z2, DWX_POINTS Points)
{
	return DWX_ApplyZoneWeight(V, Z2, DWX_ZoneWeight(P, Z0, Z1, Z2.w, Points));
}

// How much of a channel the clouds let through: nothing below Lo, all of it from Hi (Hi <= Lo: no limit). Rain needs
// a closed cloud cover and lightning a dark one, whatever the channels say on their own: a change of weather that
// blends every channel at once would otherwise rain from a sky already breaking up, or flash from a clearing one.
DWX_FN float DWX_CloudAllows(float Cloud, float Lo, float Hi)
{
	return Hi > Lo ? smoothstep(Lo, Hi, Cloud) : 1.0f;
}

// The map's three channels with the precipitation held to what the cover there allows (Gate = cover lo, hi).
DWX_FN float3 DWX_GatePrecipitation(float3 V, float2 Gate)
{
	return float3(V.x, V.y, V.z * DWX_CloudAllows(V.x, Gate.x, Gate.y));
}

#endif // DREAMWEATHER_SHARED_H
