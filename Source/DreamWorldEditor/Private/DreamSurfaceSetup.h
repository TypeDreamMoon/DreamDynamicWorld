// Copyright DreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

class AStaticMeshActor;
struct FMeshDescription;
class UDreamSkyStyle;
class UMaterialInterface;
class UStaticMesh;

/** What the surface test level is built with, for the other levels built from the same pieces (the showcase). */
namespace DreamWorld::SurfaceSetup
{
	/** Saves an asset (or a level) to its package's file. */
	bool SaveAsset(UObject* Asset);

	/** A package to build an asset in afresh: a copy already in memory lets go of its file and moves aside. */
	UPackage* FreshPackage(const TCHAR* Path);

	/** The test ground's height (cm) at a point: flat around (1300, 400), rolling further out, a dip at (900, -1100). */
	double GroundHeight(double X, double Y);

	/** The test ground (80 m square around (2500, 0)) and the toon figure's stand-in mesh, built afresh. */
	UStaticMesh* EnsureGround();
	UStaticMesh* EnsureFigure();

	/** An instance of a demo material under /DreamDynamicWorld/Surface/Materials (Metallic below 0: the parent's). */
	UMaterialInterface* EnsureInstance(const TCHAR* Name, const TCHAR* ParentPath, const FLinearColor& Tint, float Roughness, float Metallic);

	/** A static mesh actor with this mesh, material, place, scale (of the 1 m basic shapes) and label. */
	AStaticMeshActor* AddMesh(UWorld& World, UStaticMesh* Mesh, UMaterialInterface* Material, const FVector& Location, const FVector& Scale,
		const FRotator& Rotation, const TCHAR* Label);

	/**
	 * Puts every static mesh's collision of a level built a moment ago into its physics scene, so that a trace over it
	 * (the wind terrain bake) hits its roofs and props: until then only some of it may be there.
	 */
	void ReadyCollision(UWorld& World);

	/**
	 * A static mesh built afresh from a mesh description, with one material slot. DistanceField scales its mesh distance
	 * field (0: none, for a mesh too large to need one).
	 */
	UStaticMesh* SaveMesh(const TCHAR* Path, FMeshDescription&& Description, const TCHAR* Slot, float DistanceField = 1.0f);
}

namespace DreamWorld::Setup
{
	inline const TCHAR* SurfaceTestMapPackage = TEXT("/DreamDynamicWorld/Surface/Maps/Lvl_DreamSurfaceTest");

	/**
	 * The surface maps' render targets (Project Settings > Dream Surface > Surface Maps): the near and far state maps
	 * (RGBA16F, 256 and 128 texels) and the cover map (RG32F, 256), all writable by a compute pass.
	 */
	bool EnsureSurfaceMaps();

	/**
	 * The surface test level, around a camera at the origin looking along +x under a clear schedule (force the weather
	 * to see it change):
	 *   ahead, near      a stone pavement (puddles gather in its hollows) under a canopy on four pillars with a shelter
	 *                    box below the roof (dry beneath it), realistic props (stone, metal, wood) and toon props and a
	 *                    toon figure on it;
	 *   around, further  rolling ground with the terrain material (snow stands up off it);
	 *   ahead right, far a snow zone: its border shows on the ground from the camera.
	 * The materials are the DreamSurface demo materials and instances of them.
	 */
	bool BuildSurfaceTestMap(UDreamSkyStyle* Style);
}
