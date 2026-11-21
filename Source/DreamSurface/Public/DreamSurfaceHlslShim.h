// Copyright DreamMoon. All Rights Reserved.

#pragma once

// The surface's shared HLSL compiled as C++, on the same HLSL-subset types and intrinsics the wind uses:
//   #include "DreamSurfaceHlslShim.h"   ->   DreamWind::Hlsl::DSF_Step(...)

#include "DreamWindHlslShim.h"

namespace DreamWind
{
namespace Hlsl
{
	#define DSF_FN inline
	#include "DreamSurfaceShared.h"
	#undef DSF_FN
} // namespace Hlsl
} // namespace DreamWind
