// Copyright DreamMoon. All Rights Reserved.

#include "DreamWindSubsystem.h"
#include "DreamWindModule.h"
#include "DreamWindSettings.h"
#include "DreamWindSourceComponent.h"
#include "DreamWindTerrainMap.h"
#include "DreamWindVolumeRenderer.h"
#include "DreamWindViewExtension.h"

#include "Components/WindDirectionalSourceComponent.h"
#include "DrawDebugHelpers.h"
#include "HAL/IConsoleManager.h"
#include "Engine/Engine.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Engine/TextureRenderTargetVolume.h"
#include "Containers/ResourceArray.h"
#include "RenderingThread.h"
#include "RenderUtils.h"
#include "RHIResources.h"
#include "RHICommandList.h"
#include "RHIUtilities.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
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

	// Console overrides for A/B tests and profiling. -1 = follow the project settings.
	static int32 GCVarEnable = -1;
	static FAutoConsoleVariableRef CVarEnable(TEXT("DreamWind.Enable"), GCVarEnable,
		TEXT("Override the wind master switch: -1 = project setting, 0 = off (calm), 1 = on."), ECVF_Default);
	static int32 GCVarVolume = -1;
	static FAutoConsoleVariableRef CVarVolume(TEXT("DreamWind.Volume"), GCVarVolume,
		TEXT("Override the dynamic volume: -1 = project setting, 0 = off, 1 = on."), ECVF_Default);
	static int32 GCVarDebugDraw = 0;
	static FAutoConsoleVariableRef CVarDebugDraw(TEXT("DreamWind.DebugDraw"), GCVarDebugDraw,
		TEXT("Draw the wind: 1 = volume window, sources and the global direction; 2 = also a field of arrows around the volume centre."), ECVF_Default);
	static float GCVarTimeScale = -1.0f;
	static FAutoConsoleVariableRef CVarTimeScale(TEXT("DreamWind.TimeScale"), GCVarTimeScale,
		TEXT("Override the wind time scale: <0 = the value set through the API (default 1), 0 freezes the wind."), ECVF_Default);

	static bool IsWindEnabledBySettings(const UDreamWindSettings* Settings)
	{
		return GCVarEnable < 0 ? Settings->bEnabled : (GCVarEnable != 0);
	}
	static bool IsVolumeEnabledBySettings(const UDreamWindSettings* Settings)
	{
		return GCVarVolume < 0 ? Settings->bVolumeEnabled : (GCVarVolume != 0);
	}

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

