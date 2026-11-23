// Copyright DreamMoon. All Rights Reserved.

#include "DreamWeatherFog.h"

#include "Components/LocalFogVolumeComponent.h"
#include "DreamGroundFog.h"
#include "Components/PostProcessComponent.h"
#include "DreamWeatherSubsystem.h"
#include "DreamWindSubsystem.h"
#include "Engine/World.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "EngineUtils.h"
#include "HAL/IConsoleManager.h"

// ---- Fog bank ---------------------------------------------------------------------------------------------------

ADreamFogBank::ADreamFogBank()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = true;
	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	FogVolume = CreateDefaultSubobject<UDreamGroundFogComponent>(TEXT("FogVolume"));
	FogVolume->SetupAttachment(RootComponent);
}

void ADreamFogBank::ApplyVolumetric()
{
	if (!FogVolume)
	{
		return;
	}
	// A flat layer on the box's floor (again when the bank moved or changed size).
	const FVector Floor = GetActorLocation();
	const FVector Key(Floor.X + Extent.X * 1.0e-3, Floor.Y + Extent.Y * 1.0e-3, Floor.Z);
	if (!LayerAt.Equals(Key, 0.01))
	{
		FogVolume->SetFlatLayer(Floor, FVector2D(Extent.X, Extent.Y));
		LayerAt = Key;
	}
	// Its mist: as thick as the weather makes it, thinning upward over HeightFalloff.
	FDreamGroundFogParams P;
	P.Radiation = 1.0f;
	P.MistThickness = 2.0f * HeightFalloff;
	P.PoolExtinction = 0.0f;
	P.MistExtinction = DensityNow * DreamWeatherFX::GroundFog::ExtinctionPerDensity;
	P.NoiseSize = 2.5f * PatchRadius;
	P.NoiseAmount = 0.9f;
	P.Churn = 0.01f * PatchRadius;
	P.EdgeFade = 0.35f;
	P.BoxLow = float(Floor.Z) - 10.0f;
	P.BoxHigh = float(Floor.Z + Extent.Z);
	FogVolume->SetFog(P);
}

FVector ADreamFogBank::GetPatchLocation(int32 Index) const
{
	return PatchStates.IsValidIndex(Index) ? PatchStates[Index].Location : FVector::ZeroVector;
}

void ADreamFogBank::Respawn(FPatch& Patch, bool bAnyAge)
{
	// Anywhere in the box, on its floor (the patch's fog thins upward from its centre).
	const FVector Floor = GetActorLocation();
	Patch.Location = Floor + FVector(Random.FRandRange(-Extent.X, Extent.X), Random.FRandRange(-Extent.Y, Extent.Y), Extent.Z * 0.25);
	Patch.Radius = PatchRadius * Random.FRandRange(0.75f, 1.25f);
	Patch.Life = Random.FRandRange(24.0f, 36.0f);
	Patch.Age = bAnyAge ? Random.FRandRange(0.0f, Patch.Life) : 0.0f;
	Patch.Weight = Random.FRandRange(0.6f, 1.0f);
	Patch.Velocity = FVector::ZeroVector;
}

void ADreamFogBank::PostRegisterAllComponents()
{
	Super::PostRegisterAllComponents();
	UWorld* World = GetWorld();
	if (!World || World->IsNetMode(NM_DedicatedServer) || Volumes.Num() == FMath::Clamp(Patches, 1, 64))
	{
		return;
	}
	for (ULocalFogVolumeComponent* Volume : Volumes)
	{
		if (Volume)
		{
			Volume->DestroyComponent();
		}
	}
	Volumes.Reset();
	PatchStates.Reset();
	// The same patches every time for the same bank where it stands.
	Random.Initialize(int32(GetTypeHash(GetActorLocation())));
	for (int32 i = 0; i < FMath::Clamp(Patches, 1, 64); ++i)
	{
		ULocalFogVolumeComponent* Volume = NewObject<ULocalFogVolumeComponent>(this, NAME_None, RF_Transient | RF_TextExportTransient | RF_DuplicateTransient);
		Volume->SetupAttachment(RootComponent);
		Volume->SetUsingAbsoluteLocation(true);
		Volume->SetUsingAbsoluteRotation(true);
		Volume->SetUsingAbsoluteScale(true);
		Volume->RegisterComponent();
		Volumes.Add(Volume);
		FPatch& Patch = PatchStates.AddDefaulted_GetRef();
		Respawn(Patch, true);
	}
	Refresh();
	Apply();
}

void ADreamFogBank::Refresh()
{
	// The weather's fog at the bank: none, the clear share; full, all of it.
	float Weather = 1.0f;
	if (const UDreamWeatherSubsystem* Subsystem = GetWorld() ? GetWorld()->GetSubsystem<UDreamWeatherSubsystem>() : nullptr)
	{
		Weather = Subsystem->IsActive() ? FMath::Clamp(Subsystem->SampleWeather(GetActorLocation()).Fog, 0.0f, 1.0f) : 1.0f;
	}
	DensityNow = Density * FMath::Lerp(ClearWeatherShare, 1.0f, Weather);
}

