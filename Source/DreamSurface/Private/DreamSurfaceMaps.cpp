// Copyright DreamMoon. All Rights Reserved.

#include "DreamSurfaceMaps.h"
#include "DreamSurfaceMapRenderer.h"
#include "DreamSurfaceMask.h"
#include "DreamSurfaceRoofCapture.h"
#include "DreamSurfaceModule.h"
#include "DreamSurfaceSettings.h"
#include "DreamSurfaceSubsystem.h"

#include "DreamWeatherChannels.h"
#include "DreamWeatherSettings.h"
#include "DreamWeatherShelter.h"
#include "DreamWeatherSubsystem.h"
#include "DreamWeatherZone.h"
#include "DreamWindSubsystem.h"
#include "DreamWorldSubsystem.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"
#include "Misc/App.h"
#include "UObject/UObjectIterator.h"

namespace DreamSurface::Maps
{
	static TAutoConsoleVariable<int32> CVarIntegrateEveryFrame(
		TEXT("DreamSurface.IntegrateEveryFrame"), 0,
		TEXT("1: integrate the surface maps' whole memory on every update (to measure what an integration costs)."),
		ECVF_Default);

	/** Loads a map's render target and makes it something a compute pass can write. */
	UTextureRenderTarget2D* LoadTarget(const FSoftObjectPath& Path, EPixelFormat Format)
	{
		UTextureRenderTarget2D* RT = Cast<UTextureRenderTarget2D>(Path.TryLoad());
		if (RT && (!RT->bSupportsUAV || RT->GetFormat() != Format))
		{
			RT->bSupportsUAV = true;
			RT->InitCustomFormat(FMath::Max(16, RT->SizeX), FMath::Max(16, RT->SizeY), Format, true);
			RT->UpdateResourceImmediate(false);
		}
		return RT;
	}

	/**
	 * The zones of a world as the surface map reads them (DreamSurfaceMap.usf, four float4 each), in the order the weather
	 * applies them: the weather map's three (footprint and coverage, darkness, precipitation), then the snow ratio,
	 * temperature offset, dust and wind (below -9999: not set). Override zones only, as the weather map takes them.
	 */
	TArray<FVector4f> GatherZones(const UWorld* World, int32 MaxZones)
	{
		using C = FDreamWeatherChannels;
		TArray<const UDreamWeatherZoneComponent*> Zones;
		for (TObjectIterator<UDreamWeatherZoneComponent> It; It; ++It)
		{
			const UDreamWeatherZoneComponent* Zone = *It;
			if (Zone && Zone->GetWorld() == World && Zone->IsRegistered() && Zone->bDrawIntoWeatherMap && Zone->Blend == EDreamWeatherBlend::Override && Zone->Weight > 0.0f)
			{
				Zones.Add(Zone);
			}
		}
		Zones.StableSort([](const UDreamWeatherZoneComponent& A, const UDreamWeatherZoneComponent& B)
		{
			const int32 PA = A.GetWeatherInfluencePriority();
			const int32 PB = B.GetWeatherInfluencePriority();
			return PA != PB ? PA < PB : A.GetWeatherInfluenceStableId() < B.GetWeatherInfluenceStableId();
		});
		// Too many: keep the highest priorities, which are last.
		if (Zones.Num() > MaxZones)
		{
			Zones.RemoveAt(0, Zones.Num() - MaxZones);
		}
		TArray<FVector4f> Out;
		for (const UDreamWeatherZoneComponent* Zone : Zones)
		{
			auto Value = [Zone](int32 Channel, float Unset)
			{
				float V;
				return Zone->GetChannelValue(Channel, V) ? V : Unset;
			};
			const FVector Location = Zone->GetComponentLocation();
			const double Yaw = FMath::DegreesToRadians(Zone->GetComponentRotation().Yaw);
			Out.Add(FVector4f(float(Location.X), float(Location.Y), float(FMath::Cos(Yaw)), float(FMath::Sin(Yaw))));
			Out.Add(FVector4f(float(Zone->Extent.X), float(Zone->Extent.Y), Zone->BlendDistance, Zone->Shape == EDreamWeatherZoneShape::Circle ? 1.0f : 0.0f));
			Out.Add(FVector4f(Value(C::CloudCoverage, -1.0f), Value(C::CloudDarkness, -1.0f), Value(C::Precipitation, -1.0f), FMath::Clamp(Zone->Weight, 0.0f, 1.0f)));
			Out.Add(FVector4f(Value(C::SnowRatio, -1.0e4f), Value(C::Temperature, -1.0e4f), Value(C::Dust, -1.0e4f), Value(C::Wind, -1.0e4f)));
		}
		return Out;
	}

