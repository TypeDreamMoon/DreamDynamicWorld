// Copyright DreamMoon. All Rights Reserved.
//
// DreamWindHlslShim.h - just enough HLSL (types + intrinsics) to compile Shaders/Shared/DreamWindShared.h
// as C++. Everything lives in DreamWind::Hlsl so it can never collide with engine math.
//
// Usage:
//   #include "DreamWindHlslShim.h"
//   using namespace DreamWind::Hlsl;   // or qualify
//   FDreamWindSample S = DW_SampleAnalytic(Params, ToHlsl(WorldPos));

#pragma once

#include "CoreMinimal.h"
#include <cmath>

namespace DreamWind
{
namespace Hlsl
{
	typedef unsigned int uint;

	struct float2
	{
		float x, y;
		float2() : x(0.f), y(0.f) {}
		explicit float2(float s) : x(s), y(s) {}
		float2(float InX, float InY) : x(InX), y(InY) {}
	};

	struct float3
	{
		float x, y, z;
		float3() : x(0.f), y(0.f), z(0.f) {}
		explicit float3(float s) : x(s), y(s), z(s) {}
		float3(float InX, float InY, float InZ) : x(InX), y(InY), z(InZ) {}
	};

	struct float4
	{
		float x, y, z, w;
		float4() : x(0.f), y(0.f), z(0.f), w(0.f) {}
		explicit float4(float s) : x(s), y(s), z(s), w(s) {}
		float4(float InX, float InY, float InZ, float InW) : x(InX), y(InY), z(InZ), w(InW) {}
	};

	struct int2
	{
		int x, y;
		int2() : x(0), y(0) {}
		int2(int InX, int InY) : x(InX), y(InY) {}
		int2(float InX, float InY) : x(int(InX)), y(int(InY)) {}
	};

	// ---- component-wise operators -------------------------------------------------------------
	#define DW_SHIM_VEC2_OPS(OP) \
		inline float2 operator OP (float2 a, float2 b) { return float2(a.x OP b.x, a.y OP b.y); } \
		inline float2 operator OP (float2 a, float s)  { return float2(a.x OP s, a.y OP s); } \
		inline float2 operator OP (float s, float2 a)  { return float2(s OP a.x, s OP a.y); }
	#define DW_SHIM_VEC3_OPS(OP) \
		inline float3 operator OP (float3 a, float3 b) { return float3(a.x OP b.x, a.y OP b.y, a.z OP b.z); } \
		inline float3 operator OP (float3 a, float s)  { return float3(a.x OP s, a.y OP s, a.z OP s); } \
		inline float3 operator OP (float s, float3 a)  { return float3(s OP a.x, s OP a.y, s OP a.z); }
	#define DW_SHIM_VEC4_OPS(OP) \
		inline float4 operator OP (float4 a, float4 b) { return float4(a.x OP b.x, a.y OP b.y, a.z OP b.z, a.w OP b.w); } \
		inline float4 operator OP (float4 a, float s)  { return float4(a.x OP s, a.y OP s, a.z OP s, a.w OP s); } \
		inline float4 operator OP (float s, float4 a)  { return float4(s OP a.x, s OP a.y, s OP a.z, s OP a.w); }

	DW_SHIM_VEC2_OPS(+) DW_SHIM_VEC2_OPS(-) DW_SHIM_VEC2_OPS(*) DW_SHIM_VEC2_OPS(/)
	DW_SHIM_VEC3_OPS(+) DW_SHIM_VEC3_OPS(-) DW_SHIM_VEC3_OPS(*) DW_SHIM_VEC3_OPS(/)
	DW_SHIM_VEC4_OPS(+) DW_SHIM_VEC4_OPS(-) DW_SHIM_VEC4_OPS(*) DW_SHIM_VEC4_OPS(/)

	#undef DW_SHIM_VEC2_OPS
	#undef DW_SHIM_VEC3_OPS
	#undef DW_SHIM_VEC4_OPS

	inline float2 operator-(float2 a) { return float2(-a.x, -a.y); }
	inline float3 operator-(float3 a) { return float3(-a.x, -a.y, -a.z); }
	inline float4 operator-(float4 a) { return float4(-a.x, -a.y, -a.z, -a.w); }

	inline float2& operator+=(float2& a, float2 b) { a = a + b; return a; }
	inline float3& operator+=(float3& a, float3 b) { a = a + b; return a; }
	inline float2& operator*=(float2& a, float s) { a = a * s; return a; }
	inline float3& operator*=(float3& a, float s) { a = a * s; return a; }

