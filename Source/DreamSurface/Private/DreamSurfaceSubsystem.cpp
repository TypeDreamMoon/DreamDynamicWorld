// Copyright DreamMoon. All Rights Reserved.

#include "DreamSurfaceSubsystem.h"
#include "DreamSurfaceHlslShim.h"
#include "DreamSurfaceMaps.h"
#include "DreamSurfaceInteraction.h"
#include "DreamSurfaceMask.h"
#include "DreamSurfaceModule.h"
#include "DreamSurfaceSettings.h"

#include "DreamWeatherChannels.h"
#include "DreamWeatherSettings.h"
#include "DreamWeatherSubsystem.h"
#include "DreamWorldCelestial.h"
#include "DreamWorldSubsystem.h"
#include "DreamWindSubsystem.h"
#include "Engine/World.h"

namespace DreamSurface::Private
{
	DreamWind::Hlsl::float4 ToHlsl4(const FVector4f& V)
	{
		return DreamWind::Hlsl::float4(V.X, V.Y, V.Z, V.W);
	}

	/** The view moving less than this keeps its step's start state (the weather does not change over a few metres). */
	constexpr double ViewAnchorReach = 500.0;

	/**
	 * A state under the mask brushes' multipliers (wet, water, snow, dust): the glaze goes with the wet, the frost and the
	 * rime with the snow (white deposits a brush keeping the snow off keeps off too). The materials do the same (DSM_MaskIce).
	 */
	FDreamSurfaceState Masked(const FDreamSurfaceState& State, const FVector4f& Mask)
	{
		const FVector4f V = State.ToVector();
		const FVector4f C = State.ToIceVector();
		auto Unit = [](float X) { return FMath::Clamp(X, 0.0f, 1.0f); };
		// (The hail with the snow, a white cover a brush keeping the snow off keeps off too: DSM_HailShown.)
		return FDreamSurfaceState::FromVectors(FVector4f(Unit(V.X * Mask.X), Unit(V.Y * Mask.Y), Unit(V.Z * Mask.Z), Unit(V.W * Mask.W)),
			FVector4f(Unit(C.X * Mask.X), Unit(C.Y * Mask.Z), Unit(C.Z * Mask.Z), Unit(C.W)), FVector4f(Unit(State.Hail * Mask.Z), 0.0f, 0.0f, 0.0f));
	}
}

UDreamSurfaceSubsystem* UDreamSurfaceSubsystem::Get(const UObject* WorldContextObject)
{
	const UWorld* World = WorldContextObject ? WorldContextObject->GetWorld() : nullptr;
	return World ? World->GetSubsystem<UDreamSurfaceSubsystem>() : nullptr;
}

uint32 UDreamSurfaceSubsystem::GetMapDrawCount() const
{
	return Maps.IsValid() ? Maps->GetDrawCount() : 0;
}

bool UDreamSurfaceSubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
	if (!Super::ShouldCreateSubsystem(Outer))
	{
		return false;
	}
	const UWorld* World = Cast<UWorld>(Outer);
	return World && (World->WorldType == EWorldType::Game || World->WorldType == EWorldType::PIE || World->WorldType == EWorldType::Editor);
}

void UDreamSurfaceSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	ClockSubsystem = Collection.InitializeDependency<UDreamWorldSubsystem>();
	if (UDreamWeatherSubsystem* Weather = Collection.InitializeDependency<UDreamWeatherSubsystem>())
	{
		WeatherSubsystem = Weather;
		FrameHandle = Weather->OnWeatherFrame.AddUObject(this, &UDreamSurfaceSubsystem::OnWeatherFrame);
		StoppedHandle = Weather->OnWeatherStopped.AddWeakLambda(this, [this]()
		{
			ViewState = FDreamSurfaceState();
			ViewStep = TNumericLimits<int64>::Min();
			if (Maps)
			{
				Maps->Stop(ClockSubsystem.Get());
			}
		});
	}
	Maps = MakeShared<FDreamSurfaceMaps>();
}

void UDreamSurfaceSubsystem::Deinitialize()
{
	if (UDreamWeatherSubsystem* Weather = WeatherSubsystem.Get())
	{
		Weather->OnWeatherFrame.Remove(FrameHandle);
		Weather->OnWeatherStopped.Remove(StoppedHandle);
	}
	History.Reset();
	Maps.Reset();
	Super::Deinitialize();
}