	/** Minus the centre (add a world position), 1 / extent, 1 when live: what a material needs to find its texel. */
	FLinearColor Placement(const FVector2D& Center, float Extent, bool bLive)
	{
		return bLive ? FLinearColor(float(-Center.X), float(-Center.Y), 1.0f / FMath::Max(Extent, 1.0f), 1.0f) : FLinearColor(0.0f, 0.0f, 0.0f, 0.0f);
	}
}

FDreamSurfaceMaps::FDreamSurfaceMaps()
	: Renderer(MakeShared<FDreamSurfaceMapRenderer, ESPMode::ThreadSafe>())
{
}

FDreamSurfaceMaps::~FDreamSurfaceMaps() = default;

void FDreamSurfaceMaps::AddReferencedObjects(FReferenceCollector& Collector)
{
	Collector.AddReferencedObject(Near.Target);
	Collector.AddReferencedObject(Far.Target);
	Collector.AddReferencedObject(Cover);
	Collector.AddReferencedObject(Mask);
}

uint32 FDreamSurfaceMaps::GetDrawCount() const
{
	return Renderer ? Renderer->GetDrawCount() : 0;
}

uint32 FDreamSurfaceMaps::GetIntegrateCount() const
{
	return Renderer ? Renderer->GetIntegrateCount() : 0;
}

bool FDreamSurfaceMaps::Resolve()
{
	if (Near.Target && Far.Target && Cover && Mask)
	{
		return true;
	}
	const double Now = FPlatformTime::Seconds();
	if (Now - LastResolve < 5.0)
	{
		return false;
	}
	LastResolve = Now;
	const UDreamSurfaceSettings* S = UDreamSurfaceSettings::Get();
	using namespace DreamSurface::Maps;
	Near.Target = LoadTarget(S->NearMapTexture, PF_FloatRGBA);
	Far.Target = LoadTarget(S->FarMapTexture, PF_FloatRGBA);
	Cover = LoadTarget(S->CoverMapTexture, PF_G32R32F);
	Mask = LoadTarget(S->MaskMapTexture, PF_FloatRGBA);
	if (!Near.Target || !Far.Target || !Cover)
	{
		UE_LOG(LogDreamSurface, Warning, TEXT("Surface maps missing (run the DreamDynamicWorld Setup): near %s, far %s, cover %s"),
			Near.Target ? TEXT("ok") : TEXT("missing"), Far.Target ? TEXT("ok") : TEXT("missing"), Cover ? TEXT("ok") : TEXT("missing"));
	}
	return Near.Target && Far.Target && Cover && Mask;
}

void FDreamSurfaceMaps::Stop(UDreamWorldSubsystem* Clock)
{
	Near.bLive = false;
	Far.bLive = false;
	Near.Step = TNumericLimits<int64>::Min();
	Far.Step = TNumericLimits<int64>::Min();
	if (Clock)
	{
		using namespace DreamSurface::Names;
		const FLinearColor Off(0.0f, 0.0f, 0.0f, 0.0f);
		Clock->SetVectorParameter(MPC_SurfaceNear, Off);
		Clock->SetVectorParameter(MPC_SurfaceFar, Off);
		Clock->SetVectorParameter(MPC_SurfaceView, Off);
	}
}

