// Copyright DreamMoon. All Rights Reserved.

#include "DreamWindSubsystem.h"
#include "DreamWindModule.h"
#include "DreamWindSettings.h"
#include "DreamWindSourceComponent.h"
#include "DreamWindPreset.h"
#include "DreamWindTerrainMap.h"
#include "DreamWindVolumeRenderer.h"
#include "DreamWindViewExtension.h"
#include "DreamWindTerrainField.h"
#include "DataDrivenShaderPlatformInfo.h"

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
#include "DreamWorldSubsystem.h"
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
	// Console overrides for A/B tests and profiling. -1 = follow the project settings.
	static int32 GCVarEnable = -1;
	static FAutoConsoleVariableRef CVarEnable(TEXT("DreamWind.Enable"), GCVarEnable,
		TEXT("Override the wind master switch: -1 = project setting, 0 = off (calm), 1 = on."), ECVF_Default);
	static int32 GCVarVolume = -1;
	static FAutoConsoleVariableRef CVarVolume(TEXT("DreamWind.Volume"), GCVarVolume,
		TEXT("Override the dynamic volume: -1 = project setting, 0 = off, 1 = on."), ECVF_Default);
	static int32 GCVarDebugDraw = 0;
	static FAutoConsoleVariableRef CVarDebugDraw(TEXT("DreamWind.DebugDraw"), GCVarDebugDraw,
		TEXT("Draw the wind: 1 = volume windows, sources and the global direction; 2 = also the arrow field (a lattice fixed in world space, drawn around the camera); 3 = the arrow field only."), ECVF_Default);
	static int32 GCVarDebugField3D = 1;
	static FAutoConsoleVariableRef CVarDebugField3D(TEXT("DreamWind.DebugDraw.Field3D"), GCVarDebugField3D,
		TEXT("Arrow field shape: 1 = a 3D lattice inside a sphere of Range around the camera (default); 0 = flat sheet(s): Layers horizontal layers starting at world Z = Height, within Range in XY."), ECVF_Default);
	static float GCVarDebugRange = 2000.0f;
	static FAutoConsoleVariableRef CVarDebugRange(TEXT("DreamWind.DebugDraw.Range"), GCVarDebugRange,
		TEXT("Arrow field: radius around the camera (cm) inside which lattice cells are drawn. Default 2000."), ECVF_Default);
	static float GCVarDebugSpacing = 250.0f;
	static FAutoConsoleVariableRef CVarDebugSpacing(TEXT("DreamWind.DebugDraw.Spacing"), GCVarDebugSpacing,
		TEXT("Arrow field: lattice spacing (cm); the lattice is anchored at the world origin in X, Y and Z. Default 250. Widened automatically when the range would need more than 4096 cells."), ECVF_Default);
	static float GCVarDebugHeight = 100.0f;
	static FAutoConsoleVariableRef CVarDebugHeight(TEXT("DreamWind.DebugDraw.Height"), GCVarDebugHeight,
		TEXT("Arrow field, sheet mode (Field3D 0): world Z of the first layer (cm). Default 100."), ECVF_Default);
	static int32 GCVarDebugLayers = 1;
	static FAutoConsoleVariableRef CVarDebugLayers(TEXT("DreamWind.DebugDraw.Layers"), GCVarDebugLayers,
		TEXT("Arrow field, sheet mode (Field3D 0): number of horizontal layers, one Spacing apart, starting at Height. Default 1."), ECVF_Default);
	static float GCVarDebugMaxSpeed = 1500.0f;
	static FAutoConsoleVariableRef CVarDebugMaxSpeed(TEXT("DreamWind.DebugDraw.MaxSpeed"), GCVarDebugMaxSpeed,
		TEXT("Arrow field: wind speed (cm/s) that maps to the top of the colour ramp / full brightness. Default 1500."), ECVF_Default);
	static float GCVarDebugScale = 0.25f;
	static FAutoConsoleVariableRef CVarDebugScale(TEXT("DreamWind.DebugDraw.Scale"), GCVarDebugScale,
		TEXT("Arrow field: arrow length in cm per cm/s of wind (clamped to 90% of the spacing). Default 0.25."), ECVF_Default);
	static int32 GCVarDebugColor = 0;
	static FAutoConsoleVariableRef CVarDebugColor(TEXT("DreamWind.DebugDraw.Color"), GCVarDebugColor,
		TEXT("Arrow field colouring: 0 = speed ramp (blue -> green -> yellow -> red up to MaxSpeed), 1 = direction hue like M_DreamWindDebug (+X red, +Y green, -X cyan) with brightness = speed, 2 = gust factor (blue = lull, white = nominal, red = strong gust)."), ECVF_Default);
	static int32 GCVarOcclusion = -1;
	static FAutoConsoleVariableRef CVarOcclusion(TEXT("DreamWind.Occlusion"), GCVarOcclusion,
		TEXT("Override the terrain map's wind shadow: -1 = project setting (Occlusion Strength), 0 = off (every consumer skips its upwind march), 1 = on at the project's strength."), ECVF_Default);
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

