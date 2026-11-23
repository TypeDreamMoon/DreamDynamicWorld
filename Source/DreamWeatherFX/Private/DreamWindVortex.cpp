// Copyright DreamMoon. All Rights Reserved.

#include "DreamWindVortex.h"
#include "DreamWeatherSelector.h"
#include "DreamWeatherSubsystem.h"
#include "DreamWindSourceComponent.h"
#include "DreamWindSubsystem.h"
#include "DreamWorldCelestial.h"
#include "DreamWorldSubsystem.h"

#include "Engine/World.h"
#include "EngineUtils.h"
#include "HAL/IConsoleManager.h"
#include "NiagaraComponent.h"
#include "NiagaraSystem.h"
#include "UObject/ConstructorHelpers.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(DreamWindVortex)

float FDreamVortexPlan::StrengthAt(double WorldSeconds) const
{
	if (LifetimeSeconds <= 0.0f)
	{
		return 0.0f;
	}
	const float U = float((WorldSeconds - StartSeconds) / double(LifetimeSeconds));
	if (U < 0.0f || U >= 1.0f)
	{
		return 0.0f;
	}
	return FMath::SmoothStep(0.0f, 1.0f / 6.0f, U) * (1.0f - FMath::SmoothStep(0.75f, 1.0f, U));
}

float FDreamVortexPlan::DecayAt(double WorldSeconds) const
{
	if (LifetimeSeconds <= 0.0f)
	{
		return 0.0f;
	}
	const float U = float((WorldSeconds - StartSeconds) / double(LifetimeSeconds));
	return U < 0.0f ? 0.0f : FMath::SmoothStep(0.75f, 1.0f, U);
}

// ---- The whirlwind -------------------------------------------------------------------------------------------------

ADreamWindVortex::ADreamWindVortex()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = true;
	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Foot"));
	Wind = CreateDefaultSubobject<UDreamWindSourceComponent>(TEXT("Wind"));
	Wind->SetupAttachment(RootComponent);
	Wind->Type = EDreamWindSourceType::Twister;
	Effect = CreateDefaultSubobject<UNiagaraComponent>(TEXT("Effect"));
	Effect->SetupAttachment(RootComponent);
	Effect->bAutoActivate = true;
	static ConstructorHelpers::FObjectFinderOptional<UNiagaraSystem> System(TEXT("/DreamDynamicWorld/Wind/Effects/NS_DreamWindVortex.NS_DreamWindVortex"));
	if (System.Succeeded())
	{
		Effect->SetAsset(System.Get());
	}
	SetKind(EDreamVortexKind::DustDevil);
}

void ADreamWindVortex::SetKind(EDreamVortexKind NewKind, float Scale)
{
	Kind = NewKind;
	const float K = FMath::Clamp(Scale, 0.2f, 5.0f);
	if (Kind == EDreamVortexKind::Tornado)
	{
		Radius = 12000.0f * K;
		CoreRadius = 3500.0f * K;
		Height = 60000.0f * K;
		TangentialSpeed = 5000.0f;
		Updraft = 2500.0f;
		Inflow = 1200.0f;
		DustRate = 160.0f;
		PuffSize = 700.0f * K;
		DustColor = FLinearColor(0.22f, 0.21f, 0.19f, 0.45f);
		DebrisRate = 120.0f;
		DebrisSize = 30.0f;
		DebrisColor = FLinearColor(0.06f, 0.05f, 0.04f, 0.95f);
	}
	else
	{
		Radius = 800.0f * K;
		CoreRadius = 250.0f * K;
		Height = 4000.0f * K;
		TangentialSpeed = 1200.0f;
		Updraft = 900.0f;
		Inflow = 300.0f;
		DustRate = 220.0f;
		PuffSize = 190.0f * K;
		DustColor = FLinearColor(0.50f, 0.42f, 0.32f, 0.10f);
		DebrisRate = 10.0f;
		DebrisSize = 8.0f;
		DebrisColor = FLinearColor(0.10f, 0.08f, 0.06f, 0.9f);
	}
}

void ADreamWindVortex::SetPlan(const FDreamVortexPlan& NewPlan)
{
	Plan = NewPlan;
	bHasPlan = true;
	SetKind(Plan.Kind, Plan.Scale);
	bClockwise = Plan.Spin < 0;
}

double ADreamWindVortex::GroundAt(const FVector2D& Foot) const
{
	const UWorld* World = GetWorld();
	const double Here = GetActorLocation().Z;
	if (!World)
	{
		return Here;
	}
	FHitResult Hit;
	FCollisionQueryParams Params(SCENE_QUERY_STAT(DreamWindVortexGround), false, this);
	if (World->LineTraceSingleByChannel(Hit, FVector(Foot, Here + 50000.0), FVector(Foot, Here - 50000.0), ECC_Visibility, Params))
	{
		return Hit.ImpactPoint.Z;
	}
	return Here;
}

