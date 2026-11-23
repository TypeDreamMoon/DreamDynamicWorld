// Copyright DreamMoon. All Rights Reserved.

#include "DreamGroundFog.h"

#include "DreamSky.h"
#include "Engine/StaticMesh.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Engine/Texture2D.h"
#include "HAL/IConsoleManager.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "TextureResource.h"
#include "UObject/ConstructorHelpers.h"

namespace DreamWeatherFX::GroundFog
{
	static TAutoConsoleVariable<int32> CVarVolumetric(
		TEXT("DreamWeather.Fog.Volumetric"), 1,
		TEXT("The fog actors' (terrain fog, fog bank) fog: 1 marched as a volume (noise, lit by the sun and the sky, no limit on ")
		TEXT("how many), 0 the engine's local fog volumes (the cheaper look; at most 32 a view). Set by the fog quality level."),
		ECVF_Scalability);

	static TAutoConsoleVariable<int32> CVarSamples(
		TEXT("DreamWeather.Fog.Samples"), 32,
		TEXT("How many fine steps a ray through the marched ground fog takes. Set by the fog quality level."),
		ECVF_Scalability);

	static TAutoConsoleVariable<int32> CVarDebug(
		TEXT("DreamWeather.Fog.Debug"), 0,
		TEXT("The marched ground fog's debug view: 1 the layer under what each ray meets (r ground height, in bands of 2 m; ")
		TEXT("g the hollow's depth, full at 4 m; b ground there), 2 the fog's opacity alone, 0 off."),
		ECVF_Cheat);

	bool UseVolumetric()
	{
		return CVarVolumetric.GetValueOnGameThread() != 0;
	}

	int32 Samples()
	{
		return FMath::Clamp(CVarSamples.GetValueOnGameThread(), 4, 256);
	}

	static const FName LayerMapName(TEXT("LayerMap"));
	static const FName MapOriginName(TEXT("MapOrigin"));
	static const FName MapAreaName(TEXT("MapArea"));
	static const FName FogStateName(TEXT("FogState"));
	static const FName FogLayersName(TEXT("FogLayers"));
	static const FName FogNoiseName(TEXT("FogNoise"));
	static const FName FogLookName(TEXT("FogLook"));
	static const FName FogShadowName(TEXT("FogShadow"));
	static const FName CloudShadowMapName(TEXT("CloudShadowMap"));
	static const FName CloudShadowName(TEXT("CloudShadow"));
	// A layer's alpha where nothing shadows it (far below anything).
	constexpr float Unshadowed = -60000.0f;
}

UDreamGroundFogComponent::UDreamGroundFogComponent(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	static ConstructorHelpers::FObjectFinder<UStaticMesh> Cube(TEXT("/Engine/BasicShapes/Cube.Cube"));
	if (Cube.Succeeded())
	{
		SetStaticMesh(Cube.Object);
	}
	SetCollisionEnabled(ECollisionEnabled::NoCollision);
	SetCollisionProfileName(UCollisionProfile::NoCollision_ProfileName);
	SetGenerateOverlapEvents(false);
	SetCanEverAffectNavigation(false);
	SetUsingAbsoluteLocation(true);
	SetUsingAbsoluteRotation(true);
	SetUsingAbsoluteScale(true);
	CastShadow = false;
	bCastDynamicShadow = false;
	bAffectDistanceFieldLighting = false;
	bAffectDynamicIndirectLighting = false;
	bReceivesDecals = false;
	bUseAsOccluder = false;
	bVisibleInRayTracing = false;
	bVisibleInReflectionCaptures = false;
	bSelectable = false;
	// Under the other translucency (rain, snow, sprays): they draw over the fog.
	TranslucencySortPriority = -10;
	SetHiddenInGame(false);
	SetVisibility(false);
}

void UDreamGroundFogComponent::EnsureMaterial()
{
	if (FogMaterial)
	{
		return;
	}
	if (UMaterialInterface* Base = LoadObject<UMaterialInterface>(nullptr, DreamWeatherFX::GroundFog::MaterialPath))
	{
		FogMaterial = UMaterialInstanceDynamic::Create(Base, this);
		SetMaterial(0, FogMaterial);
		if (LayerMap)
		{
			FogMaterial->SetTextureParameterValue(DreamWeatherFX::GroundFog::LayerMapName, LayerMap);
		}
	}
}