FVector4f FDreamWindTerrainCpuData::Sample(double WorldX, double WorldY) const
{
	if (Width <= 0 || Height <= 0 || Texels.Num() != Width * Height || !Bounds.bIsValid)
	{
		return FVector4f(1, 0, 0, 0);
	}
	const FVector2D Size = Bounds.GetSize();
	const double U = (WorldX - Bounds.Min.X) / FMath::Max(Size.X, 1.0);
	const double V = (WorldY - Bounds.Min.Y) / FMath::Max(Size.Y, 1.0);
	const double FX = FMath::Clamp(U * Width - 0.5, 0.0, double(Width - 1));
	const double FY = FMath::Clamp(V * Height - 0.5, 0.0, double(Height - 1));
	const int32 X0 = FMath::FloorToInt32(FX);
	const int32 Y0 = FMath::FloorToInt32(FY);
	const int32 X1 = FMath::Min(X0 + 1, Width - 1);
	const int32 Y1 = FMath::Min(Y0 + 1, Height - 1);
	const float TX = float(FX - X0);
	const float TY = float(FY - Y0);
	const FVector4f A = Texels[Y0 * Width + X0];
	const FVector4f B = Texels[Y0 * Width + X1];
	const FVector4f C = Texels[Y1 * Width + X0];
	const FVector4f D = Texels[Y1 * Width + X1];
	return FMath::Lerp(FMath::Lerp(A, B, TX), FMath::Lerp(C, D, TX), TY);
}

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
	VolumeRendererFar = MakeShared<FDreamWindVolumeRenderer, ESPMode::ThreadSafe>();

	if (FApp::CanEverRender())
	{
		ViewExtension = FSceneViewExtensions::NewExtension<FDreamWindViewExtension>(GetWorld(), this);
	}

	if (Settings->DefaultTerrainMap.IsValid())
	{
		if (UDreamWindTerrainMap* DefaultMap = Cast<UDreamWindTerrainMap>(Settings->DefaultTerrainMap.TryLoad()))
		{
			SetTerrainMap(DefaultMap);
			bTerrainMapExplicit = false;
		}
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
	if (VolumeRendererFar.IsValid())
	{
		VolumeRendererFar->EnqueueReset();
		VolumeRendererFar.Reset();
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

	bEnabledThisFrame = DreamWind::IsWindEnabledBySettings(Settings);
	if (!bEnabledThisFrame)
	{
		// Keep the frame data honest (calm) but do nothing else.
		FrameData.Sources.Reset();
		FrameData.P0 = FVector4f(1, 0, 0, 0);
		FrameData.V1.W = 0.0f;
		FrameData.V3.W = 0.0f;
		return;
	}

	const float EffectiveTimeScale = DreamWind::GCVarTimeScale >= 0.0f ? DreamWind::GCVarTimeScale : TimeScale;
	const float WindDelta = DeltaTime * EffectiveTimeScale;
	WindTime += WindDelta;

	AdvanceGlobalWind(DeltaTime);
	GatherSources();
	BuildFrameData(DeltaTime);

	const bool bCanRender = FApp::CanEverRender() && World->Scene != nullptr && !IsRunningDedicatedServer();
	if (bCanRender)
	{
		ResolveAssets();
		UpdateEngineWindSource();
		UpdateTerrainTexture();
		UpdateVolume(WindDelta);
		UpdateMaterialParameterCollection();
	}

	if (Settings->bShowDebugText)
	{
		UpdateDebugDisplay();
	}
	if (DreamWind::GCVarDebugDraw > 0)
	{
		DrawDebug();
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

	// Volume windows for this frame. Whether they are actually driven is decided in UpdateVolume,
	// which flips V1.w / V3.w back to 0 when this world does not own the render targets.
	const FVector RawCenter = ChooseVolumeCenter();
	const FIntVector Res = Settings->VolumeResolution;
	const float Voxel = FMath::Max(Settings->VolumeVoxelSize, 1.0f);
	const float VoxelFar = Voxel * FMath::Max(Settings->VolumeFarVoxelScale, 1.0f);
	VolumeCenter = SnapToVoxels(RawCenter, Voxel);
	VolumeCenterFar = SnapToVoxels(RawCenter, VoxelFar);
	const FVector3f Extent(float(Res.X) * Voxel, float(Res.Y) * Voxel, float(Res.Z) * Voxel);
	const FVector3f ExtentFar(float(Res.X) * VoxelFar, float(Res.Y) * VoxelFar, float(Res.Z) * VoxelFar);
	FrameData.V0 = FVector4f(float(VolumeCenter.X), float(VolumeCenter.Y), float(VolumeCenter.Z), Settings->VolumeBorderFade);
	FrameData.V1 = FVector4f(1.0f / Extent.X, 1.0f / Extent.Y, 1.0f / Extent.Z, 0.0f);
	FrameData.V2 = FVector4f(float(VolumeCenterFar.X), float(VolumeCenterFar.Y), float(VolumeCenterFar.Z), Settings->VolumeBorderFade);
	FrameData.V3 = FVector4f(1.0f / ExtentFar.X, 1.0f / ExtentFar.Y, 1.0f / ExtentFar.Z, 0.0f);
	FrameData.VolumeTexture = nullptr;
	FrameData.VolumeFarTexture = nullptr;

	// Terrain map: geometry from the active map; enabled only when both the map and its texture exist
	// (the texture flag is finalised in UpdateTerrainTexture, the CPU side needs only the map).
	const bool bTerrain = Settings->bTerrainEnabled && TerrainCpu.IsValid() && TerrainCpu->Bounds.bIsValid;
	if (bTerrain)
	{
		const FVector2D Size = TerrainCpu->Bounds.GetSize();
		FrameData.T0 = FVector4f(float(TerrainCpu->Bounds.Min.X), float(TerrainCpu->Bounds.Min.Y), 1.0f / float(FMath::Max(Size.X, 1.0)), 1.0f / float(FMath::Max(Size.Y, 1.0)));
		FrameData.T1 = FVector4f(1.0f, Settings->TerrainSteerScale, Settings->TerrainSpeedScale, 0.0f);
	}
	else
	{
		FrameData.T0 = FVector4f::Zero();
		FrameData.T1 = FVector4f::Zero();
	}
	FrameData.TerrainCpu = bTerrain ? TerrainCpu : nullptr;
	FrameData.TerrainTexture = nullptr;
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

	return Center;
}

FVector UDreamWindSubsystem::SnapToVoxels(const FVector& Center, float VoxelSize) const
{
	if (!UDreamWindSettings::Get()->bVolumeSnapToVoxels)
	{
		return Center;
	}
	const double Voxel = FMath::Max(VoxelSize, 1.0f);
	return FVector(
		FMath::RoundToDouble(Center.X / Voxel) * Voxel,
		FMath::RoundToDouble(Center.Y / Voxel) * Voxel,
		FMath::RoundToDouble(Center.Z / Voxel) * Voxel);
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

	if (!VolumeTexture && bRetryDue && DreamWind::IsVolumeEnabledBySettings(Settings) && Settings->VolumeTexture.IsValid())
	{
		VolumeTexture = Cast<UTextureRenderTargetVolume>(Settings->VolumeTexture.TryLoad());
		if (!VolumeTexture && !bAssetsResolved)
		{
			UE_LOG(LogDreamWind, Warning, TEXT("Volume render target '%s' not found. The dynamic wind volume is off until Tools > DreamWind > Setup Assets has run."), *Settings->VolumeTexture.ToString());
		}
	}

	if (!VolumeFarTexture && bRetryDue && Settings->bVolumeFarLevel && DreamWind::IsVolumeEnabledBySettings(Settings) && Settings->VolumeFarTexture.IsValid())
	{
		VolumeFarTexture = Cast<UTextureRenderTargetVolume>(Settings->VolumeFarTexture.TryLoad());
		if (!VolumeFarTexture && !bAssetsResolved)
		{
			UE_LOG(LogDreamWind, Warning, TEXT("Far volume render target '%s' not found. Run Tools > DreamWind > Setup Assets, or turn the far level off."), *Settings->VolumeFarTexture.ToString());
		}
	}

	if (!TerrainTexture && bRetryDue && Settings->bTerrainEnabled && Settings->TerrainTexture.IsValid())
	{
		TerrainTexture = Cast<UTextureRenderTarget2D>(Settings->TerrainTexture.TryLoad());
		bTerrainTextureDirty = TerrainTexture != nullptr;
		if (!TerrainTexture && !bAssetsResolved)
		{
			UE_LOG(LogDreamWind, Warning, TEXT("Terrain render target '%s' not found. Materials ignore the terrain map until Tools > DreamWind > Setup Assets has run."), *Settings->TerrainTexture.ToString());
		}
	}

	if (VolumeTexture)
	{
		EnsureVolumeTextureMatches(VolumeTexture, Settings->VolumeResolution, VolumeRenderer);
	}
	if (VolumeFarTexture)
	{
		EnsureVolumeTextureMatches(VolumeFarTexture, Settings->VolumeResolution, VolumeRendererFar);
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
	ParameterCollectionInstance->SetVectorParameterValue(DreamWind::Names::MPC_V2, ToLinearColor(ToHlsl(FrameData.V2)));
	ParameterCollectionInstance->SetVectorParameterValue(DreamWind::Names::MPC_V3, ToLinearColor(ToHlsl(FrameData.V3)));
	ParameterCollectionInstance->SetVectorParameterValue(DreamWind::Names::MPC_T0, ToLinearColor(ToHlsl(FrameData.T0)));
	ParameterCollectionInstance->SetVectorParameterValue(DreamWind::Names::MPC_T1, ToLinearColor(ToHlsl(FrameData.T1)));
}

void UDreamWindSubsystem::EnsureVolumeTextureMatches(UTextureRenderTargetVolume* Texture, const FIntVector& Res, const TSharedPtr<FDreamWindVolumeRenderer, ESPMode::ThreadSafe>& Renderer)
{
	const bool bMismatch =
		Texture->SizeX != Res.X || Texture->SizeY != Res.Y || Texture->SizeZ != Res.Z ||
		Texture->GetFormat() != PF_FloatRGBA || !Texture->bSupportsUAV;
	if (!bMismatch)
	{
		return;
	}
	UE_LOG(LogDreamWind, Log, TEXT("Re-initialising wind volume render target '%s' to %dx%dx%d RGBA16F with UAV support (in memory)."),
		*Texture->GetPathName(), Res.X, Res.Y, Res.Z);
	Texture->bSupportsUAV = true;
	Texture->bHDR = true;
	Texture->OverrideFormat = PF_FloatRGBA;
	Texture->ClearColor = FLinearColor::Transparent;
	Texture->Init(Res.X, Res.Y, Res.Z, PF_FloatRGBA);
	Texture->UpdateResourceImmediate(true);
	if (Renderer.IsValid())
	{
		Renderer->EnqueueReset();
	}
}

void UDreamWindSubsystem::UpdateVolume(float WindDelta)
{
	const UDreamWindSettings* Settings = UDreamWindSettings::Get();
	UWorld* World = GetWorld();

	FrameData.V1.W = 0.0f;
	FrameData.V3.W = 0.0f;
	if (!DreamWind::IsVolumeEnabledBySettings(Settings) || !VolumeTexture || !VolumeRenderer.IsValid())
	{
		return;
	}

	// Arbitrate the shared render targets between worlds.
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
			bVolumeHistoryDirtyFar = true;
		}
		else
		{
			return;
		}
	}

	const float Voxel = FMath::Max(Settings->VolumeVoxelSize, 1.0f);
	if (UpdateVolumeLevel(WindDelta, VolumeTexture, VolumeRenderer, Voxel, VolumeCenter, PrevVolumeCenter, bVolumeHistoryDirty))
	{
		FrameData.V1.W = 1.0f;
		FrameData.VolumeTexture = VolumeTexture;
	}
	if (Settings->bVolumeFarLevel && VolumeFarTexture && VolumeRendererFar.IsValid())
	{
		const float VoxelFar = Voxel * FMath::Max(Settings->VolumeFarVoxelScale, 1.0f);
		if (UpdateVolumeLevel(WindDelta, VolumeFarTexture, VolumeRendererFar, VoxelFar, VolumeCenterFar, PrevVolumeCenterFar, bVolumeHistoryDirtyFar))
		{
			FrameData.V3.W = 1.0f;
			FrameData.VolumeFarTexture = VolumeFarTexture;
		}
	}
}

bool UDreamWindSubsystem::UpdateVolumeLevel(float WindDelta, UTextureRenderTargetVolume* Texture, const TSharedPtr<FDreamWindVolumeRenderer, ESPMode::ThreadSafe>& Renderer,
	float VoxelSize, const FVector& Center, FVector& InOutPrevCenter, bool& bInOutHistoryDirty)
{
	const UDreamWindSettings* Settings = UDreamWindSettings::Get();
	FTextureRenderTargetResource* Resource = Texture->GameThread_GetRenderTargetResource();
	if (!Resource)
	{
		return false;
	}

	FDreamWindVolumeRenderer::FUpdateArgs Args;
	Args.Frame = FrameData;
	Args.TargetResource = Resource;
	Args.TerrainResource = (TerrainTexture && FrameData.T1.X > 0.5f) ? TerrainTexture->GameThread_GetRenderTargetResource() : nullptr;
	Args.Resolution = Settings->VolumeResolution;
	Args.PrevCenter = FVector3f(InOutPrevCenter);
	Args.NewCenter = FVector3f(Center);
	Args.VoxelSize = VoxelSize;
	Args.DeltaTime = FMath::Max(WindDelta, 0.0f);
	Args.DecayFactor = FMath::Exp(-Args.DeltaTime / FMath::Max(Settings->VolumeDecayTime, 0.01f));
	Args.bAdvect = Settings->bVolumeAdvect;
	Args.bResetHistory = bInOutHistoryDirty;
	Renderer->EnqueueUpdate(MoveTemp(Args));

	bInOutHistoryDirty = false;
	InOutPrevCenter = Center;
	return true;
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
			FrameData.Sources.Num(), FrameData.V1.W > 0.5f ? (FrameData.V3.W > 0.5f ? TEXT("near+far") : TEXT("near")) : TEXT("off"), VolumeCenter.X, VolumeCenter.Y, VolumeCenter.Z));
}