	// ---- scalar intrinsics -------------------------------------------------------------------
	inline float abs(float x)               { return std::fabs(x); }
	inline float floor(float x)             { return std::floor(x); }
	inline float ceil(float x)              { return std::ceil(x); }
	inline float frac(float x)              { return x - std::floor(x); }
	inline float sqrt(float x)              { return std::sqrt(x); }
	inline float rsqrt(float x)             { return 1.0f / std::sqrt(x); }
	inline float sin(float x)               { return std::sin(x); }
	inline float cos(float x)               { return std::cos(x); }
	inline float exp(float x)               { return std::exp(x); }
	inline float exp2(float x)              { return std::exp2(x); }
	inline float pow(float x, float y)      { return std::pow(x, y); }
	inline float fmod(float x, float y)     { return std::fmod(x, y); }
	inline float min(float a, float b)      { return a < b ? a : b; }
	inline float max(float a, float b)      { return a > b ? a : b; }
	inline int   min(int a, int b)          { return a < b ? a : b; }
	inline int   max(int a, int b)          { return a > b ? a : b; }
	inline float clamp(float x, float a, float b) { return x < a ? a : (x > b ? b : x); }
	inline float saturate(float x)          { return clamp(x, 0.0f, 1.0f); }
	inline float lerp(float a, float b, float t) { return a + (b - a) * t; }
	inline float step(float edge, float x)  { return x >= edge ? 1.0f : 0.0f; }
	inline float sign(float x)              { return x > 0.0f ? 1.0f : (x < 0.0f ? -1.0f : 0.0f); }
	inline float smoothstep(float a, float b, float x)
	{
		const float t = saturate((x - a) / (b - a));
		return t * t * (3.0f - 2.0f * t);
	}

	// ---- vector intrinsics -------------------------------------------------------------------
	inline float2 abs(float2 v)   { return float2(abs(v.x), abs(v.y)); }
	inline float3 abs(float3 v)   { return float3(abs(v.x), abs(v.y), abs(v.z)); }
	inline float2 floor(float2 v) { return float2(floor(v.x), floor(v.y)); }
	inline float3 floor(float3 v) { return float3(floor(v.x), floor(v.y), floor(v.z)); }
	inline float2 frac(float2 v)  { return float2(frac(v.x), frac(v.y)); }
	inline float3 frac(float3 v)  { return float3(frac(v.x), frac(v.y), frac(v.z)); }
	inline float2 min(float2 a, float2 b) { return float2(min(a.x, b.x), min(a.y, b.y)); }
	inline float3 min(float3 a, float3 b) { return float3(min(a.x, b.x), min(a.y, b.y), min(a.z, b.z)); }
	inline float2 max(float2 a, float2 b) { return float2(max(a.x, b.x), max(a.y, b.y)); }
	inline float3 max(float3 a, float3 b) { return float3(max(a.x, b.x), max(a.y, b.y), max(a.z, b.z)); }
	inline float2 saturate(float2 v) { return float2(saturate(v.x), saturate(v.y)); }
	inline float3 saturate(float3 v) { return float3(saturate(v.x), saturate(v.y), saturate(v.z)); }
	inline float2 lerp(float2 a, float2 b, float t) { return a + (b - a) * t; }
	inline float3 lerp(float3 a, float3 b, float t) { return a + (b - a) * t; }
	inline float4 lerp(float4 a, float4 b, float t) { return a + (b - a) * t; }
	inline float dot(float2 a, float2 b) { return a.x * b.x + a.y * b.y; }
	inline float dot(float3 a, float3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
	inline float dot(float4 a, float4 b) { return a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w; }
	inline float3 cross(float3 a, float3 b)
	{
		return float3(a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x);
	}
	inline float length(float2 v) { return sqrt(dot(v, v)); }
	inline float length(float3 v) { return sqrt(dot(v, v)); }
	inline float2 normalize(float2 v) { const float l = length(v); return l > 0.f ? v * (1.0f / l) : v; }
	inline float3 normalize(float3 v) { const float l = length(v); return l > 0.f ? v * (1.0f / l) : v; }

	// ---- the shared model ---------------------------------------------------------------------
	#define DW_FN inline
	#include "DreamWindShared.h"

	// ---- conversions to and from engine math ------------------------------------------------
	inline float3 ToHlsl(const FVector& V)    { return float3(float(V.X), float(V.Y), float(V.Z)); }
	inline float3 ToHlsl(const FVector3f& V)  { return float3(V.X, V.Y, V.Z); }
	inline float4 ToHlsl(const FVector4f& V)  { return float4(V.X, V.Y, V.Z, V.W); }
	inline float4 ToHlsl(const FLinearColor& C) { return float4(C.R, C.G, C.B, C.A); }
	inline FVector   ToVector(float3 V)       { return FVector(V.x, V.y, V.z); }
	inline FVector3f ToVector3f(float3 V)     { return FVector3f(V.x, V.y, V.z); }
	inline FVector4f ToVector4f(float4 V)     { return FVector4f(V.x, V.y, V.z, V.w); }
	inline FLinearColor ToLinearColor(float4 V) { return FLinearColor(V.x, V.y, V.z, V.w); }
} // namespace Hlsl
} // namespace DreamWind
