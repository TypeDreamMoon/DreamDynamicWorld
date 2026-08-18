// Copyright DreamMoon. All Rights Reserved.

#include "DreamWindSubsystem.h"
#include "DreamWindModule.h"
#include "DreamWindSettings.h"
#include "DreamWindSourceComponent.h"
#include "DreamWindVolumeRenderer.h"
#include "DreamWindViewExtension.h"

#include "Components/WindDirectionalSourceComponent.h"
#include "Engine/Engine.h"
#include "Engine/TextureRenderTargetVolume.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "Camera/PlayerCameraManager.h"
#include "Materials/MaterialParameterCollection.h"
#include "Materials/MaterialParameterCollectionInstance.h"
#include "Misc/App.h"
#include "SceneInterface.h"
#include "SceneViewExtension.h"
#include "TextureResource.h"
#include "UObject/Package.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(DreamWindSubsystem)

DECLARE_STATS_GROUP(TEXT("DreamWind"), STATGROUP_DreamWind, STATCAT_Advanced);
DECLARE_CYCLE_STAT(TEXT("DreamWind Tick"), STAT_DreamWindTick, STATGROUP_DreamWind);
DECLARE_CYCLE_STAT(TEXT("DreamWind Sample (CPU)"), STAT_DreamWindSample, STATGROUP_DreamWind);

namespace DreamWind
{
	/** Only one world may drive the shared volume render target at a time; game worlds win over the editor world. */
	static TWeakObjectPtr<UDreamWindSubsystem> GVolumeDriver;

	static FVector SafeDirection(const FVector& In)
	{
		const FVector N = In.GetSafeNormal();
		return N.IsNearlyZero() ? FVector(1.0, 0.0, 0.0) : N;
	}

	static FDreamWindGlobalWind LerpWind(const FDreamWindGlobalWind& A, const FDreamWindGlobalWind& B, float T)
	{
		FDreamWindGlobalWind R;
		R.Direction            = SafeDirection(FMath::Lerp(SafeDirection(A.Direction), SafeDirection(B.Direction), T));
		R.Speed                = FMath::Lerp(A.Speed, B.Speed, T);
		R.GustStrength         = FMath::Lerp(A.GustStrength, B.GustStrength, T);
		R.GustWavelength       = FMath::Lerp(A.GustWavelength, B.GustWavelength, T);
		R.GustSpeedScale       = FMath::Lerp(A.GustSpeedScale, B.GustSpeedScale, T);
		R.GustFrequency        = FMath::Lerp(A.GustFrequency, B.GustFrequency, T);
		R.TurbulenceStrength   = FMath::Lerp(A.TurbulenceStrength, B.TurbulenceStrength, T);
		R.TurbulenceWavelength = FMath::Lerp(A.TurbulenceWavelength, B.TurbulenceWavelength, T);
		return R;
	}
}

// -------------------------------------------------------------------------------------------------

UDreamWindSubsystem::UDreamWindSubsystem()
{
}

UDreamWindSubsystem* UDreamWindSubsystem::Get(const UObject* WorldContextObject)
{
	if (!WorldContextObject || !GEngine)
	{
		return nullptr;
	}
	const UWorld* World = GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::ReturnNull);
	return World ? World->GetSubsystem<UDreamWindSubsystem>() : nullptr;
}

bool UDreamWindSubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
	if (!Super::ShouldCreateSubsystem(Outer))
	{
		return false;
	}
	const UWorld* World = Cast<UWorld>(Outer);
	if (!World)
	{
		return false;
	}
	return World->WorldType == EWorldType::Game || World->WorldType == EWorldType::PIE || World->WorldType == EWorldType::Editor;
}

void UDreamWindSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	const UDreamWindSettings* Settings = UDreamWindSettings::Get();
	CurrentWind = Settings->DefaultWind;
	WindTime = 0.0;
	TimeScale = 1.0f;

	VolumeRenderer = MakeShared<FDreamWindVolumeRenderer, ESPMode::ThreadSafe>();

	if (FApp::CanEverRender())
	{
		ViewExtension = FSceneViewExtensions::NewExtension<FDreamWindViewExtension>(GetWorld(), this);
	}

	// A first frame of data so samples taken before the first tick already see the default wind.
	GatherSources();
	BuildFrameData(0.0f);
}

