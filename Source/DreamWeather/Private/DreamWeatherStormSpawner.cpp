// Copyright DreamMoon. All Rights Reserved.

#include "DreamWeatherStormSpawner.h"
#include "DreamWeatherChannels.h"
#include "DreamWeatherHlslShim.h"
#include "DreamWeatherPreset.h"
#include "DreamWeatherSubsystem.h"
#include "DreamWeatherSelector.h"
#include "DreamWeatherZone.h"
#include "DreamWorldSubsystem.h"

#include "Engine/World.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(DreamWeatherStormSpawner)

float FDreamStormKind::WeightIn(int32 Season) const
{
	switch (Season)
	{
	case 0: return FMath::Max(0.0f, Spring);
	case 1: return FMath::Max(0.0f, Summer);
	case 2: return FMath::Max(0.0f, Autumn);
	default: return FMath::Max(0.0f, Winter);
	}
}

ADreamWeatherStormSpawner::ADreamWeatherStormSpawner()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.TickInterval = 1.0f;
	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
}

bool ADreamWeatherStormSpawner::PlanSlot(int32 WorldSeed, int64 Slot, FDreamStormPlan& OutPlan) const
{
	const int32 Seed = WorldSeed ^ (SeedOffset * 0x2c1b3c6d) ^ 0x6a09e667;
	auto Rand = [Seed, Slot](uint32 Salt) { return UDreamWeatherSelector::Hash01(Seed, Slot, Salt); };
	const double SlotSeconds = double(FMath::Max(SlotHours, 1.0f)) * 3600.0;
	const double SlotStart = double(Slot) * SlotSeconds;
	const int32 Season = UDreamWeatherSelector::SeasonAt(SlotStart, bSouthernHemisphere);
	const float Chance = FMath::Clamp(Season == 0 ? SeasonChance.X : Season == 1 ? SeasonChance.Y : Season == 2 ? SeasonChance.Z : SeasonChance.W, 0.0f, 1.0f);
	if (Rand(1u) >= Chance)
	{
		return false;
	}
	float Total = 0.0f;
	for (const FDreamStormKind& Kind : Kinds)
	{
		Total += Kind.Preset ? Kind.WeightIn(Season) : 0.0f;
	}
	if (Total <= 0.0f)
	{
		return false;
	}
	float Pick = Rand(2u) * Total;
	const FDreamStormKind* Chosen = nullptr;
	for (const FDreamStormKind& Kind : Kinds)
	{
		const float W = Kind.Preset ? Kind.WeightIn(Season) : 0.0f;
		if (W > 0.0f)
		{
			Chosen = &Kind;
			if (Pick < W)
			{
				break;
			}
			Pick -= W;
		}
	}

	OutPlan = FDreamStormPlan();
	OutPlan.Slot = Slot;
	OutPlan.Preset = Chosen->Preset;
	OutPlan.StartSeconds = SlotStart + double(Rand(3u)) * SlotSeconds;
	OutPlan.LifetimeHours = float(FMath::Lerp(LifetimeHours.X, LifetimeHours.Y, double(Rand(4u))));
	OutPlan.Radius = float(FMath::Lerp(RadiusRange.X, RadiusRange.Y, double(Rand(5u))));
	const double Speed = FMath::Lerp(SpeedRange.X, SpeedRange.Y, double(Rand(6u)));
	const double Yaw = FMath::DegreesToRadians(double(PrevailingYaw) + (double(Rand(7u)) * 2.0 - 1.0) * double(DirectionVariation));
	const FVector2D Direction(FMath::Cos(Yaw), FMath::Sin(Yaw));
	OutPlan.Velocity = Direction * Speed;

	// Over a point near the spawner halfway through its life.
	const double PassAngle = double(Rand(8u)) * UE_DOUBLE_TWO_PI;
	const double PassDistance = FMath::Sqrt(double(Rand(9u))) * double(PassRadius);
	const FVector Here = GetActorLocation();
	const FVector2D Pass = FVector2D(Here.X, Here.Y) + FVector2D(FMath::Cos(PassAngle), FMath::Sin(PassAngle)) * PassDistance;
	const double HalfTravelCm = Speed * 100000.0 * double(OutPlan.LifetimeHours) * 0.5;
	OutPlan.Origin = Pass - Direction * HalfTravelCm;
	return true;
}