void UDreamGroundFogComponent::UploadLayer(int32 Cells, const TArray<FFloat16Color>& Texels)
{
	if (!LayerMap || LayerMap->GetSizeX() != Cells || LayerMap->GetSizeY() != Cells)
	{
		LayerMap = UTexture2D::CreateTransient(Cells, Cells, PF_FloatRGBA, MakeUniqueObjectName(GetTransientPackage(), UTexture2D::StaticClass(), TEXT("GroundFogLayer")));
		if (!LayerMap)
		{
			return;
		}
		LayerMap->Filter = TF_Bilinear;
		LayerMap->AddressX = TA_Clamp;
		LayerMap->AddressY = TA_Clamp;
		LayerMap->SRGB = false;
		LayerMap->NeverStream = true;
	}
	LayerTexels = Texels;
	LayerCells = Cells;
	FTexture2DMipMap& Mip = LayerMap->GetPlatformData()->Mips[0];
	void* Data = Mip.BulkData.Lock(LOCK_READ_WRITE);
	FMemory::Memcpy(Data, Texels.GetData(), Texels.Num() * sizeof(FFloat16Color));
	Mip.BulkData.Unlock();
	LayerMap->UpdateResource();
	EnsureMaterial();
	if (FogMaterial)
	{
		FogMaterial->SetTextureParameterValue(DreamWeatherFX::GroundFog::LayerMapName, LayerMap);
	}
}

void UDreamGroundFogComponent::SetLayer(const FVector& Origin, const FVector2D& Area, int32 Cells, TConstArrayView<float> Heights, TConstArrayView<float> Hollows, float NoGround)
{
	LayerOrigin = Origin;
	LayerArea = FVector2D(FMath::Max(Area.X, 1.0), FMath::Max(Area.Y, 1.0));
	const int32 N = FMath::Max(Cells, 1);
	if (Heights.Num() != N * N)
	{
		SetFlatLayer(Origin, Area);
		return;
	}
	// Cells without ground take the lowest ground's height (so the bilinear read between them and their neighbours keeps
	// a sensible ground), no hollow and no fog.
	float Lowest = TNumericLimits<float>::Max();
	for (float H : Heights)
	{
		Lowest = H != NoGround ? FMath::Min(Lowest, H) : Lowest;
	}
	Lowest = Lowest == TNumericLimits<float>::Max() ? float(Origin.Z) : Lowest;
	TArray<FFloat16Color> Texels;
	Texels.SetNumUninitialized(N * N);
	for (int32 i = 0; i < N * N; ++i)
	{
		const bool bGround = Heights[i] != NoGround;
		const float Hollow = Hollows.IsValidIndex(i) ? Hollows[i] : 0.0f;
		Texels[i] = FFloat16Color(FLinearColor(float((bGround ? Heights[i] : Lowest) - Origin.Z), bGround ? Hollow : 0.0f, bGround ? 1.0f : 0.0f, DreamWeatherFX::GroundFog::Unshadowed));
	}
	UploadLayer(N, Texels);
}

void UDreamGroundFogComponent::SetFlatLayer(const FVector& Origin, const FVector2D& Area)
{
	LayerOrigin = Origin;
	LayerArea = FVector2D(FMath::Max(Area.X, 1.0), FMath::Max(Area.Y, 1.0));
	TArray<FFloat16Color> Texels;
	Texels.Add(FFloat16Color(FLinearColor(0.0f, 0.0f, 1.0f, DreamWeatherFX::GroundFog::Unshadowed)));
	UploadLayer(1, Texels);
}

void UDreamGroundFogComponent::SetShadowTops(TConstArrayView<float> Tops)
{
	if (LayerCells <= 0 || Tops.Num() != LayerTexels.Num())
	{
		return;
	}
	TArray<FFloat16Color> Texels = LayerTexels;
	for (int32 i = 0; i < Texels.Num(); ++i)
	{
		Texels[i].A = FFloat16(FMath::Clamp(float(Tops[i] - LayerOrigin.Z), DreamWeatherFX::GroundFog::Unshadowed, 60000.0f));
	}
	UploadLayer(LayerCells, Texels);
}