bool UDreamSurfaceSubsystem::IsActive() const
{
	const UDreamWeatherSubsystem* Weather = WeatherSubsystem.Get();
	return UDreamSurfaceSettings::Get()->bEnabled && Weather && Weather->IsActive() && ClockSubsystem.IsValid();
}

double UDreamSurfaceSubsystem::StepSeconds()
{
	return FMath::Max(1.0, double(UDreamSurfaceSettings::Get()->StepMinutes)) * 60.0;
}

int32 UDreamSurfaceSubsystem::MemorySteps()
{
	const UDreamSurfaceSettings* S = UDreamSurfaceSettings::Get();
	return FMath::Max(1, FMath::CeilToInt32(double(S->MemoryHours) * 3600.0 / StepSeconds()));
}

FDreamSurfaceState UDreamSurfaceSubsystem::Step(const FDreamSurfaceState& State, const FDreamSurfaceDrive& Drive, const FDreamSurfaceRates& Rates, float Hours)
{
	using namespace DreamSurface::Private;
	using DreamWind::Hlsl::float4;
	const float4 S = ToHlsl4(State.ToVector());
	const float4 S2 = ToHlsl4(State.ToIceVector());
	const float4 S3 = ToHlsl4(State.ToHailVector());
	const float4 D0 = ToHlsl4(Drive.D0());
	const float4 D1 = ToHlsl4(Drive.D1());
	const float4 D2 = ToHlsl4(Drive.D2());
	const float4 R0 = ToHlsl4(Rates.R0());
	const float4 R3 = ToHlsl4(Rates.R3());
	const float4 R4 = ToHlsl4(Rates.R4());
	const float4 R5 = ToHlsl4(Rates.R5());
	const float H = FMath::Max(Hours, 0.0f);
	// Every side from the state at the start of the step, as the surface map does.
	const float4 Out = DreamWind::Hlsl::DSF_Step(S, S2, S3, D0, D1, R0, ToHlsl4(Rates.R1()), ToHlsl4(Rates.R2()), R3, R4, R5, H);
	const float4 Cold = DreamWind::Hlsl::DSF_StepIce(S, S2, D0, D1, R0, R3, R4, H);
	const float4 Hail = DreamWind::Hlsl::DSF_StepHail(S3, D0, D1, D2, R4, R5, H);
	return FDreamSurfaceState::FromVectors(FVector4f(Out.x, Out.y, Out.z, Out.w), FVector4f(Cold.x, Cold.y, Cold.z, Cold.w), FVector4f(Hail.x, Hail.y, Hail.z, Hail.w));
}

FDreamWeatherValues UDreamSurfaceSubsystem::GlobalAtStep(int64 StepIndex) const
{
	if (const FDreamWeatherValues* Seen = History.Find(StepIndex))
	{
		return *Seen;
	}
	const UDreamWeatherSubsystem* Weather = WeatherSubsystem.Get();
	return Weather ? Weather->EvaluateSchedule(double(StepIndex) * StepSeconds()) : FDreamWeatherChannels::Defaults();
}