void UDreamWindSubsystem::UpdateTerrainTexture()
{
	const UDreamWindSettings* Settings = UDreamWindSettings::Get();
	if (!Settings->bTerrainEnabled || !TerrainTexture)
	{
		FrameData.T1.X = 0.0f;   // GPU consumers: no texture, no terrain
		return;
	}
	if (!TerrainCpu.IsValid() || !TerrainCpu->Bounds.bIsValid)
	{
		FrameData.T1.X = 0.0f;
		return;
	}

	if (bTerrainTextureDirty)
	{
		bTerrainTextureDirty = false;
		const int32 W = TerrainCpu->Width;
		const int32 H = TerrainCpu->Height;
		if (TerrainTexture->SizeX != W || TerrainTexture->SizeY != H || TerrainTexture->GetFormat() != PF_FloatRGBA)
		{
			TerrainTexture->InitCustomFormat(W, H, PF_FloatRGBA, true);
			TerrainTexture->UpdateResourceImmediate(false);
		}
		FTextureRenderTargetResource* Resource = TerrainTexture->GameThread_GetRenderTargetResource();
		if (Resource)
		{
			// Upload through a staging texture created with the data, then a GPU copy: works for
			// render targets on every RHI, unlike a raw UpdateTexture2D.
			TSharedPtr<const FDreamWindTerrainCpuData, ESPMode::ThreadSafe> Data = TerrainCpu;
			ENQUEUE_RENDER_COMMAND(DreamWindTerrainUpload)(
				[Resource, Data, W, H](FRHICommandListImmediate& RHICmdList)
				{
					FRHITexture* Dst = Resource->GetRenderTargetTexture();
					if (!Dst)
					{
						return;
					}
					TArray<FFloat16Color> Pixels;
					Pixels.SetNumUninitialized(W * H);
					for (int32 i = 0; i < W * H; ++i)
					{
						const FVector4f& T = Data->Texels[i];
						Pixels[i] = FFloat16Color(FLinearColor(T.X, T.Y, T.Z, T.W));
					}
					struct FPixelsBulkData final : public FResourceBulkDataInterface
					{
						const void* Ptr; uint32 Bytes;
						FPixelsBulkData(const void* InPtr, uint32 InBytes) : Ptr(InPtr), Bytes(InBytes) {}
						virtual const void* GetResourceBulkData() const override { return Ptr; }
						virtual uint32 GetResourceBulkDataSize() const override { return Bytes; }
						virtual void Discard() override {}
					};
					FPixelsBulkData BulkData(Pixels.GetData(), uint32(Pixels.Num() * sizeof(FFloat16Color)));
					const FRHITextureCreateDesc Desc = FRHITextureCreateDesc::Create2D(TEXT("DreamWind.TerrainUpload"), W, H, PF_FloatRGBA)
						.SetFlags(ETextureCreateFlags::ShaderResource)
						.SetInitialState(ERHIAccess::CopySrc)
						.SetInitActionBulkData(&BulkData);
					FTextureRHIRef Staging = RHICmdList.CreateTexture(Desc);
					if (Staging.IsValid())
					{
						TransitionAndCopyTexture(RHICmdList, Staging, Dst, FRHICopyTextureInfo());
					}
				});
		}
	}
	FrameData.T1.X = 1.0f;
	FrameData.TerrainTexture = TerrainTexture;
}

