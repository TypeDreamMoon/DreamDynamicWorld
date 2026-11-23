// Copyright DreamMoon. All Rights Reserved.

#include "DreamWindMoverComponent.h"
#include "DreamWindModule.h"
#include "DreamWindSubsystem.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "HAL/IConsoleManager.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(DreamWindMoverComponent)

// DreamWind.Rotor X Y Z [radius] [thrust] [reach] | off: a hovering rotor's downwash there (a debug and showcase stand-in).
static FAutoConsoleCommandWithWorldAndArgs GDreamWindRotorCommand(
	TEXT("DreamWind.Rotor"),
	TEXT("DreamWind.Rotor <x> <y> <z> [radius cm, 300] [thrust cm/s, 1500] [reach cm, 1000] | off: a rotor hovering there, its downwash in the wind fluid."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
	{
		if (!World)
		{
			return;
		}
		static const FName Tag(TEXT("DreamWind.Rotor"));
		for (TActorIterator<AActor> It(World); It; ++It)
		{
			if (It->ActorHasTag(Tag))
			{
				It->Destroy();
			}
		}
		if (Args.Num() < 3 || Args[0].Equals(TEXT("off"), ESearchCase::IgnoreCase))
		{
			return;
		}
		FActorSpawnParameters Spawn;
		Spawn.ObjectFlags |= RF_Transient;
		AActor* Rotor = World->SpawnActor<AActor>(AActor::StaticClass(), FTransform::Identity, Spawn);
		if (!Rotor)
		{
			return;
		}
		Rotor->Tags.Add(Tag);
		UDreamWindMoverComponent* Mover = NewObject<UDreamWindMoverComponent>(Rotor, TEXT("Rotor"), RF_Transient);
		Mover->Shape = EDreamWindMoverShape::Rotor;
		Mover->Radius = Args.Num() > 3 ? FCString::Atof(*Args[3]) : 300.0f;
		Mover->RotorThrust = Args.Num() > 4 ? FCString::Atof(*Args[4]) : 1500.0f;
		Mover->RotorReach = Args.Num() > 5 ? FCString::Atof(*Args[5]) : 1000.0f;
		Rotor->SetRootComponent(Mover);
		Mover->RegisterComponent();
		Rotor->SetActorLocation(FVector(FCString::Atod(*Args[0]), FCString::Atod(*Args[1]), FCString::Atod(*Args[2])));
		UE_LOG(LogDreamWind, Display, TEXT("Wind rotor at %s, %.0f cm across, %.0f cm/s down"), *Rotor->GetActorLocation().ToString(), Mover->Radius * 2.0f, Mover->RotorThrust);
	}));

UDreamWindMoverComponent::UDreamWindMoverComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	bAutoActivate = true;
}

void UDreamWindMoverComponent::OnRegister()
{
	Super::OnRegister();
	if (UWorld* World = GetWorld())
	{
		if (UDreamWindSubsystem* Wind = World->GetSubsystem<UDreamWindSubsystem>())
		{
			Wind->RegisterMover(this);
		}
	}
	bHasLastLocation = false;
	Velocity = FVector::ZeroVector;
}

void UDreamWindMoverComponent::OnUnregister()
{
	if (UWorld* World = GetWorld())
	{
		if (UDreamWindSubsystem* Wind = World->GetSubsystem<UDreamWindSubsystem>())
		{
			Wind->UnregisterMover(this);
		}
	}
	Super::OnUnregister();
}

void UDreamWindMoverComponent::BuildSources(TArray<FDreamWindSourceDesc>& Out, float DeltaTime)
{
	const FTransform& Xf = GetComponentTransform();
	const FVector Location = Xf.GetLocation();
	if (bHasLastLocation && DeltaTime > KINDA_SMALL_NUMBER)
	{
		// (a teleport is not a speed)
		const FVector Step = (Location - LastLocation) / DeltaTime;
		Velocity = Step.SizeSquared() < FMath::Square(10000.0) ? Step : FVector::ZeroVector;
	}
	LastLocation = Location;
	bHasLastLocation = true;
	if (!IsActive())
	{
		return;
	}
	const FVector Up = Xf.GetUnitAxis(EAxis::Z);

	if (Shape == EDreamWindMoverShape::Rotor)
	{
		// a jet down from the disc, as long as the reach (past the ground, the fluid spreads it out over it)
		FDreamWindSourceDesc Jet;
		Jet.Type = EDreamWindSourceType::Directional;
		Jet.Position = Location;
		Jet.Direction = -Up;
		Jet.Radius = FMath::Max(Radius, 1.0f);
		Jet.Stretch = FMath::Max(RotorReach / Jet.Radius, 1.0f);
		Jet.Strength = RotorThrust;
		Jet.InjectRate = 20.0f;
		Jet.InnerRadiusFraction = 0.3f;
		if (!FMath::IsNearlyZero(Jet.Strength))
		{
			Out.Add(Jet);
		}
		return;
	}

	const float Speed = float(Velocity.Size());
	const FVector Heading = Speed > 1.0f ? Velocity / Speed : Xf.GetUnitAxis(EAxis::X);
	if (bObstacle)
	{
		// the air inside the body moves with it (a box round the capsule, in the component's frame)
		FDreamWindSourceDesc Body;
		Body.Type = EDreamWindSourceType::Shelter;
		Body.Position = Location;
		Body.BoxExtent = FVector(Radius, Radius, HalfHeight + Radius);
		Body.BoxRotation = Xf.GetRotation().Rotator();
		Body.Radius = Body.GetBoundingRadius();
		Body.Direction = Heading;
		Body.Strength = Speed;
		Body.InjectRate = 30.0f;
		Body.InnerRadiusFraction = 0.5f;
		Out.Add(Body);
	}
	if (Push > 0.0f && Speed > 10.0f)
	{
		// behind it, the air pulled along: an egg reaching from far behind up to the body
		FDreamWindSourceDesc Wake;
		Wake.Type = EDreamWindSourceType::Directional;
		Wake.Radius = FMath::Max(Radius * 2.0f, HalfHeight + Radius);
		Wake.Stretch = FMath::Max(WakeLength, 1.0f);
		Wake.Position = Location - Heading * (Wake.Radius * Wake.Stretch);
		Wake.Direction = Heading;
		Wake.Strength = Speed * Push;
		Wake.InjectRate = 10.0f;
		Wake.InnerRadiusFraction = 0.2f;
		Out.Add(Wake);
	}
}
