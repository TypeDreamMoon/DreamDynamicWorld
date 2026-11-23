// Copyright DreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "DreamSurfaceVehicleGlass.generated.h"

class UPrimitiveComponent;

/**
 * Put it on a vehicle: it tells the vehicle's glass (MF_DreamGlassRain with Vehicle Glass on) how fast it moves and what
 * the wipers do, through the glass meshes' custom primitive data, so the drops are pushed along by the air, the panes
 * facing the way it goes catch more rain, and the wipers sweep their arcs clear. Where the wipers are on the glass and
 * how far they swing are the glass material's parameters (group "Vehicle Glass").
 *
 * The data, per glass mesh: 24..26 the velocity in the mesh's own frame (cm/s), 28 seconds per wiper swing out and back
 * (0 parked), 29 the world time they started, 30 the heater (0..1, melts the snow on the glass).
 */
UCLASS(ClassGroup = "Dream", meta = (BlueprintSpawnableComponent, DisplayName = "Dream Surface Vehicle Glass"))
class DREAMSURFACE_API UDreamSurfaceVehicleGlassComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UDreamSurfaceVehicleGlassComponent();

	/** The first custom primitive data index the glass material reads (MF_DreamGlassRain: 24). */
	static constexpr int32 DataIndex = 24;

	/** The glass: the owner's primitive components with this tag; none tagged (or no tag): every primitive of the owner. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vehicle Glass")
	FName GlassTag = TEXT("DreamGlass");

	/** The wipers run. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Vehicle Glass")
	bool bWipers = false;

	/** Seconds per wiper swing, out and back. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vehicle Glass", meta = (ClampMin = "0.3", Units = "s"))
	float WiperPeriod = 1.2f;

	/** The glass's heater, 0..1: melts the snow on it. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vehicle Glass", meta = (ClampMin = "0", ClampMax = "1"))
	float Heater = 0.0f;

	/**
	 * A velocity added to the vehicle's own, cm/s in the owner's frame (x forward): a parked vehicle shown as if it drove
	 * (a showcase, a test), or wind on a parked one.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vehicle Glass")
	FVector ExtraVelocity = FVector::ZeroVector;

	/** Starts or stops the wipers (they start a swing now). */
	UFUNCTION(BlueprintCallable, Category = "Dream Surface")
	void SetWipers(bool bOn);

	/** The vehicle's velocity this frame (its own and the extra), world cm/s. */
	UFUNCTION(BlueprintPure, Category = "Dream Surface")
	FVector GetGlassVelocity() const { return Velocity; }

	/** The glass meshes it writes to. */
	TArray<UPrimitiveComponent*> GetGlass() const;

	/** Writes the data now (it does every tick). */
	void UpdateGlass();

	virtual void BeginPlay() override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

private:
	FVector Velocity = FVector::ZeroVector;
	FVector LastLocation = FVector::ZeroVector;
	bool bHasLast = false;
	double WiperStart = 0.0;
};