void UDreamWindSubsystem::SetTerrainMap(UDreamWindTerrainMap* Map)
{
	bTerrainMapExplicit = true;
	if (TerrainMap == Map && (Map == nullptr || TerrainCpu.IsValid()))
	{
		return;
	}
	TerrainMap = Map;
	if (Map && Map->IsValidMap())
	{
		TSharedRef<FDreamWindTerrainCpuData, ESPMode::ThreadSafe> Cpu = MakeShared<FDreamWindTerrainCpuData, ESPMode::ThreadSafe>();
		Cpu->Bounds = Map->Bounds;
		Cpu->Width = Map->Width;
		Cpu->Height = Map->Height;
		Cpu->Texels = Map->Texels;
		TerrainCpu = Cpu;
	}
	else
	{
		TerrainCpu.Reset();
	}
	bTerrainTextureDirty = true;
}

DreamWind::Hlsl::FDreamWindSample UDreamWindSubsystem::ApplyTerrain(DreamWind::Hlsl::FDreamWindSample S, DreamWind::Hlsl::float3 Pos) const
{
	using namespace DreamWind::Hlsl;
	if (!FrameData.TerrainCpu.IsValid() || FrameData.T1.X <= 0.0f)
	{
		return S;
	}
	const FDreamWindTerrainParams T = FrameData.GetTerrainParams();
	const float2 UV = DW_TerrainUV(Pos, T);
	const float Inside = DW_TerrainInside(UV, T);
	if (Inside <= 0.0f)
	{
		return S;
	}
	const FVector4f Texel = FrameData.TerrainCpu->Sample(Pos.x, Pos.y);
	return DW_ApplyTerrain(S, ToHlsl(Texel), T, Inside);
}