void ADreamWindVortex::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	const UDreamWorldSubsystem* Clock = UDreamWorldSubsystem::Get(this);
	if (bHasPlan && Clock)
	{
		// (on the plan, from the clock: the same place and strength for any moment, a jump of the clock or a replay)
		const double Now = Clock->GetWorldSeconds();
		const FVector2D Foot = Plan.FootAt(Now);
		Intensity = Plan.StrengthAt(Now);
		Decay = Plan.DecayAt(Now);
		SetActorLocation(FVector(Foot, GroundAt(Foot)));
	}
	// The weather's say: a dust devil needs dry, sunlit ground (eased over some seconds; at once the first time and when
	// the clock jumps, as everything else that eases snaps then).
	float Allowed = 1.0f;
	if (bFollowWeather && Kind == EDreamVortexKind::DustDevil)
	{
		const UDreamWeatherSubsystem* Weather = UDreamWeatherSubsystem::Get(this);
		if (Weather && Weather->IsActive() && Clock)
		{
			const FDreamWeatherSample W = Weather->SampleWeather(GetActorLocation());
			Allowed = DustDevilWeather(W.Precipitation, W.Wetness, W.SnowCover, W.CloudCoverage, Clock->GetCelestial().SunElevation);
		}
	}
	const uint32 Jumps = Clock ? Clock->GetTimeJumpSerial() : 0u;
	const bool bSnap = !bActivityPrimed || Jumps != LastJumpSerial;
	Activity = bSnap ? Allowed : FMath::FInterpConstantTo(Activity, Allowed, FMath::Max(DeltaSeconds, 0.0f), 0.15f);
	bActivityPrimed = true;
	LastJumpSerial = Jumps;
	Refresh();
}

float ADreamWindVortex::DustDevilWeather(float Precipitation, float Wetness, float SnowCover, float CloudCoverage, float SunElevationDegrees)
{
	const float Sun = FMath::SmoothStep(5.0f, 20.0f, SunElevationDegrees);
	const float Dry = (1.0f - FMath::SmoothStep(0.02f, 0.2f, Precipitation)) * (1.0f - FMath::SmoothStep(0.15f, 0.5f, Wetness))
		* (1.0f - FMath::SmoothStep(0.05f, 0.3f, SnowCover));
	const float Clear = 1.0f - FMath::SmoothStep(0.6f, 0.95f, CloudCoverage);
	return FMath::Clamp(Sun * Dry * Clear, 0.0f, 1.0f);
}