FDreamSurfaceDrive UDreamSurfaceSubsystem::DriveAt(const FVector& Position, double WorldSeconds, bool bOpenSky) const
{
	FDreamSurfaceDrive Drive;
	const UDreamWeatherSubsystem* Weather = WeatherSubsystem.Get();
	const UDreamWorldSubsystem* Clock = ClockSubsystem.Get();
	if (!Weather || !Clock)
	{
		return Drive;
	}
	const int64 StepIndex = FMath::FloorToInt64(WorldSeconds / StepSeconds());
	const FDreamWeatherSample W = Weather->SampleWeatherAt(Position, GlobalAtStep(StepIndex), WorldSeconds);
	const FDreamCelestialState Sky = DreamWorld::Celestial::Evaluate(Clock->GetCelestialSettings(), WorldSeconds);
	// The open-sky drive is the one the surface map computes per texel (DSF_OpenD0 / D1)...
	using namespace DreamSurface::Private;
	const FVector4f Here(W.CloudCoverage, W.CloudDarkness, W.Precipitation, W.Values.Get(FDreamWeatherChannels::SnowRatio));
	const FVector4f Air(W.Temperature, W.Wind, W.Dust, float(Sky.SunDirection.Z));
	const float Freezing = W.Values.Get(FDreamWeatherChannels::FreezingRain);
	const DreamWind::Hlsl::float2 Kinds(W.Values.Get(FDreamWeatherChannels::Hail), W.Values.Get(FDreamWeatherChannels::Sleet));
	const DreamWind::Hlsl::float4 D0 = DreamWind::Hlsl::DSF_OpenD0(ToHlsl4(Here), ToHlsl4(Air), Freezing, Kinds);
	const DreamWind::Hlsl::float4 D1 = DreamWind::Hlsl::DSF_OpenD1(ToHlsl4(Here), ToHlsl4(Air), W.Values.Get(FDreamWeatherChannels::Fog));
	const DreamWind::Hlsl::float4 D2 = DreamWind::Hlsl::DSF_OpenD2(ToHlsl4(Here), ToHlsl4(Air), Freezing, Kinds);
	// ...then the cover: under a roof nothing lands, the air is stiller, the sun and the sky are off and less dust gets in.
	const float Exposure = bOpenSky ? 1.0f : W.Exposure;
	Drive.Rain = D0.x * Exposure;
	Drive.Snow = D0.y * Exposure;
	Drive.Temperature = D0.z;
	Drive.Wind = D0.w * (0.3f + 0.7f * Exposure);
	Drive.Sunlight = D1.x * Exposure;
	Drive.Dust = D1.y * (0.2f + 0.8f * Exposure);
	Drive.OpenSky = D1.z * Exposure;
	Drive.Fog = D1.w;
	Drive.Hail = D2.x * Exposure;
	return Drive;
}

FDreamSurfaceState UDreamSurfaceSubsystem::IntegrateSteps(const FVector& Position, int64 FirstStep, int64 EndStep, const FDreamSurfaceState& Start, bool bOpenSky) const
{
	const FDreamSurfaceRates& Rates = UDreamSurfaceSettings::Get()->Rates;
	const double Seconds = StepSeconds();
	const float Hours = float(Seconds / 3600.0);
	FDreamSurfaceState S = Start;
	for (int64 k = FirstStep; k < EndStep; ++k)
	{
		S = Step(S, DriveAt(Position, double(k) * Seconds, bOpenSky), Rates, Hours);
	}
	return S;
}

FDreamSurfaceState UDreamSurfaceSubsystem::SampleSurfaceAt(const FVector& Position, double WorldSeconds) const
{
	if (!IsActive())
	{
		return FDreamSurfaceState();
	}
	// Whole steps of the timeline's own grid up to the step holding WorldSeconds, then the part of it gone by: two
	// questions a moment apart share every whole step.
	const double Seconds = StepSeconds();
	const int64 Now = FMath::FloorToInt64(WorldSeconds / Seconds);
	const FDreamSurfaceState AtStep = IntegrateSteps(Position, Now - MemorySteps(), Now, FDreamSurfaceState());
	const float Partial = float((WorldSeconds - double(Now) * Seconds) / 3600.0);
	const FDreamSurfaceState State = Step(AtStep, DriveAt(Position, double(Now) * Seconds), UDreamSurfaceSettings::Get()->Rates, Partial);
	// The mask brushes over the point (what the materials read from the mask map).
	const FVector4f Mask = GetMaskAt(Position);
	return DreamSurface::Private::Masked(State, Mask);
}

FDreamSurfaceState UDreamSurfaceSubsystem::SampleOpenSurface(FVector Position) const
{
	const UDreamWorldSubsystem* Clock = ClockSubsystem.Get();
	if (!IsActive() || !Clock)
	{
		return FDreamSurfaceState();
	}
	const double WorldSeconds = Clock->GetWorldSeconds();
	const double Seconds = StepSeconds();
	const int64 Now = FMath::FloorToInt64(WorldSeconds / Seconds);
	const FDreamSurfaceState AtStep = IntegrateSteps(Position, Now - MemorySteps(), Now, FDreamSurfaceState(), true);
	const float Partial = float((WorldSeconds - double(Now) * Seconds) / 3600.0);
	return Step(AtStep, DriveAt(Position, double(Now) * Seconds, true), UDreamSurfaceSettings::Get()->Rates, Partial);
}

