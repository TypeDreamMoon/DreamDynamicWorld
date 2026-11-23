// Copyright DreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "DreamSurfaceWalker.generated.h"

class UDreamSurfaceInteractionComponent;
class UStaticMeshComponent;

/**
 * Something that walks a loop at a steady pace, its feet stepping in turn (two surface interaction components): prints
 * in the snow and the dust, the puddles pushed away and ringing, for showing the surfaces off without a character.
 * The loop is an ellipse round Center; the body follows the ground (traced) and faces the way it goes. Anything else
 * that moves with a body (the wind's moving bodies, gameplay) can be attached to it.
 */
UCLASS(meta = (DisplayName = "Dream Surface Walker"))
class DREAMSURFACE_API ADreamSurfaceWalker : public AActor
{
	GENERATED_BODY()

public:
	ADreamSurfaceWalker();

	/** The body (a mesh standing on its feet, its pivot at the ground). */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Walker")
	TObjectPtr<UStaticMeshComponent> Body;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Walker")
	TObjectPtr<UDreamSurfaceInteractionComponent> LeftFoot;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Walker")
	TObjectPtr<UDreamSurfaceInteractionComponent> RightFoot;

	/** The loop's centre, world cm. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Walker")
	FVector2D Center = FVector2D::ZeroVector;

	/** The loop's half widths along X and Y, cm. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Walker")
	FVector2D Radii = FVector2D(500.0, 350.0);

	/** Walking speed, cm a second. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Walker", meta = (ClampMin = "0", Units = "cm/s"))
	float Speed = 120.0f;

	/** How far it goes between two steps of the same foot, cm (the feet step in turn, half a stride apart). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Walker", meta = (ClampMin = "10", Units = "cm"))
	float Stride = 130.0f;

	/** Where on the loop it starts, degrees from +X. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Walker")
	float StartAngle = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Walker")
	bool bWalking = true;

	/** Where it stands on the loop now, radians from +X. */
	float GetAngle() const { return Angle; }

	/** Footsteps so far. */
	int32 GetStepCount() const { return Steps; }

	/** Walks on by Seconds now (it does every tick). */
	void WalkFor(float Seconds);

	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;

private:
	void PlaceOnLoop();

	float Angle = 0.0f;
	float SinceStep = 0.0f;
	int32 Steps = 0;
	bool bLeftNext = true;
};