void UDreamWindSubsystem::PostInitialize()
{
	Super::PostInitialize();
}

void UDreamWindSubsystem::Deinitialize()
{
	if (DreamWind::GVolumeDriver.Get() == this)
	{
		DreamWind::GVolumeDriver.Reset();
	}

	if (ViewExtension.IsValid())
	{
		ViewExtension->Detach();
		ViewExtension.Reset();
	}

	if (VolumeRenderer.IsValid())
	{
		VolumeRenderer->EnqueueReset();
		VolumeRenderer.Reset();
	}

	if (EngineWindComponent)
	{
		if (EngineWindComponent->IsRegistered())
		{
			EngineWindComponent->UnregisterComponent();
		}
		EngineWindComponent->DestroyComponent();
		EngineWindComponent = nullptr;
	}

	SourceComponents.Reset();
	Impulses.Reset();

	Super::Deinitialize();
}

bool UDreamWindSubsystem::IsTickable() const
{
	return Super::IsTickable() && !IsTemplate() && GetWorld() != nullptr;
}

bool UDreamWindSubsystem::IsTickableInEditor() const
{
	return UDreamWindSettings::Get()->bTickInEditor;
}

TStatId UDreamWindSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UDreamWindSubsystem, STATGROUP_Tickables);
}

// -------------------------------------------------------------------------------------------------
// Tick
// -------------------------------------------------------------------------------------------------

void UDreamWindSubsystem::Tick(float DeltaTime)
{
	SCOPE_CYCLE_COUNTER(STAT_DreamWindTick);
	Super::Tick(DeltaTime);

	UWorld* World = GetWorld();
	const UDreamWindSettings* Settings = UDreamWindSettings::Get();
	if (!World || !Settings)
	{
		return;
	}

	bEnabledThisFrame = Settings->bEnabled;
	if (!bEnabledThisFrame)
	{
		// Keep the frame data honest (calm) but do nothing else.
		FrameData.Sources.Reset();
		FrameData.P0 = FVector4f(1, 0, 0, 0);
		FrameData.V1.W = 0.0f;
		return;
	}

	const float WindDelta = DeltaTime * TimeScale;
	WindTime += WindDelta;

	AdvanceGlobalWind(DeltaTime);
	GatherSources();
	BuildFrameData(DeltaTime);

	const bool bCanRender = FApp::CanEverRender() && World->Scene != nullptr && !IsRunningDedicatedServer();
	if (bCanRender)
	{
		ResolveAssets();
		UpdateEngineWindSource();
		UpdateVolume(WindDelta);
		UpdateMaterialParameterCollection();
	}

	if (Settings->bShowDebugText)
	{
		UpdateDebugDisplay();
	}

	++TickCounter;
}

void UDreamWindSubsystem::AdvanceGlobalWind(float DeltaTime)
{
	if (!bBlending)
	{
		return;
	}
	BlendElapsed += DeltaTime;
	const float T = BlendDuration > 0.0f ? FMath::Clamp(BlendElapsed / BlendDuration, 0.0f, 1.0f) : 1.0f;
	CurrentWind = DreamWind::LerpWind(BlendFromWind, BlendToWind, FMath::SmoothStep(0.0f, 1.0f, T));
	if (T >= 1.0f)
	{
		CurrentWind = BlendToWind;
		bBlending = false;
	}
}

