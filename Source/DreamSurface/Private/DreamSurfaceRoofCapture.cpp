// Copyright DreamMoon. All Rights Reserved.

#include "DreamSurfaceRoofCapture.h"
#include "DreamSurfaceModule.h"
#include "DreamSurfaceSettings.h"

#include "Components/BoxComponent.h"
#include "Components/SceneCaptureComponent2D.h"
#include "DreamWeatherSettings.h"
#include "DreamWeatherSubsystem.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Engine/World.h"
#include "Misc/App.h"
#include "RenderingThread.h"
#include "TextureResource.h"

namespace DreamSurface::Roof
{
	/** Lower than any height: nothing captured here. */
	constexpr float None = -1.0e9f;

	bool CanCapture(const UWorld* World)
	{
		return World && World->IsGameWorld() && World->Scene && FApp::CanEverRender() && !IsRunningDedicatedServer();
	}
}

UDreamSurfaceRoofCaptureComponent::UDreamSurfaceRoofCaptureComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = false;
	SetMobility(EComponentMobility::Movable);
	bWantsOnUpdateTransform = true;
}

double UDreamSurfaceRoofCaptureComponent::GetCaptureZ() const
{
	return GetComponentLocation().Z + Extent.Z + 10.0;
}

void UDreamSurfaceRoofCaptureComponent::OnRegister()
{
	Super::OnRegister();
	if (UDreamWeatherSubsystem* Weather = GetWorld() ? GetWorld()->GetSubsystem<UDreamWeatherSubsystem>() : nullptr)
	{
		Weather->RegisterShelter(this);
	}
	if (DreamSurface::Roof::CanCapture(GetWorld()))
	{
		// The scene is not drawn yet while components register: capture on the first tick.
		bCapturePending = true;
		SetComponentTickEnabled(true);
	}
}

void UDreamSurfaceRoofCaptureComponent::OnUnregister()
{
	if (UDreamWeatherSubsystem* Weather = GetWorld() ? GetWorld()->GetSubsystem<UDreamWeatherSubsystem>() : nullptr)
	{
		Weather->UnregisterShelter(this);
	}
	if (Capture)
	{
		Capture->DestroyComponent();
		Capture = nullptr;
	}
	Super::OnUnregister();
}

void UDreamSurfaceRoofCaptureComponent::OnUpdateTransform(EUpdateTransformFlags UpdateTransformFlags, ETeleportType Teleport)
{
	Super::OnUpdateTransform(UpdateTransformFlags, Teleport);
	if (DreamSurface::Roof::CanCapture(GetWorld()))
	{
		bCapturePending = true;
		SetComponentTickEnabled(true);
	}
}

void UDreamSurfaceRoofCaptureComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	if (bCapturePending)
	{
		CaptureNow();
	}
	SetComponentTickEnabled(false);
}

void UDreamSurfaceRoofCaptureComponent::CaptureNow()
{
	bCapturePending = false;
	UWorld* World = GetWorld();
	if (!DreamSurface::Roof::CanCapture(World))
	{
		return;
	}
	const int32 Size = FMath::Clamp(Resolution, 16, 1024);
	if (!Target || Target->SizeX != Size)
	{
		Target = NewObject<UTextureRenderTarget2D>(this, NAME_None, RF_Transient);
		Target->RenderTargetFormat = RTF_RGBA16f;
		Target->ClearColor = FLinearColor(1.0e4f, 0.0f, 0.0f, 0.0f);
		Target->InitAutoFormat(Size, Size);
		Target->UpdateResourceImmediate(true);
	}
	if (!Capture)
	{
		Capture = NewObject<USceneCaptureComponent2D>(GetOwner() ? static_cast<UObject*>(GetOwner()) : this, NAME_None, RF_Transient);
		Capture->bCaptureEveryFrame = false;
		Capture->bCaptureOnMovement = false;
		Capture->ProjectionType = ECameraProjectionMode::Orthographic;
		Capture->CaptureSource = ESceneCaptureSource::SCS_SceneDepth;
		Capture->bAlwaysPersistRenderingState = false;
		Capture->RegisterComponentWithWorld(World);
	}
	// Straight down from just over the box, turned with its yaw, over a square of its longer side.
	const FVector Centre = GetComponentLocation();
	const double Yaw = GetComponentRotation().Yaw;
	Capture->SetWorldLocationAndRotation(FVector(Centre.X, Centre.Y, GetCaptureZ()), FRotator(-90.0, Yaw, 0.0));
	Capture->OrthoWidth = float(GetCaptureSize());
	Capture->TextureTarget = Target;
	Capture->CaptureScene();
	ReadBack();
	++CaptureCount;
}

