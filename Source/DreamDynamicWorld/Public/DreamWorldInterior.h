// Copyright DreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/SceneComponent.h"
#include "GameFramework/Actor.h"
#include "DreamWorldTypes.h"
#include "DreamWorldInterior.generated.h"

class UBoxComponent;
class UDreamPortalComponent;

/**
 * An indoor space: a box round a room (oriented by the component, the extent unscaled). What it tells the world:
 *   - the view inside it is indoors, closed on every side but where its openings (UDreamPortalComponent) are: the sky's
 *     interior lighting, the weather's sound (muffled, coming from the doors and windows) take that over the view
 *     probe's rays as the view goes in;
 *   - no rain or snow lands inside (the weather's exposure is 0 anywhere in it and the open's right outside its walls,
 *     so the rain seen through a window falls up to the wall);
 *   - the wind inside is calm (a shelter box to the wind's CPU sample and its fluid), its openings let the air through.
 */
UCLASS(ClassGroup = "Dream", meta = (BlueprintSpawnableComponent, DisplayName = "Dream Interior"))
class DREAMDYNAMICWORLD_API UDreamInteriorComponent : public USceneComponent
{
	GENERATED_BODY()

public:
	UDreamInteriorComponent();

	/** Half size of the room, cm (to the inside of its walls). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Interior", meta = (ClampMin = "1"))
	FVector Extent = FVector(300.0, 300.0, 150.0);

	/** Inside the box, how far from its faces the room takes over in full, cm (a doorway is a fade, not a step). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Interior", meta = (ClampMin = "0"))
	float FadeDistance = 80.0f;

	/** Keep the rain and snow out (the weather's exposure). Off: only the view and the sound take it. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Interior")
	bool bShelterWeather = true;

	/** Calm the wind inside (a shelter box to the wind field; its open, ventilating portals let the air through). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Interior")
	bool bShelterWind = true;

	/** How much a point is inside: 1 deeper than FadeDistance, 0 on the box's faces and outside. */
	float GetWeightAt(const FVector& Position) const;

	/** The box in world space: centre, half extent, rotation. */
	FVector GetCenter() const { return GetComponentLocation(); }
	FQuat GetRotation() const { return GetComponentQuat(); }

	/** Whether an opening belongs to this room: its centre within its wall's reach of the box (Margin cm out). */
	bool HasPortal(const UDreamPortalComponent& Portal, float Margin = 60.0f) const;

	//~ UActorComponent
	virtual void OnRegister() override;
	virtual void OnUnregister() override;
};

/**
 * A door or a window of a room (UDreamInteriorComponent): an opening Width x Height in the component's YZ plane, its
 * +X pointing out of the room, Depth through the wall. Open (Openness > 0) it lets the outdoors in: the view inside sees
 * out through it, the weather's sound comes from it, and -- ventilating -- the wind's fluid flows through it (the wall
 * there is carved out of its walls) and the CPU wind blows in or out of it with the outdoor wind across it. Opening and
 * closing (SetOpenness) tells the wind to look at the walls there again.
 */
UCLASS(ClassGroup = "Dream", meta = (BlueprintSpawnableComponent, DisplayName = "Dream Portal"))
class DREAMDYNAMICWORLD_API UDreamPortalComponent : public USceneComponent
{
	GENERATED_BODY()

public:
	UDreamPortalComponent();

	/** The opening's width (local Y) and height (local Z), cm. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Portal", meta = (ClampMin = "1"))
	float Width = 120.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Portal", meta = (ClampMin = "1"))
	float Height = 220.0f;

	/** How thick the wall is here (local X), cm: what the wind's fluid carves out when it ventilates. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Portal", meta = (ClampMin = "1"))
	float Depth = 60.0f;

	/** 0 shut .. 1 wide open (a door ajar: 0.3). Set it through SetOpenness at run time. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, BlueprintSetter = SetOpenness, Category = "Portal", meta = (ClampMin = "0", ClampMax = "1"))
	float Openness = 1.0f;

	/** Lets the air through when open. Off: light and sound only (a closed glass window). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, BlueprintSetter = SetVentilate, Category = "Portal")
	bool bVentilate = true;

	/** Open or close it: the view, the sound and the wind follow; the wind looks at the walls here again. */
	UFUNCTION(BlueprintCallable, Category = "Portal")
	void SetOpenness(float NewOpenness);

	UFUNCTION(BlueprintCallable, Category = "Portal")
	void SetVentilate(bool bNewVentilate);

	/** Out of the room. */
	FVector GetOutward() const { return GetForwardVector(); }
	float GetArea() const { return Width * Height; }
	/** Open and letting the air through (how much, 0..1). */
	float GetVentilation() const { return bVentilate ? FMath::Clamp(Openness, 0.0f, 1.0f) : 0.0f; }
	/** The opening's box, world space (Depth through the wall). */
	FBox GetBounds() const;

	//~ UActorComponent
	virtual void OnRegister() override;
	virtual void OnUnregister() override;

private:
	void NotifyChanged();
};

/** A room you can drop in a level. */
UCLASS(ClassGroup = "Dream", hidecategories = (Collision, Physics, Input, Replication, HLOD, Cooking, Networking), meta = (DisplayName = "Dream Interior"))
class DREAMDYNAMICWORLD_API ADreamInterior : public AActor
{
	GENERATED_BODY()

public:
	ADreamInterior();

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Interior")
	TObjectPtr<UDreamInteriorComponent> Interior;

#if WITH_EDITORONLY_DATA
	UPROPERTY()
	TObjectPtr<UBoxComponent> Outline;
#endif

	virtual void OnConstruction(const FTransform& Transform) override;
};

/** A door or window you can drop in a level (its arrow points out of the room). */
UCLASS(ClassGroup = "Dream", hidecategories = (Collision, Physics, Input, Replication, HLOD, Cooking, Networking), meta = (DisplayName = "Dream Portal"))
class DREAMDYNAMICWORLD_API ADreamPortal : public AActor
{
	GENERATED_BODY()

public:
	ADreamPortal();

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Portal")
	TObjectPtr<UDreamPortalComponent> Portal;

#if WITH_EDITORONLY_DATA
	UPROPERTY()
	TObjectPtr<UBoxComponent> Outline;
#endif

	virtual void OnConstruction(const FTransform& Transform) override;
};

namespace DreamWorld::Interiors
{
	/**
	 * The view's occlusion with the rooms over it: inside a room (by its weight) indoors, a roof overhead, every side
	 * closed but where its open portals are -- each by how much of the view it fills -- and Opening toward them; the
	 * probe's rays elsewhere. Pure: the clock calls it on the probe's result.
	 */
	DREAMDYNAMICWORLD_API FDreamViewOcclusion Apply(const FDreamViewOcclusion& Probe, const FVector& View,
		TConstArrayView<const UDreamInteriorComponent*> Rooms, TConstArrayView<const UDreamPortalComponent*> Portals);

	/** How much of the view an open portal fills from a point, 0..1 (its openness times its apparent size). */
	DREAMDYNAMICWORLD_API float PortalView(const UDreamPortalComponent& Portal, const FVector& View);
}