void ADreamFogBank::Drift(float Seconds)
{
	const UDreamWindSubsystem* Wind = GetWorld() ? GetWorld()->GetSubsystem<UDreamWindSubsystem>() : nullptr;
	const FVector Center = GetActorLocation();
	const float Blend = 1.0f - FMath::Exp(-FMath::Max(WindDrag, 0.0f) * Seconds);
	for (FPatch& Patch : PatchStates)
	{
		// Along the ground with the wind (not up and down with its gusts), round within the box.
		FVector Air = Wind ? Wind->SampleWindVelocity(Patch.Location) : FVector::ZeroVector;
		Air.Z = 0.0;
		Patch.Velocity = FMath::Lerp(Patch.Velocity, Air, double(Blend));
		Patch.Location += Patch.Velocity * Seconds;
		for (int32 Axis = 0; Axis < 2; ++Axis)
		{
			const double Half = FMath::Max(Extent[Axis], 1.0);
			Patch.Location[Axis] = Center[Axis] + (FMath::Fmod(Patch.Location[Axis] - Center[Axis] + Half + 4.0 * Half, 2.0 * Half) - Half);
		}
		// Coming and going over its life, then another somewhere else.
		Patch.Age += Seconds;
		if (Patch.Age >= Patch.Life)
		{
			Respawn(Patch, false);
		}
	}
}

void ADreamFogBank::Apply()
{
	// The marched fog, or the local fog volumes (the low quality level's).
	if (DreamWeatherFX::GroundFog::UseVolumetric() && FogVolume)
	{
		for (ULocalFogVolumeComponent* Volume : Volumes)
		{
			if (Volume)
			{
				Volume->SetVisibility(false);
			}
		}
		ApplyVolumetric();
		return;
	}
	if (FogVolume)
	{
		FogVolume->SetVisibility(false);
	}
	for (int32 i = 0; i < Volumes.Num() && i < PatchStates.Num(); ++i)
	{
		ULocalFogVolumeComponent* Volume = Volumes[i];
		const FPatch& Patch = PatchStates[i];
		if (!Volume)
		{
			continue;
		}
		const float Fade = FMath::Sin(FMath::Clamp(Patch.Age / FMath::Max(Patch.Life, 1.0f), 0.0f, 1.0f) * UE_PI);
		const float Here = DensityNow * Patch.Weight * Fade;
		const float Scale = Patch.Radius / ULocalFogVolumeComponent::GetBaseVolumeSize();
		Volume->SetWorldLocation(Patch.Location);
		Volume->SetWorldScale3D(FVector(Scale, Scale, Scale * 0.6));
		Volume->SetRadialFogExtinction(Here);
		Volume->SetHeightFogExtinction(Here);
		Volume->SetHeightFogFalloff(HeightFalloff);
		Volume->SetHeightFogOffset(-0.4f);
		Volume->SetVisibility(Here > 1.0e-4f);
	}
}

namespace DreamWeatherFX::Fog
{
	static FAutoConsoleCommandWithWorldAndArgs BankCommand(
		TEXT("DreamWeather.FogBank"),
		TEXT("DreamWeather.FogBank [density] [clear weather share]: list the fog banks (their density now and patches), and set ")
		TEXT("their density and how much of it stays with no fog in the weather when given."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
		{
			for (TActorIterator<ADreamFogBank> It(World); It; ++It)
			{
				if (Args.Num() > 0)
				{
					It->Density = FMath::Max(0.0f, FCString::Atof(*Args[0]));
				}
				if (Args.Num() > 1)
				{
					It->ClearWeatherShare = FMath::Clamp(FCString::Atof(*Args[1]), 0.0f, 1.0f);
				}
				It->Refresh();
				UE_LOG(LogTemp, Display, TEXT("Fog bank %s: density %.3f now (%.3f at full fog, %.2f with none), %d patches"), *It->GetActorNameOrLabel(),
					It->GetDensityNow(), It->Density, It->ClearWeatherShare, It->GetPatchCount());
			}
		}));
}

void ADreamFogBank::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	Refresh();
	Drift(DeltaSeconds);
	Apply();
}

// ---- Wind fog ---------------------------------------------------------------------------------------------------

ADreamWindFog::ADreamWindFog()
{
	PrimaryActorTick.bCanEverTick = true;
	PostProcess = CreateDefaultSubobject<UPostProcessComponent>(TEXT("PostProcess"));
	RootComponent = PostProcess;
	PostProcess->bUnbound = true;
}

void ADreamWindFog::PostRegisterAllComponents()
{
	Super::PostRegisterAllComponents();
	if (!Pass)
	{
		if (UMaterialInterface* Parent = Cast<UMaterialInterface>(Material.TryLoad()))
		{
			Pass = UMaterialInstanceDynamic::Create(Parent, this);
			PostProcess->Settings.WeightedBlendables.Array.Add(FWeightedBlendable(1.0f, Pass));
		}
	}
	UpdatePass();
}

void ADreamWindFog::UpdatePass()
{
	if (Pass)
	{
		Pass->SetScalarParameterValue(TEXT("FogBase"), float(GetActorLocation().Z));
		Pass->SetScalarParameterValue(TEXT("FogThickness"), Thickness);
		Pass->SetScalarParameterValue(TEXT("FogAmount"), Amount);
		Pass->SetScalarParameterValue(TEXT("FogReach"), Reach);
	}
}

void ADreamWindFog::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	UpdatePass();
}
