// Copyright DreamMoon. All Rights Reserved.

#include "DreamWeatherStatusComponent.h"
#include "DreamWeatherFXSubsystem.h"
#include "DreamWeatherSubsystem.h"
#include "DreamWeatherTypes.h"
#include "DreamWindSubsystem.h"

#include "CollisionQueryParams.h"
#include "Components/MeshComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/HitResult.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "NiagaraComponent.h"
#include "NiagaraFunctionLibrary.h"
#include "NiagaraSystem.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(DreamWeatherStatusComponent)

namespace DreamWeatherStatus
{
	/** Move Value toward Target at 1/Seconds of the way a second (linear, so "soaked in 20 s" means it). */
	static float Toward(float Value, float Target, float Seconds, float DeltaTime)
	{
		return FMath::FInterpConstantTo(Value, Target, DeltaTime, 1.0f / FMath::Max(Seconds, 0.01f));
	}
}

UDreamWeatherStatusComponent::UDreamWeatherStatusComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.TickInterval = 0.1f;
}

void UDreamWeatherStatusComponent::BeginPlay()
{
	Super::BeginPlay();
	UpdateStatus(0.0f);
}

void UDreamWeatherStatusComponent::EndPlay(const EEndPlayReason::Type Reason)
{
	if (Breath)
	{
		Breath->DestroyComponent();
		Breath = nullptr;
	}
	Super::EndPlay(Reason);
}

void UDreamWeatherStatusComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	UpdateStatus(DeltaTime);
}

void UDreamWeatherStatusComponent::ResetStatus()
{
	Status.Wetness = Status.SnowCover = Status.Dustiness = Status.Chill = 0.0f;
	UpdateStatus(0.0f);
}

float UDreamWeatherStatusComponent::TracedCover(const FVector& Position, const FVector& RainDirection) const
{
	UWorld* World = GetWorld();
	if (!World || !bTraceShelter)
	{
		return 0.0f;
	}
	FCollisionQueryParams Query(SCENE_QUERY_STAT(DreamWeatherStatus), false, GetOwner());
	FHitResult Hit;
	// Up against the fall direction: rain blown under an eave still lands.
	return World->LineTraceSingleByChannel(Hit, Position, Position - RainDirection * ShelterTraceLength, ECC_Visibility, Query) ? 1.0f : 0.0f;
}

void UDreamWeatherStatusComponent::UpdateStatus(float Seconds)
{
	using namespace DreamWeatherStatus;
	const AActor* Owner = GetOwner();
	const UDreamWeatherSubsystem* Weather = UDreamWeatherSubsystem::Get(this);
	if (!Owner || !Weather)
	{
		return;
	}
	const FVector Position = Owner->GetActorTransform().TransformPosition(SampleOffset);
	const FDreamWeatherSample S = Weather->SampleWeather(Position);

	FVector RainDirection(0.0, 0.0, -1.0);
	if (const UDreamWeatherFXSubsystem* FX = UDreamWeatherFXSubsystem::Get(this))
	{
		const FVector Fall = FX->GetFallVelocity(UDreamWeatherFXSubsystem::RainLayer);
		if (Fall.Z < -1.0)
		{
			RainDirection = Fall.GetSafeNormal();
		}
	}
	const float Open = FMath::Min(S.Exposure, 1.0f - TracedCover(Position, RainDirection));
	Status.bSheltered = Open < 0.5f;
	Status.RainExposure = S.Rain * Open;
	Status.SnowExposure = S.Snow * Open;

	float WindSpeed = S.Wind * WindFullSpeed;
	if (const UDreamWindSubsystem* Wind = UDreamWindSubsystem::Get(this))
	{
		WindSpeed = Wind->SampleWind(Position).Speed;
	}
	Status.WindExposure = FMath::Clamp(WindSpeed / WindFullSpeed, 0.0f, 1.0f);
	Status.DustExposure = FMath::Clamp(S.Dust * (0.3f + 0.7f * Status.WindExposure) * FMath::Lerp(0.3f, 1.0f, Open), 0.0f, 1.0f);
	Status.Temperature = S.Temperature;
	// Wind and wet clothes make it feel colder (about a degree per m/s of wind in the cold, more when wet).
	const float Cold = FMath::Clamp((15.0f - S.Temperature) / 15.0f, 0.0f, 1.0f);
	Status.FeelsLike = S.Temperature - Cold * (WindSpeed / 100.0f) * (0.6f + 0.6f * Status.Wetness) - 3.0f * Status.Wetness * Cold;

	if (Seconds > 0.0f)
	{
		// Wet: up with the rain (and melting snow), down with warmth and wind.
		const float DryRate = (1.0f + FMath::Max(S.Temperature - 15.0f, 0.0f) / 10.0f) * (1.0f + Status.WindExposure);
		const float Melting = S.Temperature > 0.5f ? Status.SnowCover : 0.0f;
		if (Status.RainExposure > 0.01f)
		{
			Status.Wetness = Toward(Status.Wetness, 1.0f, WetSeconds / Status.RainExposure, Seconds);
		}
		else
		{
			Status.Wetness = Toward(Status.Wetness, 0.0f, DrySeconds / DryRate, Seconds);
		}
		// Snow: builds below freezing, melts above it into wetness.
		if (Status.SnowExposure > 0.01f && S.Temperature < 1.0f)
		{
			Status.SnowCover = Toward(Status.SnowCover, 1.0f, SnowSeconds / Status.SnowExposure, Seconds);
		}
		else
		{
			const float Before = Status.SnowCover;
			const float MeltSeconds = S.Temperature > 0.5f ? SnowMeltSeconds / (1.0f + S.Temperature / 5.0f) : SnowMeltSeconds * 4.0f;
			Status.SnowCover = Toward(Status.SnowCover, 0.0f, MeltSeconds, Seconds);
			Status.Wetness = FMath::Min(1.0f, Status.Wetness + (Before - Status.SnowCover) * (Melting > 0.0f ? 0.8f : 0.0f));
		}
		// Dust: blown on, washed off by rain.
		if (Status.DustExposure > 0.01f)
		{
			Status.Dustiness = Toward(Status.Dustiness, 1.0f, DustSeconds / Status.DustExposure, Seconds);
		}
		else
		{
			Status.Dustiness = Toward(Status.Dustiness, 0.0f, DustClearSeconds / (1.0f + 10.0f * Status.RainExposure), Seconds);
		}
		// Chill: faster the colder it feels; warming up above the cold threshold.
		const float Below = ColdTemperature - Status.FeelsLike;
		if (Below > 0.0f)
		{
			Status.Chill = Toward(Status.Chill, 1.0f, ChillSeconds * 15.0f / FMath::Max(Below, 0.5f), Seconds);
		}
		else
		{
			Status.Chill = Toward(Status.Chill, 0.0f, WarmSeconds / (1.0f + FMath::Max(-Below, 0.0f) / 10.0f), Seconds);
		}
	}

	FireEvents();
	WriteMaterials();
	UpdateBreath();
}