void UDreamSurfaceRoofCaptureComponent::ReadBack()
{
	FTextureRenderTargetResource* Resource = Target ? Target->GameThread_GetRenderTargetResource() : nullptr;
	if (!Resource)
	{
		return;
	}
	FlushRenderingCommands();
	TArray<FLinearColor> Pixels;
	Resource->ReadLinearColorPixels(Pixels);
	const int32 Size = Target->SizeX;
	if (Pixels.Num() != Size * Size)
	{
		return;
	}
	// Depth below the camera to height; deeper than the box's own bottom means nothing stood there.
	const double CaptureZ = GetCaptureZ();
	const double Deepest = Extent.Z * 2.0 + 10.0;
	Heights.SetNumUninitialized(Pixels.Num());
	for (int32 i = 0; i < Pixels.Num(); ++i)
	{
		const double Depth = Pixels[i].R;
		Heights[i] = Depth > 0.0 && Depth < Deepest ? float(CaptureZ - Depth) : DreamSurface::Roof::None;
	}
	HeightsSize = Size;
	HeightsFrame = FTransform(FRotator(0.0, GetComponentRotation().Yaw, 0.0), FVector(GetComponentLocation().X, GetComponentLocation().Y, 0.0));
}

void UDreamSurfaceRoofCaptureComponent::SetHeightsForTest(const TArray<float>& InHeights)
{
	const int32 Size = FMath::Clamp(Resolution, 16, 1024);
	if (InHeights.Num() != Size * Size)
	{
		return;
	}
	Heights = InHeights;
	HeightsSize = Size;
	HeightsFrame = FTransform(FRotator(0.0, GetComponentRotation().Yaw, 0.0), FVector(GetComponentLocation().X, GetComponentLocation().Y, 0.0));
}

float UDreamSurfaceRoofCaptureComponent::GetHeightAt(double WorldX, double WorldY) const
{
	if (HeightsSize <= 0)
	{
		return DreamSurface::Roof::None;
	}
	// The capture's texel over the point: its frame (x right, y down the image) is the box's, seen from above.
	const FVector Local = HeightsFrame.InverseTransformPositionNoScale(FVector(WorldX, WorldY, 0.0));
	const double Side = GetCaptureSize();
	const double U = Local.Y / Side + 0.5;
	const double V = -Local.X / Side + 0.5;
	if (U < 0.0 || U >= 1.0 || V < 0.0 || V >= 1.0 || FMath::Abs(Local.X) > Extent.X || FMath::Abs(Local.Y) > Extent.Y)
	{
		return DreamSurface::Roof::None;
	}
	const int32 X = FMath::Clamp(int32(U * HeightsSize), 0, HeightsSize - 1);
	const int32 Y = FMath::Clamp(int32(V * HeightsSize), 0, HeightsSize - 1);
	return Heights[Y * HeightsSize + X];
}

float UDreamSurfaceRoofCaptureComponent::GetShelterAt(const FVector& Position) const
{
	const float Height = GetHeightAt(Position.X, Position.Y);
	if (Height <= DreamSurface::Roof::None * 0.5f)
	{
		return 0.0f;
	}
	// Covered from the roof's clearance down, softly: the materials' cover test (DreamSurfaceMaterial.ush, DSM_Exposure).
	const float Clearance = UDreamWeatherSettings::Get()->RoofClearance;
	const float Softness = FMath::Max(1.0f, UDreamSurfaceSettings::Get()->CoverSoftness);
	const float Below = Height - Clearance - float(Position.Z);
	return FMath::Clamp(Strength, 0.0f, 1.0f) * FMath::Clamp(Below / Softness + 0.5f, 0.0f, 1.0f);
}

ADreamSurfaceRoofCapture::ADreamSurfaceRoofCapture()
{
	PrimaryActorTick.bCanEverTick = false;
	Roof = CreateDefaultSubobject<UDreamSurfaceRoofCaptureComponent>(TEXT("Roof"));
	RootComponent = Roof;
#if WITH_EDITORONLY_DATA
	Outline = CreateEditorOnlyDefaultSubobject<UBoxComponent>(TEXT("Outline"));
	if (Outline)
	{
		Outline->SetupAttachment(Roof);
		Outline->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Outline->SetHiddenInGame(true);
		Outline->ShapeColor = FColor(255, 120, 200);
		Outline->bIsEditorOnly = true;
	}
#endif
}

void ADreamSurfaceRoofCapture::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);
#if WITH_EDITORONLY_DATA
	if (Outline)
	{
		Outline->SetBoxExtent(Roof->Extent, false);
	}
#endif
}
