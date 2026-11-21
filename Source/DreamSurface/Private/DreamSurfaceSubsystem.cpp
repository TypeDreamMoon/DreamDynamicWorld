// Copyright DreamMoon. All Rights Reserved.

#include "DreamSurfaceSubsystem.h"
#include "DreamSurfaceHlslShim.h"
#include "DreamSurfaceModule.h"
#include "DreamSurfaceSettings.h"

#include "DreamWeatherChannels.h"
#include "DreamWeatherSubsystem.h"
#include "DreamWorldCelestial.h"
#include "DreamWorldSubsystem.h"
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
		});
	}
}

void UDreamSurfaceSubsystem::Deinitialize()
{
	if (UDreamWeatherSubsystem* Weather = WeatherSubsystem.Get())
	{
		Weather->OnWeatherFrame.Remove(FrameHandle);
		Weather->OnWeatherStopped.Remove(StoppedHandle);
	}
	History.Reset();
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
	return Step(AtStep, DriveAt(Position, double(Now) * Seconds), UDreamSurfaceSettings::Get()->Rates, Partial);
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
	}
	const float Partial = float((Now - double(StepIndex) * Seconds) / 3600.0);
	ViewState = Step(ViewStepStart, DriveAt(ViewAnchor, double(StepIndex) * Seconds, true), UDreamSurfaceSettings::Get()->Rates, Partial);
}
