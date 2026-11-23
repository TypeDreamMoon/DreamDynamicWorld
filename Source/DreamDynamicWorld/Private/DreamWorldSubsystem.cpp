// Copyright DreamMoon. All Rights Reserved.

#include "DreamWorldSubsystem.h"
#include "DreamWorldQuality.h"
#include "DreamDynamicWorldModule.h"
#include "DreamWorldCelestial.h"
#include "DreamWorldSettings.h"
#include "DreamWorldViewExtension.h"

#include "Engine/Engine.h"
#include "Engine/EngineCustomTimeStep.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "HAL/PlatformTime.h"
#include "Materials/MaterialParameterCollection.h"
#include "Materials/MaterialParameterCollectionInstance.h"
#include "Misc/App.h"
#include "SceneViewExtension.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(DreamWorldSubsystem)

DECLARE_STATS_GROUP(TEXT("DreamWorld"), STATGROUP_DreamWorld, STATCAT_Advanced);
DECLARE_CYCLE_STAT(TEXT("DreamWorld Tick"), STAT_DreamWorldTick, STATGROUP_DreamWorld);
DECLARE_CYCLE_STAT(TEXT("DreamWorld View Probe"), STAT_DreamWorldViewProbe, STATGROUP_DreamWorld);

static TAutoConsoleVariable<int32> CVarDreamWorldCinematicMode(
	TEXT("DreamWorld.CinematicMode"),
	-1,
	TEXT("Cinematic updates (every frame a full update, no easing): -1 while a Movie Render Queue render drives the frames (default), 0 never, 1 always."),
	ECVF_Default);

UDreamWorldSubsystem* UDreamWorldSubsystem::Get(const UObject* WorldContextObject)
{
	const UWorld* World = WorldContextObject ? WorldContextObject->GetWorld() : nullptr;
	return World ? World->GetSubsystem<UDreamWorldSubsystem>() : nullptr;
}

bool UDreamWorldSubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
	if (!Super::ShouldCreateSubsystem(Outer))
	{
		return false;
	}
	const UWorld* World = Cast<UWorld>(Outer);
	return World && (World->WorldType == EWorldType::Game || World->WorldType == EWorldType::PIE || World->WorldType == EWorldType::Editor);
}

void UDreamWorldSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	ApplyClockConfig(UDreamWorldSettings::Get()->DefaultClock, true);
	if (FApp::CanEverRender())
	{
		ViewExtension = FSceneViewExtensions::NewExtension<FDreamWorldViewExtension>(GetWorld(), this);
	}
}

void UDreamWorldSubsystem::Deinitialize()
{
	if (ViewExtension.IsValid())
	{
		ViewExtension->Detach();
		ViewExtension.Reset();
	}
	OnWorldPreTick.Clear();
	OnWorldTick.Clear();
	OnCaptureSnapshot.Clear();
	OnApplySnapshot.Clear();
	ParameterCollectionInstance = nullptr;
	ParameterCollection = nullptr;
	Super::Deinitialize();
}

bool UDreamWorldSubsystem::IsTickable() const
{
	return Super::IsTickable() && !IsTemplate() && GetWorld() != nullptr;
}

bool UDreamWorldSubsystem::IsTickableInEditor() const
{
	return UDreamWorldSettings::Get()->bTickInEditor;
}

TStatId UDreamWorldSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UDreamWorldSubsystem, STATGROUP_Tickables);
}

// -------------------------------------------------------------------------------------------------
// Tick
// -------------------------------------------------------------------------------------------------

