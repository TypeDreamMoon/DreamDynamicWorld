// Copyright DreamMoon. All Rights Reserved.

#include "DreamSurfaceMaps.h"
#include "DreamWorldQuality.h"
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

	/** Float4s per zone record (DreamSurfaceMap.usf's DSM_ZONE_FLOAT4S). */
	constexpr int32 ZoneFloat4s = 5;

	/** Loads a map's render target and makes it something a compute pass can write. */
	/**
	 * The state maps sized for the surfaces' quality level (UDreamWorldQuality, r.DreamSurface.Quality) as the world
	 * starts: half their authored size at low and medium, three quarters at high, as authored above. Only then -- a level
	 * changed while the game runs takes effect at the next load: resizing would drop what the maps have gathered (the
	 * wetness, the snow) and show as a jump. A game's world only (the editor's assets are left as authored).
	 */
	void SizeForQuality(UTextureRenderTarget2D* RT, EPixelFormat Format)
	{
		static TMap<TWeakObjectPtr<UTextureRenderTarget2D>, FIntPoint> Authored;
		if (!RT)
		{
			return;
		}
		const FIntPoint* Known = Authored.Find(RT);
		const FIntPoint Size = Known ? *Known : Authored.Add(RT, FIntPoint(RT->SizeX, RT->SizeY));
		const int32 Level = UDreamWorldQuality::GetLevel(EDreamWorldQualityGroup::Surface);
		const float Scale = Level <= 1 ? 0.5f : (Level == 2 ? 0.75f : 1.0f);
		const FIntPoint Want(FMath::Max(16, FMath::RoundToInt(Size.X * Scale)), FMath::Max(16, FMath::RoundToInt(Size.Y * Scale)));
		if (RT->SizeX != Want.X || RT->SizeY != Want.Y)
		{
			UE_LOG(LogDreamSurface, Display, TEXT("Surface map %s: %dx%d for quality level %d"), *RT->GetName(), Want.X, Want.Y, Level);
			RT->InitCustomFormat(Want.X, Want.Y, Format, true);
			RT->UpdateResourceImmediate(false);
		}
	}

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
	 * The zones of a world as the surface map reads them (DreamSurfaceMap.usf), in the order the weather applies them.
	 * Override zones only, as the weather map takes them.
	 *   Records  five float4 each: the weather map's three (footprint and coverage, darkness, precipitation, strength),
	 *            then the snow ratio, temperature offset, dust and wind, then the fog, the freezing rain, the hail and the
	 *            sleet (below -9999: not set); then the polygons' points.
	 *            The footprint and strength are those at the start of the step in progress, as a query takes them.
	 *   Tracks   NumSteps float4 per zone, oldest step first: where its footprint's centre stood (xy) and how strong it
	 *            was (z) at the start of each whole step. A zone that moves (a storm drifting across) or fades has its
	 *            weather where it was then, as a query does (UDreamWeatherZoneComponent::GetWeightAtTime): taking it
	 *            where it is now for every step laid a storm's whole memory of snow under wherever it had just got to.
	 * Both depend on the steps only, not on the moment within one: a zone moving along changes nothing until a step goes by.
	 */
	struct FZoneSet
	{
		TArray<FVector4f> Records;
		TArray<FVector4f> Tracks;
		int32 Num = 0;
	};

	FZoneSet GatherZones(const UWorld* World, int32 MaxZones, int64 FirstStep, int32 NumSteps, double StepSeconds)
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
		FZoneSet Set;
		Set.Num = Zones.Num();
		const double Current = double(FirstStep + NumSteps) * StepSeconds;   // the start of the step in progress
		TArray<FVector4f> PolygonPoints;
		for (const UDreamWeatherZoneComponent* Zone : Zones)
		{
			auto Value = [Zone](int32 Channel, float Unset)
			{
				float V;
				return Zone->GetChannelValue(Channel, V) ? V : Unset;
			};
			FVector4f Z0, Z1;
			Zone->MakeFootprintRecord(Current, Z0, Z1, PolygonPoints);
			Set.Records.Add(Z0);
			Set.Records.Add(Z1);
			Set.Records.Add(FVector4f(Value(C::CloudCoverage, -1.0f), Value(C::CloudDarkness, -1.0f), Value(C::Precipitation, -1.0f), Zone->GetStrengthAt(Current)));
			Set.Records.Add(FVector4f(Value(C::SnowRatio, -1.0e4f), Value(C::Temperature, -1.0e4f), Value(C::Dust, -1.0e4f), Value(C::Wind, -1.0e4f)));
			Set.Records.Add(FVector4f(Value(C::Fog, -1.0e4f), Value(C::FreezingRain, -1.0e4f), Value(C::Hail, -1.0e4f), Value(C::Sleet, -1.0e4f)));
			for (int32 k = 0; k < NumSteps; ++k)
			{
				const double At = double(FirstStep + k) * StepSeconds;
				const FVector Center = Zone->GetFootprintCenterAt(At);
				Set.Tracks.Add(FVector4f(float(Center.X), float(Center.Y), Zone->GetStrengthAt(At), 0.0f));
			}
		}
		UDreamWeatherZoneComponent::AppendPolygonPoints(Set.Records, DreamSurface::Maps::ZoneFloat4s, PolygonPoints);
		return Set;
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
	Collector.AddReferencedObject(Near.IceTarget);
	Collector.AddReferencedObject(Far.IceTarget);
	Collector.AddReferencedObject(Near.HailTarget);
	Collector.AddReferencedObject(Far.HailTarget);
	Collector.AddReferencedObject(Cover);
	Collector.AddReferencedObject(Mask);
	Collector.AddReferencedObject(Trails);
}

