// Copyright DreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "DreamCloudField.generated.h"

class UStaticMesh;

/**
 * A cloud's shape baked to a signed distance field: a box of texels, each holding the signed distance (cm, < 0 inside)
 * from its centre to the shape's surface. A cloud volume draws it as a volume texture; only a new shape bakes it again
 * (moving, turning or scaling the volume changes its transform alone).
 */
USTRUCT(BlueprintType)
struct DREAMCLOUD_API FDreamCloudField
{
	GENERATED_BODY()

	/** Texels along x, y, z (the longest axis has the bake's resolution; texels are cubes). */
	UPROPERTY(VisibleAnywhere, Category = "Cloud")
	FIntVector Resolution = FIntVector::ZeroValue;

	/** The box's centre in the shape's space (cm). */
	UPROPERTY(VisibleAnywhere, Category = "Cloud")
	FVector Center = FVector::ZeroVector;

	/** The box's size (cm): Resolution times the texel size. */
	UPROPERTY(VisibleAnywhere, Category = "Cloud")
	FVector Size = FVector::ZeroVector;

	/** The distances as half floats, x fastest, then y, then z. */
	UPROPERTY()
	TArray<uint16> Distances;

	/** What the field was baked from (see UDreamCloudVolumeComponent): baked again when it no longer matches. */
	UPROPERTY()
	uint32 SourceHash = 0;

	bool IsValid() const;
	float TexelSize() const;
	int32 Index(int32 X, int32 Y, int32 Z) const { return X + Resolution.X * (Y + Resolution.Y * Z); }
	float At(int32 X, int32 Y, int32 Z) const;
	/** The texel centre's position in the shape's space. */
	FVector TexelCenter(int32 X, int32 Y, int32 Z) const;
	/** Trilinear, as the GPU samples it; outside the box at least the distance to the box. */
	float Sample(const FVector& Point) const;
};

/** A primitive of a cloud made of primitives. */
UENUM(BlueprintType)
enum class EDreamCloudPrimitive : uint8
{
	Sphere,
	Box,
	Capsule,
};

USTRUCT(BlueprintType)
struct DREAMCLOUD_API FDreamCloudPrimitiveShape
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cloud")
	EDreamCloudPrimitive Type = EDreamCloudPrimitive::Sphere;

	/** Where it stands in the volume and how it is turned (its scale is ignored: Size says how big it is). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cloud")
	FTransform Transform;

	/** Sphere: X = radius. Box: half its size along each axis. Capsule: X = radius, Z = half the length of its middle. (cm) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cloud")
	FVector Size = FVector(500.0);

	/** Box only: how round its edges are (cm). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cloud", meta = (ClampMin = "0"))
	float Rounding = 100.0f;

	/** Carves this primitive out of the others (a tunnel, a hollow) instead of adding it, its seams rounded by the same blend. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cloud")
	bool bSubtract = false;

	/** The signed distance to it from a point in the volume's space. */
	float Distance(const FVector& Point) const;
	/** The box it fills in the volume's space. */
	FBox Bounds() const;
};

namespace DreamCloudBake
{
	/** Bump when baking changes, so every field bakes again. */
	inline constexpr uint32 Version = 3;

	struct FSettings
	{
		/** Texels along the longest axis of the box (16..256). */
		int32 Resolution = 96;
		/** Room left around the shape, as a share of its largest size (for the cloud to grow past it and for its noise). */
		float Padding = 0.15f;
	};

	/** Bakes a triangle mesh (positions in the shape's space, three indices a triangle). A mesh merged from overlapping
	 *  parts becomes their union (each part measured on its own, so the surfaces inside one another leave no seams);
	 *  inside is where a part's surfaces wind around a point, decided by a vote of rays along the three axes (an open
	 *  mesh gets the majority). False when there is nothing to bake. */
	DREAMCLOUD_API bool FromTriangles(TConstArrayView<FVector3f> Positions, TConstArrayView<uint32> Indices, const FSettings& Settings, FDreamCloudField& Out);

	/** Bakes primitives blended together: the union of the added ones less the subtracted ones, seams rounded over
	 *  Blend cm (0 = hard). */
	DREAMCLOUD_API bool FromPrimitives(TConstArrayView<FDreamCloudPrimitiveShape> Shapes, float Blend, const FSettings& Settings, FDreamCloudField& Out);

	/** The triangles of a static mesh's first LOD: its source in the editor, its CPU copy in a game (the mesh must
	 *  allow CPU access there). False when there are none. */
	DREAMCLOUD_API bool GatherMesh(const UStaticMesh* Mesh, TArray<FVector3f>& OutPositions, TArray<uint32>& OutIndices);
}