FDreamWeatherZoneMotion ADreamWeatherStormSpawner::MakeMotion(const FDreamStormPlan& Plan) const
{
	FDreamWeatherZoneMotion Motion;
	Motion.bEnabled = true;
	Motion.StartSeconds = Plan.StartSeconds;
	Motion.Velocity = Plan.Velocity;
	Motion.LifetimeHours = Plan.LifetimeHours;
	Motion.FadeInHours = Plan.LifetimeHours * 0.2f;
	Motion.FadeOutHours = Plan.LifetimeHours * 0.25f;
	// The spawner decides when its actor goes (a jump back in time brings it back).
	Motion.bDestroyWhenDone = false;
	return Motion;
}

FVector2D ADreamWeatherStormSpawner::CenterAt(const FDreamStormPlan& Plan, double WorldSeconds)
{
	return Plan.Origin + Plan.Velocity * (FMath::Max(WorldSeconds - Plan.StartSeconds, 0.0) / 3600.0 * 100000.0);
}

TArray<TPair<FDreamStormPlan, float>> ADreamWeatherStormSpawner::WeightsAt(const FVector& Position, double WorldSeconds) const
{
	TArray<TPair<FDreamStormPlan, float>> Out;
	using namespace DreamWind::Hlsl;
	for (const FDreamStormPlan& Plan : GetStormsAt(WorldSeconds))
	{
		const float Fade = MakeMotion(Plan).FadeAt(WorldSeconds - Plan.StartSeconds);
		if (Fade <= 0.0f)
		{
			continue;
		}
		// The same footprint and falloff as a round zone (DreamWeatherShared.h), so the map and the queries agree.
		const FVector2D Center = CenterAt(Plan, WorldSeconds);
		const FVector4f Z0(float(Center.X), float(Center.Y), 1.0f, 0.0f);
		const FVector4f Z1(Plan.Radius, Plan.Radius, BlendDistance, 1.0f);
		const float W = DWX_ZoneWeight(float2(float(Position.X), float(Position.Y)), ToHlsl(Z0), ToHlsl(Z1), Fade, FDreamZonePoints());
		if (W > 0.0f)
		{
			Out.Emplace(Plan, W);
		}
	}
	return Out;
}

void ADreamWeatherStormSpawner::ApplyWeatherInfluence(const FVector& Position, FDreamWeatherValues& InOut) const
{
	const UDreamWorldSubsystem* Clock = UDreamWorldSubsystem::Get(this);
	ApplyWeatherInfluenceAt(Position, Clock ? Clock->GetWorldSeconds() : 0.0, InOut);
}

void ADreamWeatherStormSpawner::ApplyWeatherInfluenceAt(const FVector& Position, double WorldSeconds, FDreamWeatherValues& InOut) const
{
	const TArray<FDreamWeatherChannelDef>& Defs = FDreamWeatherChannels::Get();
	// Storms in slot order: the same order on every run.
	for (const TPair<FDreamStormPlan, float>& Storm : WeightsAt(Position, WorldSeconds))
	{
		const UDreamWeatherPreset* Preset = Storm.Key.Preset;
		if (!Preset)
		{
			continue;
		}
		for (int32 i = 0; i < InOut.Values.Num() && i < Defs.Num(); ++i)
		{
			// A preset gives every channel (its own values, the defaults for the rest), as a zone with a preset does.
			InOut.Values[i] = FMath::Clamp(FDreamWeatherChannels::Combine(EDreamWeatherBlend::Override, InOut.Values[i], Preset->GetValue(i), Storm.Value),
				Defs[i].Min, Defs[i].Max);
		}
	}
}

const UDreamWeatherPreset* ADreamWeatherStormSpawner::GetWeatherInfluencePreset(const FVector& Position, float& OutWeight, EDreamWeatherBlend& OutBlend) const
{
	const UDreamWorldSubsystem* Clock = UDreamWorldSubsystem::Get(this);
	OutWeight = 0.0f;
	OutBlend = EDreamWeatherBlend::Override;
	const UDreamWeatherPreset* Strongest = nullptr;
	for (const TPair<FDreamStormPlan, float>& Storm : WeightsAt(Position, Clock ? Clock->GetWorldSeconds() : 0.0))
	{
		if (Storm.Key.Preset && Storm.Value > OutWeight)
		{
			OutWeight = Storm.Value;
			Strongest = Storm.Key.Preset;
		}
	}
	return Strongest;
}

