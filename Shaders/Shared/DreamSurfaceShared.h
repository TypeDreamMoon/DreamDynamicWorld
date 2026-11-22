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
//   y  Water  water standing in its hollows: rising, it fills the deepest dips first, then spreads into puddles
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

// How fast a surface dries, 0..: the sun and the wind dry it, the warmth helps, and while rain or snow keeps falling on it
// nothing dries.
DSF_FN float DSF_DryFactor(float4 D0, float4 D1)
{
	float Warm = clamp((D0.z + 10.0f) / 30.0f, 0.1f, 1.5f);
	float Air = (0.25f + 0.75f * saturate(D1.x)) * (1.0f + saturate(D0.w));
	return Warm * Air * (1.0f - saturate((D0.x + D0.y) * 20.0f));
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

	// Snow first: what melts runs into the film and the hollows. Warm ground melts the flakes as they land: what does not
	// settle wets the surface as rain would.
	float Settle = 1.0f - smoothstep(0.5f, 3.0f, D0.z);
	float SnowIn = saturate(S.z + SnowFall * Settle * Hours / max(R1.x, 1.0e-3f));
	float Melted = min(SnowIn, DSF_Melt(D0, D1, R1, Hours));
	float Snow = SnowIn - Melted;
	float MeltWater = Melted * R1.z;
	Rain = saturate(Rain + SnowFall * (1.0f - Settle));

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

// How far the ground at a point lies below its surroundings, cm (positive in a dip, negative on a rise), from its height
// and the heights at eight points around it (A, B: four each). Whatever stands much higher than the point (a wall, a
// box, a post) is left out: it does not make the ground beside it a dip.
DSF_FN float DSF_GroundLow(float Here, float4 A, float4 B)
{
	float Sum = 0.0f;
	float Count = 0.0f;
	float Around[8] = { A.x, A.y, A.z, A.w, B.x, B.y, B.z, B.w };
	for (int i = 0; i < 8; ++i)
	{
		float Rise = Around[i] - Here;
		if (Rise < 80.0f)
		{
			Sum += clamp(Rise, -40.0f, 40.0f);
			Count += 1.0f;
		}
	}
	return Count > 0.0f ? Sum / Count : 0.0f;
}

// How deep a point lies for the water, 0 (a crest) .. 1 (the bottom of a dip): the small hollows of the surface (Hollow,
// the hollow pattern at the puddle scale) and the ground's own lows (Low: how far the ground lies below its surroundings,
// cm, from the level's heights; 0 where they are not known). A dip a quarter of a metre deep holds water whatever the
// pattern; on flat ground only the pattern's deepest hollows do.
DSF_FN float DSF_PuddleDepth(float Hollow, float Low)
{
	return saturate(saturate(Hollow) * 0.8f + saturate(Low / 25.0f) * 0.7f);
}

// The water a point holds at this depth (DSF_PuddleDepth): how much of a puddle stands there, 0..1. Rising Water fills
// the deepest first and spreads to shallower places; full, on flat ground it stands in the pattern's deepest hollows
// only (a tenth or so of the surface), in a real dip all over it.
DSF_FN float DSF_PuddleAt(float Water, float Depth, float Softness)
{
	// Dry, the level stands above the deepest point; full, at 0.58.
	float Level = 1.0f + Softness - (0.42f + Softness) * saturate(Water);
	return smoothstep(Level - Softness, Level + Softness, saturate(Depth));
}

// A hash of one cell's own values, 0..1 (a drop's place, a ring's start): each cell asks it about itself only.
DSF_FN float DSF_Hash(float X, float Y)
{
	return frac(sin(X * 127.1f + Y * 311.7f) * 43758.5453f);
}

// A hash of a lattice point, 0..1, in integer arithmetic. Value noise asks it about a corner from each cell sharing it,
// so the answers must agree to the bit: a float hash (DSF_Hash) does not, as the compiler may round (IY + 1) * k one way
// in one cell and another in the next, which its large multiplier turns into a different value - a seam along every
// lattice line.
DSF_FN float DSF_LatticeHash(int X, int Y, uint Seed)
{
	uint H = uint(X) * 0x9E3779B1u + Seed;
	H = (H ^ (H >> 16u)) * 0x7feb352du;
	H = (H ^ (H >> 15u)) * 0x846ca68bu;
	H = H ^ (H >> 16u) ^ (uint(Y) * 0x85EBCA77u);
	H = (H ^ (H >> 16u)) * 0x7feb352du;
	H = (H ^ (H >> 15u)) * 0x846ca68bu;
	H = H ^ (H >> 16u);
	return float(H & 0x00ffffffu) * (1.0f / 16777216.0f);
}

// The ground's hollow pattern at a world point (cm): 1 at the bottom of a dip, 0 on a crest. Two octaves of value
// noise Scale cm across: where the standing water gathers first (DSF_PuddleAt) and where snow lies thinnest last.
DSF_FN float DSF_Hollow(float WorldX, float WorldY, float Scale)
{
	float X = WorldX / max(Scale, 1.0f);
	float Y = WorldY / max(Scale, 1.0f);
	float N = 0.0f;
	float A = 0.65f;
	for (int Octave = 0; Octave < 2; ++Octave)
	{
		float IX = floor(X);
		float IY = floor(Y);
		float FX = X - IX;
		float FY = Y - IY;
		float UX = FX * FX * (3.0f - 2.0f * FX);
		float UY = FY * FY * (3.0f - 2.0f * FY);
		int CX = int(IX);
		int CY = int(IY);
		uint Seed = uint(Octave) * 0x632BE5ABu;
		float Bottom = lerp(DSF_LatticeHash(CX, CY, Seed), DSF_LatticeHash(CX + 1, CY, Seed), UX);
		float Top = lerp(DSF_LatticeHash(CX, CY + 1, Seed), DSF_LatticeHash(CX + 1, CY + 1, Seed), UX);
		N += A * lerp(Bottom, Top, UY);
		X = X * 2.03f + 17.0f;
		Y = Y * 2.03f + 17.0f;
		A *= 0.5f;
	}
	return saturate(1.0f - N / 0.975f);
}

// The pattern a snow cover's edge follows, 0..1: patches of some ten metres over drifts of some thirty, so where the
// snow thins out (a zone's border, a light fall, a thaw) it breaks up over metres to tens of metres, not along a line.
DSF_FN float DSF_SnowEdge(float WorldX, float WorldY)
{
	return DSF_Hollow(WorldX, WorldY, 900.0f) * 0.6f + DSF_Hollow(WorldX + 311.0f, WorldY - 97.0f, 3200.0f) * 0.4f;
}

// How much of a surface the lying snow covers, softly: what faces up first (Up = the normal's Z), steep slopes last,
// the small hollows (Hollow) and the edge pattern (Edge, DSF_SnowEdge) breaking it up as it thins. A full cover is
// whole on anything facing up; a toon look hardens the edge.
DSF_FN float DSF_SnowCover(float Snow, float Up, float Hollow, float Edge)
{
	return saturate((Snow * 1.4f - (1.0f - Up) * 1.6f - (1.0f - Hollow) * 0.15f - (1.0f - Edge) * 0.45f) * 2.5f);
}

// How much of a surface the standing water covers: where it lies deep enough (Depth, DSF_PuddleDepth) on what faces up,
// faint on soft ground (Soft 0 hard .. 1 soil or grass, which drinks the water and hides it in its texture).
DSF_FN float DSF_PuddleCover(float Water, float Up, float Depth, float Soft)
{
	return DSF_PuddleAt(Water, Depth, 0.05f) * smoothstep(0.88f, 0.97f, Up) * (1.0f - 0.7f * saturate(Soft));
}

// ---------------------------------------------------------------------------------------------------------------------
// Rain on glass (MF_DreamGlassRain). Glass = (across, up the pane) in cm.

// One layer of drops sitting on glass: one per cell of Scale cm at most, as many as Amount (0..1) brings, each a small
// dome. While it rains (Cycle > 0) a drop comes and goes over some seconds; once it stops they stay as they are. Clear
// (0..1) wipes them where a running drop went by. Returns the dome's slope (x, y) and how much of the glass is water (z).
DSF_FN float3 DSF_GlassBeads(float2 Glass, float Time, float Amount, float Cycle, float Clear, float Scale, float Seed)
{
	float2 Q = Glass * (1.0f / Scale);
	float2 C = floor(Q);
	float Here = saturate((Amount - DSF_Hash(C.x + Seed, C.y + 17.0f)) * 6.0f) * (1.0f - Clear);
	if (Here <= 0.0f)
	{
		return float3(0.0f, 0.0f, 0.0f);
	}
	float Age = frac(Time * 0.12f + DSF_Hash(C.x + 4.1f, C.y + Seed));
	float Life = smoothstep(0.0f, 0.05f, Age) * (1.0f - smoothstep(0.75f, 1.0f, Age));
	Here = Here * lerp(1.0f, Life, Cycle);
	// Anywhere in its cell it fits in, so the drops show no grid.
	float R = 0.12f + 0.2f * DSF_Hash(C.x + 8.9f, C.y + Seed + 3.0f);
	float2 Centre = C + R + (1.0f - 2.0f * R) * float2(DSF_Hash(C.x + 2.3f, C.y + Seed), DSF_Hash(C.x + Seed, C.y + 6.1f));
	float2 D = (Q - Centre) * (1.0f / R);
	float Dist2 = dot(D, D);
	if (Dist2 >= 1.0f)
	{
		return float3(0.0f, 0.0f, 0.0f);
	}
	// A dome's height is 1 - d²: its slope points to the centre, steepest at the rim.
	float Edge = 1.0f - smoothstep(0.7f, 1.0f, Dist2);
	float K = 1.6f * Here * Edge;
	return float3(-D.x * K, -D.y * K, Here * Edge);
}

// Drops running down a pane: columns Width cm wide, each with a drop now and then (more columns the harder it rains)
// that slides down at its own speed along a slowly wandering path, a thin wet trail and a few beads behind it. Returns
// the slope (x, y), how much is water (z) and where it went by (w: to clear the sitting drops there).
DSF_FN float4 DSF_GlassRunners(float2 Glass, float Time, float Rain, float Width)
{
	float Column = floor(Glass.x / Width);
	float Running = saturate((Rain - DSF_Hash(Column + 11.0f, 5.0f)) * 3.0f);
	if (Running <= 0.0f)
	{
		return float4(0.0f, 0.0f, 0.0f, 0.0f);
	}
	float Speed = 6.0f + 14.0f * DSF_Hash(Column, 23.0f);
	float Period = 50.0f + 90.0f * DSF_Hash(Column + 3.0f, 41.0f);
	float Size = 0.35f + 0.3f * DSF_Hash(Column + 7.0f, 13.0f);
	// The path wanders across the column as it goes down: a function of the height, so the trail follows the drop.
	float Path = (Column + 0.5f) * Width + sin(Glass.y * 0.21f + DSF_Hash(Column, 3.0f) * 6.28f) * Width * 0.18f
		+ sin(Glass.y * 0.57f + DSF_Hash(Column, 9.0f) * 6.28f) * Width * 0.07f;
	float Across = Glass.x - Path;
	// Height above the drop's head, 0..Period: the head moves down, so a point's height above it grows with time.
	float Above = frac((Glass.y + Time * Speed) / Period + DSF_Hash(Column, 31.0f)) * Period;

	// The head: a drop a little longer than wide.
	float HX = Across / Size;
	float HY = (Above - Size * 1.2f) / (Size * 1.4f);
	float HeadDist2 = HX * HX + HY * HY;
	float Head = HeadDist2 < 1.0f ? (1.0f - smoothstep(0.6f, 1.0f, HeadDist2)) : 0.0f;
	float SlopeX = -HX * 2.0f * Head;
	float SlopeY = -HY * 2.0f * Head;

	// The trail: thin, narrowing and fading with height, a bead left now and then.
	float TrailLength = 12.0f + 20.0f * DSF_Hash(Column + 5.0f, 2.0f);
	float Behind = Above - Size * 2.0f;
	float Along = saturate(Behind / TrailLength);
	float InTrail = (Behind > 0.0f && Along < 1.0f) ? 1.0f : 0.0f;
	float TrailWidth = max(Size * 0.45f * (1.0f - Along), 0.01f);
	float Trail = InTrail * (1.0f - smoothstep(TrailWidth * 0.5f, TrailWidth, abs(Across))) * (1.0f - Along);
	float BeadSize = max(Size * 0.4f * (1.0f - Along), 0.05f);
	float BX = Across / BeadSize;
	float BY = (frac(Behind / 1.6f) - 0.5f) * 1.6f / BeadSize;
	float BeadDist2 = BX * BX + BY * BY;
	float Bead = (InTrail > 0.0f && BeadDist2 < 1.0f && DSF_Hash(Column + floor(Behind / 1.6f), 77.0f) > 0.55f)
		? (1.0f - smoothstep(0.6f, 1.0f, BeadDist2)) * (1.0f - Along) : 0.0f;
	SlopeX += -BX * 1.2f * Bead - sign(Across) * Trail * 0.6f;
	SlopeY += -BY * 1.2f * Bead;

	float Water = max(Head, max(Trail * 0.6f, Bead));
	float Swept = InTrail * (1.0f - smoothstep(Size * 0.8f, Size * 1.4f, abs(Across))) * (1.0f - Along * Along);
	return float4(SlopeX * Running, SlopeY * Running, Water * Running, max(Swept, Head) * Running);
}

// Rain on a pane of glass. Rain = the rain landing there now; Wet = the wetness left there (the state's x, under
// cover), so the drops stay after the rain and go as the surface dries; Steep = how upright the pane is (0 flat ..
// 1 upright: on a flat pane nothing runs). Returns the slope of the water over the glass (x across, y up; a normal on
// the pane bends by -slope), how much of it is water (z) and the running drops (w, their heads and trails).
DSF_FN float4 DSF_GlassRain(float2 Glass, float Time, float Rain, float Wet, float Steep)
{
	float Amount = max(Rain * 0.9f, Wet * 0.75f);
	if (Amount <= 0.0f)
	{
		return float4(0.0f, 0.0f, 0.0f, 0.0f);
	}
	float4 Run = float4(0.0f, 0.0f, 0.0f, 0.0f);
	if (Steep > 0.0f && Rain > 0.0f)
	{
		Run = DSF_GlassRunners(Glass, Time, Rain * Steep, 2.5f);
	}
	float Cycle = saturate(Rain * 4.0f);
	float3 Big = DSF_GlassBeads(Glass, Time, Amount * 0.8f, Cycle, Run.w, 1.4f, 19.0f);
	float3 Small = DSF_GlassBeads(Glass + 0.37f, Time * 1.3f, Amount, Cycle, Run.w, 0.55f, 53.0f);
	return float4(Run.x + Big.x + Small.x, Run.y + Big.y + Small.y, saturate(max(Run.z, max(Big.z, Small.z))), saturate(Run.z));
}

#endif // DREAMSURFACE_SHARED_H