uint32 FDreamSurfaceMaps::GetDrawCount() const
{
	return Renderer ? Renderer->GetDrawCount() : 0;
}

uint32 FDreamSurfaceMaps::GetIntegrateCount() const
{
	return Renderer ? Renderer->GetIntegrateCount() : 0;
}

bool FDreamSurfaceMaps::Resolve(bool bGameWorld)
{
	if (Near.Target && Far.Target && Near.IceTarget && Far.IceTarget && Near.HailTarget && Far.HailTarget && Cover && Mask && Trails)
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
	Near.IceTarget = LoadTarget(S->NearIceMapTexture, PF_FloatRGBA);
	Far.IceTarget = LoadTarget(S->FarIceMapTexture, PF_FloatRGBA);
	Near.HailTarget = LoadTarget(S->NearHailMapTexture, PF_FloatRGBA);
	Far.HailTarget = LoadTarget(S->FarHailMapTexture, PF_FloatRGBA);
	Cover = LoadTarget(S->CoverMapTexture, PF_A32B32G32R32F);
	Mask = LoadTarget(S->MaskMapTexture, PF_FloatRGBA);
	Trails = LoadTarget(S->TrailMapTexture, PF_FloatRGBA);
	if (bGameWorld)
	{
		SizeForQuality(Near.Target, PF_FloatRGBA);
		SizeForQuality(Far.Target, PF_FloatRGBA);
		SizeForQuality(Near.IceTarget, PF_FloatRGBA);
		SizeForQuality(Far.IceTarget, PF_FloatRGBA);
		SizeForQuality(Near.HailTarget, PF_FloatRGBA);
		SizeForQuality(Far.HailTarget, PF_FloatRGBA);
	}
	if (!Near.Target || !Far.Target || !Near.IceTarget || !Far.IceTarget || !Near.HailTarget || !Far.HailTarget || !Cover)
	{
		UE_LOG(LogDreamSurface, Warning, TEXT("Surface maps missing (run the DreamDynamicWorld Setup): near %s, far %s, ice %s, hail %s, cover %s"),
			Near.Target ? TEXT("ok") : TEXT("missing"), Far.Target ? TEXT("ok") : TEXT("missing"),
			Near.IceTarget && Far.IceTarget ? TEXT("ok") : TEXT("missing"), Near.HailTarget && Far.HailTarget ? TEXT("ok") : TEXT("missing"),
			Cover ? TEXT("ok") : TEXT("missing"));
	}
	return Near.Target && Far.Target && Near.IceTarget && Far.IceTarget && Near.HailTarget && Far.HailTarget && Cover && Mask && Trails;
}