void FDreamSurfaceMaps::Update(UDreamSurfaceSubsystem& Surface, UDreamWorldSubsystem& Clock, UDreamWeatherSubsystem& Weather)
{
	using namespace DreamSurface::Names;
	const UDreamSurfaceSettings* S = UDreamSurfaceSettings::Get();
	const FDreamSurfaceState& View = Surface.GetViewState();
	Clock.SetVectorParameter(MPC_SurfaceView, FLinearColor(View.Wetness, View.Water, View.Snow, View.Dust));
	Clock.SetVectorParameter(MPC_SurfaceParams, FLinearColor(S->MaxSnowDepth, UDreamWeatherSettings::Get()->RoofClearance, S->CoverSoftness, 0.0f));

	UWorld* World = Surface.GetWorld();
	const bool bCanRender = S->bSurfaceMaps && FApp::CanEverRender() && World && World->Scene != nullptr && !IsRunningDedicatedServer();
	if (!bCanRender || !Resolve())
	{
		Clock.SetVectorParameter(MPC_SurfaceNear, FLinearColor(0.0f, 0.0f, 0.0f, 0.0f));
		Clock.SetVectorParameter(MPC_SurfaceFar, FLinearColor(0.0f, 0.0f, 0.0f, 0.0f));
		return;
	}

	const double Now = FPlatformTime::Seconds();
	const TArray<FVector4f> Zones = DreamSurface::Maps::GatherZones(World, FMath::Max(1, UDreamWeatherSettings::Get()->MaxMapZones));
	const bool bZonesChanged = Zones != LastZones || bIntegrateRequested || DreamSurface::Maps::CVarIntegrateEveryFrame.GetValueOnGameThread() != 0;
	LastZones = Zones;
	bIntegrateRequested = false;

	// The near map moves in eighths of its size (each move integrates it afresh), the far one with the weather map.
	const FVector View2D = Clock.HasViewLocation() ? Clock.GetViewLocation() : FVector::ZeroVector;
	const float NearExtent = FMath::Max(1000.0f, S->NearMapExtent);
	const double Snap = NearExtent / 8.0;
	const FVector2D NearCenter(FMath::GridSnap(View2D.X, Snap), FMath::GridSnap(View2D.Y, Snap));
	const float FarExtent = FMath::Max(NearExtent, UDreamWeatherSettings::Get()->WeatherMapExtent);
	const double FarTexel = FarExtent / FMath::Max(1, Far.Target->SizeX);
	const FVector2D FarCenter(FMath::GridSnap(View2D.X, FarTexel * 8.0), FMath::GridSnap(View2D.Y, FarTexel * 8.0));

	DrawState(0, Near, NearCenter, NearExtent, bZonesChanged, Surface, Weather, Now);
	DrawState(1, Far, FarCenter, FarExtent, bZonesChanged, Surface, Weather, Now);
	DrawCover(Clock, NearCenter, NearExtent, Now);

	using namespace DreamSurface::Maps;
	Clock.SetVectorParameter(MPC_SurfaceNear, Placement(Near.Center, Near.Extent, Near.bLive));
	Clock.SetVectorParameter(MPC_SurfaceFar, Placement(Far.Center, Far.Extent, Far.bLive));
}

void FDreamSurfaceMaps::DrawState(int32 Slot, FMap& Map, const FVector2D& Center, float Extent, bool bForce, UDreamSurfaceSubsystem& Surface, UDreamWeatherSubsystem& Weather, double Now)
{
	const FDreamSurfaceMapSteps& Steps = Surface.GetMapSteps();
	const bool bIntegrate = bForce || !Map.bLive || Steps.Step != Map.Step || Center != Map.Center || Extent != Map.Extent;
	if (!bIntegrate && Now - Map.LastDraw < UDreamSurfaceSettings::Get()->MapInterval)
	{
		return;
	}
	FDreamSurfaceMapRenderer::FStateArgs Args;
	Args.Target = Map.Target->GameThread_GetRenderTargetResource();
	if (!Args.Target)
	{
		return;
	}
	Args.Center = FVector2f(Center);
	Args.Extent = Extent;
	Args.bIntegrate = bIntegrate;
	if (bIntegrate)
	{
		Args.Steps = Steps.Steps;
	}
	Args.StepHours = float(UDreamSurfaceSubsystem::StepSeconds() / 3600.0);
	Args.PartialWeather = Steps.PartialWeather;
	Args.PartialChannels = Steps.PartialChannels;
	Args.PartialAir = Steps.PartialAir;
	Args.PartialHours = Steps.PartialHours;
	const FDreamSurfaceRates& Rates = UDreamSurfaceSettings::Get()->Rates;
	Args.Rates[0] = Rates.R0();
	Args.Rates[1] = Rates.R1();
	Args.Rates[2] = Rates.R2();
	Args.PrecipitationGate = FVector2f(UDreamWeatherSettings::Get()->PrecipitationCloudCover);
	Args.Zones = LastZones;
	Renderer->EnqueueState(Slot, MoveTemp(Args));
	Map.Center = Center;
	Map.Extent = Extent;
	Map.Step = Steps.Step;
	Map.LastDraw = Now;
	Map.bLive = true;
}

