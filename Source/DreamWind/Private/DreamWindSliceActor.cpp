// Copyright DreamMoon. All Rights Reserved.

#include "DreamWindSliceActor.h"
#include "DreamWindModule.h"
#include "DreamWindSubsystem.h"

#include "Components/StaticMeshComponent.h"
#include "Engine/CollisionProfile.h"
#include "Engine/Engine.h"
#include "Engine/StaticMesh.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "UObject/ConstructorHelpers.h"

namespace DreamWindSliceImpl
{
	const TCHAR* SliceMaterial = TEXT("/DreamDynamicWorld/Wind/Materials/M_DreamWindSlice.M_DreamWindSlice");
	const TCHAR* ProbeMaterial = TEXT("/DreamDynamicWorld/Wind/M_DreamWindProbeSlice.M_DreamWindProbeSlice");
	const FName ModeName(TEXT("Mode"));
	const FName SpeedName(TEXT("SpeedForFullBrightness"));
	const FName ProbeSliceName(TEXT("ProbeSlice"));
	const FName ProbeMapName(TEXT("ProbeMap"));

	/** The camera of a world: the player's in a game (with its facing), the last view the wind saw in the editor. */
	bool CameraOf(const UWorld* World, FVector& Out, FVector& OutForward)
	{
		OutForward = FVector::ForwardVector;
		if (!World)
		{
			return false;
		}
		if (const APlayerController* PC = World->GetFirstPlayerController())
		{
			FRotator Rotation;
			PC->GetPlayerViewPoint(Out, Rotation);
			OutForward = Rotation.Vector();
			return true;
		}
		if (const UDreamWindSubsystem* Wind = World->GetSubsystem<UDreamWindSubsystem>())
		{
			Out = Wind->GetVolumeCenter();
			return true;
		}
		return false;
	}
}

ADreamWindSliceActor::ADreamWindSliceActor()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = true;
	Plane = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Plane"));
	SetRootComponent(Plane);
	static ConstructorHelpers::FObjectFinder<UStaticMesh> PlaneMesh(TEXT("/Engine/BasicShapes/Plane.Plane"));
	if (PlaneMesh.Succeeded())
	{
		Plane->SetStaticMesh(PlaneMesh.Object);
	}
	Plane->SetCollisionProfileName(UCollisionProfile::NoCollision_ProfileName);
	Plane->SetCastShadow(false);
	// (Not in the distance field itself: the distance field slice would see its own plane.)
	Plane->bAffectDynamicIndirectLighting = false;
	Plane->SetAffectDistanceFieldLighting(false);
}

void ADreamWindSliceActor::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);
	bHasMaterial = false;
	Refresh();
}

void ADreamWindSliceActor::Refresh()
{
	using namespace DreamWindSliceImpl;
	// The plane is 1 m across: scaled to Size, laid flat or stood up across the actor's yaw.
	Plane->SetRelativeScale3D(FVector(Size / 100.0f, Size / 100.0f, 1.0f));
	Plane->SetRelativeRotation(bVertical ? FRotator(0.0, 0.0, 90.0) : FRotator::ZeroRotator);

	const bool bProbe = Mode == EDreamWindSliceMode::DistanceField;
	if (!bHasMaterial || MaterialMode != Mode)
	{
		UMaterialInterface* Parent = LoadObject<UMaterialInterface>(nullptr, bProbe ? ProbeMaterial : SliceMaterial);
		Material = Parent ? UMaterialInstanceDynamic::Create(Parent, this) : nullptr;
		Plane->SetMaterial(0, Material);
		MaterialMode = Mode;
		bHasMaterial = true;
		SinceProbe = 1.0e9f;
	}
	if (!Material)
	{
		return;
	}
	if (!bProbe)
	{
		Material->SetScalarParameterValue(ModeName, float(int32(Mode)));
		Material->SetScalarParameterValue(SpeedName, SpeedForFullBrightness);
		return;
	}
	if (!Probe || Probe->SizeX != ProbeResolution)
	{
		Probe = NewObject<UTextureRenderTarget2D>(this, NAME_None, RF_Transient);
		Probe->bSupportsUAV = true;
		Probe->ClearColor = FLinearColor::Black;
		Probe->InitCustomFormat(ProbeResolution, ProbeResolution, PF_FloatRGBA, true);
		Probe->UpdateResourceImmediate(true);
		Material->SetTextureParameterValue(ProbeSliceName, Probe);
	}
	const FVector At = GetActorLocation();
	Material->SetVectorParameterValue(ProbeMapName, FLinearColor(float(At.X), float(At.Y), 1.0f / Size, ProbeRange));
}

