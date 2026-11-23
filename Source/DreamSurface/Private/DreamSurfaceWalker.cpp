// Copyright DreamMoon. All Rights Reserved.

#include "DreamSurfaceWalker.h"
#include "DreamSurfaceInteraction.h"

#include "Camera/CameraActor.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"

namespace DreamSurface::Walker
{
	/** The feet's distance either side of the body's line, cm. */
	constexpr double FootSpacing = 11.0;
}

ADreamSurfaceWalker::ADreamSurfaceWalker()
{
	PrimaryActorTick.bCanEverTick = true;
	Body = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Body"));
	RootComponent = Body;
	Body->SetMobility(EComponentMobility::Movable);
	Body->SetCollisionProfileName(TEXT("NoCollision"));
	auto MakeFoot = [this](const TCHAR* Name, double Side)
	{
		UDreamSurfaceInteractionComponent* Foot = CreateDefaultSubobject<UDreamSurfaceInteractionComponent>(Name);
		Foot->SetupAttachment(Body);
		Foot->SetRelativeLocation(FVector(0.0, Side * DreamSurface::Walker::FootSpacing, 10.0));
		// The walker steps them in turn: they do not print on their own.
		Foot->StepDistance = 1.0e7f;
		Foot->Radius = 10.0f;
		return Foot;
	};
	LeftFoot = MakeFoot(TEXT("LeftFoot"), -1.0);
	RightFoot = MakeFoot(TEXT("RightFoot"), 1.0);
}

void ADreamSurfaceWalker::BeginPlay()
{
	Super::BeginPlay();
	Angle = FMath::DegreesToRadians(StartAngle);
	SinceStep = 0.0f;
	PlaceOnLoop();
}

void ADreamSurfaceWalker::PlaceOnLoop()
{
	const double A = Radii.X, B = Radii.Y;
	FVector At(Center.X + A * FMath::Cos(Angle), Center.Y + B * FMath::Sin(Angle), GetActorLocation().Z);
	// On the ground below.
	if (const UWorld* World = GetWorld())
	{
		FHitResult Hit;
		FCollisionQueryParams Params(SCENE_QUERY_STAT(DreamSurfaceWalker), false, this);
		if (World->LineTraceSingleByChannel(Hit, FVector(At.X, At.Y, At.Z + 300.0), FVector(At.X, At.Y, At.Z - 1000.0), ECC_Visibility, Params))
		{
			At.Z = Hit.ImpactPoint.Z;
		}
	}
	// Facing the way it goes (the ellipse's tangent, anticlockwise seen from above).
	const FVector Ahead(-A * FMath::Sin(Angle), B * FMath::Cos(Angle), 0.0);
	SetActorLocationAndRotation(At, FRotator(0.0, Ahead.Rotation().Yaw, 0.0));
}

void ADreamSurfaceWalker::WalkFor(float Seconds)
{
	if (!bWalking || Seconds <= 0.0f || Speed <= 0.0f)
	{
		return;
	}
	// Along the ellipse at a steady speed: the angle moves by the distance over how far a radian reaches there.
	const double A = Radii.X, B = Radii.Y;
	const double Reach = FMath::Max(FMath::Sqrt(FMath::Square(A * FMath::Sin(Angle)) + FMath::Square(B * FMath::Cos(Angle))), 1.0);
	Angle = float(FMath::Fmod(double(Angle) + double(Speed) * Seconds / Reach, 2.0 * UE_DOUBLE_PI));
	PlaceOnLoop();
	// A foot down every half stride, in turn.
	SinceStep += Speed * Seconds;
	while (SinceStep >= Stride * 0.5f)
	{
		SinceStep -= Stride * 0.5f;
		(bLeftNext ? LeftFoot : RightFoot)->PrintNow();
		bLeftNext = !bLeftNext;
		++Steps;
	}
}

void ADreamSurfaceWalker::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	WalkFor(DeltaSeconds);
}

namespace DreamSurface::Walker
{
	static FAutoConsoleCommandWithWorldAndArgs FrameCommand(
		TEXT("DreamSurface.Walker.Frame"),
		TEXT("DreamSurface.Walker.Frame [behind cm=350] [height cm=200] [pitch=-22] [side cm=0]: put the view behind the first Dream Surface Walker, ")
		TEXT("looking at it (a shot tour's following view)."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
		{
			ADreamSurfaceWalker* Walker = nullptr;
			for (TActorIterator<ADreamSurfaceWalker> It(World); It && !Walker; ++It)
			{
				Walker = *It;
			}
			APlayerController* PC = World ? World->GetFirstPlayerController() : nullptr;
			if (!Walker || !PC)
			{
				return;
			}
			auto Num = [&Args](int32 i, double Default) { return Args.IsValidIndex(i) ? FCString::Atod(*Args[i]) : Default; };
			const FRotator Facing(0.0, Walker->GetActorRotation().Yaw, 0.0);
			const FVector Eye = Walker->GetActorLocation() - Facing.Vector() * Num(0, 350.0) + FRotationMatrix(Facing).GetUnitAxis(EAxis::Y) * Num(3, 0.0)
				+ FVector(0.0, 0.0, Num(1, 200.0));
			const FRotator Look(Num(2, -22.0), (Walker->GetActorLocation() - Eye).Rotation().Yaw, 0.0);
			// The view the script's 'move' and 'aim' carry: the camera viewed through, else the pawn.
			AActor* Carrier = PC->GetViewTarget();
			if (Carrier && !Carrier->IsA<ACameraActor>() && PC->GetPawn())
			{
				Carrier = PC->GetPawn();
			}
			if (Carrier)
			{
				Carrier->SetActorLocation(Eye);
				if (Carrier->IsA<ACameraActor>())
				{
					Carrier->SetActorRotation(Look);
				}
			}
			PC->SetControlRotation(Look);
		}));
}