namespace DreamSurface::Contact
{
	/** How far the ground lies below its surroundings at a point (DSF_GroundLow), cm, from the wind's baked level heights
	 *  as the cover map has it; 0 when the level has none. */
	static float GroundLowAt(const UWorld* World, const FVector& Location)
	{
		const UDreamWindSubsystem* Wind = World && UDreamWeatherSettings::Get()->bUseWindTerrainHeights ? World->GetSubsystem<UDreamWindSubsystem>() : nullptr;
		const FDreamWindTerrainCpuData* Terrain = Wind ? Wind->GetFrameData().TerrainCpu.Get() : nullptr;
		if (!Terrain || !Terrain->IsInside(Location.X, Location.Y))
		{
			return 0.0f;
		}
		auto HeightAt = [Terrain](double X, double Y)
		{
			return Terrain->HeightBilinear(FMath::Clamp(X, Terrain->Bounds.Min.X, Terrain->Bounds.Max.X),
				FMath::Clamp(Y, Terrain->Bounds.Min.Y, Terrain->Bounds.Max.Y), 0);
		};
		const double X = Location.X, Y = Location.Y;
		const double R = FMath::Max(UDreamSurfaceSettings::Get()->LowRadius, 50.0f), H = R * 0.5;
		using DreamWind::Hlsl::float4;
		const float4 A(HeightAt(X + R, Y), HeightAt(X - R, Y), HeightAt(X, Y + R), HeightAt(X, Y - R));
		const float4 B(HeightAt(X + H, Y + H), HeightAt(X - H, Y - H), HeightAt(X + H, Y - H), HeightAt(X - H, Y + H));
		return DreamWind::Hlsl::DSF_GroundLow(HeightAt(X, Y), A, B);
	}
}

FDreamSurfaceContact UDreamSurfaceSubsystem::SampleContact(FVector Location, FVector Normal) const
{
	FDreamSurfaceContact Contact;
	Contact.Location = Location;
	Contact.Normal = Normal;
	Contact.State = SampleSurface(Location);
	const float Up = FMath::Clamp(float(Normal.Z), 0.0f, 1.0f);
	// Where the materials put the water and the snow (DSM_Realistic, on a hard surface).
	using namespace DreamWind::Hlsl;
	const float X = float(Location.X), Y = float(Location.Y);
	const float Hollow = DSF_Hollow(X, Y, 260.0f);
	const float Depth = DSF_PuddleDepth(DSF_Hollow(X + 57.0f, Y - 23.0f, FMath::Max(UDreamSurfaceSettings::Get()->PuddleScale, 20.0f)),
		DreamSurface::Contact::GroundLowAt(GetWorld(), Location));
	Contact.Puddle = DSF_PuddleCover(Contact.State.Water, Up, Depth, 0.0f);
	const float SnowCover = DSF_SnowCover(Contact.State.Snow, Up, Hollow, DSF_SnowEdge(X, Y));
	Contact.SnowDepth = Contact.State.Snow * SnowCover * UDreamSurfaceSettings::Get()->MaxSnowDepth;
	// The grip: the worst of what lies there, each by how much of it covers the point (as the materials draw it).
	const FDreamSurfaceState& S = Contact.State;
	const float FrostCover = FMath::Clamp(FMath::Max(S.Frost, S.Rime) * (1.0f - SnowCover), 0.0f, 1.0f);
	const float FrozenPuddle = Contact.Puddle * S.FrozenWater;
	const float IceCover = FMath::Clamp(S.Ice * (1.0f - SnowCover), 0.0f, 1.0f);
	const float HailCover = FMath::Clamp(S.Hail * Up * (1.0f - SnowCover), 0.0f, 1.0f);
	const FDreamSurfaceGrip& Grip = UDreamSurfaceSettings::Get()->Grip;
	Contact.Grip = DSF_Grip(DreamSurface::Private::ToHlsl4(Grip.G0()), DreamSurface::Private::ToHlsl4(Grip.G1()),
		float4(IceCover, FrozenPuddle, FrostCover, SnowCover), float4(Contact.Puddle * (1.0f - S.FrozenWater), S.Wetness, HailCover, 0.0f));
	// The dominant: snow over ice over frost over water over dust over a wet film.
	if (SnowCover > 0.5f)
	{
		Contact.Kind = EDreamSurfaceContactKind::Snow;
	}
	else if (IceCover > 0.4f || FrozenPuddle > 0.5f)
	{
		Contact.Kind = EDreamSurfaceContactKind::Ice;
	}
	else if (HailCover > 0.4f)
	{
		Contact.Kind = EDreamSurfaceContactKind::Hail;
	}
	else if (FrostCover > 0.4f)
	{
		Contact.Kind = EDreamSurfaceContactKind::Frost;
	}
	else if (Contact.Puddle > 0.5f)
	{
		Contact.Kind = EDreamSurfaceContactKind::Puddle;
	}
	else if (Contact.State.Dust > 0.5f)
	{
		Contact.Kind = EDreamSurfaceContactKind::Dust;
	}
	else if (Contact.State.Wetness > 0.3f)
	{
		Contact.Kind = EDreamSurfaceContactKind::Wet;
	}
	return Contact;
}

