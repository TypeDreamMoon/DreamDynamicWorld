// Copyright DreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/SceneComponent.h"
#include "DreamSurfaceTypes.h"
#include "DreamSurfaceInteraction.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FDreamSurfaceContactEvent, const FDreamSurfaceContact&, Contact);

/**
 * Put it on a foot, a wheel, a hoof (a socket of a skeletal mesh): as it moves near the ground it presses prints into the
 * snow and the dust, stirs the puddles, and tells what it met (OnContact) for sounds and effects. The prints fill again
 * with new snow and blow away with the wind; the stirred water settles in a second or two.
 */
UCLASS(ClassGroup = "Dream", meta = (BlueprintSpawnableComponent, DisplayName = "Dream Surface Interaction"))
class DREAMSURFACE_API UDreamSurfaceInteractionComponent : public USceneComponent
{
	GENERATED_BODY()

public:
	UDreamSurfaceInteractionComponent();

	/** Radius of the print, cm. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Interaction", meta = (ClampMin = "1"))
	float Radius = 12.0f;

	/** How deep a print presses, 0..1 (1 = down to the ground). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Interaction", meta = (ClampMin = "0", ClampMax = "1"))
	float Strength = 0.9f;

	/** Distance it moves between prints, cm (a wheel: small; a foot: a stride's fraction). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Interaction", meta = (ClampMin = "1"))
	float StepDistance = 20.0f;

	/** Look down for the ground this far (cm) and print where it is; off: print at the component itself. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Interaction")
	bool bTraceGround = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Interaction", meta = (ClampMin = "1", EditCondition = "bTraceGround"))
	float TraceDistance = 30.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Interaction")
	bool bAffectsSnowAndDust = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Interaction")
	bool bAffectsPuddles = true;

	/** Each print: what it met. */
	UPROPERTY(BlueprintAssignable, Category = "Interaction")
	FDreamSurfaceContactEvent OnContact;

	/** Print here now (whatever the distance moved). Returns what it met. */
	UFUNCTION(BlueprintCallable, Category = "Dream Surface")
	FDreamSurfaceContact PrintNow();

	/** Prints so far. */
	int32 GetPrintCount() const { return PrintCount; }

	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

private:
	bool FindGround(FVector& OutLocation, FVector& OutNormal) const;

	FVector LastPrint = FVector(1.0e30);
	int32 PrintCount = 0;
};
