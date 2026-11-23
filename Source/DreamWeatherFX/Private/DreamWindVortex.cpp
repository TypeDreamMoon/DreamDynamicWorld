// Copyright DreamMoon. All Rights Reserved.

#include "DreamWindVortex.h"
#include "DreamWeatherSelector.h"
#include "DreamWindSourceComponent.h"
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
		DustRate = 180.0f;
		PuffSize = 900.0f * K;
		DustColor = FLinearColor(0.30f, 0.28f, 0.25f, 0.7f);
		DebrisRate = 120.0f;
		DebrisSize = 30.0f;
		DebrisColor = FLinearColor(0.25f, 0.20f, 0.15f, 1.0f);
	}
	else
	{
		Radius = 800.0f * K;
		CoreRadius = 250.0f * K;
		Height = 4000.0f * K;
		TangentialSpeed = 1200.0f;
		Updraft = 600.0f;
		Inflow = 300.0f;
		DustRate = 80.0f;
		PuffSize = 120.0f * K;
		DustColor = FLinearColor(0.62f, 0.52f, 0.40f, 0.55f);
		DebrisRate = 10.0f;
		DebrisSize = 8.0f;
		DebrisColor = FLinearColor(0.45f, 0.33f, 0.20f, 1.0f);
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
		SetActorLocation(FVector(Foot, GroundAt(Foot)));
	}
	Refresh();
}

void ADreamWindVortex::Refresh()
{
	const float S = FMath::Clamp(Intensity, 0.0f, 1.0f);
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

	if (Effect)
	{
		const bool bTornado = Kind == EDreamVortexKind::Tornado;
		Effect->SetVariableFloat(TEXT("Intensity"), S);
		Effect->SetVariableFloat(TEXT("DustRate"), DustRate);
		Effect->SetVariableFloat(TEXT("Radius"), CoreRadius * 1.3f);
		Effect->SetVariableFloat(TEXT("Height"), bTornado ? 1500.0f : 150.0f);
		Effect->SetVariableFloat(TEXT("Lifetime"), bTornado ? 10.0f : 7.0f);
		Effect->SetVariableFloat(TEXT("PuffSize"), PuffSize);
		Effect->SetVariableLinearColor(TEXT("Tint"), DustColor);
		Effect->SetVariableFloat(TEXT("DebrisRate"), DebrisRate);
		Effect->SetVariableFloat(TEXT("DebrisSize"), DebrisSize);
		Effect->SetVariableLinearColor(TEXT("DebrisColor"), DebrisColor);
	}
}

void ADreamWindVortex::Unregister()
{
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
	TEXT("DreamWeather.Vortex <DustDevil|Tornado> <x> <y> [intensity 0..1] [scale]: stand a whirlwind there (on the ground under it); DreamWeather.Vortex off: take the ones placed so away."),
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
			Vortex->Intensity = Args.Num() > 3 ? FMath::Clamp(FCString::Atof(*Args[3]), 0.0f, 1.0f) : 1.0f;
			Vortex->Tags.Add(Tag);
			Vortex->Refresh();
		}
	}));