float UDreamSurfaceSubsystem::GetGripAt(FVector Location) const
{
	return SampleContact(Location, FVector::UpVector).Grip;
}

void UDreamSurfaceSubsystem::Stamp(FVector Location, float Radius, float Strength, bool bSnowAndDust, bool bPuddles)
{
	if (PendingStamps.Num() >= 512)
	{
		return;
	}
	PendingStamps.Add(FVector4f(float(Location.X), float(Location.Y), FMath::Max(Radius, 1.0f), FMath::Clamp(Strength, 0.0f, 1.0f)));
	PendingStamps.Add(FVector4f(bSnowAndDust ? 1.0f : 0.0f, bPuddles ? 1.0f : 0.0f, 0.0f, 0.0f));
}

void UDreamSurfaceSubsystem::RegisterMask(UDreamSurfaceMaskComponent* Mask)
{
	MaskBrushes.AddUnique(Mask);
}

void UDreamSurfaceSubsystem::UnregisterMask(UDreamSurfaceMaskComponent* Mask)
{
	MaskBrushes.Remove(Mask);
}

FVector4f UDreamSurfaceSubsystem::GetMaskAt(const FVector& Position) const
{
	FVector4f Product(1.0f, 1.0f, 1.0f, 1.0f);
	for (const TWeakObjectPtr<UDreamSurfaceMaskComponent>& Weak : MaskBrushes)
	{
		if (const UDreamSurfaceMaskComponent* Mask = Weak.Get())
		{
			Product *= Mask->GetMultipliersAt(Position);
		}
	}
	return Product;
}

FDreamSurfaceState UDreamSurfaceSubsystem::SampleSurface(FVector Position) const
{
	const UDreamWorldSubsystem* Clock = ClockSubsystem.Get();
	return Clock ? SampleSurfaceAt(Position, Clock->GetWorldSeconds()) : FDreamSurfaceState();
}

float UDreamSurfaceSubsystem::GetSnowDepthAt(FVector Position) const
{
	return SampleSurface(Position).Snow * UDreamSurfaceSettings::Get()->MaxSnowDepth;
}

void UDreamSurfaceSubsystem::Record(int64 StepIndex, const FDreamWeatherValues& Global)
{
	// A jump back in time: what was recorded after it never happened in this timeline.
	for (auto It = History.CreateIterator(); It; ++It)
	{
		if (It.Key() > StepIndex || It.Key() < StepIndex - MemorySteps() - 1)
		{
			It.RemoveCurrent();
		}
	}
	if (!History.Contains(StepIndex))
	{
		History.Add(StepIndex, Global);
	}
}

