// Copyright DreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

/**
 * The engine's global distance field as the wind reads it: the obstacles of the fluid wind volume come from it where the
 * project has one. The renderer keeps it up to date only while something uses it (Lumen, distance field AO, a material or
 * a Niagara system that reads it); Request asks for it on the wind's behalf.
 */
namespace DreamWind::DistanceField
{
	/** Whether this project and GPU can have a global distance field at all (Generate Mesh Distance Fields on, SM5 or better). */
	DREAMWIND_API bool IsSupported();

	/**
	 * Keep the global distance field up to date although nothing else uses it (r.AOGlobalDistanceField.DetailedNecessityCheck 0
	 * while requested; the previous value comes back on release). Requests count: each Request(true) wants a Request(false).
	 */
	DREAMWIND_API void Request(bool bRequest);

	/** Whether the wind has it requested now. */
	DREAMWIND_API bool IsRequested();
}