void UDreamWindSubsystem::GatherSources()
{
	FrameData.Sources.Reset();
	const UDreamWindSettings* Settings = UDreamWindSettings::Get();
	const double Now = WindTime;
	const float DeltaTime = static_cast<float>(FApp::GetDeltaTime());

	// impulses
	for (int32 Index = Impulses.Num() - 1; Index >= 0; --Index)
	{
		FActiveImpulse& Impulse = Impulses[Index];
		if (Now >= Impulse.EndTime && Impulse.bApplied)
		{
			Impulses.RemoveAtSwap(Index);
			continue;
		}
		Impulse.bApplied = true;
		float Envelope = 1.0f;
		if (Impulse.FadeOutTime > 0.0f)
		{
			Envelope = FMath::Clamp(static_cast<float>((Impulse.EndTime - Now) / Impulse.FadeOutTime), 0.0f, 1.0f);
		}
		if (Envelope > 0.0f)
		{
			FrameData.Sources.Add(PackSource(Impulse.Desc, Envelope));
		}
	}

	// components
	for (int32 Index = SourceComponents.Num() - 1; Index >= 0; --Index)
	{
		UDreamWindSourceComponent* Component = SourceComponents[Index].Get();
		if (!Component || !IsValid(Component))
		{
			SourceComponents.RemoveAtSwap(Index);
			continue;
		}
		FDreamWindSourceDesc Desc;
		if (Component->BuildSourceDesc(Desc, DeltaTime))
		{
			if (Component->CullDistance > 0.0f && bHasViewOrigin)
			{
				if (FVector::DistSquared(Desc.Position, VolumeCenter) > FMath::Square(Component->CullDistance + Desc.Radius))
				{
					continue;
				}
			}
			FrameData.Sources.Add(PackSource(Desc, 1.0f));
		}
	}

	// strongest first, then cap what the GPU sees
	FrameData.Sources.Sort([](const DreamWind::Hlsl::FDreamWindSourceData& A, const DreamWind::Hlsl::FDreamWindSourceData& B)
	{
		return FMath::Abs(A.DirectionStrength.w) > FMath::Abs(B.DirectionStrength.w);
	});
	const int32 MaxSources = FMath::Max(1, Settings ? Settings->MaxGpuSources : 64);
	if (FrameData.Sources.Num() > MaxSources)
	{
		FrameData.Sources.SetNum(MaxSources);
	}
}

DreamWind::Hlsl::FDreamWindSourceData UDreamWindSubsystem::PackSource(const FDreamWindSourceDesc& Desc, float StrengthScale)
{
	using namespace DreamWind::Hlsl;
	const FVector Dir = DreamWind::SafeDirection(Desc.Direction);
	FDreamWindSourceData S;
	S.PositionRadius    = float4(float(Desc.Position.X), float(Desc.Position.Y), float(Desc.Position.Z), FMath::Max(Desc.Radius, 1.0f));
	S.DirectionStrength = float4(float(Dir.X), float(Dir.Y), float(Dir.Z), Desc.Strength * StrengthScale);
	S.Misc              = float4(float(static_cast<int32>(Desc.Type)), FMath::Max(Desc.InjectRate, 0.0f), FMath::Clamp(Desc.InnerRadiusFraction, 0.0f, 1.0f), 0.0f);
	return S;
}

void UDreamWindSubsystem::BuildFrameData(float DeltaTime)
{
	const UDreamWindSettings* Settings = UDreamWindSettings::Get();
	const FVector Dir = DreamWind::SafeDirection(CurrentWind.Direction);

	FrameData.P0 = FVector4f(float(Dir.X), float(Dir.Y), float(Dir.Z), CurrentWind.Speed);
	FrameData.P1 = FVector4f(CurrentWind.GustStrength, CurrentWind.GustWavelength, CurrentWind.GustSpeedScale, CurrentWind.GustFrequency);
	FrameData.P2 = FVector4f(CurrentWind.TurbulenceStrength, CurrentWind.TurbulenceWavelength, static_cast<float>(WindTime), static_cast<float>(Settings->Seed));

	// Volume window for this frame. Whether it is actually driven is decided in UpdateVolume,
	// which flips V1.w back to 0 when this world does not own the render target.
	VolumeCenter = ChooseVolumeCenter();
	const FIntVector Res = Settings->VolumeResolution;
	const float Voxel = FMath::Max(Settings->VolumeVoxelSize, 1.0f);
	const FVector3f Extent(float(Res.X) * Voxel, float(Res.Y) * Voxel, float(Res.Z) * Voxel);
	FrameData.V0 = FVector4f(float(VolumeCenter.X), float(VolumeCenter.Y), float(VolumeCenter.Z), Settings->VolumeBorderFade);
	FrameData.V1 = FVector4f(1.0f / Extent.X, 1.0f / Extent.Y, 1.0f / Extent.Z, 0.0f);
	FrameData.VolumeTexture = nullptr;
	FrameData.FrameNumber = GFrameCounter;
}