void ADreamWindVortex::Refresh()
{
	const float S = FMath::Clamp(Intensity, 0.0f, 1.0f) * FMath::Clamp(Activity, 0.0f, 1.0f);
	const float R = FMath::Max(Radius, 50.0f);
	Wind->Type = EDreamWindSourceType::Twister;
	Wind->Radius = R;
	Wind->Stretch = FMath::Max(Height / R, 1.0f);
	Wind->InnerRadiusFraction = FMath::Clamp(CoreRadius / R, 0.05f, 0.9f);
	// (positive: counter-clockwise seen from above)
	Wind->Strength = TangentialSpeed * S * (bClockwise ? -1.0f : 1.0f);
	Wind->Updraft = Updraft * S;
	Wind->Inflow = Inflow * S;
	Wind->InjectRate = 4.0f;
	// (on while it blows: a world that has not begun play does not switch components on by itself)
	Wind->SetActive(S > 0.001f);

	// The clock's register: the sky draws a tornado's funnel (and the dust it raises) from it. A dust devil is not in it:
	// its look is its own dust.
	if (UDreamWorldSubsystem* Clock = UDreamWorldSubsystem::Get(this))
	{
		if (S <= 0.001f || Kind != EDreamVortexKind::Tornado)
		{
			Unregister();
		}
		else
		{
			if (!bBorn)
			{
				BornSeconds = bHasPlan ? Plan.StartSeconds : Clock->GetWorldSeconds();
				bBorn = true;
			}
			FDreamWorldVortex V;
			V.Location = GetActorLocation();
			V.Radius = R;
			V.Strength = S;
			V.Spin = bClockwise ? -1.0f : 1.0f;
			// (Its wall's speed as it looks, not as it blows: the sky reckons how far it has turned from it times the time
			// since it formed, and the speed falling as it died turned the funnel backwards.)
			V.TangentialSpeed = TangentialSpeed;
			// It leans with the wind aloft: its top drifts downwind for about the time the air takes to rise up it.
			if (const UDreamWindSubsystem* Air = UDreamWindSubsystem::Get(this))
			{
				const FDreamWindGlobalWind G = Air->GetGlobalWind();
				const FVector Drift = G.Direction.GetSafeNormal2D() * double(G.Speed) * double(Height / FMath::Max(Updraft, 100.0f)) * 0.3;
				V.Lean = FVector2D(Drift).ClampAxes(-Height * 0.5, Height * 0.5);
			}
			V.BornSeconds = BornSeconds;
			V.bFunnel = true;
			V.Decay = FMath::Clamp(Decay, 0.0f, 1.0f);
			VortexId = Clock->SetVortex(VortexId, V);
		}
	}

	if (Effect)
	{
		const bool bTornado = Kind == EDreamVortexKind::Tornado;
		// The dust's column: a dust devil's the whole of it, a tornado's the cloud of dirt round its foot (the sky draws
		// its funnel); the puffs climb it in a few seconds and turn round it at a share of the core's own turn.
		const float Column = bTornado ? Height * 0.12f : Height;
		const float Rise = bTornado ? 1200.0f : 650.0f;
		const float Spin = 0.6f * TangentialSpeed / FMath::Max(CoreRadius, 10.0f) * (bClockwise ? -1.0f : 1.0f);
		// Its top stands off downwind, about as far as the wind aloft carries the air while it rises up it.
		FVector Lean = FVector::ZeroVector;
		if (const UDreamWindSubsystem* Air = UDreamWindSubsystem::Get(this))
		{
			const FDreamWindGlobalWind G = Air->GetGlobalWind();
			Lean = G.Direction.GetSafeNormal2D() * double(G.Speed) * double(Column / FMath::Max(Updraft, 100.0f)) * 0.3;
			Lean = Lean.GetClampedToMaxSize(Column * 0.5);
		}
		Effect->SetVariableFloat(TEXT("User.Intensity"), S);
		Effect->SetVariableFloat(TEXT("User.DustRate"), DustRate);
		Effect->SetVariableFloat(TEXT("User.Radius"), CoreRadius);
		Effect->SetVariableFloat(TEXT("User.Height"), bTornado ? 1500.0f : 150.0f);
		Effect->SetVariableFloat(TEXT("User.ColumnHeight"), Column);
		Effect->SetVariableFloat(TEXT("User.Rise"), Rise);
		Effect->SetVariableFloat(TEXT("User.Spin"), Spin);
		Effect->SetVariableFloat(TEXT("User.Flare"), bTornado ? 0.6f : 1.2f);
		Effect->SetVariableVec3(TEXT("User.Lean"), Lean);
		Effect->SetVariableFloat(TEXT("User.Lifetime"), Column / Rise * 1.1f);
		Effect->SetVariableFloat(TEXT("User.Drag"), 6.0f);
		Effect->SetVariableFloat(TEXT("User.PuffSize"), PuffSize);
		Effect->SetVariableLinearColor(TEXT("User.Tint"), DustColor);
		Effect->SetVariableFloat(TEXT("User.DebrisRate"), DebrisRate);
		Effect->SetVariableFloat(TEXT("User.DebrisSize"), DebrisSize);
		Effect->SetVariableLinearColor(TEXT("User.DebrisColor"), DebrisColor);
	}
}

void ADreamWindVortex::Unregister()
{
	if (VortexId != 0)
	{
		if (UDreamWorldSubsystem* Clock = UDreamWorldSubsystem::Get(this))
		{
			Clock->RemoveVortex(VortexId);
		}
	}
	VortexId = 0;
}

void ADreamWindVortex::EndPlay(const EEndPlayReason::Type Reason)
{
	Unregister();
	Super::EndPlay(Reason);
}

void ADreamWindVortex::Destroyed()
{
	Unregister();
	Super::Destroyed();
}

#if WITH_EDITOR
void ADreamWindVortex::PostEditChangeProperty(FPropertyChangedEvent& Event)
{
	Super::PostEditChangeProperty(Event);
	if (Event.GetPropertyName() == GET_MEMBER_NAME_CHECKED(ADreamWindVortex, Kind))
	{
		SetKind(Kind);
	}
	Refresh();
}
#endif

// ---- The spawner ---------------------------------------------------------------------------------------------------

ADreamWindVortexSpawner::ADreamWindVortexSpawner()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = true;
	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
}