void FDreamWindTerrainCpuData::BuildFromMap(const UDreamWindTerrainMap& Map)
{
	Bounds = Map.Bounds;
	Width = Map.Width;
	Height = Map.Height;
	HeightBase = float(Map.HeightRange.X);
	Texels.Reset();
	if (!Map.IsValidMap())
	{
		Width = Height = 0;
		return;
	}
	// level 0 = the map, then the max chain below it: level L is (W >> L) x (H >> L) starting at row
	// 2H - 2H / 2^L (see DW_TerrainAtlasUV); .x = 1, .yz = 0 there so a stray fetch is harmless
	Texels.Init(FVector4f(1, 0, 0, 0), Width * Height * 2);
	FMemory::Memcpy(Texels.GetData(), Map.Texels.GetData(), Map.Texels.Num() * sizeof(FVector4f));
	int32 PrevRow = 0, PrevW = Width, PrevH = Height;
	for (int32 L = 1; L <= DW_TERRAIN_MAX_LEVEL; ++L)
	{
		const int32 LW = FMath::Max(Width >> L, 1);
		const int32 LH = FMath::Max(Height >> L, 1);
		const int32 Row = 2 * Height - ((2 * Height) >> L);
		for (int32 y = 0; y < LH; ++y)
		{
			for (int32 x = 0; x < LW; ++x)
			{
				float M = -FLT_MAX;
				for (int32 dy = 0; dy < 2; ++dy)
				{
					for (int32 dx = 0; dx < 2; ++dx)
					{
						const int32 sx = FMath::Min(2 * x + dx, PrevW - 1);
						const int32 sy = FMath::Min(2 * y + dy, PrevH - 1);
						M = FMath::Max(M, Texels[(PrevRow + sy) * Width + sx].W);
					}
				}
				Texels[(Row + y) * Width + x] = FVector4f(1, 0, 0, M);
			}
		}
		PrevRow = Row; PrevW = LW; PrevH = LH;
	}
}

