// Copyright DreamMoon. All Rights Reserved.

#include "DreamSurfaceDripLine.h"
#include "DreamSurfaceHlslShim.h"
#include "DreamSurfaceSubsystem.h"

#include "DreamWeatherSubsystem.h"
#include "DreamWorldSubsystem.h"
#include "Engine/World.h"
#include "NiagaraComponent.h"
#include "NiagaraEmitterInstance.h"
#include "NiagaraSystem.h"
#include "NiagaraSystemInstance.h"
#include "NiagaraSystemInstanceController.h"

namespace DreamSurface::Drips
{
	/** Real seconds between asks of the weather (a query integrates the whole memory). */
	constexpr double RefreshSeconds = 1.0;
	/** The drops stir the puddles within this distance of the view only (the trail map's reach). */
	constexpr double StirReach = 3000.0;
}

ADreamSurfaceDripLine::ADreamSurfaceDripLine()
{
	Drips = CreateDefaultSubobject<UDreamSurfaceDripLineComponent>(TEXT("Drips"));
	RootComponent = Drips;
}

UDreamSurfaceDripLineComponent::UDreamSurfaceDripLineComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.TickInterval = 0.0f;
	bAutoActivate = true;
}

void UDreamSurfaceDripLineComponent::GetLine(FVector& OutStart, FVector& OutEnd) const
{
	const FVector Half = GetComponentTransform().TransformVectorNoScale(FVector(Length * 0.5, 0.0, 0.0));
	OutStart = GetComponentLocation() - Half;
	OutEnd = GetComponentLocation() + Half;
}

bool UDreamSurfaceDripLineComponent::HasEffect() const
{
	return Drips != nullptr && Drips->IsActive();
}

int32 UDreamSurfaceDripLineComponent::GetParticleCount() const
{
	int32 Count = 0;
	if (Drips)
	{
		if (FNiagaraSystemInstanceControllerConstPtr Controller = Drips->GetSystemInstanceController())
		{
			if (const FNiagaraSystemInstance* Instance = Controller->GetSystemInstance_Unsafe())
			{
				for (const FNiagaraEmitterInstanceRef& Emitter : Instance->GetEmitters())
				{
					Count += Emitter->GetNumParticles();
				}
			}
		}
	}
	return Count;
}

float UDreamSurfaceDripLineComponent::GetDropRate() const
{
	return Strength * DropsPerMetre * Length / 100.0f;
}

float UDreamSurfaceDripLineComponent::FloorBelow(const FVector& Point) const
{
	const UWorld* World = GetWorld();
	if (!World)
	{
		return float(Point.Z - 300.0);
	}
	FHitResult Hit;
	FCollisionQueryParams Params(SCENE_QUERY_STAT(DreamSurfaceDripLine), false, GetOwner());
	if (World->LineTraceSingleByChannel(Hit, Point - FVector(0.0, 0.0, 2.0), Point - FVector(0.0, 0.0, 5000.0), ECC_Visibility, Params))
	{
		return float(Hit.ImpactPoint.Z);
	}
	return float(Point.Z - 300.0);
}

void UDreamSurfaceDripLineComponent::OnRegister()
{
	Super::OnRegister();
	const UWorld* World = GetWorld();
	if (!World || !World->IsGameWorld() || Drips)
	{
		return;
	}
	if (UNiagaraSystem* System = Cast<UNiagaraSystem>(Effect.TryLoad()))
	{
		Drips = NewObject<UNiagaraComponent>(this, TEXT("Drips"), RF_Transient);
		Drips->SetAsset(System);
		Drips->SetupAttachment(this);
		Drips->SetUsingAbsoluteLocation(true);
		Drips->SetUsingAbsoluteRotation(true);
		Drips->bAutoActivate = true;
		Drips->RegisterComponent();
	}
	LastRefresh = -1.0e30;
}

void UDreamSurfaceDripLineComponent::OnUnregister()
{
	if (Drips)
	{
		Drips->DestroyComponent();
		Drips = nullptr;
	}
	Super::OnUnregister();
}

void UDreamSurfaceDripLineComponent::Refresh()
{
	FVector Start, End;
	GetLine(Start, End);
	FloorStart = FloorBelow(Start);
	FloorEnd = FloorBelow(End);
	const UWorld* World = GetWorld();
	const UDreamSurfaceSubsystem* Surface = World ? World->GetSubsystem<UDreamSurfaceSubsystem>() : nullptr;
	const UDreamWeatherSubsystem* Weather = World ? World->GetSubsystem<UDreamWeatherSubsystem>() : nullptr;
	if (!Surface || !Weather || !Surface->IsActive())
	{
		Strength = 0.0f;
		return;
	}
	// The roof's top at the eave: the state in the open there, and the rain falling on it.
	const FVector Mid = (Start + End) * 0.5;
	const FDreamSurfaceState Roof = Surface->SampleOpenSurface(Mid);
	const float Rain = Weather->SampleWeather(Mid).Rain;
	Strength = DreamWind::Hlsl::DSF_EaveDrip(Rain, Roof.Wetness, Roof.Ice, DripFromWetness);
}

void UDreamSurfaceDripLineComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	const double Now = FPlatformTime::Seconds();
	if (Now - LastRefresh >= DreamSurface::Drips::RefreshSeconds)
	{
		LastRefresh = Now;
		Refresh();
	}
	FVector Start, End;
	GetLine(Start, End);
	if (Drips)
	{
		Drips->SetVariableFloat(TEXT("User.Rate"), GetDropRate());
		Drips->SetVariableVec3(TEXT("User.LineStart"), Start);
		Drips->SetVariableVec3(TEXT("User.LineEnd"), End);
		Drips->SetVariableFloat(TEXT("User.FloorStart"), FloorStart);
		Drips->SetVariableFloat(TEXT("User.FloorEnd"), FloorEnd);
	}
	// The drops landing ring the puddles below (near the view, where the trail map reaches).
	UWorld* World = GetWorld();
	UDreamSurfaceSubsystem* Surface = World ? World->GetSubsystem<UDreamSurfaceSubsystem>() : nullptr;
	if (!bStirPuddles || !Surface || Strength <= 0.0f)
	{
		PendingStirs = 0.0f;
		return;
	}
	const UDreamWorldSubsystem* Clock = World->GetSubsystem<UDreamWorldSubsystem>();
	const FVector View = Clock && Clock->HasViewLocation() ? Clock->GetViewLocation() : GetComponentLocation();
	if (FVector::Dist2D(View, (Start + End) * 0.5) > DreamSurface::Drips::StirReach + Length)
	{
		PendingStirs = 0.0f;
		return;
	}
	PendingStirs = FMath::Min(PendingStirs + GetDropRate() * DeltaTime, 8.0f);
	while (PendingStirs >= 1.0f)
	{
		PendingStirs -= 1.0f;
		const float Along = FMath::FRand();
		const FVector At = FMath::Lerp(Start, End, Along);
		Surface->Stamp(FVector(At.X, At.Y, FMath::Lerp(FloorStart, FloorEnd, Along)), 6.0f, 0.0f, false, true);
	}
}