bool ADreamWindVortexSpawner::PlanSlot(int32 WorldSeed, int64 Slot, FDreamVortexPlan& OutPlan) const
{
	const int32 Seed = WorldSeed ^ (SeedOffset * 0x2c1b3c6d) ^ 0x3c6ef372;
	auto Rand = [Seed, Slot](uint32 Salt) { return UDreamWeatherSelector::Hash01(Seed, Slot, Salt); };
	const double SlotSeconds = double(FMath::Max(SlotHours, 0.25f)) * 3600.0;
	const double SlotStart = double(Slot) * SlotSeconds;
	const int32 Season = UDreamWeatherSelector::SeasonAt(SlotStart, bSouthernHemisphere);
	auto In = [Season](const FVector4f& V) { return FMath::Clamp(Season == 0 ? V.X : Season == 1 ? V.Y : Season == 2 ? V.Z : V.W, 0.0f, 1.0f); };
	const float Tornado = In(TornadoChance);
	const float Dust = In(DustDevilChance);
	const float Roll = Rand(1u);
	if (Roll >= Tornado + Dust)
	{
		return false;
	}
	OutPlan = FDreamVortexPlan();
	OutPlan.Slot = Slot;
	OutPlan.Kind = Roll < Tornado ? EDreamVortexKind::Tornado : EDreamVortexKind::DustDevil;
	OutPlan.StartSeconds = SlotStart + double(Rand(2u)) * SlotSeconds;
	const bool bTornado = OutPlan.Kind == EDreamVortexKind::Tornado;
	if (!bTornado)
	{
		// (a dust devil in the afternoon's heat only)
		const double Hour = DreamWorld::Celestial::HoursOfDay(OutPlan.StartSeconds);
		if (Hour < DustDevilHours.X || Hour >= DustDevilHours.Y)
		{
			return false;
		}
	}
	const FVector2D Minutes = bTornado ? TornadoMinutes : DustDevilMinutes;
	const FVector2D Speeds = bTornado ? TornadoSpeed : DustDevilSpeed;
	OutPlan.LifetimeSeconds = float(FMath::Lerp(Minutes.X, Minutes.Y, double(Rand(3u))) * 60.0);
	const double Speed = FMath::Lerp(Speeds.X, Speeds.Y, double(Rand(4u)));
	const double Yaw = FMath::DegreesToRadians(PrevailingYaw + (Rand(5u) * 2.0f - 1.0f) * DirectionVariation);
	const FVector2D Dir(FMath::Cos(Yaw), FMath::Sin(Yaw));
	const FVector2D Across(-Dir.Y, Dir.X);
	const FVector2D Pass = FVector2D(GetActorLocation()) + Across * (double(Rand(6u)) * 2.0 - 1.0) * PassRadius + Dir * (double(Rand(7u)) * 2.0 - 1.0) * PassRadius * 0.5;
	OutPlan.Velocity = Dir * Speed;
	OutPlan.Origin = Pass - OutPlan.Velocity * double(OutPlan.LifetimeSeconds) * 0.5;
	OutPlan.Scale = FMath::Lerp(0.7f, 1.3f, Rand(8u));
	// (most tornadoes turn as the storms do: counter-clockwise in the north; dust devils either way)
	const float Usual = bTornado ? 0.85f : 0.5f;
	OutPlan.Spin = (Rand(9u) < Usual) != bSouthernHemisphere ? 1 : -1;
	return true;
}

TArray<FDreamVortexPlan> ADreamWindVortexSpawner::GetVorticesAt(double WorldSeconds) const
{
	TArray<FDreamVortexPlan> Out;
	const UDreamWorldSubsystem* Clock = UDreamWorldSubsystem::Get(this);
	const int32 Seed = Clock ? Clock->GetSeed() : 0;
	const double SlotSeconds = double(FMath::Max(SlotHours, 0.25f)) * 3600.0;
	const int64 Slot = FMath::FloorToInt64(WorldSeconds / SlotSeconds);
	// (a slot's whirlwind may live on into the slots after it)
	const double Longest = FMath::Max(FMath::Max(DustDevilMinutes.X, DustDevilMinutes.Y), FMath::Max(TornadoMinutes.X, TornadoMinutes.Y)) * 60.0;
	const int64 Back = int64(FMath::CeilToDouble(Longest / SlotSeconds)) + 1;
	for (int64 S = Slot - Back; S <= Slot; ++S)
	{
		FDreamVortexPlan Plan;
		if (PlanSlot(Seed, S, Plan) && Plan.IsAlive(WorldSeconds))
		{
			Out.Add(Plan);
		}
	}
	return Out;
}

