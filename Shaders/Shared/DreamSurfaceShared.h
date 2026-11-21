// Copyright DreamMoon. All Rights Reserved.
//
// DreamSurfaceShared.h - how the weather wets, snows over and dusts a surface over time, compiled BOTH as HLSL and as
// C++.
//
//   HLSL:  #include "/Plugin/DreamDynamicWorld/Shared/DreamSurfaceShared.h"   (the surface map compute pass)
//   C++:   #include "DreamSurfaceHlslShim.h"  ->  DreamWind::Hlsl::DSF_*      (SampleSurface, tests)
//
// A surface's state is four amounts, each 0..1, packed in a float4:
//   x  Wet    the film of water on it: dark, glossy
//   y  Water  water standing in its hollows: the low half fills the small dips, the high half makes puddles
//   z  Snow   lying snow (0..1 of the deepest cover the settings allow)
//   w  Dust   dust or sand settled on it
// What drives it over a step, two float4s:
//   D0 = (rain landing 0..1, snow landing 0..1, air temperature in Celsius, wind 0..1)
//   D1 = (sunlight on it 0..1, dust in the air 0..1, unused, unused)
// The landing amounts are the weather's times the exposure there: under a roof nothing lands, and the shade there keeps
// the sun off too. The rates, three float4s, in game hours (DSF_Rates):
//   R0 = (wet, dry, fill, drain)            hours for a full rise or fall at full drive
//   R1 = (settle, melt, melt to water, -)   settle: hours to full snow in a full snowfall; melt: hours to melt all of
//                                           it at 10 Celsius in the sun; melt to water: how much of the melt wets
//   R2 = (dust, dust wash, dust clear, -)   dust: hours to full dust in a full dust storm; wash: by full rain; clear:
//                                           by wind and time alone
//
// The rates are linear in the step, so a long step and many short ones come out close while the drive holds still
// (they part only where one amount waits on another: the hollows on a soaked film, the water on the melt).
//
// The rules of DreamWindShared.h apply (no swizzles, no out parameters, f suffixes, only the shim's intrinsics).

#ifndef DREAMSURFACE_SHARED_H
#define DREAMSURFACE_SHARED_H

// DSF_FN prefixes every function: empty in HLSL, `inline` in C++ (DreamSurfaceHlslShim.h defines it).
#ifndef DSF_FN
#define DSF_FN
#endif

// The share of what falls that is snow: the weather's own ratio, and near freezing it turns to snow whatever the
// weather says (the weather's samples, UDreamWeatherSubsystem::MakeSample, do the same).
DSF_FN float DSF_SnowRatio(float Channel, float Temperature)
{
	return max(Channel, 1.0f - smoothstep(-1.0f, 2.0f, Temperature));
}

// How much sunlight reaches the ground for a sun this high (Z of its direction, -1..1) under this much cloud: none
// below the horizon, full from some 20 degrees up, a fifth of it under a closed cover.
DSF_FN float DSF_Sunlight(float SunZ, float CloudCoverage)
{
	return smoothstep(-0.02f, 0.35f, SunZ) * (1.0f - 0.8f * saturate(CloudCoverage));
}

// How fast a surface dries, 0..: the sun and the wind dry it, the warmth helps, and while rain keeps falling on it
// nothing dries.
DSF_FN float DSF_DryFactor(float4 D0, float4 D1)
{
	float Warm = clamp((D0.z + 10.0f) / 30.0f, 0.1f, 1.5f);
	float Air = (0.25f + 0.75f * saturate(D1.x)) * (1.0f + saturate(D0.w));
	return Warm * Air * (1.0f - saturate(D0.x * 20.0f));
}

// How much of the lying snow melts over Hours (0..1 of a full cover): above freezing, faster in the sun and the rain.
DSF_FN float DSF_Melt(float4 D0, float4 D1, float4 R1, float Hours)
{
	float Degrees = max(D0.z, 0.0f);
	float Sun = 0.5f + saturate(D1.x);
	float Rain = 1.0f + 4.0f * saturate(D0.x);
	return Hours * Degrees / (10.0f * max(R1.y, 1.0e-3f)) * Sun * Rain / 1.5f;
}

// The state after Hours of this weather.
DSF_FN float4 DSF_Step(float4 S, float4 D0, float4 D1, float4 R0, float4 R1, float4 R2, float Hours)
{
	float Rain = saturate(D0.x);
	float SnowFall = saturate(D0.y);
	float Dry = DSF_DryFactor(D0, D1) * Hours;

	// Snow first: what melts runs into the film and the hollows.
	float SettleWarm = 1.0f - smoothstep(0.5f, 3.0f, D0.z); // warm ground melts the flakes as they land
	float SnowIn = saturate(S.z + SnowFall * SettleWarm * Hours / max(R1.x, 1.0e-3f));
	float Melted = min(SnowIn, DSF_Melt(D0, D1, R1, Hours));
	float Snow = SnowIn - Melted;
	float MeltWater = Melted * R1.z;

	// The film wets fast and dries by the weather (not under lying snow); the hollows fill once the film is soaked and
	// drain slower.
	float Wet = saturate(S.x + Rain * Hours / max(R0.x, 1.0e-3f) + MeltWater - Dry * (1.0f - Snow) / max(R0.y, 1.0e-3f));
	float Soaked = saturate((Wet - 0.8f) * 5.0f);
	float Water = saturate(S.y + (Rain * Soaked * Hours + MeltWater * 0.5f) / max(R0.z, 1.0e-3f) - Dry / max(R0.w, 1.0e-3f));

	// Dust settles from a dusty air, a little from dry wind, and rain washes it off; it clears on its own slowly.
	float DustIn = saturate(D1.y) * Hours / max(R2.x, 1.0e-3f) + saturate(D0.w) * (1.0f - Wet) * 0.1f * Hours / max(R2.x, 1.0e-3f);
	float DustOut = Rain * Hours / max(R2.y, 1.0e-3f) + Hours / max(R2.z, 1.0e-3f);
	float Dust = saturate(S.w + DustIn - DustOut);

	return float4(Wet, Water, Snow, Dust);
}

// A step's drive in the open from the weather there (what the surface map does per texel; a query's DriveAt gets the
// same from the weather's sample): Weather = (cloud coverage, -, precipitation after the cloud gate, snow ratio
// channel), Air = (temperature, wind, dust, the sun's Z).
DSF_FN float4 DSF_OpenD0(float4 Weather, float4 Air)
{
	float Ratio = DSF_SnowRatio(Weather.w, Air.x);
	return float4(Weather.z * (1.0f - Ratio), Weather.z * Ratio, Air.x, Air.y);
}

DSF_FN float4 DSF_OpenD1(float4 Weather, float4 Air)
{
	return float4(DSF_Sunlight(Air.w, Weather.x), Air.z, 0.0f, 0.0f);
}

// The water a point holds at a hollow this deep (0 on a crest .. 1 at the bottom of a dip): how much of a puddle stands
// there, 0..1. The low half of Water fills the deepest dips, the high half spreads to shallower ones.
DSF_FN float DSF_PuddleAt(float Water, float Hollow, float Softness)
{
	float Level = 1.0f - Water;
	return smoothstep(Level - Softness, Level + Softness, saturate(Hollow));
}

#endif // DREAMSURFACE_SHARED_H