void UDreamGroundFogComponent::UpdateCloudShadow()
{
	using namespace DreamWeatherFX::GroundFog;
	// The sky cloud renderer's shadow on the ground, when it draws the clouds (looked for again every few seconds).
	UWorld* World = GetWorld();
	if (!Sky.IsValid() && World && World->GetTimeSeconds() >= NextSkySearch)
	{
		NextSkySearch = World->GetTimeSeconds() + 3.0;
		for (TActorIterator<ADreamSky> It(World); It; ++It)
		{
			Sky = *It;
			break;
		}
	}
	UTextureRenderTarget2D* Target = Sky.IsValid() && Sky->DrawsClouds() ? Sky->GetCloudShadowTarget() : nullptr;
	if (!Target)
	{
		FogMaterial->SetVectorParameterValue(CloudShadowName, FLinearColor(0.0f, 0.0f, 1.0e-6f, 0.0f));
		return;
	}
	if (FogMaterial->K2_GetTextureParameterValue(CloudShadowMapName) != Target)
	{
		FogMaterial->SetTextureParameterValue(CloudShadowMapName, Target);
	}
	const FVector4f& Placement = Sky->GetCloudShadowPlacement();
	FogMaterial->SetVectorParameterValue(CloudShadowName, FLinearColor(Placement.X, Placement.Y, 0.5f / FMath::Max(Placement.Z, 1.0f), Placement.W));
}

void UDreamGroundFogComponent::SetFog(const FDreamGroundFogParams& InParams)
{
	using namespace DreamWeatherFX::GroundFog;
	Params = InParams;
	const bool bAny = (Params.Radiation > 1.0e-3f && (Params.PoolExtinction > 0.0f || Params.MistExtinction > 0.0f))
		|| (Params.Advection > 1.0e-3f && Params.PoolExtinction > 0.0f);
	if (!bAny)
	{
		SetVisibility(false);
		return;
	}
	EnsureMaterial();
	if (!FogMaterial)
	{
		SetVisibility(false);
		return;
	}
	// The box: the layer's area, from the lowest ground to above the fog's highest top.
	const float Low = FMath::Min(Params.BoxLow, Params.BoxHigh - 10.0f);
	const FVector Center(LayerOrigin.X, LayerOrigin.Y, 0.5 * (Low + Params.BoxHigh));
	const FVector Scale(2.0 * LayerArea.X / 100.0, 2.0 * LayerArea.Y / 100.0, (Params.BoxHigh - Low) / 100.0);
	if (!GetComponentLocation().Equals(Center, 1.0) || !GetComponentScale().Equals(Scale, 1.0e-3))
	{
		SetWorldLocationAndRotation(Center, FRotator::ZeroRotator);
		SetWorldScale3D(Scale);
	}
	FogMaterial->SetVectorParameterValue(MapOriginName, FLinearColor(float(LayerOrigin.X), float(LayerOrigin.Y), float(LayerOrigin.Z), Params.EdgeFade));
	FogMaterial->SetVectorParameterValue(MapAreaName, FLinearColor(float(LayerArea.X), float(LayerArea.Y), Low, Params.BoxHigh));
	FogMaterial->SetVectorParameterValue(FogStateName, FLinearColor(Params.Radiation, Params.Advection, Params.Lowest, Params.AdvectionDepth));
	FogMaterial->SetVectorParameterValue(FogLayersName, FLinearColor(Params.MistThickness, Params.PoolExtinction, Params.MistExtinction, float(CVarDebug.GetValueOnGameThread())));
	FogMaterial->SetVectorParameterValue(FogNoiseName, FLinearColor(Params.NoiseSize, Params.NoiseAmount, Params.Churn, FMath::Max(Params.NoiseSize * 3.0f, 2000.0f)));
	FogMaterial->SetVectorParameterValue(FogLookName, FLinearColor(Params.SunStrength, Params.AmbientStrength, float(Samples()), FMath::Max(Params.NoiseSize * 0.02f, 10.0f)));
	FogMaterial->SetVectorParameterValue(FogShadowName, FLinearColor(FMath::Max(Params.ShadowSoftness, 1.0f), 0.0f, 0.0f, 0.0f));
	UpdateCloudShadow();
	SetVisibility(true);
}