void UDreamWeatherStatusComponent::FireEvents()
{
	const float On = ExposureThreshold;
	const float Off = ExposureThreshold * 0.5f;
	const float ColdNow = FMath::Clamp((ColdTemperature - Status.FeelsLike) / 5.0f, 0.0f, 1.0f);
	const float HeatNow = FMath::Clamp((Status.Temperature - HotTemperature) / 5.0f, 0.0f, 1.0f);
	const float Values[(int32)EDreamWeatherExposure::Num] = {
		Status.RainExposure, Status.SnowExposure, Status.WindExposure > 0.5f ? Status.WindExposure : 0.0f, Status.DustExposure,
		ColdNow > 0.0f ? FMath::Max(ColdNow, On + 0.01f) : 0.0f, HeatNow > 0.0f ? FMath::Max(HeatNow, On + 0.01f) : 0.0f,
		Status.Wetness > 0.8f ? 1.0f : (Status.Wetness > 0.7f && bOn[(int32)EDreamWeatherExposure::Soaked] ? 1.0f : 0.0f),
		Status.Chill > 0.8f ? 1.0f : (Status.Chill > 0.7f && bOn[(int32)EDreamWeatherExposure::Freezing] ? 1.0f : 0.0f) };
	for (int32 i = 0; i < (int32)EDreamWeatherExposure::Num; ++i)
	{
		const bool bWas = bOn[i];
		const bool bNow = bWas ? Values[i] > Off : Values[i] > On;
		if (bNow != bWas)
		{
			bOn[i] = bNow;
			OnExposureChanged.Broadcast(EDreamWeatherExposure(i), bNow);
		}
	}
}

void UDreamWeatherStatusComponent::WriteMaterials()
{
	AActor* Owner = GetOwner();
	if (!bWriteMaterialParameters || !Owner)
	{
		return;
	}
	TInlineComponentArray<UMeshComponent*> Meshes(Owner);
	for (UMeshComponent* Mesh : Meshes)
	{
		const TPair<FName, float> Params[] = { { WetParameter, Status.Wetness }, { SnowParameter, Status.SnowCover },
			{ DustParameter, Status.Dustiness }, { WindParameter, Status.WindExposure } };
		for (const TPair<FName, float>& P : Params)
		{
			if (!P.Key.IsNone())
			{
				Mesh->SetScalarParameterValueOnMaterials(P.Key, P.Value);
			}
		}
	}
}

void UDreamWeatherStatusComponent::UpdateBreath()
{
	const float Intensity = bFreezingBreath ? FMath::Clamp((BreathTemperature - Status.FeelsLike) / 10.0f, 0.0f, 1.0f) : 0.0f;
	if (Intensity <= 0.0f)
	{
		if (Breath && Breath->IsActive())
		{
			Breath->Deactivate();
		}
		return;
	}
	UWorld* World = GetWorld();
	if (!Breath && World && World->IsGameWorld())
	{
		UNiagaraSystem* System = Cast<UNiagaraSystem>(BreathEffect.TryLoad());
		AActor* Owner = GetOwner();
		if (System && Owner)
		{
			USkeletalMeshComponent* Body = Owner->FindComponentByClass<USkeletalMeshComponent>();
			const bool bSocket = Body && !BreathSocket.IsNone() && Body->DoesSocketExist(BreathSocket);
			USceneComponent* Parent = bSocket ? Body : Owner->GetRootComponent();
			// At the socket, or at the mouth in the actor's frame (forward +X) - turned with it, never scaled with a
			// stretched root.
			const FTransform Frame = bSocket ? Body->GetSocketTransform(BreathSocket) : Parent->GetComponentTransform();
			const FVector Mouth = Frame.GetLocation() + Frame.GetRotation().RotateVector(bSocket ? FVector::ZeroVector : BreathOffset);
			Breath = UNiagaraFunctionLibrary::SpawnSystemAttached(System, Parent, bSocket ? BreathSocket : NAME_None, Mouth, Frame.Rotator(),
				EAttachLocation::KeepWorldPosition, false, true);
		}
	}
	if (Breath)
	{
		Breath->SetVariableFloat(TEXT("User.Intensity"), Intensity);
		if (!Breath->IsActive())
		{
			Breath->Activate();
		}
	}
}
