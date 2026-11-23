// Copyright DreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/SceneComponent.h"
#include "GameFramework/Actor.h"
#include "DreamSurfaceDripLine.generated.h"

class UNiagaraComponent;
class UNiagaraSystem;

/**
 * An eave, a gutter's edge, a canopy's rim: drops fall from it while it rains and for a while after, as long as the roof
 * stays wet (the surface state in the open there), and stop as it dries or freezes. The line runs along the
 * component's X axis, Length long and centred on it; the drops fall from it to the ground below (traced), splash there and
 * stir the puddles they land in.
 */
UCLASS(ClassGroup = "Dream", meta = (BlueprintSpawnableComponent, DisplayName = "Dream Surface Drip Line"))
class DREAMSURFACE_API UDreamSurfaceDripLineComponent : public USceneComponent
{
	GENERATED_BODY()

public:
	UDreamSurfaceDripLineComponent();

	/** The eave's length along the component's X axis, cm (centred on it). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Drips", meta = (ClampMin = "1", Units = "cm"))
	float Length = 300.0f;

	/** Drops a second per metre of eave while it runs in a full rain. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Drips", meta = (ClampMin = "0"))
	float DropsPerMetre = 8.0f;

	/**
	 * How wet the roof must stay for the eave to drip on after the rain, 0..1 of a soaked film: lower, it drips longer.
	 * The film dries in the sun and the wind (Project Settings > Dream Surface > Rates > Dry Hours), so a shaded, still
	 * eave drips longest.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Drips", meta = (ClampMin = "0", ClampMax = "0.99"))
	float DripFromWetness = 0.55f;

	/** Ripple the puddles the drops land in. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Drips")
	bool bStirPuddles = true;

	/** The effect (NS_DreamEaveDrips by default); none: the drips are only asked (GetDripStrength) and stir the puddles. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Drips", meta = (AllowedClasses = "/Script/Niagara.NiagaraSystem"))
	FSoftObjectPath Effect = FSoftObjectPath(TEXT("/DreamDynamicWorld/Surface/Effects/NS_DreamEaveDrips.NS_DreamEaveDrips"));

	/** How hard it drips now, 0..1 (DSF_EaveDrip of the state in the open at the eave). */
	UFUNCTION(BlueprintPure, Category = "Dream Surface")
	float GetDripStrength() const { return Strength; }

	/** Drops a second along the whole eave now. */
	UFUNCTION(BlueprintPure, Category = "Dream Surface")
	float GetDropRate() const;

	/** The eave's ends, world. */
	void GetLine(FVector& OutStart, FVector& OutEnd) const;

	/** The drops are drawn (the effect loaded in a game world). */
	bool HasEffect() const;

	/** Drops and splashes alive in the effect now (DreamSurface.Dump). */
	int32 GetParticleCount() const;

	/** The ground below each end, world Z (traced). */
	FVector2D GetFloors() const { return FVector2D(FloorStart, FloorEnd); }

	/** Asks the weather again now (it does every second or so). */
	void Refresh();

	virtual void OnRegister() override;
	virtual void OnUnregister() override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

private:
	float FloorBelow(const FVector& Point) const;

	UPROPERTY(Transient)
	TObjectPtr<UNiagaraComponent> Drips;

	float Strength = 0.0f;
	float FloorStart = 0.0f;
	float FloorEnd = 0.0f;
	double LastRefresh = -1.0e30;
	float PendingStirs = 0.0f;
};

/** An eave that drips after the rain (UDreamSurfaceDripLineComponent): place it along a roof's lower edge. */
UCLASS(meta = (DisplayName = "Dream Surface Drip Line"))
class DREAMSURFACE_API ADreamSurfaceDripLine : public AActor
{
	GENERATED_BODY()

public:
	ADreamSurfaceDripLine();

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Drips")
	TObjectPtr<UDreamSurfaceDripLineComponent> Drips;
};