void UDreamWorldSubsystem::Tick(float DeltaTime)
{
	SCOPE_CYCLE_COUNTER(STAT_DreamWorldTick);
	const double TickStart = FPlatformTime::Seconds();
	// (The quality levels: their console variables set again when a level changed, before anything reads them.)
	UDreamWorldQuality::Apply();

	const double PrevSeconds = WorldSeconds;
	bool bJumped = bJumpPending;
	bJumpPending = false;
	// Cinematic updates: everything that eases snaps instead, every frame.
	const bool bCinematic = IsCinematicMode();
	if (bCinematic)
	{
		++TimeJumpSerial;
	}

	if (bTransitionActive)
	{
		TransitionElapsed += DeltaTime;
		const float Alpha = TransitionDuration > 0.0f ? FMath::Clamp(TransitionElapsed / TransitionDuration, 0.0f, 1.0f) : 1.0f;
		// Smooth start and stop; the hours in between run, so their events fire.
		WorldSeconds = FMath::Lerp(TransitionFrom, TransitionTo, double(FMath::SmoothStep(0.0f, 1.0f, Alpha)));
		if (Alpha >= 1.0f)
		{
			bTransitionActive = false;
		}
	}
	else if (IsClockRunning())
	{
		WorldSeconds += double(DeltaTime) * double(GetTimeScale());
	}

	UpdateCelestial();
	FireClockEvents(PrevSeconds, bJumped);
	WriteClockParameters();
	UpdateViewProbe(DeltaTime, bCinematic);

	OnWorldPreTick.Broadcast(*this, DeltaTime);
	OnWorldTick.Broadcast(*this, DeltaTime);

	Scheduler.Tick(FPlatformTime::Seconds(), UDreamWorldSettings::Get()->SchedulerBudgetMs);

	LastTickMs = (FPlatformTime::Seconds() - TickStart) * 1000.0;
	AverageTickMs = AverageTickMs <= 0.0 ? LastTickMs : FMath::Lerp(AverageTickMs, LastTickMs, 0.01);
}

bool UDreamWorldSubsystem::IsClockRunning() const
{
	if (bPaused || ExternalDrivers.Num() > 0)
	{
		return false;
	}
	const UWorld* World = GetWorld();
	return World && World->IsGameWorld() ? Clock.bAnimate : Clock.bAnimateInEditor;
}

void UDreamWorldSubsystem::UpdateCelestial()
{
	Celestial = DreamWorld::Celestial::Evaluate(Clock.Celestial, WorldSeconds);
}

void UDreamWorldSubsystem::FireClockEvents(double PrevSeconds, bool bJumped)
{
	if (!bEventsPrimed || bJumped)
	{
		// Nothing ran between the two times; report the jump once and start counting from here.
		if (bEventsPrimed)
		{
			OnTimeJumped.Broadcast();
		}
		bEventsPrimed = true;
		LastEventSeconds = WorldSeconds;
		LastSunElevation = Celestial.SunElevation;
		return;
	}

	using namespace DreamWorld::Celestial;
	const int64 PrevHour = int64(FMath::FloorToDouble(LastEventSeconds / 3600.0));
	const int64 NowHour = int64(FMath::FloorToDouble(WorldSeconds / 3600.0));
	if (NowHour != PrevHour)
	{
		OnHourChanged.Broadcast(int32(FMath::FloorToDouble(HoursOfDay(WorldSeconds))));
	}
	if (DayIndex(WorldSeconds) != DayIndex(LastEventSeconds))
	{
		OnDateChanged.Broadcast(ToDateTime(WorldSeconds));
	}
	if (int64(FMath::FloorToDouble(WorldSeconds / 60.0)) != int64(FMath::FloorToDouble(LastEventSeconds / 60.0)))
	{
		const double Hours = HoursOfDay(WorldSeconds);
		OnMinuteChanged.Broadcast(int32(Hours), int32((Hours - FMath::FloorToDouble(Hours)) * 60.0));
	}
	// (Each registered time the clock passed since the last frame: a day's crossing of it counted from midnight.)
	for (const TPair<FName, float>& Event : TimeEvents)
	{
		const double At = double(Event.Value) * 3600.0;
		if (FMath::FloorToDouble((WorldSeconds - At) / 86400.0) > FMath::FloorToDouble((LastEventSeconds - At) / 86400.0))
		{
			OnTimeEvent.Broadcast(Event.Key, Event.Value);
		}
	}
	if (LastSunElevation <= 0.0f && Celestial.SunElevation > 0.0f)
	{
		OnSunrise.Broadcast();
	}
	else if (LastSunElevation > 0.0f && Celestial.SunElevation <= 0.0f)
	{
		OnSunset.Broadcast();
	}
	LastEventSeconds = WorldSeconds;
	LastSunElevation = Celestial.SunElevation;
}