void FDreamSurfaceMaps::DrawCover(UDreamWorldSubsystem& Clock, const FVector2D& Center, float Extent, double Now)
{
	const UDreamSurfaceSettings* S = UDreamSurfaceSettings::Get();
	// A roof capture that captured again redraws the cover at once.
	int32 RoofCaptures = 0;
	for (TObjectIterator<UDreamSurfaceRoofCaptureComponent> It; It; ++It)
	{
		if (It->GetWorld() == Clock.GetWorld())
		{
			RoofCaptures += It->GetCaptureCount();
		}
	}
	if (RoofCaptures == LastRoofCaptures && Center == CoverCenter && Now - LastCoverDraw < S->CoverInterval)
	{
		return;
	}
	LastRoofCaptures = RoofCaptures;
	FDreamSurfaceMapRenderer::FCoverArgs Args;
	Args.Target = Cover->GameThread_GetRenderTargetResource();
	if (!Args.Target)
	{
		return;
	}
	Args.Center = FVector2f(Center);
	Args.Extent = Extent;

	// The wind's baked level heights: the level's top surfaces.
	UWorld* World = Clock.GetWorld();
	const UDreamWindSubsystem* Wind = World && UDreamWeatherSettings::Get()->bUseWindTerrainHeights ? World->GetSubsystem<UDreamWindSubsystem>() : nullptr;
	if (Wind)
	{
		const FDreamWindFrameData& Frame = Wind->GetFrameData();
		UTextureRenderTarget2D* Terrain = Frame.TerrainTexture.Get();
		if (Terrain && Frame.T1.X > 0.5f)
		{
			Args.Terrain = Terrain->GameThread_GetRenderTargetResource();
			Args.TerrainParams[0] = Frame.T0;
			Args.TerrainParams[1] = Frame.T1;
			Args.TerrainParams[2] = Frame.T2;
		}
	}

	// Shelter boxes in reach of the map.
	const FBox2D Reach(FVector2D(Center) - FVector2D(Extent), FVector2D(Center) + FVector2D(Extent));
	for (TObjectIterator<UDreamWeatherShelterComponent> It; It; ++It)
	{
		const UDreamWeatherShelterComponent* Box = *It;
		if (!Box || Box->GetWorld() != World || !Box->IsRegistered())
		{
			continue;
		}
		const FTransform& T = Box->GetComponentTransform();
		const FVector C = T.GetLocation();
		const float Radius = float(FVector2D(Box->Extent.X, Box->Extent.Y).Size()) + Box->FadeDistance;
		if (!Reach.ExpandBy(Radius).IsInside(FVector2D(C)))
		{
			continue;
		}
		const float Yaw = FMath::DegreesToRadians(float(T.Rotator().Yaw));
		Args.Shelters.Add(FVector4f(float(C.X), float(C.Y), FMath::Cos(Yaw), FMath::Sin(Yaw)));
		Args.Shelters.Add(FVector4f(float(Box->Extent.X), float(Box->Extent.Y), float(C.Z + Box->Extent.Z), Box->FadeDistance));
		Args.Shelters.Add(FVector4f(Box->Strength, 0.0f, 0.0f, 0.0f));
	}
	// Roof captures in reach (up to four, the nearest), and the mask brushes.
	TArray<const UDreamSurfaceRoofCaptureComponent*> Roofs;
	for (TObjectIterator<UDreamSurfaceRoofCaptureComponent> It; It; ++It)
	{
		const UDreamSurfaceRoofCaptureComponent* Roof = *It;
		if (Roof && Roof->GetWorld() == World && Roof->IsRegistered() && Roof->GetCaptureTarget() && Roof->GetCaptureCount() > 0
			&& Reach.ExpandBy(Roof->GetCaptureSize()).IsInside(FVector2D(Roof->GetComponentLocation())))
		{
			Roofs.Add(Roof);
		}
	}
	Roofs.Sort([&Center](const UDreamSurfaceRoofCaptureComponent& A, const UDreamSurfaceRoofCaptureComponent& B)
	{
		return FVector2D::DistSquared(FVector2D(A.GetComponentLocation()), Center) < FVector2D::DistSquared(FVector2D(B.GetComponentLocation()), Center);
	});
	for (const UDreamSurfaceRoofCaptureComponent* Roof : Roofs)
	{
		if (Args.RoofTargets.Num() >= 4)
		{
			break;
		}
		const FVector C = Roof->GetComponentLocation();
		const float Yaw = FMath::DegreesToRadians(float(Roof->GetComponentRotation().Yaw));
		Args.RoofTargets.Add(Roof->GetCaptureTarget()->GameThread_GetRenderTargetResource());
		Args.Roofs.Add(FVector4f(float(C.X), float(C.Y), FMath::Cos(Yaw), FMath::Sin(Yaw)));
		Args.Roofs.Add(FVector4f(float(Roof->Extent.X), float(Roof->Extent.Y), float(Roof->GetCaptureZ()), float(Roof->GetCaptureSize())));
		Args.Roofs.Add(FVector4f(Roof->Strength, float(Roof->Extent.Z * 2.0 + 10.0), 0.0f, 0.0f));
	}
	for (TObjectIterator<UDreamSurfaceMaskComponent> It; It; ++It)
	{
		const UDreamSurfaceMaskComponent* Brush = *It;
		if (Brush && Brush->GetWorld() == World && Brush->IsRegistered()
			&& Reach.ExpandBy(FMath::Max(Brush->Extent.X, Brush->Extent.Y) * 1.5 + Brush->FadeDistance).IsInside(FVector2D(Brush->GetComponentLocation())))
		{
			Brush->MakeMapRecord(Args.Masks);
		}
	}
	Args.MaskTarget = Mask->GameThread_GetRenderTargetResource();
	Renderer->EnqueueCover(MoveTemp(Args));
	CoverCenter = Center;
	LastCoverDraw = Now;
}