FVector4f FDreamWindTerrainCpuData::Sample(double WorldX, double WorldY) const
{
	if (Width <= 0 || Height <= 0 || Texels.Num() != Width * Height * 2 || !Bounds.bIsValid)
	{
		return FVector4f(1, 0, 0, 0);
	}
	const FVector2D Size = Bounds.GetSize();
	const double U = (WorldX - Bounds.Min.X) / FMath::Max(Size.X, 1.0);
	const double V = (WorldY - Bounds.Min.Y) / FMath::Max(Size.Y, 1.0);
	// texel centres at (i + 0.5) / N, matching a GPU bilinear fetch of level 0
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

float FDreamWindTerrainCpuData::HeightBilinear(double WorldX, double WorldY, int32 Level) const
{
	if (Width <= 0 || Height <= 0 || Texels.Num() != Width * Height * 2 || !Bounds.bIsValid)
	{
		return 0.0f;
	}
	Level = FMath::Clamp(Level, 0, DW_TERRAIN_MAX_LEVEL);
	const int32 LW = FMath::Max(Width >> Level, 1);
	const int32 LH = FMath::Max(Height >> Level, 1);
	const int32 Row = Level == 0 ? 0 : 2 * Height - ((2 * Height) >> Level);
	const FVector2D Size = Bounds.GetSize();
	const double U = (WorldX - Bounds.Min.X) / FMath::Max(Size.X, 1.0);
	const double V = (WorldY - Bounds.Min.Y) / FMath::Max(Size.Y, 1.0);
	// texel centres at (i + 0.5) / N, matching a GPU bilinear fetch of the same level (DW_TerrainAtlasUV)
	const double FX = FMath::Clamp(U * LW - 0.5, 0.0, double(LW - 1));
	const double FY = FMath::Clamp(V * LH - 0.5, 0.0, double(LH - 1));
	const int32 X0 = FMath::FloorToInt32(FX);
	const int32 Y0 = FMath::FloorToInt32(FY);
	const int32 X1 = FMath::Min(X0 + 1, LW - 1);
	const int32 Y1 = FMath::Min(Y0 + 1, LH - 1);
	const float TX = float(FX - X0);
	const float TY = float(FY - Y0);
	const float A = Texels[(Row + Y0) * Width + X0].W;
	const float B = Texels[(Row + Y0) * Width + X1].W;
	const float C = Texels[(Row + Y1) * Width + X0].W;
	const float D = Texels[(Row + Y1) * Width + X1].W;
	return FMath::Lerp(FMath::Lerp(A, B, TX), FMath::Lerp(C, D, TX), TY);
}

float FDreamWindTerrainCpuData::HeightAt(double WorldX, double WorldY, int32 Level) const
{
	if (Width <= 0 || Height <= 0 || Texels.Num() != Width * Height * 2 || !Bounds.bIsValid)
	{
		return 0.0f;
	}
	Level = FMath::Clamp(Level, 0, DW_TERRAIN_MAX_LEVEL);
	const int32 LW = FMath::Max(Width >> Level, 1);
	const int32 LH = FMath::Max(Height >> Level, 1);
	const int32 Row = Level == 0 ? 0 : 2 * Height - ((2 * Height) >> Level);
	const FVector2D Size = Bounds.GetSize();
	const double U = FMath::Clamp((WorldX - Bounds.Min.X) / FMath::Max(Size.X, 1.0), 0.0, 1.0);
	const double V = FMath::Clamp((WorldY - Bounds.Min.Y) / FMath::Max(Size.Y, 1.0), 0.0, 1.0);
	// the texel that contains the point, matching DW_TerrainAtlasTexelUV
	const int32 X = FMath::Clamp(FMath::FloorToInt32(U * LW), 0, LW - 1);
	const int32 Y = FMath::Clamp(FMath::FloorToInt32(V * LH), 0, LH - 1);
	return Texels[(Row + Y) * Width + X].W;
}

float FDreamWindTerrainCpuData::OcclusionShadow(const DreamWind::Hlsl::FDreamWindTerrainParams& T, const DreamWind::Hlsl::float3& Pos, const DreamWind::Hlsl::float3& Velocity) const
{
	using namespace DreamWind::Hlsl;
	const float Speed2 = Velocity.x * Velocity.x + Velocity.y * Velocity.y;
	if (!DW_OcclusionActive(T, FMath::Sqrt(Speed2)))
	{
		return 0.0f;
	}
	float Shadow = DW_OcclusionSelf(HeightBilinear(Pos.x, Pos.y, 0) + T.HeightBase, Pos.z);
	if (Shadow >= 1.0f)
	{
		return Shadow;
	}
	const float InvSpeed = 1.0f / FMath::Sqrt(Speed2);
	const float2 UpDir(-Velocity.x * InvSpeed, -Velocity.y * InvSpeed);
	for (int32 k = 1; k <= DW_OCCLUSION_TAPS; ++k)
	{
		const int32 Level = DW_OcclusionTapLevel(k, T);
		const float TapDist = DW_OcclusionTapDistance(k, T);
		const float2 XY = DW_OcclusionXYAt(Pos, UpDir, TapDist);
		if (!IsInside(XY.x, XY.y))
		{
			break;   // the map ends here; beyond it there is nothing baked to shade us
		}
		const float MidDist = DW_OcclusionMidDistance(k, T);
		const float2 MidXY = DW_OcclusionXYAt(Pos, UpDir, MidDist);
		Shadow = DW_OcclusionAccumulate(Shadow, HeightBilinear(MidXY.x, MidXY.y, Level) + T.HeightBase, Pos.z, MidDist, T);
		Shadow = DW_OcclusionAccumulate(Shadow, HeightBilinear(XY.x, XY.y, Level) + T.HeightBase, Pos.z, TapDist, T);
	}
	return Shadow;
}

DreamWind::Hlsl::FDreamWindSample FDreamWindTerrainCpuData::ApplyAll(DreamWind::Hlsl::FDreamWindSample S, const DreamWind::Hlsl::FDreamWindTerrainParams& T, const DreamWind::Hlsl::float3& Pos) const
{
	using namespace DreamWind::Hlsl;
	const float2 UV = DW_TerrainUV(Pos, T);
	const float Inside = DW_TerrainInside(UV, T);
	if (Inside <= 0.0f)
	{
		return S;
	}
	S = DW_ApplyTerrain(S, ToHlsl(Sample(Pos.x, Pos.y)), T, Inside);
	return DW_ApplyOcclusion(S, OcclusionShadow(T, Pos, S.Velocity), T);
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
	// (Its captures and render commands hold only resources the render thread keeps alive; flush before letting go.)
	if (TerrainField.IsValid())
	{
		FlushRenderingCommands();
		TerrainField.Reset();
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
		UpdateRuntimeTerrain();
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
				if (FVector::DistSquared(Desc.Position, VolumeCenter) > FMath::Square(Component->CullDistance + Desc.GetBoundingRadius()))
				{
					continue;
				}
			}
			FrameData.Sources.Add(PackSource(Desc, 1.0f));
		}
	}

	// shelters first (they are structure, and a calm one has strength 0), then strongest first,
	// then cap what the GPU sees
	FrameData.Sources.Sort([](const DreamWind::Hlsl::FDreamWindSourceData& A, const DreamWind::Hlsl::FDreamWindSourceData& B)
	{
		const bool bShelterA = int32(A.Misc.x) == DW_SOURCE_SHELTER;
		const bool bShelterB = int32(B.Misc.x) == DW_SOURCE_SHELTER;
		if (bShelterA != bShelterB)
		{
			return bShelterA;
		}
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
	// PositionRadius.w stays the footprint radius R the shader scales by; the bounding sphere of a
	// stretched jet is what the culling uses (GetBoundingRadius), not this
	S.PositionRadius    = float4(float(Desc.Position.X), float(Desc.Position.Y), float(Desc.Position.Z), FMath::Max(Desc.Type == EDreamWindSourceType::Shelter ? Desc.GetBoundingRadius() : Desc.Radius, 1.0f));
	S.DirectionStrength = float4(float(Dir.X), float(Dir.Y), float(Dir.Z), Desc.Strength * StrengthScale);
	S.Misc              = float4(float(static_cast<int32>(Desc.Type)), FMath::Max(Desc.InjectRate, 0.0f), FMath::Clamp(Desc.InnerRadiusFraction, 0.0f, 1.0f), FMath::Max(Desc.Stretch, 1.0f));
	S.BoxExtent         = float4(0.0f, 0.0f, 0.0f, 0.0f);
	S.BoxRotation       = float4(0.0f, 0.0f, 0.0f, 1.0f);
	if (Desc.Type == EDreamWindSourceType::Shelter)
	{
		const FVector Ext = Desc.BoxExtent.GetAbs().ComponentMax(FVector(1.0));
		const FQuat Q = Desc.BoxRotation.Quaternion().GetNormalized();
		S.BoxExtent   = float4(float(Ext.X), float(Ext.Y), float(Ext.Z), 0.0f);
		S.BoxRotation = float4(float(Q.X), float(Q.Y), float(Q.Z), float(Q.W));
	}
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
		FrameData.T1 = FVector4f(1.0f, Settings->TerrainSteerScale, Settings->TerrainSpeedScale, TerrainCpu->TexelSize());
		const float Occlusion = DreamWind::GCVarOcclusion == 0 ? 0.0f : Settings->OcclusionStrength;
		FrameData.T2 = FVector4f(Occlusion, Settings->OcclusionRange, Settings->OcclusionSlope, TerrainCpu->HeightBase);
	}
	else
	{
		FrameData.T0 = FVector4f::Zero();
		FrameData.T1 = FVector4f::Zero();
		FrameData.T2 = FVector4f::Zero();
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
		Center = FindCameraLocation();
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

uint32 UDreamWindSubsystem::RequestDistanceFieldProbe(UTextureRenderTarget2D* Target, FVector Center, float Size)
{
	FTextureRenderTargetResource* Resource = Target ? Target->GameThread_GetRenderTargetResource() : nullptr;
	if (!Resource || !ViewExtension.IsValid() || !FApp::CanEverRender())
	{
		return 0;
	}
	DreamWind::DistanceField::FProbeRequest Request;
	Request.Target = Resource;
	Request.Resolution = FIntPoint(Target->SizeX, Target->SizeY);
	Request.Center = Center;
	Request.Size = FMath::Max(Size, 1.0f);
	ViewExtension->QueueDistanceProbe(Request);
	// (One probe at a time: a second request before the first is written replaces it.)
	return ViewExtension->GetDistanceProbeCount() + 1;
}

uint32 UDreamWindSubsystem::GetDistanceProbeCount() const
{
	return ViewExtension.IsValid() ? ViewExtension->GetDistanceProbeCount() : 0;
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

	// (The volumes are made writable where they are written, by the world that owns them: UpdateVolume.)
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
	// The plugin's shared collection, written through the world clock (one instance, unchanged values skipped).
	UDreamWorldSubsystem* Shared = UDreamWorldSubsystem::Get(this);
	if (!Shared)
	{
		return;
	}
	using namespace DreamWind::Hlsl;
	Shared->SetVectorParameter(DreamWind::Names::MPC_P0, ToLinearColor(ToHlsl(FrameData.P0)));
	Shared->SetVectorParameter(DreamWind::Names::MPC_P1, ToLinearColor(ToHlsl(FrameData.P1)));
	Shared->SetVectorParameter(DreamWind::Names::MPC_P2, ToLinearColor(ToHlsl(FrameData.P2)));
	Shared->SetVectorParameter(DreamWind::Names::MPC_V0, ToLinearColor(ToHlsl(FrameData.V0)));
	Shared->SetVectorParameter(DreamWind::Names::MPC_V1, ToLinearColor(ToHlsl(FrameData.V1)));
	Shared->SetVectorParameter(DreamWind::Names::MPC_V2, ToLinearColor(ToHlsl(FrameData.V2)));
	Shared->SetVectorParameter(DreamWind::Names::MPC_V3, ToLinearColor(ToHlsl(FrameData.V3)));
	Shared->SetVectorParameter(DreamWind::Names::MPC_T0, ToLinearColor(ToHlsl(FrameData.T0)));
	Shared->SetVectorParameter(DreamWind::Names::MPC_T1, ToLinearColor(ToHlsl(FrameData.T1)));
	Shared->SetVectorParameter(DreamWind::Names::MPC_T2, ToLinearColor(ToHlsl(FrameData.T2)));
	Shared->SetVectorParameter(DreamWind::Names::MPC_L1Near, ToLinearColor(ToHlsl(FrameData.L1Near)));
	Shared->SetVectorParameter(DreamWind::Names::MPC_L1Far, ToLinearColor(ToHlsl(FrameData.L1Far)));
	Shared->SetVectorParameter(DreamWind::Names::MPC_L1Params, ToLinearColor(ToHlsl(FrameData.L1Params)));
}

void UDreamWindSubsystem::UpdateRuntimeTerrain()
{
	const UDreamWindSettings* Settings = UDreamWindSettings::Get();
	UWorld* World = GetWorld();
	FrameData.L1Near = FVector4f::Zero();
	FrameData.L1Far = FVector4f::Zero();
	FrameData.L1Params = FVector4f::Zero();
	// The maps are shared by every world: computed only by the one that owns them (afresh when they come back to it).
	bool bRedrawAll = false;
	const bool bRun = Settings->bRuntimeTerrain && Settings->bTerrainEnabled && World && GMaxRHIFeatureLevel >= ERHIFeatureLevel::SM5
		&& RuntimeTerrainWriter.Acquire(World, bRedrawAll);
	if (!bRun)
	{
		if (TerrainField.IsValid())
		{
			TerrainField->Stop();
		}
		return;
	}
	if (!TerrainField.IsValid())
	{
		TerrainField = MakeShared<FDreamWindTerrainField>();
	}
	TerrainField->Update(*World, FindCameraLocation(), GetGlobalWindDirection(), bRedrawAll);
	FrameData.L1Near = TerrainField->GetNearLevel();
	FrameData.L1Far = TerrainField->GetFarLevel();
	FrameData.L1Params = TerrainField->GetParams();
	if (DreamWind::GCVarOcclusion == 0)
	{
		FrameData.L1Params.X = 0.0f;
	}
	FrameData.L1NearDerived = TerrainField->GetNearDerived();
	FrameData.L1FarDerived = TerrainField->GetFarDerived();
	FrameData.L1NearHeight = TerrainField->GetNearHeight();
	FrameData.L1FarHeight = TerrainField->GetFarHeight();
}

void UDreamWindSubsystem::MarkObstaclesDirty(FBox Bounds)
{
	if (TerrainField.IsValid())
	{
		TerrainField->MarkDirty(Bounds);
	}
}

uint32 UDreamWindSubsystem::GetTerrainCaptureCount() const
{
	return TerrainField.IsValid() ? TerrainField->GetCaptureCount() : 0;
}

uint32 UDreamWindSubsystem::GetTerrainDeriveCount() const
{
	return TerrainField.IsValid() ? TerrainField->GetDeriveCount() : 0;
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

	// The volumes are shared by every world: only the world that owns them writes (and reads them as its wind field);
	// back from another world, their history is that world's wind and starts afresh.
	bool bRedrawAll = false;
	if (!VolumeWriter.Acquire(World, bRedrawAll))
	{
		return;
	}
	if (bRedrawAll)
	{
		bVolumeHistoryDirty = true;
		bVolumeHistoryDirtyFar = true;
	}
	EnsureVolumeTextureMatches(VolumeTexture, Settings->VolumeResolution, VolumeRenderer);
	if (Settings->bVolumeFarLevel && VolumeFarTexture && VolumeRendererFar.IsValid())
	{
		EnsureVolumeTextureMatches(VolumeFarTexture, Settings->VolumeResolution, VolumeRendererFar);
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

	// The terrain map is shared by every world: only the world that owns it uploads, and afresh when it comes back.
	bool bRedrawAll = false;
	if (!TerrainWriter.Acquire(GetWorld(), bRedrawAll))
	{
		return;
	}
	bTerrainTextureDirty |= bRedrawAll;
	if (bTerrainTextureDirty)
	{
		bTerrainTextureDirty = false;
		const int32 W = TerrainCpu->Width;
		const int32 H = TerrainCpu->Height * 2;   // the atlas: the map plus its max-height chain
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
		Cpu->BuildFromMap(*Map);
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
	return FrameData.TerrainCpu->ApplyAll(S, FrameData.GetTerrainParams(), Pos);
}

void UDreamWindSubsystem::DrawDebug()
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}
	using namespace DreamWind::Hlsl;
	if (DreamWind::GCVarDebugDraw >= 3)
	{
		DrawDebugField(World);
		return;
	}
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
		if (Type == DW_SOURCE_SHELTER)
		{
			const FColor Colour = S.DirectionStrength.w > 0.0f ? FColor(120, 255, 120) : FColor(90, 200, 90);
			const FQuat Q(S.BoxRotation.x, S.BoxRotation.y, S.BoxRotation.z, S.BoxRotation.w);
			DrawDebugBox(World, Centre, FVector(S.BoxExtent.x, S.BoxExtent.y, S.BoxExtent.z), Q, Colour, false, -1.0f, 0, 1.5f);
			if (S.DirectionStrength.w > 0.0f)
			{
				const FVector Axis(S.DirectionStrength.x, S.DirectionStrength.y, S.DirectionStrength.z);
				DrawDebugDirectionalArrow(World, Centre, Centre + Axis * FMath::Min(S.PositionRadius.w, 300.0f), 40.0f, Colour, false, -1.0f, 0, 3.0f);
			}
			continue;
		}
		const FColor Colour = Type == DW_SOURCE_RADIAL ? FColor::Orange : (Type == DW_SOURCE_VORTEX ? FColor::Magenta : FColor::Yellow);
		const float R = S.PositionRadius.w;
		const float Stretch = FMath::Max(S.Misc.w, 1.0f);
		const FVector Axis(S.DirectionStrength.x, S.DirectionStrength.y, S.DirectionStrength.z);
		if (Type != DW_SOURCE_RADIAL && Stretch > 1.0f)
		{
			// the egg: R behind the centre, R * Stretch ahead of it - drawn as the capsule that contains it
			const float HalfHeight = R * (Stretch + 1.0f) * 0.5f;
			const FVector CapsuleCentre = Centre + Axis * (R * (Stretch - 1.0f) * 0.5f);
			DrawDebugCapsule(World, CapsuleCentre, HalfHeight, R, FRotationMatrix::MakeFromZ(Axis).ToQuat(), Colour, false, -1.0f, 0, 1.0f);
		}
		else
		{
			DrawDebugSphere(World, Centre, R, 16, Colour, false, -1.0f, 0, 1.0f);
		}
		if (Type != DW_SOURCE_RADIAL)
		{
			DrawDebugDirectionalArrow(World, Centre, Centre + Axis * FMath::Min(R * Stretch, 300.0f), 40.0f, Colour, false, -1.0f, 0, 3.0f);
		}
	}

	// the arrow field (levels 2 and 3)
	if (DreamWind::GCVarDebugDraw >= 2)
	{
		DrawDebugField(World);
	}
}

namespace DreamWind
{
	static FColor SpeedRampColor(float T)
	{
		// blue -> green -> yellow -> red
		static const FLinearColor Stops[4] = { FLinearColor(0.15f, 0.35f, 1.0f), FLinearColor(0.15f, 0.9f, 0.45f), FLinearColor(1.0f, 0.85f, 0.15f), FLinearColor(1.0f, 0.15f, 0.1f) };
		T = FMath::Clamp(T, 0.0f, 1.0f) * 3.0f;
		const int32 I = FMath::Min(int32(T), 2);
		return FMath::Lerp(Stops[I], Stops[I + 1], T - float(I)).ToFColor(false);
	}

	static FColor DirectionHueColor(const FVector& Dir, float Brightness)
	{
		// the same wheel as M_DreamWindDebug: +X red, +Y green, -X cyan-ish, -Y magenta-ish
		const FLinearColor C(FMath::Clamp(float(Dir.X) * 0.5f + 0.5f, 0.0f, 1.0f), FMath::Clamp(float(Dir.Y) * 0.5f + 0.5f, 0.0f, 1.0f), FMath::Clamp(float(-Dir.X) * 0.5f + 0.5f, 0.0f, 1.0f));
		return (C * FMath::Clamp(Brightness, 0.08f, 1.0f)).ToFColor(false);
	}

	static FColor GustColor(float Gust)
	{
		// lull (0) blue, nominal (1) white, strong gust (2) red
		const FLinearColor C = Gust < 1.0f
			? FMath::Lerp(FLinearColor(0.2f, 0.4f, 1.0f), FLinearColor::White, FMath::Clamp(Gust, 0.0f, 1.0f))
			: FMath::Lerp(FLinearColor::White, FLinearColor(1.0f, 0.15f, 0.1f), FMath::Clamp(Gust - 1.0f, 0.0f, 1.0f));
		return C.ToFColor(false);
	}
}

void UDreamWindSubsystem::DrawDebugField(UWorld* World)
{
	// A 3D lattice anchored at the world origin (X, Y and Z): the arrows stay put while the camera moves
	// and cells appear/disappear at the range boundary. Sampled with the CPU path (analytic + terrain +
	// sources, no volume read-back), so it is exactly what cloth and gameplay queries see.
	const FVector Cam = FindCameraLocation();
	const bool b3D = DreamWind::GCVarDebugField3D != 0;
	const float Range = FMath::Max(DreamWind::GCVarDebugRange, 50.0f);
	const int32 Layers = b3D ? 1 : FMath::Clamp(DreamWind::GCVarDebugLayers, 1, 16);
	float Spacing = FMath::Max(DreamWind::GCVarDebugSpacing, 10.0f);
	{
		// keep the cell count sane whatever the range: widen the lattice instead of dropping cells
		constexpr float MaxCells = 4096.0f;
		const float Needed = b3D
			? (4.0f / 3.0f) * PI * FMath::Cube(Range / Spacing)
			: PI * FMath::Square(Range / Spacing) * float(Layers);
		if (Needed > MaxCells)
		{
			Spacing = b3D
				? Range * FMath::Pow((4.0f / 3.0f) * PI / MaxCells, 1.0f / 3.0f)
				: Range * FMath::Sqrt(PI * float(Layers) / MaxCells);
		}
	}
	const float MaxSpeed = FMath::Max(DreamWind::GCVarDebugMaxSpeed, 1.0f);
	const float Scale = FMath::Max(DreamWind::GCVarDebugScale, 0.0f);
	const int32 ColorMode = DreamWind::GCVarDebugColor;

	const int32 X0 = FMath::FloorToInt(float(Cam.X - Range) / Spacing), X1 = FMath::CeilToInt(float(Cam.X + Range) / Spacing);
	const int32 Y0 = FMath::FloorToInt(float(Cam.Y - Range) / Spacing), Y1 = FMath::CeilToInt(float(Cam.Y + Range) / Spacing);
	// 3D: every lattice plane inside the sphere; sheet mode: Layers planes from Height upward
	const int32 Z0 = b3D ? FMath::FloorToInt(float(Cam.Z - Range) / Spacing) : 0;
	const int32 Z1 = b3D ? FMath::CeilToInt(float(Cam.Z + Range) / Spacing) : Layers - 1;
	const float RangeSq = Range * Range;
	const float TooCloseSq = FMath::Square(Spacing * 0.6f); // do not draw cells sitting on the lens
	int32 Drawn = 0;
	for (int32 L = Z0; L <= Z1; ++L)
	{
		const double Z = b3D ? double(L) * Spacing : double(DreamWind::GCVarDebugHeight) + double(L) * Spacing;
		for (int32 X = X0; X <= X1; ++X)
		{
			for (int32 Y = Y0; Y <= Y1; ++Y)
			{
				const FVector P(double(X) * Spacing, double(Y) * Spacing, Z);
				const float DistSq = b3D ? float(FVector::DistSquared(P, Cam)) : float(FVector::DistSquaredXY(P, Cam));
				if (DistSq > RangeSq || DistSq < TooCloseSq)
				{
					continue;
				}
				const FDreamWindSampleResult R = SampleWind(P, EDreamWindQuality::Cheap);
				const float Speed01 = FMath::Clamp(R.Speed / MaxSpeed, 0.0f, 1.0f);
				FColor Colour;
				switch (ColorMode)
				{
				case 1:  Colour = DreamWind::DirectionHueColor(R.Direction, Speed01); break;
				case 2:  Colour = DreamWind::GustColor(R.Gust); break;
				default: Colour = DreamWind::SpeedRampColor(Speed01); break;
				}
				if (R.Speed < MaxSpeed * 0.01f)
				{
					// calm cell (a shelter, the eye of a vortex): a dot, so calm reads as calm and not as "no data"
					DrawDebugPoint(World, P, 5.0f, Colour, false, -1.0f, 0);
				}
				else
				{
					const float Len = FMath::Clamp(R.Speed * Scale, Spacing * 0.08f, Spacing * 0.9f);
					DrawDebugDirectionalArrow(World, P, P + R.Direction * Len, FMath::Clamp(Len * 0.25f, 6.0f, 40.0f), Colour, false, -1.0f, 0, 1.0f + 2.0f * Speed01);
				}
				++Drawn;
			}
		}
	}

	static const TCHAR* ColorNames[] = { TEXT("speed"), TEXT("direction"), TEXT("gust") };
	DrawDebugString(World, Cam + FVector(0, 0, -60), FString::Printf(TEXT("DreamWind field (%s): %d cells, spacing %.0f, range %.0f, colour = %s (0..%.0f cm/s)"), b3D ? TEXT("3D") : TEXT("sheet"), Drawn, Spacing, Range, ColorNames[FMath::Clamp(ColorMode, 0, 2)], MaxSpeed), nullptr, FColor(200, 200, 200), 0.0f, true);
}

FVector UDreamWindSubsystem::FindCameraLocation() const
{
	if (const UWorld* World = GetWorld(); World && World->IsGameWorld())
	{
		if (const APlayerController* PC = World->GetFirstPlayerController())
		{
			if (PC->PlayerCameraManager)
			{
				return PC->PlayerCameraManager->GetCameraLocation();
			}
			if (const APawn* Pawn = PC->GetPawn())
			{
				return Pawn->GetActorLocation();
			}
		}
	}
	return bHasViewOrigin ? LatestViewOrigin : VolumeCenter;
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

void UDreamWindSubsystem::ApplyPreset(const UDreamWindPreset* Preset, float BlendTime)
{
	if (Preset)
	{
		SetGlobalWind(Preset->Wind, BlendTime);
	}
}

void UDreamWindSubsystem::ApplyPresetKeepDirection(const UDreamWindPreset* Preset, float BlendTime)
{
	if (Preset)
	{
		FDreamWindGlobalWind Wind = Preset->Wind;
		Wind.Direction = BlendDuration > 0.0f ? BlendToWind.Direction : CurrentWind.Direction;
		SetGlobalWind(Wind, BlendTime);
	}
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