FVector UDreamWindSubsystem::ChooseVolumeCenter() const
{
	FVector Center = VolumeCenter;
	if (bVolumeCenterOverridden)
	{
		Center = VolumeCenterOverride;
	}
	else
	{
		bool bFound = false;
		if (const UWorld* World = GetWorld(); World && World->IsGameWorld())
		{
			if (const APlayerController* PC = World->GetFirstPlayerController())
			{
				if (PC->PlayerCameraManager)
				{
					Center = PC->PlayerCameraManager->GetCameraLocation();
					bFound = true;
				}
				else if (const APawn* Pawn = PC->GetPawn())
				{
					Center = Pawn->GetActorLocation();
					bFound = true;
				}
			}
		}
		if (!bFound && bHasViewOrigin)
		{
			Center = LatestViewOrigin;
		}
	}

	const UDreamWindSettings* Settings = UDreamWindSettings::Get();
	if (Settings->bVolumeSnapToVoxels)
	{
		const double Voxel = FMath::Max(Settings->VolumeVoxelSize, 1.0f);
		Center.X = FMath::RoundToDouble(Center.X / Voxel) * Voxel;
		Center.Y = FMath::RoundToDouble(Center.Y / Voxel) * Voxel;
		Center.Z = FMath::RoundToDouble(Center.Z / Voxel) * Voxel;
	}
	return Center;
}

void UDreamWindSubsystem::ReportViewOrigin(const FVector& ViewOrigin, bool bIsGameView)
{
	// A game view beats editor views of the same world (PIE-in-editor viewport vs. level viewport).
	if (bHasGameViewOrigin && !bIsGameView)
	{
		return;
	}
	LatestViewOrigin = ViewOrigin;
	bHasViewOrigin = true;
	bHasGameViewOrigin |= bIsGameView;
}

void UDreamWindSubsystem::OverrideVolumeCenter(FVector Center, bool bEnable)
{
	VolumeCenterOverride = Center;
	bVolumeCenterOverridden = bEnable;
}

// -------------------------------------------------------------------------------------------------
// Assets, engine bridge, materials, volume
// -------------------------------------------------------------------------------------------------

void UDreamWindSubsystem::ResolveAssets()
{
	const UDreamWindSettings* Settings = UDreamWindSettings::Get();
	const double Now = FPlatformTime::Seconds();
	const bool bRetryDue = (Now - LastAssetResolveTime) > 5.0;

	if (!ParameterCollection && bRetryDue && Settings->ParameterCollection.IsValid())
	{
		ParameterCollection = Cast<UMaterialParameterCollection>(Settings->ParameterCollection.TryLoad());
		ParameterCollectionInstance = nullptr;
		if (!ParameterCollection && !bAssetsResolved)
		{
			UE_LOG(LogDreamWind, Warning, TEXT("Material Parameter Collection '%s' not found. Materials will not receive wind until Tools > DreamWind > Setup Assets has run."), *Settings->ParameterCollection.ToString());
		}
	}

	if (!VolumeTexture && bRetryDue && Settings->bVolumeEnabled && Settings->VolumeTexture.IsValid())
	{
		VolumeTexture = Cast<UTextureRenderTargetVolume>(Settings->VolumeTexture.TryLoad());
		if (!VolumeTexture && !bAssetsResolved)
		{
			UE_LOG(LogDreamWind, Warning, TEXT("Volume render target '%s' not found. The dynamic wind volume is off until Tools > DreamWind > Setup Assets has run."), *Settings->VolumeTexture.ToString());
		}
	}

	if (VolumeTexture)
	{
		const FIntVector Res = Settings->VolumeResolution;
		const bool bMismatch =
			VolumeTexture->SizeX != Res.X || VolumeTexture->SizeY != Res.Y || VolumeTexture->SizeZ != Res.Z ||
			VolumeTexture->GetFormat() != PF_FloatRGBA || !VolumeTexture->bSupportsUAV;
		if (bMismatch)
		{
			UE_LOG(LogDreamWind, Log, TEXT("Re-initialising wind volume render target '%s' to %dx%dx%d RGBA16F with UAV support (in memory)."),
				*VolumeTexture->GetPathName(), Res.X, Res.Y, Res.Z);
			VolumeTexture->bSupportsUAV = true;
			VolumeTexture->bHDR = true;
			VolumeTexture->OverrideFormat = PF_FloatRGBA;
			VolumeTexture->ClearColor = FLinearColor::Transparent;
			VolumeTexture->Init(Res.X, Res.Y, Res.Z, PF_FloatRGBA);
			VolumeTexture->UpdateResourceImmediate(true);
			if (VolumeRenderer.IsValid())
			{
				VolumeRenderer->EnqueueReset();
			}
		}
	}

	if (bRetryDue)
	{
		LastAssetResolveTime = Now;
		bAssetsResolved = true;
	}
}