void ADreamWindVortexSpawner::Refresh()
{
	UWorld* World = GetWorld();
	const UDreamWorldSubsystem* Clock = UDreamWorldSubsystem::Get(this);
	if (!World || !Clock)
	{
		return;
	}
	const TArray<FDreamVortexPlan> Plans = GetVorticesAt(Clock->GetWorldSeconds());
	for (auto It = Live.CreateIterator(); It; ++It)
	{
		if (!Plans.ContainsByPredicate([&It](const FDreamVortexPlan& P) { return P.Slot == It.Key(); }))
		{
			if (ADreamWindVortex* Vortex = It.Value().Get())
			{
				Vortex->Destroy();
			}
			It.RemoveCurrent();
		}
	}
	for (const FDreamVortexPlan& Plan : Plans)
	{
		if (Live.Contains(Plan.Slot) && Live[Plan.Slot].IsValid())
		{
			continue;
		}
		FActorSpawnParameters Spawn;
		Spawn.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		Spawn.ObjectFlags |= RF_Transient;
		const FVector Foot(Plan.FootAt(Clock->GetWorldSeconds()), GetActorLocation().Z);
		if (ADreamWindVortex* Vortex = World->SpawnActor<ADreamWindVortex>(Foot, FRotator::ZeroRotator, Spawn))
		{
			Vortex->SetPlan(Plan);
			Live.Add(Plan.Slot, Vortex);
		}
	}
}

void ADreamWindVortexSpawner::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	SinceRefresh += DeltaSeconds;
	if (SinceRefresh >= 0.5f)
	{
		SinceRefresh = 0.0f;
		Refresh();
	}
}

void ADreamWindVortexSpawner::EndPlay(const EEndPlayReason::Type Reason)
{
	for (const TPair<int64, TWeakObjectPtr<ADreamWindVortex>>& Pair : Live)
	{
		if (ADreamWindVortex* Vortex = Pair.Value.Get())
		{
			Vortex->Destroy();
		}
	}
	Live.Reset();
	Super::EndPlay(Reason);
}

// DreamWeather.Vortex <DustDevil|Tornado|off> [x y] [intensity] [scale]: a whirlwind placed by hand (shots, tests).
static FAutoConsoleCommandWithWorldAndArgs GDreamWeatherVortexCommand(
	TEXT("DreamWeather.Vortex"),
	TEXT("DreamWeather.Vortex <DustDevil|Tornado> <x> <y> [intensity 0..1] [scale] [dust shade]: stand a whirlwind there (on the ground under it); DreamWeather.Vortex off: take the ones placed so away."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
	{
		if (!World || Args.Num() < 1)
		{
			return;
		}
		static const FName Tag(TEXT("DreamWeather.Vortex.Console"));
		if (Args[0].Equals(TEXT("off"), ESearchCase::IgnoreCase))
		{
			for (TActorIterator<ADreamWindVortex> It(World); It; ++It)
			{
				if (It->Tags.Contains(Tag))
				{
					It->Destroy();
				}
			}
			return;
		}
		if (Args.Num() < 3)
		{
			return;
		}
		const EDreamVortexKind Kind = Args[0].Equals(TEXT("Tornado"), ESearchCase::IgnoreCase) ? EDreamVortexKind::Tornado : EDreamVortexKind::DustDevil;
		const FVector2D Foot(FCString::Atod(*Args[1]), FCString::Atod(*Args[2]));
		FActorSpawnParameters Spawn;
		Spawn.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		Spawn.ObjectFlags |= RF_Transient;
		FHitResult Hit;
		const double Ground = World->LineTraceSingleByChannel(Hit, FVector(Foot, 50000.0), FVector(Foot, -50000.0), ECC_Visibility) ? Hit.ImpactPoint.Z : 0.0;
		if (ADreamWindVortex* Vortex = World->SpawnActor<ADreamWindVortex>(FVector(Foot, Ground), FRotator::ZeroRotator, Spawn))
		{
			Vortex->SetKind(Kind, Args.Num() > 4 ? FCString::Atof(*Args[4]) : 1.0f);
			if (Args.Num() > 5)
			{
				// (how light its dust is against its kind's: for tuning by eye)
				const float Shade = FMath::Max(FCString::Atof(*Args[5]), 0.0f);
				Vortex->DustColor = FLinearColor(Vortex->DustColor.R * Shade, Vortex->DustColor.G * Shade, Vortex->DustColor.B * Shade, Vortex->DustColor.A);
			}
			Vortex->Intensity = Args.Num() > 3 ? FMath::Clamp(FCString::Atof(*Args[3]), 0.0f, 1.0f) : 1.0f;
			Vortex->Tags.Add(Tag);
			Vortex->Refresh();
		}
	}));
