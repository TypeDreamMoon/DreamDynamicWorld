// Copyright DreamMoon. All Rights Reserved.

#pragma once

// The surface's shared HLSL compiled as C++, on the same HLSL-subset types and intrinsics the wind uses:
//   #include "DreamSurfaceHlslShim.h"   ->   DreamWind::Hlsl::DSF_Step(...)

#include "DreamWindHlslShim.h"
#include <cmath>

namespace DreamWind
{
namespace Hlsl
{
	// What the surface's header uses beyond the wind's intrinsics.
	inline float acos(float x) { return std::acos(x); }

	#define DSF_FN inline
	#include "DreamSurfaceShared.h"
	#undef DSF_FN
} // namespace Hlsl
} // namespace DreamWind
