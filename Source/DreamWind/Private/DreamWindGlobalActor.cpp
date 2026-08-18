// Copyright DreamMoon. All Rights Reserved.

#include "DreamWindGlobalActor.h"
#include "DreamWindSubsystem.h"
#include "DreamWindSettings.h"
#include "Components/ArrowComponent.h"
#include "Components/BillboardComponent.h"
#include "Engine/World.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(DreamWindGlobalActor)

ADreamWindGlobal::ADreamWindGlobal()
{
	PrimaryActorTick.bCanEverTick = false;
#if WITH_EDITORONLY_DATA
	bIsSpatiallyLoaded = false;
#endif

	Wind = UDreamWindSettings::Get()->DefaultWind;

#if WITH_EDITORONLY_DATA
	Arrow = CreateEditorOnlyDefaultSubobject<UArrowComponent>(TEXT("WindArrow"));
	if (Arrow)
	{
		Arrow->ArrowColor = FColor(80, 200, 255);
		Arrow->ArrowSize = 3.0f;
		Arrow->ArrowLength = 120.0f;
		Arrow->bIsScreenSizeScaled = true;
		Arrow->SetupAttachment(RootComponent);
	}
#endif
}

void ADreamWindGlobal::PostRegisterAllComponents()
{
	Super::PostRegisterAllComponents();
	UpdateArrow();
	// Editor world: apply immediately so the viewport shows the authored wind.
	if (UWorld* World = GetWorld(); World && !World->IsGameWorld())
	{
		ApplyToWorld();
	}
}

void ADreamWindGlobal::BeginPlay()
{
	Super::BeginPlay();
	ApplyToWorld();
}

void ADreamWindGlobal::ApplyToWorld()
{
	if (UWorld* World = GetWorld())
	{
		if (UDreamWindSubsystem* Subsystem = World->GetSubsystem<UDreamWindSubsystem>())
		{
			Subsystem->SetGlobalWind(Wind, BlendTime);
		}
	}
}

void ADreamWindGlobal::UpdateArrow()
{
#if WITH_EDITORONLY_DATA
	if (Arrow)
	{
		const FVector Dir = Wind.Direction.GetSafeNormal();
		Arrow->SetWorldRotation(Dir.IsNearlyZero() ? FRotator::ZeroRotator : Dir.Rotation());
		Arrow->ArrowLength = FMath::Clamp(60.0f + Wind.Speed * 0.05f, 60.0f, 400.0f);
		Arrow->MarkRenderStateDirty();
	}
#endif
}

#if WITH_EDITOR
void ADreamWindGlobal::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);
	UpdateArrow();
	ApplyToWorld();
}

void ADreamWindGlobal::PostEditMove(bool bFinished)
{
	Super::PostEditMove(bFinished);
	UpdateArrow();
}
#endif
