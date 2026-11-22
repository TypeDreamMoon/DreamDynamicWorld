// Copyright DreamMoon. All Rights Reserved.

#pragma once

// The weather's shared HLSL compiled as C++, on the same HLSL-subset types and intrinsics the wind uses:
//   #include "DreamWeatherHlslShim.h"   ->   DreamWind::Hlsl::DWX_ZoneWeight(...)

#include "DreamWindHlslShim.h"

namespace DreamWind
{
namespace Hlsl
{
	/** Where a polygon footprint's points are, as the shared code reads them (Points[i]); empty for a box or a circle. */
	struct FDreamZonePoints
	{
		const FVector4f* Data = nullptr;
		FDreamZonePoints() = default;
		explicit FDreamZonePoints(const FVector4f* InData) : Data(InData) {}
		explicit FDreamZonePoints(const TArray<FVector4f>& InPoints) : Data(InPoints.GetData()) {}
		float4 operator[](int i) const { const FVector4f& V = Data[i]; return float4(V.X, V.Y, V.Z, V.W); }
	};

	#define DWX_FN inline
	#define DWX_POINTS FDreamZonePoints
	#include "DreamWeatherShared.h"
	#undef DWX_POINTS
	#undef DWX_FN
} // namespace Hlsl
} // namespace DreamWind