void UDreamWorldSubsystem::WriteClockParameters()
{
	using namespace DreamWorld::Celestial;
	const FDateTime Date = ToDateTime(WorldSeconds);
	const float DayOfYear = float(Date.GetDayOfYear() - 1) / 365.0f;
	const float SeedFraction = float(uint32(Clock.Seed) % 65536u) / 65536.0f;
	SetVectorParameter(DreamWorld::Names::MPC_Time, FLinearColor(float(HoursOfDay(WorldSeconds) / 24.0), DayOfYear, Celestial.StarRotation / 360.0f, SeedFraction));
	SetVectorParameter(DreamWorld::Names::MPC_SunDir, FLinearColor(float(Celestial.SunDirection.X), float(Celestial.SunDirection.Y), float(Celestial.SunDirection.Z), Celestial.SunElevation / 90.0f));
	SetVectorParameter(DreamWorld::Names::MPC_MoonDir, FLinearColor(float(Celestial.MoonDirection.X), float(Celestial.MoonDirection.Y), float(Celestial.MoonDirection.Z), Celestial.MoonPhase));
}

// -------------------------------------------------------------------------------------------------
// Clock
// -------------------------------------------------------------------------------------------------

void UDreamWorldSubsystem::ApplyClockConfig(const FDreamWorldClockConfig& Config, bool bResetTime)
{
	Clock = Config;
	Clock.DayLengthMinutes = FMath::Max(Clock.DayLengthMinutes, 0.1f);
	// (A picked city's place, time and daylight saving written into the settings everything reads.)
	UDreamWorldCalendar::ApplyCity(Clock.Celestial);
	if (bResetTime)
	{
		bTransitionActive = false;
		WorldSeconds = DreamWorld::Celestial::FromDateTime(Clock.StartTime);
	}
	NoteJump();
	UpdateCelestial();
}

void UDreamWorldSubsystem::RegisterTimeEvent(FName Name, float Hour)
{
	TimeEvents.Add(Name, FMath::Fmod(FMath::Max(Hour, 0.0f), 24.0f));
}

void UDreamWorldSubsystem::UnregisterTimeEvent(FName Name)
{
	TimeEvents.Remove(Name);
}

FDreamCalendarDate UDreamWorldSubsystem::GetCalendarDate() const
{
	return Clock.Calendar ? Clock.Calendar->ToDate(WorldSeconds) : UDreamWorldCalendar::GregorianDate(WorldSeconds);
}

FDateTime UDreamWorldSubsystem::GetDateTime() const
{
	return DreamWorld::Celestial::ToDateTime(WorldSeconds);
}

float UDreamWorldSubsystem::GetTimeOfDay() const
{
	return float(DreamWorld::Celestial::HoursOfDay(WorldSeconds));
}

void UDreamWorldSubsystem::SetTimeOfDay(float Hours)
{
	const double Midnight = double(DreamWorld::Celestial::DayIndex(WorldSeconds)) * 86400.0;
	SetWorldSeconds(Midnight + double(FMath::Clamp(Hours, 0.0f, 23.9999f)) * 3600.0);
}

void UDreamWorldSubsystem::SetDateTime(FDateTime DateTime)
{
	SetWorldSeconds(DreamWorld::Celestial::FromDateTime(DateTime));
}

void UDreamWorldSubsystem::SetWorldSeconds(double Seconds)
{
	bTransitionActive = false;
	WorldSeconds = FMath::Max(Seconds, 0.0);
	NoteJump();
	UpdateCelestial();
}

void UDreamWorldSubsystem::TransitionToTimeOfDay(float Hours, float Duration, bool bForward)
{
	const double Now = WorldSeconds;
	const double Midnight = double(DreamWorld::Celestial::DayIndex(Now)) * 86400.0;
	double Target = Midnight + double(FMath::Clamp(Hours, 0.0f, 23.9999f)) * 3600.0;
	if (bForward)
	{
		if (Target < Now)
		{
			Target += 86400.0;
		}
	}
	else if (FMath::Abs(Target - Now) > 43200.0)
	{
		Target += Target > Now ? -86400.0 : 86400.0;
	}
	if (Duration <= 0.0f)
	{
		SetWorldSeconds(Target);
		return;
	}
	bTransitionActive = true;
	TransitionFrom = Now;
	TransitionTo = FMath::Max(Target, 0.0);
	TransitionDuration = Duration;
	TransitionElapsed = 0.0f;
}

