// Copyright DreamMoon. All Rights Reserved.

#pragma once

// The sky grade's shared HLSL compiled as C++, on the wind's HLSL-subset types:
//   #include "DreamSkyGradeShim.h"   ->   DreamWind::Hlsl::DSG_Grade(...)

#include "DreamWindHlslShim.h"

namespace DreamWind
{
namespace Hlsl
{
	#define DSG_FN inline
	#include "DreamSkyGradeShared.h"
	#undef DSG_FN
} // namespace Hlsl
} // namespace DreamWind