void FDreamSurfaceMaps::ForgetDrawn()
{
	Near.bLive = false;
	Far.bLive = false;
	CoverCenter = FVector2D(1.0e30, 1.0e30);
	LastCoverDraw = -1.0e30;
	TrailCenter = FVector2D(1.0e30, 1.0e30);
	LastTrailDraw = -1.0e30;
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
		Clock->SetVectorParameter(MPC_SurfaceViewIce, Off);
		Clock->SetVectorParameter(MPC_SurfaceViewHail, Off);
		Clock->SetVectorParameter(MPC_SurfaceTrails, Off);
	}
}

void FDreamSurfaceMaps::Update(UDreamSurfaceSubsystem& Surface, UDreamWorldSubsystem& Clock, UDreamWeatherSubsystem& Weather)
{
	using namespace DreamSurface::Names;
	const UDreamSurfaceSettings* S = UDreamSurfaceSettings::Get();
	// Beyond both maps the land takes the region's state: the weather with no zone, no lee and no water body over it, as
	// the far map has it away from the zones (the view's own state would carry a zone or a flood at the camera out to
	// the horizon).
	const FDreamSurfaceState& Region = Surface.GetRegionState();
	Clock.SetVectorParameter(MPC_SurfaceView, FLinearColor(Region.ToVector()));
	Clock.SetVectorParameter(MPC_SurfaceViewIce, FLinearColor(Region.ToIceVector()));
	Clock.SetVectorParameter(MPC_SurfaceViewHail, FLinearColor(Region.ToHailVector()));
	Clock.SetVectorParameter(MPC_SurfaceParams, FLinearColor(S->MaxSnowDepth, UDreamWeatherSettings::Get()->RoofClearance, S->CoverSoftness, S->PuddleScale));

	UWorld* World = Surface.GetWorld();
	const bool bCanRender = S->bSurfaceMaps && FApp::CanEverRender() && World && World->Scene != nullptr && !IsRunningDedicatedServer();
	bool bRedrawAll = false;
	if (bCanRender && !Writer.Acquire(World, bRedrawAll))
	{
		// Another world has the shared maps (play in editor): leave them, and where this world's materials look, alone.
		return;
	}
	if (bRedrawAll)
	{
		ForgetDrawn();
	}
	if (!bCanRender || !Resolve(Surface.GetWorld() && Surface.GetWorld()->IsGameWorld()))
	{
		Clock.SetVectorParameter(MPC_SurfaceNear, FLinearColor(0.0f, 0.0f, 0.0f, 0.0f));
		Clock.SetVectorParameter(MPC_SurfaceFar, FLinearColor(0.0f, 0.0f, 0.0f, 0.0f));
		return;
	}

	const double Now = FPlatformTime::Seconds();
	// The zones at the starts of the steps the maps integrate: they change when a step goes by (which integrates anyway)
	// or a zone is changed, not as a zone moves along within a step.
	const FDreamSurfaceMapSteps& Steps = Surface.GetMapSteps();
	const int32 NumSteps = Steps.Steps.Num() / FDreamSurfaceMapSteps::FloatsPerStep;
	DreamSurface::Maps::FZoneSet Zones = DreamSurface::Maps::GatherZones(World, FMath::Max(1, UDreamWeatherSettings::Get()->MaxMapZones),
		Steps.Step - NumSteps, NumSteps, UDreamSurfaceSubsystem::StepSeconds());
	const bool bZonesChanged = Zones.Records != LastZones || Zones.Tracks != LastZoneTracks || bIntegrateRequested
		|| DreamSurface::Maps::CVarIntegrateEveryFrame.GetValueOnGameThread() != 0;
	LastZones = MoveTemp(Zones.Records);
	LastZoneTracks = MoveTemp(Zones.Tracks);
	LastNumZones = Zones.Num;
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
	DrawTrails(Surface, Clock, Weather.GetViewSample(), Now);
	Clock.SetVectorParameter(MPC_SurfaceTrails, Placement(TrailCenter, UDreamSurfaceSettings::Get()->TrailExtent, TrailCenter.X < 1.0e29));
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
	Args.IceTarget = Map.IceTarget ? Map.IceTarget->GameThread_GetRenderTargetResource() : nullptr;
	Args.HailTarget = Map.HailTarget ? Map.HailTarget->GameThread_GetRenderTargetResource() : nullptr;
	if (!Args.Target || !Args.IceTarget || !Args.HailTarget)
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
	Args.PartialKinds = Steps.PartialKinds;
	Args.PartialHours = Steps.PartialHours;
	const FDreamSurfaceRates& Rates = UDreamSurfaceSettings::Get()->Rates;
	Args.Rates[0] = Rates.R0();
	Args.Rates[1] = Rates.R1();
	Args.Rates[2] = Rates.R2();
	Args.Rates[3] = Rates.R3();
	Args.Rates[4] = Rates.R4();
	Args.Rates[5] = Rates.R5();
	Args.PrecipitationGate = FVector2f(UDreamWeatherSettings::Get()->PrecipitationCloudCover);
	// The wind where the surface lies, from the wind's runtime terrain map (UDreamSurfaceSubsystem::DriveAt asks the CPU's).
	UWorld* World = Surface.GetWorld();
	const UDreamWindSubsystem* Wind = World && UDreamSurfaceSettings::Get()->bWindDrift ? World->GetSubsystem<UDreamWindSubsystem>() : nullptr;
	if (Wind)
	{
		const FDreamWindFrameData& Frame = Wind->GetFrameData();
		auto Resource = [](const TWeakObjectPtr<UTextureRenderTarget2D>& Texture) { return Texture.IsValid() ? Texture->GameThread_GetRenderTargetResource() : nullptr; };
		Args.WindL1Near = Frame.L1Near;
		Args.WindL1Far = Frame.L1Far;
		Args.WindL1Params = Frame.L1Params;
		Args.WindL1NearDerived = Resource(Frame.L1NearDerived);
		Args.WindL1FarDerived = Resource(Frame.L1FarDerived);
		Args.WindL1NearHeight = Resource(Frame.L1NearHeight);
		Args.WindL1FarHeight = Resource(Frame.L1FarHeight);
	}
	Args.Zones = LastZones;
	Args.NumZones = LastNumZones;
	// The floods over the steps (the whole steps' only when integrating: the shader reads the last block either way).
	const int32 BlockFloats = Steps.NumFloodBodies * 2;
	const int32 WholeSteps = Steps.Steps.Num() / FDreamSurfaceMapSteps::FloatsPerStep;
	if (BlockFloats > 0 && Steps.Floods.Num() == (WholeSteps + 1) * BlockFloats)
	{
		Args.NumFloodBodies = Steps.NumFloodBodies;
		if (bIntegrate)
		{
			Args.Floods = Steps.Floods;
		}
		else
		{
			// Only the step in progress counts: put it where the shader looks for it (block NumSteps = 0).
			Args.Floods.Append(Steps.Floods.GetData() + WholeSteps * BlockFloats, BlockFloats);
		}
	}
	if (bIntegrate)
	{
		Args.ZoneTracks = LastZoneTracks;
	}
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
	Args.LowRadius = UDreamSurfaceSettings::Get()->LowRadius;

	// The wind's baked level heights: the level's top surfaces (roofs, and the ground's lows for the puddles).
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
		// The runtime terrain map's heights where it runs: the level as it stands now, roofs that moved included.
		UTextureRenderTarget2D* RuntimeHeight = Frame.L1NearHeight.Get();
		if (RuntimeHeight && Frame.L1Near.W > 0.5f)
		{
			Args.L1Height = RuntimeHeight->GameThread_GetRenderTargetResource();
			Args.L1Level = Frame.L1Near;
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

void FDreamSurfaceMaps::DrawTrails(UDreamSurfaceSubsystem& Surface, UDreamWorldSubsystem& Clock, const FDreamWeatherSample& View, double Now)
{
	const UDreamSurfaceSettings* S = UDreamSurfaceSettings::Get();
	const bool bPrints = Surface.GetPendingStamps().Num() > 0;
	if (bPrints)
	{
		WavesUntil = Now + 3.0 * double(FMath::Max(S->WaveSeconds, S->FlowBackSeconds));
	}
	if (!bPrints && Now >= WavesUntil && Now - LastTrailDraw < S->MapInterval)
	{
		return;
	}
	FDreamSurfaceMapRenderer::FTrailArgs Args;
	Args.Target = Trails->GameThread_GetRenderTargetResource();
	if (!Args.Target)
	{
		return;
	}
	// The map moves in eighths of its size; what it held moves with it.
	const float Extent = FMath::Max(500.0f, S->TrailExtent);
	const double Snap = Extent / 8.0;
	const FVector At = Clock.HasViewLocation() ? Clock.GetViewLocation() : FVector::ZeroVector;
	const FVector2D Center(FMath::GridSnap(At.X, Snap), FMath::GridSnap(At.Y, Snap));
	const double WorldSeconds = Clock.GetWorldSeconds();
	const bool bFirst = TrailCenter.X > 1.0e29;
	// A print fills with new snow, blows away with the wind and settles; the stirred water calms in real seconds.
	const double GameHours = bFirst ? 0.0 : FMath::Clamp((WorldSeconds - LastTrailWorldSeconds) / 3600.0, 0.0, 48.0);
	const double RealSeconds = bFirst ? 0.0 : FMath::Clamp(Now - LastTrailDraw, 0.0, 10.0);
	const double FillRate = View.SnowHere / S->PrintFillHours + View.Wind / S->PrintWindHours + 1.0 / S->PrintFadeHours;
	Args.Center = FVector2f(Center);
	Args.PreviousCenter = FVector2f(bFirst ? Center : TrailCenter);
	Args.Extent = Extent;
	Args.PrintKeep = float(FMath::Exp(-GameHours * FillRate));
	Args.RippleKeep = float(FMath::Exp(-RealSeconds / FMath::Max(0.05f, S->RippleSeconds)));
	// The water's surface: one step of the wave equation over the real time gone by (no further than half a texel's
	// reach, where it stays stable: a long frame slows the waves rather than blowing them up).
	const double Texel = Extent / FMath::Max(1, Trails->SizeX);
	const double Reach = FMath::Min(double(S->WaveSpeed) * RealSeconds / Texel, 0.7);
	Args.WaveStep = float(Reach * Reach);
	Args.WaveKeep = float(FMath::Exp(-RealSeconds / FMath::Max(0.05f, S->WaveSeconds)));
	Args.WaveSettle = float(FMath::Exp(-RealSeconds / FMath::Max(0.05f, S->FlowBackSeconds)));
	Args.bReset = bFirst || WorldSeconds < LastTrailWorldSeconds;
	Args.Stamps = Surface.TakePendingStamps();
	Renderer->EnqueueTrails(MoveTemp(Args));
	TrailCenter = Center;
	LastTrailDraw = Now;
	LastTrailWorldSeconds = WorldSeconds;
}
