// Copyright DreamMoon. All Rights Reserved.

#include "DreamWindImpulseEmitter.h"
#include "DreamWindSubsystem.h"
#include "Components/SphereComponent.h"
#include "Engine/World.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(DreamWindImpulseEmitter)

ADreamWindImpulseEmitter::ADreamWindImpulseEmitter()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = true;

	Sphere = CreateDefaultSubobject<USphereComponent>(TEXT("Radius"));
	Sphere->InitSphereRadius(100.0f);
	Sphere->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Sphere->ShapeColor = FColor(255, 140, 40);
	Sphere->bDrawOnlyIfSelected = false;
	RootComponent = Sphere;

	Impulse.Type = EDreamWindSourceType::Radial;
	Impulse.Radius = 800.0f;
	Impulse.Strength = 3000.0f;
	Impulse.InjectRate = 40.0f;
	Impulse.InnerRadiusFraction = 0.1f;
	TimeToNext = 1.0f;
}

void ADreamWindImpulseEmitter::Fire()
{
	if (UDreamWindSubsystem* Wind = UDreamWindSubsystem::Get(this))
	{
		FDreamWindSourceDesc Desc = Impulse;
		Desc.Position = GetActorLocation();
		if (Desc.Type != EDreamWindSourceType::Radial)
		{
			Desc.Direction = GetActorForwardVector();
		}
		Wind->AddImpulse(Desc, Duration, FadeOutTime);
	}
}

void ADreamWindImpulseEmitter::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	// SetSphereRadius recreates the render state; only touch it when the radius actually changed.
	const float WantedRadius = FMath::Max(Impulse.Radius, 10.0f);
	if (Sphere && !FMath::IsNearlyEqual(Sphere->GetUnscaledSphereRadius(), WantedRadius))
	{
		Sphere->SetSphereRadius(WantedRadius, false);
	}
	TimeToNext -= DeltaSeconds;
	if (TimeToNext <= 0.0f)
	{
		Fire();
		TimeToNext = FMath::Max(Interval, 0.1f);
	}
}