void UDreamSurfaceSubsystem::OnWeatherFrame(UDreamWorldSubsystem& Clock, UDreamWeatherSubsystem& Weather, const FDreamWeatherSample& View, float RealDeltaSeconds)
{
	if (!IsActive())
	{
		ViewState = FDreamSurfaceState();
		ViewStep = TNumericLimits<int64>::Min();
		if (Maps)
		{
			Maps->Stop(&Clock);
		}
		return;
	}
	const double Now = Clock.GetWorldSeconds();
	const double Seconds = StepSeconds();
	const int64 StepIndex = FMath::FloorToInt64(Now / Seconds);
	Record(StepIndex, Weather.GetGlobalValues());

	// The view's state in the open: the whole steps are integrated once per step (or again after a move), each frame
	// only adds its part of the current step.
	const FVector At = Clock.HasViewLocation() ? Clock.GetViewLocation() : FVector::ZeroVector;
	const bool bMoved = FVector::DistSquared(At, ViewAnchor) > FMath::Square(DreamSurface::Private::ViewAnchorReach);
	if (bMoved || StepIndex != ViewStep)
	{
		ViewAnchor = At;
		ViewStep = StepIndex;
		ViewStepStart = IntegrateSteps(At, StepIndex - MemorySteps(), StepIndex, FDreamSurfaceState(), true);
		BuildMapSteps(StepIndex);
	}
	const float Partial = float((Now - double(StepIndex) * Seconds) / 3600.0);
	ViewState = Step(ViewStepStart, DriveAt(ViewAnchor, double(StepIndex) * Seconds, true), UDreamSurfaceSettings::Get()->Rates, Partial);
	MapSteps.PartialHours = Partial;

	if (Maps)
	{
		Maps->Update(*this, Clock, Weather);
	}
}

void UDreamSurfaceSubsystem::MapStepRecord(int64 StepIndex, FVector4f& OutWeather, FVector4f& OutChannels, FVector4f& OutAir, FVector4f& OutKinds) const
{
	using C = FDreamWeatherChannels;
	const double T = double(StepIndex) * StepSeconds();
	const FDreamWeatherValues Global = GlobalAtStep(StepIndex);
	OutKinds = FVector4f(Global.Get(C::Hail), Global.Get(C::Sleet), 0.0f, 0.0f);
	// The global values: the map applies the zones to them per texel.
	OutWeather = FVector4f(Global.Get(C::CloudCoverage), Global.Get(C::CloudDarkness), Global.Get(C::Precipitation), Global.Get(C::SnowRatio));
	OutChannels = FVector4f(Global.Get(C::Temperature), Global.Get(C::Dust), Global.Get(C::Wind), Global.Get(C::Fog));
	const UDreamWeatherSubsystem* Weather = WeatherSubsystem.Get();
	const UDreamWorldSubsystem* Clock = ClockSubsystem.Get();
	if (!Weather || !Clock)
	{
		OutAir = FVector4f(15.0f, -1.0f, 0.0f, 0.0f);
		return;
	}
	// The temperature of the hour at the view with no offset and a clear sky, and how much less of the daily swing a
	// closed cover leaves: a texel takes its own cover's share of that and adds its own offset (a cloudy zone's night is
	// milder there, as a query has it).
	const FDreamCelestialState Sky = DreamWorld::Celestial::Evaluate(Clock->GetCelestialSettings(), T);
	const float ClearSky = Weather->GetBaseTemperatureAt(ViewAnchor, 0.0f, 0.0f, T);
	const float Overcast = Weather->GetBaseTemperatureAt(ViewAnchor, 0.0f, 1.0f, T);
	OutAir = FVector4f(ClearSky, float(Sky.SunDirection.Z), ClearSky - Overcast, Global.Get(C::FreezingRain));
}

void UDreamSurfaceSubsystem::BuildMapSteps(int64 StepIndex)
{
	MapSteps.Step = StepIndex;
	MapSteps.Steps.Reset(MemorySteps() * FDreamSurfaceMapSteps::FloatsPerStep);
	for (int64 k = StepIndex - MemorySteps(); k < StepIndex; ++k)
	{
		FVector4f Weather, Channels, Air, Kinds;
		MapStepRecord(k, Weather, Channels, Air, Kinds);
		MapSteps.Steps.Add(Weather);
		MapSteps.Steps.Add(Channels);
		MapSteps.Steps.Add(Air);
		MapSteps.Steps.Add(Kinds);
	}
	MapStepRecord(StepIndex, MapSteps.PartialWeather, MapSteps.PartialChannels, MapSteps.PartialAir, MapSteps.PartialKinds);
}

void UDreamSurfaceSubsystem::AddReferencedObjects(UObject* InThis, FReferenceCollector& Collector)
{
	Super::AddReferencedObjects(InThis, Collector);
	UDreamSurfaceSubsystem* This = CastChecked<UDreamSurfaceSubsystem>(InThis);
	if (This->Maps)
	{
		This->Maps->AddReferencedObjects(Collector);
	}
}