void ADreamWeatherStormSpawner::PostRegisterAllComponents()
{
	Super::PostRegisterAllComponents();
	if (UDreamWeatherSubsystem* Weather = GetWorld() && !IsTemplate() ? GetWorld()->GetSubsystem<UDreamWeatherSubsystem>() : nullptr)
	{
		Weather->RegisterInfluence(this);
	}
}

void ADreamWeatherStormSpawner::PostUnregisterAllComponents()
{
	if (UDreamWeatherSubsystem* Weather = GetWorld() ? GetWorld()->GetSubsystem<UDreamWeatherSubsystem>() : nullptr)
	{
		Weather->UnregisterInfluence(this);
	}
	Super::PostUnregisterAllComponents();
}

TArray<FDreamStormPlan> ADreamWeatherStormSpawner::GetStormsAt(double WorldSeconds) const
{
	TArray<FDreamStormPlan> Alive;
	const UDreamWorldSubsystem* Clock = UDreamWorldSubsystem::Get(this);
	const int32 Seed = Clock ? Clock->GetSeed() : 0;
	const double SlotSeconds = double(FMath::Max(SlotHours, 1.0f)) * 3600.0;
	const int64 Now = int64(FMath::FloorToDouble(WorldSeconds / SlotSeconds));
	// A storm can outlive its slot: look back as far as the longest life reaches.
	const int64 Back = int64(FMath::CeilToDouble(FMath::Max(LifetimeHours.X, LifetimeHours.Y) * 3600.0 / SlotSeconds)) + 1;
	for (int64 Slot = Now - Back; Slot <= Now; ++Slot)
	{
		FDreamStormPlan Plan;
		if (PlanSlot(Seed, Slot, Plan) && WorldSeconds >= Plan.StartSeconds && WorldSeconds < Plan.EndSeconds())
		{
			Alive.Add(Plan);
		}
	}
	return Alive;
}

void ADreamWeatherStormSpawner::Refresh()
{
	UWorld* World = GetWorld();
	const UDreamWorldSubsystem* Clock = UDreamWorldSubsystem::Get(this);
	if (!World || !Clock)
	{
		return;
	}
	const TArray<FDreamStormPlan> Plans = GetStormsAt(Clock->GetWorldSeconds());
	// Gone: storms whose life is over (or that the clock jumped away from).
	for (auto It = Live.CreateIterator(); It; ++It)
	{
		if (!Plans.ContainsByPredicate([&It](const FDreamStormPlan& P) { return P.Slot == It.Key(); }))
		{
			if (ADreamWeatherStorm* Storm = It.Value().Get())
			{
				Storm->Destroy();
			}
			It.RemoveCurrent();
		}
	}
	for (const FDreamStormPlan& Plan : Plans)
	{
		if (Live.Contains(Plan.Slot) && Live[Plan.Slot].IsValid())
		{
			continue;
		}
		FActorSpawnParameters Spawn;
		Spawn.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		Spawn.ObjectFlags |= RF_Transient;
		Spawn.bDeferConstruction = true;
		const FVector Origin(Plan.Origin.X, Plan.Origin.Y, GetActorLocation().Z);
		ADreamWeatherStorm* Storm = World->SpawnActor<ADreamWeatherStorm>(Origin, FRotator::ZeroRotator, Spawn);
		if (!Storm)
		{
			continue;
		}
		UDreamWeatherZoneComponent* Zone = Storm->Zone;
		Zone->Preset = Plan.Preset;
		Zone->Extent = FVector2D(Plan.Radius, Plan.Radius);
		Zone->BlendDistance = BlendDistance;
		Zone->Priority = Priority;
		Zone->Motion = MakeMotion(Plan);
		// The spawner answers the queries itself (for any moment); the actor only shows the storm in the sky.
		Zone->bAffectsQueries = false;
		Storm->FinishSpawning(FTransform(Origin));
#if WITH_EDITOR
		Storm->SetActorLabel(FString::Printf(TEXT("Storm_%lld"), Plan.Slot));
#endif
		Live.Add(Plan.Slot, Storm);
	}
}

void ADreamWeatherStormSpawner::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	Refresh();
}

void ADreamWeatherStormSpawner::EndPlay(const EEndPlayReason::Type Reason)
{
	for (TPair<int64, TWeakObjectPtr<ADreamWeatherStorm>>& Pair : Live)
	{
		if (ADreamWeatherStorm* Storm = Pair.Value.Get())
		{
			Storm->Destroy();
		}
	}
	Live.Reset();
	Super::EndPlay(Reason);
}
