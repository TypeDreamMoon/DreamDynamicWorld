// Copyright DreamMoon. All Rights Reserved.

#include "DreamSurfaceInteraction.h"
#include "DreamSurfaceSubsystem.h"

#include "CollisionQueryParams.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"

UDreamSurfaceInteractionComponent::UDreamSurfaceInteractionComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.TickGroup = TG_PostPhysics;
}

bool UDreamSurfaceInteractionComponent::FindGround(FVector& OutLocation, FVector& OutNormal) const
{
	const FVector From = GetComponentLocation();
	if (!bTraceGround)
	{
		OutLocation = From;
		OutNormal = FVector::UpVector;
		return true;
	}
	UWorld* World = GetWorld();
	if (!World)
	{
		return false;
	}
	FCollisionQueryParams Params(SCENE_QUERY_STAT(DreamSurfacePrint), false, GetOwner());
	FHitResult Hit;
	const FVector To = From - FVector(0.0, 0.0, TraceDistance + Radius);
	if (!World->LineTraceSingleByChannel(Hit, From + FVector(0.0, 0.0, Radius), To, ECC_Visibility, Params))
	{
		return false;
	}
	OutLocation = Hit.ImpactPoint;
	OutNormal = Hit.ImpactNormal;
	return true;
}

FDreamSurfaceContact UDreamSurfaceInteractionComponent::PrintNow()
{
	FDreamSurfaceContact Contact;
	UDreamSurfaceSubsystem* Surface = UDreamSurfaceSubsystem::Get(this);
	FVector Location, Normal;
	if (!Surface || !FindGround(Location, Normal))
	{
		return Contact;
	}
	Contact = Surface->SampleContact(Location, Normal);
	Surface->Stamp(Location, Radius, Strength, bAffectsSnowAndDust, bAffectsPuddles);
	LastPrint = GetComponentLocation();
	++PrintCount;
	OnContact.Broadcast(Contact);
	return Contact;
}

void UDreamSurfaceInteractionComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	// A step further: print where the ground is (off the ground, nothing happens and it tries again next frame).
	if (FVector::DistSquared(GetComponentLocation(), LastPrint) >= FMath::Square(StepDistance))
	{
		PrintNow();
	}
}