void UDreamWorldSubsystem::SetDayLengthMinutes(float Minutes)
{
	Clock.DayLengthMinutes = FMath::Max(Minutes, 0.1f);
}

float UDreamWorldSubsystem::GetTimeScale() const
{
	return 1440.0f / FMath::Max(Clock.DayLengthMinutes, 0.1f);
}

void UDreamWorldSubsystem::NoteJump()
{
	bJumpPending = true;
	++TimeJumpSerial;
}

void UDreamWorldSubsystem::SetCelestialSettings(const FDreamCelestialSettings& Settings)
{
	Clock.Celestial = Settings;
	NoteJump();
	UpdateCelestial();
}

FDreamDayPhase UDreamWorldSubsystem::GetDayPhase() const
{
	return DreamWorld::Celestial::DayPhase(Celestial.SunElevation);
}

// -------------------------------------------------------------------------------------------------
// Snapshots
// -------------------------------------------------------------------------------------------------

FDreamWorldSnapshot UDreamWorldSubsystem::CaptureSnapshot() const
{
	FDreamWorldSnapshot Snapshot;
	// A running transition is saved as where it is headed.
	Snapshot.WorldSeconds = bTransitionActive ? TransitionTo : WorldSeconds;
	Snapshot.TimeScale = GetTimeScale();
	Snapshot.bPaused = bPaused;
	Snapshot.Seed = Clock.Seed;
	Snapshot.Celestial = Clock.Celestial;
	OnCaptureSnapshot.Broadcast(Snapshot);
	return Snapshot;
}

void UDreamWorldSubsystem::ApplySnapshot(const FDreamWorldSnapshot& Snapshot)
{
	Clock.Seed = Snapshot.Seed;
	Clock.Celestial = Snapshot.Celestial;
	Clock.DayLengthMinutes = 1440.0f / FMath::Max(Snapshot.TimeScale, 0.001f);
	bPaused = Snapshot.bPaused;
	SetWorldSeconds(Snapshot.WorldSeconds);
	OnApplySnapshot.Broadcast(Snapshot);
}

// -------------------------------------------------------------------------------------------------
// Shared material parameters
// -------------------------------------------------------------------------------------------------

void UDreamWorldSubsystem::ResolveParameterCollection()
{
	if (ParameterCollection)
	{
		return;
	}
	// Retry now and then: the Setup step may create the collection while the editor runs.
	const double Now = FPlatformTime::Seconds();
	if (Now - LastCollectionResolve < 5.0)
	{
		return;
	}
	const bool bFirst = LastCollectionResolve < 0.0;
	LastCollectionResolve = Now;
	const FSoftObjectPath& Path = UDreamWorldSettings::Get()->ParameterCollection;
	ParameterCollection = Path.IsValid() ? Cast<UMaterialParameterCollection>(Path.TryLoad()) : nullptr;
	if (!ParameterCollection && bFirst)
	{
		UE_LOG(LogDreamWorld, Warning, TEXT("Parameter collection '%s' not found; materials get no time or sky values until the DreamDynamicWorldSetup step has run."), *Path.ToString());
	}
}

void UDreamWorldSubsystem::SetVectorParameter(FName Name, const FLinearColor& Value)
{
	if (const FLinearColor* Old = WrittenParameters.Find(Name); Old && *Old == Value && ParameterCollectionInstance)
	{
		return;
	}
	ResolveParameterCollection();
	UWorld* World = GetWorld();
	if (!ParameterCollection || !World)
	{
		return;
	}
	if (!ParameterCollectionInstance || ParameterCollectionInstance->GetCollection() != ParameterCollection)
	{
		ParameterCollectionInstance = World->GetParameterCollectionInstance(ParameterCollection);
		WrittenParameters.Reset();
	}
	if (ParameterCollectionInstance && ParameterCollectionInstance->SetVectorParameterValue(Name, Value))
	{
		WrittenParameters.Add(Name, Value);
	}
}

// -------------------------------------------------------------------------------------------------
// View
// -------------------------------------------------------------------------------------------------

void UDreamWorldSubsystem::ReportView(const FVector& Location, const FRotator& Rotation, bool bIsGameView)
{
	// A game view beats editor views of the same world.
	if (bHasGameViewLocation && !bIsGameView)
	{
		return;
	}
	ViewLocation = Location;
	ViewRotation = Rotation;
	bHasViewLocation = true;
	bHasViewRotation = true;
	bHasGameViewLocation |= bIsGameView;
}

