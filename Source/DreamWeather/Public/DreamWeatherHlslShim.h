// Copyright DreamMoon. All Rights Reserved.

#pragma once

// The weather's shared HLSL compiled as C++, on the same HLSL-subset types and intrinsics the wind uses:
//   #include "DreamWeatherHlslShim.h"   ->   DreamWind::Hlsl::DWX_ZoneWeight(...)

#include "DreamWindHlslShim.h"

namespace DreamWind
{
namespace Hlsl
{
	#define DWX_FN inline
	#include "DreamWeatherShared.h"
	#undef DWX_FN
} // namespace Hlsl
} // namespace DreamWind