void UDreamWindSubsystem::UpdateEngineWindSource()
{
	const UDreamWindSettings* Settings = UDreamWindSettings::Get();
	UWorld* World = GetWorld();

	if (!Settings->bDriveEngineWindSource)
	{
		if (EngineWindComponent)
		{
			if (EngineWindComponent->IsRegistered())
			{
				EngineWindComponent->UnregisterComponent();
			}
			EngineWindComponent->DestroyComponent();
			EngineWindComponent = nullptr;
		}
		return;
	}

	if (!EngineWindComponent)
	{
		// Owner-less, transient, registered straight into the world like FPreviewScene does, so the
		// level is never dirtied and nothing shows in the outliner.
		EngineWindComponent = NewObject<UWindDirectionalSourceComponent>(GetTransientPackage(), NAME_None, RF_Transient);
		EngineWindComponent->bAutoActivate = true;
		EngineWindComponent->bPointWind = false;
		EngineWindComponent->Strength = Settings->EngineWindStrength;
		EngineWindComponent->Speed = 0.0f;
		EngineWindComponent->MinGustAmount = 0.0f;
		EngineWindComponent->MaxGustAmount = 0.0f;
		EngineWindComponent->RegisterComponentWithWorld(World);
		LastEngineDirection = FVector::ZeroVector;
		LastEngineSpeed = LastEngineMinGust = LastEngineMaxGust = LastEngineStrength = -1.0f;
	}

	const FVector Dir = DreamWind::SafeDirection(CurrentWind.Direction);
	const float Speed = CurrentWind.Speed * Settings->EngineWindSpeedScale;
	const float MinGust = 0.0f;
	const float MaxGust = Speed * FMath::Max(CurrentWind.GustStrength, 0.0f);
	const float Strength = Settings->EngineWindStrength;

	if (!Dir.Equals(LastEngineDirection, 1e-4))
	{
		EngineWindComponent->SetWorldRotation(Dir.Rotation());
		LastEngineDirection = Dir;
	}
	if (!FMath::IsNearlyEqual(Speed, LastEngineSpeed, 1e-4f))
	{
		EngineWindComponent->SetSpeed(Speed);
		LastEngineSpeed = Speed;
	}
	if (!FMath::IsNearlyEqual(MinGust, LastEngineMinGust, 1e-4f))
	{
		EngineWindComponent->SetMinimumGustAmount(MinGust);
		LastEngineMinGust = MinGust;
	}
	if (!FMath::IsNearlyEqual(MaxGust, LastEngineMaxGust, 1e-4f))
	{
		EngineWindComponent->SetMaximumGustAmount(MaxGust);
		LastEngineMaxGust = MaxGust;
	}
	if (!FMath::IsNearlyEqual(Strength, LastEngineStrength, 1e-4f))
	{
		EngineWindComponent->SetStrength(Strength);
		LastEngineStrength = Strength;
	}
}

void UDreamWindSubsystem::UpdateMaterialParameterCollection()
{
	if (!ParameterCollection)
	{
		return;
	}
	UWorld* World = GetWorld();
	if (!ParameterCollectionInstance || ParameterCollectionInstance->GetCollection() != ParameterCollection)
	{
		ParameterCollectionInstance = World->GetParameterCollectionInstance(ParameterCollection);
	}
	if (!ParameterCollectionInstance)
	{
		return;
	}
	using namespace DreamWind::Hlsl;
	ParameterCollectionInstance->SetVectorParameterValue(DreamWind::Names::MPC_P0, ToLinearColor(ToHlsl(FrameData.P0)));
	ParameterCollectionInstance->SetVectorParameterValue(DreamWind::Names::MPC_P1, ToLinearColor(ToHlsl(FrameData.P1)));
	ParameterCollectionInstance->SetVectorParameterValue(DreamWind::Names::MPC_P2, ToLinearColor(ToHlsl(FrameData.P2)));
	ParameterCollectionInstance->SetVectorParameterValue(DreamWind::Names::MPC_V0, ToLinearColor(ToHlsl(FrameData.V0)));
	ParameterCollectionInstance->SetVectorParameterValue(DreamWind::Names::MPC_V1, ToLinearColor(ToHlsl(FrameData.V1)));
}

