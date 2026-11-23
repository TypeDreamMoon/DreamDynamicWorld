// Copyright DreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"

DREAMSURFACE_API DECLARE_LOG_CATEGORY_EXTERN(LogDreamSurface, Log, All);

/** The surfaces' vectors in the plugin's shared Material Parameter Collection. */
namespace DreamSurface::Names
{
	inline const FName MPC_SurfaceNear(TEXT("DDW_SurfaceNear"));     // the near surface map: minus its centre x, y (add a world position), 1 / extent (1/cm), 1 when live
	inline const FName MPC_SurfaceFar(TEXT("DDW_SurfaceFar"));       // the far surface map, the same
	inline const FName MPC_SurfaceView(TEXT("DDW_SurfaceView"));     // the region's state in the open (no zone, lee or water body; GetRegionState): wet, water, snow, dust (beyond both maps)
	inline const FName MPC_SurfaceViewIce(TEXT("DDW_SurfaceViewIce")); // the same for the cold: ice, frost, rime, frozen water
	inline const FName MPC_SurfaceViewHail(TEXT("DDW_SurfaceViewHail")); // and for the hail: hail, flooded, silt, whether the maps may hold hail or silt at all (0: the materials skip them)
	inline const FName MPC_SurfaceParams(TEXT("DDW_SurfaceParams")); // deepest snow (cm), roof clearance (cm), cover softness (cm), puddle scale (cm)
	inline const FName MPC_SurfaceTrails(TEXT("DDW_SurfaceTrails")); // the trail map: minus its centre x, y, 1 / extent, 1 when live
}

class FDreamSurfaceModule : public IModuleInterface
{
public:
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;
};