void ADreamWindSliceActor::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (bFollowCamera)
	{
		FVector Camera, Forward;
		if (DreamWindSliceImpl::CameraOf(GetWorld(), Camera, Forward))
		{
			// A flat slice lies under the camera; an upright one stands 15 m ahead of it (through the camera it is edge-on).
			const FVector Ahead = bVertical ? FVector(Forward.X, Forward.Y, 0.0).GetSafeNormal() * 1500.0 : FVector::ZeroVector;
			SetActorLocation(FVector(Camera.X + Ahead.X, Camera.Y + Ahead.Y, GetActorLocation().Z));
		}
	}
	Refresh();
	if (Mode == EDreamWindSliceMode::DistanceField && Probe)
	{
		SinceProbe += DeltaSeconds;
		if (SinceProbe >= ProbeInterval)
		{
			SinceProbe = 0.0f;
			if (UDreamWindSubsystem* Wind = GetWorld() ? GetWorld()->GetSubsystem<UDreamWindSubsystem>() : nullptr)
			{
				Wind->RequestDistanceFieldProbe(Probe, GetActorLocation(), Size);
			}
		}
	}
}

// DreamWind.Slice <mode|off> [height] [size] [yaw for a vertical slice]
static FAutoConsoleCommandWithWorldAndArgs GDreamWindSliceCommand(
	TEXT("DreamWind.Slice"),
	TEXT("DreamWind.Slice <0 wind | 1 terrain | 2 volume | 3 distance field | off> [height cm, default 100] [size cm, default 6000] [yaw: a vertical slice along it]: a debug plane following the camera that paints one layer of the wind."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
	{
		if (!World)
		{
			return;
		}
		ADreamWindSliceActor* Slice = nullptr;
		for (TActorIterator<ADreamWindSliceActor> It(World); It; ++It)
		{
			if (It->HasAnyFlags(RF_Transient))
			{
				Slice = *It;
				break;
			}
		}
		if (Args.Num() == 0 || Args[0].Equals(TEXT("off"), ESearchCase::IgnoreCase))
		{
			if (Slice)
			{
				Slice->Destroy();
			}
			return;
		}
		if (!Slice)
		{
			FActorSpawnParameters Spawn;
			Spawn.ObjectFlags |= RF_Transient;
			Spawn.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
			Slice = World->SpawnActor<ADreamWindSliceActor>(FVector::ZeroVector, FRotator::ZeroRotator, Spawn);
		}
		if (!Slice)
		{
			return;
		}
		Slice->Mode = EDreamWindSliceMode(FMath::Clamp(FCString::Atoi(*Args[0]), 0, 3));
		const float Height = Args.Num() > 1 ? FCString::Atof(*Args[1]) : 100.0f;
		Slice->Size = Args.Num() > 2 ? FMath::Max(FCString::Atof(*Args[2]), 100.0f) : 6000.0f;
		Slice->bVertical = Args.Num() > 3;
		Slice->bFollowCamera = true;
		Slice->SetActorLocationAndRotation(FVector(0.0, 0.0, Height), FRotator(0.0, Args.Num() > 3 ? FCString::Atof(*Args[3]) : 0.0f, 0.0));
		UE_LOG(LogDreamWind, Display, TEXT("Wind slice: mode %d at %.0f cm, %.0f cm wide%s"), int32(Slice->Mode), Height, Slice->Size, Slice->bVertical ? TEXT(", vertical") : TEXT(""));
	}));