void UDreamWindSubsystem::UpdateVolume(float WindDelta)
{
	const UDreamWindSettings* Settings = UDreamWindSettings::Get();
	UWorld* World = GetWorld();

	if (!Settings->bVolumeEnabled || !VolumeTexture || !VolumeRenderer.IsValid())
	{
		FrameData.V1.W = 0.0f;
		return;
	}

	// Arbitrate the shared render target between worlds.
	UDreamWindSubsystem* Driver = DreamWind::GVolumeDriver.Get();
	if (Driver != this)
	{
		const bool bDriverAlive = Driver && IsValid(Driver) && Driver->GetWorld() && IsValid(Driver->GetWorld());
		const bool bIAmGame = World->IsGameWorld();
		const bool bDriverIsGame = bDriverAlive && Driver->GetWorld()->IsGameWorld();
		if (!bDriverAlive || (bIAmGame && !bDriverIsGame))
		{
			DreamWind::GVolumeDriver = this;
			bVolumeHistoryDirty = true;
		}
		else
		{
			FrameData.V1.W = 0.0f;
			return;
		}
	}

	FTextureRenderTargetResource* Resource = VolumeTexture->GameThread_GetRenderTargetResource();
	if (!Resource)
	{
		FrameData.V1.W = 0.0f;
		return;
	}

	FDreamWindVolumeRenderer::FUpdateArgs Args;
	Args.Frame = FrameData;
	Args.TargetResource = Resource;
	Args.Resolution = Settings->VolumeResolution;
	Args.PrevCenter = FVector3f(PrevVolumeCenter);
	Args.NewCenter = FVector3f(VolumeCenter);
	Args.VoxelSize = FMath::Max(Settings->VolumeVoxelSize, 1.0f);
	Args.DeltaTime = FMath::Max(WindDelta, 0.0f);
	Args.DecayFactor = FMath::Exp(-Args.DeltaTime / FMath::Max(Settings->VolumeDecayTime, 0.01f));
	Args.bAdvect = Settings->bVolumeAdvect;
	Args.bResetHistory = bVolumeHistoryDirty;
	VolumeRenderer->EnqueueUpdate(MoveTemp(Args));

	bVolumeHistoryDirty = false;
	PrevVolumeCenter = VolumeCenter;
	FrameData.V1.W = 1.0f;
	FrameData.VolumeTexture = VolumeTexture;
}

void UDreamWindSubsystem::UpdateDebugDisplay()
{
	if (!GEngine)
	{
		return;
	}
	const FVector Dir = DreamWind::SafeDirection(CurrentWind.Direction);
	const uint64 Key = static_cast<uint64>(GetTypeHash(this)) ^ 0x4D6F6F6E57696E64ull; // "DreamWind"
	GEngine->AddOnScreenDebugMessage(Key, 0.0f, FColor::Cyan,
		FString::Printf(TEXT("DreamWind  dir(%.2f %.2f %.2f) speed %.0f cm/s  gust %.2f  turb %.2f  t=%.1fs  sources %d  volume %s @ (%.0f %.0f %.0f)"),
			Dir.X, Dir.Y, Dir.Z, CurrentWind.Speed, CurrentWind.GustStrength, CurrentWind.TurbulenceStrength, WindTime,
			FrameData.Sources.Num(), FrameData.V1.W > 0.5f ? TEXT("on") : TEXT("off"), VolumeCenter.X, VolumeCenter.Y, VolumeCenter.Z));
}

// -------------------------------------------------------------------------------------------------
// Public API
// -------------------------------------------------------------------------------------------------

void UDreamWindSubsystem::SetGlobalWind(const FDreamWindGlobalWind& Wind, float BlendTime)
{
	if (BlendTime <= 0.0f)
	{
		CurrentWind = Wind;
		bBlending = false;
	}
	else
	{
		BlendFromWind = CurrentWind;
		BlendToWind = Wind;
		BlendDuration = BlendTime;
		BlendElapsed = 0.0f;
		bBlending = true;
	}
}

void UDreamWindSubsystem::SetGlobalWindDirectionAndSpeed(FVector Direction, float Speed, float BlendTime)
{
	FDreamWindGlobalWind Wind = bBlending ? BlendToWind : CurrentWind;
	Wind.Direction = Direction;
	Wind.Speed = FMath::Max(Speed, 0.0f);
	SetGlobalWind(Wind, BlendTime);
}