void UDreamWindSubsystem::DrawDebug()
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}
	using namespace DreamWind::Hlsl;
	const UDreamWindSettings* Settings = UDreamWindSettings::Get();
	const FVector Dir = DreamWind::SafeDirection(CurrentWind.Direction);

	// volume window
	if (FrameData.V1.W > 0.5f)
	{
		const FIntVector Res = Settings->VolumeResolution;
		const float Voxel = FMath::Max(Settings->VolumeVoxelSize, 1.0f);
		const FVector HalfExtent(Res.X * Voxel * 0.5, Res.Y * Voxel * 0.5, Res.Z * Voxel * 0.5);
		DrawDebugBox(World, VolumeCenter, HalfExtent, FColor(80, 200, 255), false, -1.0f, 0, 2.0f);
	}
	if (FrameData.V3.W > 0.5f)
	{
		const FIntVector Res = Settings->VolumeResolution;
		const float Voxel = FMath::Max(Settings->VolumeVoxelSize, 1.0f) * FMath::Max(Settings->VolumeFarVoxelScale, 1.0f);
		const FVector HalfExtent(Res.X * Voxel * 0.5, Res.Y * Voxel * 0.5, Res.Z * Voxel * 0.5);
		DrawDebugBox(World, VolumeCenterFar, HalfExtent, FColor(40, 120, 200), false, -1.0f, 0, 1.0f);
	}

	// global direction at the volume centre / camera
	const FVector ArrowBase = VolumeCenter + FVector(0, 0, 50);
	DrawDebugDirectionalArrow(World, ArrowBase, ArrowBase + Dir * FMath::Clamp(CurrentWind.Speed * 0.5f, 100.0f, 800.0f), 60.0f, FColor::Cyan, false, -1.0f, 0, 4.0f);
	DrawDebugString(World, ArrowBase + FVector(0, 0, 40), FString::Printf(TEXT("DreamWind %.0f cm/s  gust %.2f  t %.1f"), CurrentWind.Speed, CurrentWind.GustStrength, WindTime), nullptr, FColor::Cyan, 0.0f, true);

	// sources
	for (const FDreamWindSourceData& S : FrameData.Sources)
	{
		const FVector Centre(S.PositionRadius.x, S.PositionRadius.y, S.PositionRadius.z);
		const int32 Type = int32(S.Misc.x);
		const FColor Colour = Type == DW_SOURCE_RADIAL ? FColor::Orange : (Type == DW_SOURCE_VORTEX ? FColor::Magenta : FColor::Yellow);
		DrawDebugSphere(World, Centre, S.PositionRadius.w, 16, Colour, false, -1.0f, 0, 1.0f);
		if (Type != DW_SOURCE_RADIAL)
		{
			const FVector Axis(S.DirectionStrength.x, S.DirectionStrength.y, S.DirectionStrength.z);
			DrawDebugDirectionalArrow(World, Centre, Centre + Axis * FMath::Min(S.PositionRadius.w, 300.0f), 40.0f, Colour, false, -1.0f, 0, 3.0f);
		}
	}

	// a coarse field of arrows: what the CPU sample says around the centre (level 2)
	if (DreamWind::GCVarDebugDraw >= 2)
	{
		const float Spacing = 400.0f;
		for (int32 X = -6; X <= 6; ++X)
		{
			for (int32 Y = -6; Y <= 6; ++Y)
			{
				const FVector P = VolumeCenter + FVector(X * Spacing, Y * Spacing, 0.0);
				const FDreamWindSampleResult R = SampleWind(P, EDreamWindQuality::Cheap);
				const float Len = FMath::Clamp(R.Speed * 0.25f, 10.0f, Spacing * 0.9f);
				const uint8 G = uint8(FMath::Clamp(R.Gust, 0.0f, 2.0f) * 127.0f);
				DrawDebugDirectionalArrow(World, P, P + R.Direction * Len, 25.0f, FColor(255, G, 60), false, -1.0f, 0, 1.5f);
			}
		}
	}
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
	const FDreamWindSample S = ApplyTerrain((Quality == EDreamWindQuality::Full) ? DW_SampleAnalytic(P, Pos) : DW_SampleAnalyticCheap(P, Pos), Pos);

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