void UDreamWorldSubsystem::ReportViewLocation(const FVector& Location, bool bIsGameView)
{
	if (bHasGameViewLocation && !bIsGameView)
	{
		return;
	}
	ViewLocation = Location;
	bHasViewLocation = true;
	bHasGameViewLocation |= bIsGameView;
}

void UDreamWorldSubsystem::RefreshViewOcclusion()
{
	UpdateViewProbe(0.0f, true);
}

void UDreamWorldSubsystem::UpdateViewProbe(float DeltaTime, bool bSnap)
{
	SCOPE_CYCLE_COUNTER(STAT_DreamWorldViewProbe);
	const UDreamWorldSettings* S = UDreamWorldSettings::Get();
	UWorld* World = GetWorld();
	if (!S->bProbeViewOcclusion || !bHasViewLocation || !World)
	{
		if (ViewOcclusion.bValid)
		{
			ViewProbe.Reset();
			ViewOcclusion = FDreamViewOcclusion();
			SetVectorParameter(DreamWorld::Names::MPC_Interior, FLinearColor(0.0f, 0.0f, 0.0f, 0.0f));
		}
		return;
	}

	FDreamViewProbe::FParams Params;
	Params.Channel = S->ProbeChannel;
	Params.Distance = S->ProbeDistance;
	Params.RoundSeconds = S->ProbeRoundSeconds;
	Params.ResponseSeconds = S->ProbeResponseSeconds;
	Params.InteriorRange = S->ProbeInteriorRange;
	Params.TeleportDistance = S->ProbeTeleportDistance;
	// The camera usually sits in or near the player's own pawn; it is not a wall.
	if (const APlayerController* Player = World->GetFirstPlayerController())
	{
		Params.IgnoredActor = Player->GetPawn();
	}
	ViewProbe.Update(World, ViewLocation, DeltaTime, Params, bSnap);
	ViewOcclusion = ViewProbe.Get();
	SetVectorParameter(DreamWorld::Names::MPC_Interior,
		FLinearColor(ViewOcclusion.Interior, ViewOcclusion.Overhead, 0.0f, ViewOcclusion.bValid ? 1.0f : 0.0f));
}

// -------------------------------------------------------------------------------------------------

void UDreamWorldSubsystem::BeginExternalDrive(const UObject* Driver)
{
	ExternalDrivers.RemoveAll([](const TWeakObjectPtr<const UObject>& D) { return !D.IsValid(); });
	ExternalDrivers.AddUnique(Driver);
}

void UDreamWorldSubsystem::EndExternalDrive(const UObject* Driver)
{
	ExternalDrivers.RemoveAll([Driver](const TWeakObjectPtr<const UObject>& D) { return !D.IsValid() || D.Get() == Driver; });
}

void UDreamWorldSubsystem::DriveWorldSeconds(double Seconds, double MaxRunSeconds)
{
	Seconds = FMath::Max(Seconds, 0.0);
	if (Seconds >= WorldSeconds && Seconds - WorldSeconds <= MaxRunSeconds && bEventsPrimed)
	{
		// As if it ran: the next tick fires the events of the hours passed.
		bTransitionActive = false;
		WorldSeconds = Seconds;
		UpdateCelestial();
		return;
	}
	if (Seconds != WorldSeconds)
	{
		SetWorldSeconds(Seconds);
	}
}

bool UDreamWorldSubsystem::IsCinematicMode() const
{
	if (bCinematicOverride)
	{
		return true;
	}
	const int32 Mode = CVarDreamWorldCinematicMode.GetValueOnGameThread();
	if (Mode >= 0)
	{
		return Mode > 0;
	}
	// A Movie Render Queue render steps the engine with its own custom time step.
	static const FName MoviePipelineTimeStep(TEXT("MoviePipelineCustomTimeStep"));
	const UEngineCustomTimeStep* TimeStep = GEngine ? GEngine->GetCustomTimeStep() : nullptr;
	for (const UClass* Class = TimeStep ? TimeStep->GetClass() : nullptr; Class; Class = Class->GetSuperClass())
	{
		if (Class->GetFName() == MoviePipelineTimeStep)
		{
			return true;
		}
	}
	return false;
}