FVector UDreamWindSubsystem::GetGlobalWindDirection() const
{
	return DreamWind::SafeDirection(CurrentWind.Direction);
}

FDreamWindSampleResult UDreamWindSubsystem::SampleWind(FVector WorldPosition, EDreamWindQuality Quality) const
{
	SCOPE_CYCLE_COUNTER(STAT_DreamWindSample);
	using namespace DreamWind::Hlsl;

	FDreamWindSampleResult Result;
	Result.Direction = GetGlobalWindDirection();
	if (!bEnabledThisFrame && TickCounter > 0)
	{
		return Result;
	}

	const FDreamWindParams P = FrameData.GetParams();
	const float3 Pos = ToHlsl(WorldPosition);
	const FDreamWindSample S = (Quality == EDreamWindQuality::Full) ? DW_SampleAnalytic(P, Pos) : DW_SampleAnalyticCheap(P, Pos);

	float3 V = S.Velocity;
	for (const FDreamWindSourceData& Source : FrameData.Sources)
	{
		V = DW_ApplySource(V, Source, Pos, 1.0f);
	}

	Result.Velocity = ToVector(V);
	Result.Gust = S.Gust;
	Result.Speed = static_cast<float>(Result.Velocity.Size());
	if (Result.Speed > KINDA_SMALL_NUMBER)
	{
		Result.Direction = Result.Velocity / Result.Speed;
	}
	return Result;
}

FVector UDreamWindSubsystem::SampleWindVelocity(FVector WorldPosition) const
{
	return SampleWind(WorldPosition, EDreamWindQuality::Cheap).Velocity;
}

FDreamWindImpulseHandle UDreamWindSubsystem::AddImpulse(const FDreamWindSourceDesc& Source, float Duration, float FadeOutTime)
{
	FActiveImpulse Impulse;
	Impulse.Desc = Source;
	Impulse.Id = NextImpulseId++;
	Impulse.StartTime = WindTime;
	Impulse.EndTime = WindTime + FMath::Max(Duration, 0.0f);
	Impulse.FadeOutTime = FMath::Clamp(FadeOutTime, 0.0f, FMath::Max(Duration, 0.0f));
	Impulses.Add(Impulse);

	FDreamWindImpulseHandle Handle;
	Handle.Id = Impulse.Id;
	return Handle;
}

FDreamWindImpulseHandle UDreamWindSubsystem::AddRadialImpulse(FVector Position, float Radius, float Strength, float Duration)
{
	FDreamWindSourceDesc Desc;
	Desc.Type = EDreamWindSourceType::Radial;
	Desc.Position = Position;
	Desc.Radius = Radius;
	Desc.Strength = Strength;
	Desc.InjectRate = 40.0f;
	Desc.InnerRadiusFraction = 0.1f;
	return AddImpulse(Desc, Duration, Duration * 0.5f);
}

FDreamWindImpulseHandle UDreamWindSubsystem::AddDirectionalImpulse(FVector Position, FVector Direction, float Radius, float Strength, float Duration)
{
	FDreamWindSourceDesc Desc;
	Desc.Type = EDreamWindSourceType::Directional;
	Desc.Position = Position;
	Desc.Direction = Direction;
	Desc.Radius = Radius;
	Desc.Strength = Strength;
	Desc.InjectRate = 25.0f;
	Desc.InnerRadiusFraction = 0.25f;
	return AddImpulse(Desc, Duration, Duration * 0.5f);
}

void UDreamWindSubsystem::CancelImpulse(FDreamWindImpulseHandle Handle)
{
	Impulses.RemoveAll([Handle](const FActiveImpulse& I) { return I.Id == Handle.Id; });
}

void UDreamWindSubsystem::RegisterSourceComponent(UDreamWindSourceComponent* Component)
{
	if (Component)
	{
		SourceComponents.AddUnique(Component);
	}
}

void UDreamWindSubsystem::UnregisterSourceComponent(UDreamWindSourceComponent* Component)
{
	SourceComponents.RemoveAll([Component](const TWeakObjectPtr<UDreamWindSourceComponent>& W) { return !W.IsValid() || W.Get() == Component; });
}
