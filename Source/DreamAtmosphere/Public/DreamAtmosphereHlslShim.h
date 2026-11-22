// Copyright DreamMoon. All Rights Reserved.

#pragma once

// The atmosphere's shared HLSL compiled as C++, on the same HLSL-subset types and intrinsics the wind uses:
//   #include "DreamAtmosphereHlslShim.h"   ->   DreamWind::Hlsl::DSA_SkyLuminance(...)
//
// The tables the GPU reads are the direct integrals here: exact, and slow (a sky value walks the multiple scattering's
// 64 rays at each of its steps, some milliseconds). For the few values the CPU wants, or tests.

#include "DreamWindHlslShim.h"

namespace DreamWind
{
namespace Hlsl
{
	#define DSA_FN inline
	#include "DreamAtmosphereShared.h"

	inline float3 DSA_LookupTransmittance(FDSA_Atmosphere A, float r, float mu)
	{
		return DSA_TransmittanceToTopDirect(A, r, mu);
	}

	// (Defined after the walk it needs: the multiple scattering's own walk never asks for it.)
	inline float3 DSA_LookupMultiScattering(FDSA_Atmosphere A, float r, float MuS);

	#include "DreamAtmosphereScattering.h"

	inline float3 DSA_LookupMultiScattering(FDSA_Atmosphere A, float r, float MuS)
	{
		return DSA_MultiScatteringDirect(A, r, MuS);
	}
	#undef DSA_FN
} // namespace Hlsl
} // namespace DreamWind
