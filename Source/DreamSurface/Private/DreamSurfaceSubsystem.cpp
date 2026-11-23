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
	const DreamWind::Hlsl::float4 Out = DreamWind::Hlsl::DSF_Step(ToHlsl4(State.ToVector()), ToHlsl4(Drive.D0()), ToHlsl4(Drive.D1()),
		ToHlsl4(Rates.R0()), ToHlsl4(Rates.R1()), ToHlsl4(Rates.R2()), FMath::Max(Hours, 0.0f));
	return FDreamSurfaceState::FromVector(FVector4f(Out.x, Out.y, Out.z, Out.w));
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
	const DreamWind::Hlsl::float4 D0 = DreamWind::Hlsl::DSF_OpenD0(ToHlsl4(Here), ToHlsl4(Air));
	const DreamWind::Hlsl::float4 D1 = DreamWind::Hlsl::DSF_OpenD1(ToHlsl4(Here), ToHlsl4(Air));
	// ...then the cover: under a roof nothing lands, the air is stiller, the sun is off and less dust gets in.
	const float Exposure = bOpenSky ? 1.0f : W.Exposure;
	Drive.Rain = D0.x * Exposure;
	Drive.Snow = D0.y * Exposure;
	Drive.Temperature = D0.z;
	Drive.Wind = D0.w * (0.3f + 0.7f * Exposure);
	Drive.Sunlight = D1.x * Exposure;
	Drive.Dust = D1.y * (0.2f + 0.8f * Exposure);
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
	const FVector4f V = State.ToVector();
	return FDreamSurfaceState::FromVector(FVector4f(FMath::Clamp(V.X * Mask.X, 0.0f, 1.0f), FMath::Clamp(V.Y * Mask.Y, 0.0f, 1.0f),
		FMath::Clamp(V.Z * Mask.Z, 0.0f, 1.0f), FMath::Clamp(V.W * Mask.W, 0.0f, 1.0f)));
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
	// The dominant: snow over water over dust over a wet film.
	if (SnowCover > 0.5f)
	{
		Contact.Kind = EDreamSurfaceContactKind::Snow;
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

void UDreamSurfaceSubsystem::MapStepRecord(int64 StepIndex, FVector4f& OutWeather, FVector4f& OutChannels, FVector4f& OutAir) const
{
	using C = FDreamWeatherChannels;
	const double T = double(StepIndex) * StepSeconds();
	const FDreamWeatherValues Global = GlobalAtStep(StepIndex);
	// The global values: the map applies the zones to them per texel.
	OutWeather = FVector4f(Global.Get(C::CloudCoverage), Global.Get(C::CloudDarkness), Global.Get(C::Precipitation), Global.Get(C::SnowRatio));
	OutChannels = FVector4f(Global.Get(C::Temperature), Global.Get(C::Dust), Global.Get(C::Wind), 0.0f);
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
	OutAir = FVector4f(ClearSky, float(Sky.SunDirection.Z), ClearSky - Overcast, 0.0f);
}

void UDreamSurfaceSubsystem::BuildMapSteps(int64 StepIndex)
{
	MapSteps.Step = StepIndex;
	MapSteps.Steps.Reset(MemorySteps() * 3);
	for (int64 k = StepIndex - MemorySteps(); k < StepIndex; ++k)
	{
		FVector4f Weather, Channels, Air;
		MapStepRecord(k, Weather, Channels, Air);
		MapSteps.Steps.Add(Weather);
		MapSteps.Steps.Add(Channels);
		MapSteps.Steps.Add(Air);
	}
	MapStepRecord(StepIndex, MapSteps.PartialWeather, MapSteps.PartialChannels, MapSteps.PartialAir);
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
