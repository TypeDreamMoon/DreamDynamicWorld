// Copyright DreamMoon. All Rights Reserved.

#include "DreamWaterBody.h"

#include "DreamWaterSubsystem.h"
#include "Components/DecalComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"

// ---- The model --------------------------------------------------------------------------------------------------

namespace DreamWater::Model
{
	FDreamWaterState Advance(const FDreamWaterState& State, const FDreamWaterRules& Rules, const FDreamWaterWeather& Weather, float Hours)
	{
		FDreamWaterState Next = State;
		if (Hours <= 0.0f)
		{
			return Next;
		}
		// The rain on the water and on the ground running off into it: mm on the catchment, cm on the water.
		const float Inflow = FMath::Max(Weather.Rain, 0.0f) * Rules.RainAtFull * 0.1f * (1.0f + Rules.CatchmentRatio) * Hours;
		float Level = State.LevelOffset + Inflow;
		// Back toward the usual level: a rise drains out over its outlet (half of it each DrainHours), a fall fills back
		// from the ground water, slowly (RefillHours).
		const float Half = FMath::Max(Level < 0.0f ? Rules.RefillHours : Rules.DrainHours, 0.1f);
		Level *= FMath::Exp(-Hours * UE_LN2 / Half);
		// Evaporation: warm, clear days draw it down.
		const float Warmth = FMath::Clamp((Weather.Temperature + 5.0f) / 25.0f, 0.0f, 1.0f);
		Level -= Rules.EvaporationPerDay * Warmth * (1.0f - 0.67f * FMath::Clamp(Weather.Cloud, 0.0f, 1.0f)) * Hours / 24.0f;
		Next.LevelOffset = FMath::Clamp(Level, -Rules.MaxDrop, Rules.MaxRise);
		// What the rain brings in muddies it; the mud settles.
		const float Turbid = FMath::Min(State.Turbidity + Inflow * Rules.TurbidityPerCm, 1.0f);
		Next.Turbidity = FMath::Clamp(Turbid * FMath::Exp(-Hours * UE_LN2 / FMath::Max(Rules.ClearHours, 0.1f)), 0.0f, 1.0f);
		return Next;
	}

	EDreamWaterBand BandOf(float LevelOffset, const FDreamWaterRules& Rules, EDreamWaterBand Current)
	{
		// A band is left only a couple of cm past its edge, so a level hovering at one does not flicker.
		constexpr float Margin = 2.0f;
		auto Raw = [&Rules](float Offset)
		{
			if (Offset >= Rules.MaxRise - 0.5f)
			{
				return EDreamWaterBand::Overflow;
			}
			if (Offset > Rules.HighAbove)
			{
				return EDreamWaterBand::High;
			}
			return Offset < -Rules.LowBelow ? EDreamWaterBand::Low : EDreamWaterBand::Normal;
		};
		const EDreamWaterBand Here = Raw(LevelOffset);
		if (Here == Current)
		{
			return Current;
		}
		// Moving to a band only once well inside it: the same test a margin further on.
		const float Toward = uint8(Here) > uint8(Current) ? LevelOffset - Margin : LevelOffset + Margin;
		return Raw(Toward) == Here || Here == EDreamWaterBand::Overflow ? Here : Current;
	}
}

// ---- The component ----------------------------------------------------------------------------------------------

UDreamWaterBodyComponent::UDreamWaterBodyComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
}

void UDreamWaterBodyComponent::OnRegister()
{
	Super::OnRegister();
	// The usual level: where the component stands, less what it has been lifted by.
	const float Lifted = bMoveOwner && bBaseKnown ? State.LevelOffset : 0.0f;
	BaseHeight = float(GetComponentLocation().Z) - Lifted;
	if (const AActor* Owner = GetOwner())
	{
		OwnerBaseZ = float(Owner->GetActorLocation().Z) - Lifted;
	}
	bBaseKnown = true;
	if (UDreamWaterSubsystem* Water = UDreamWaterSubsystem::Get(GetWorld()))
	{
		Water->RegisterBody(this);
	}
	UpdateCaustics();
}

#if WITH_EDITOR
void UDreamWaterBodyComponent::PostEditChangeProperty(FPropertyChangedEvent& Event)
{
	Super::PostEditChangeProperty(Event);
	CausticsDrawn = FVector4f(-1.0e9f);
	UpdateCaustics();
}
#endif

void UDreamWaterBodyComponent::UpdateCaustics()
{
	UWorld* World = GetWorld();
	if (!bCaustics || !World || World->IsNetMode(NM_DedicatedServer))
	{
		if (CausticsDecal)
		{
			CausticsDecal->SetVisibility(false);
		}
		return;
	}
	if (!CausticsDecal)
	{
		UMaterialInterface* Base = LoadObject<UMaterialInterface>(nullptr, TEXT("/DreamDynamicWorld/Water/M_DreamWaterCaustics.M_DreamWaterCaustics"));
		if (!Base || !GetOwner())
		{
			return;
		}
		CausticsMaterial = UMaterialInstanceDynamic::Create(Base, this);
		CausticsDecal = NewObject<UDecalComponent>(GetOwner(), NAME_None, RF_Transient | RF_TextExportTransient | RF_DuplicateTransient);
		CausticsDecal->SetupAttachment(this);
		CausticsDecal->SetUsingAbsoluteLocation(true);
		CausticsDecal->SetUsingAbsoluteRotation(true);
		CausticsDecal->SetUsingAbsoluteScale(true);
		CausticsDecal->SetDecalMaterial(CausticsMaterial);
		CausticsDecal->SetFadeScreenSize(0.0f);
		CausticsDecal->RegisterComponent();
	}
	// From just above the surface down past the bottom, over the water's area (the decal's X is its depth, pointing down);
	// again only when the level has moved half a cm or the water's look changed.
	const float Surface = GetSurfaceHeight();
	const float Bottom = GetBottomHeight();
	const FVector4f Now(Surface, State.Turbidity, CausticStrength, Bottom);
	if (FMath::Abs(Now.X - CausticsDrawn.X) < 0.5f && FMath::Abs(Now.Y - CausticsDrawn.Y) < 0.01f && Now.Z == CausticsDrawn.Z && Now.W == CausticsDrawn.W)
	{
		return;
	}
	CausticsDrawn = Now;
	const FVector At = GetComponentLocation();
	CausticsDecal->SetWorldLocationAndRotation(FVector(At.X, At.Y, 0.5 * (Surface + Bottom)), FRotator(-90.0, GetComponentRotation().Yaw, 0.0));
	CausticsDecal->DecalSize = FVector(0.5 * (Surface - Bottom) + 30.0, Extent.Y, Extent.X);
	CausticsDecal->MarkRenderStateDirty();
	CausticsDecal->SetVisibility(true);
	CausticsMaterial->SetVectorParameterValue(TEXT("WaterSurface"), FLinearColor(Surface, State.Turbidity, CausticStrength, Bottom));
	CausticsMaterial->SetVectorParameterValue(TEXT("CausticLook"), FLinearColor(CausticSize, CausticSpeed, CausticFadeDepth, 0.0f));
}

void UDreamWaterBodyComponent::OnUnregister()
{
	if (CausticsDecal)
	{
		CausticsDecal->DestroyComponent();
		CausticsDecal = nullptr;
		CausticsDrawn = FVector4f(-1.0e9f);
	}
	if (UDreamWaterSubsystem* Water = UDreamWaterSubsystem::Get(GetWorld()))
	{
		Water->UnregisterBody(this);
	}
	Super::OnUnregister();
}

void UDreamWaterBodyComponent::SetState(const FDreamWaterState& NewState)
{
	State.LevelOffset = FMath::Clamp(NewState.LevelOffset, -Rules.MaxDrop, Rules.MaxRise);
	State.Turbidity = FMath::Clamp(NewState.Turbidity, 0.0f, 1.0f);
	ApplyLevel();
}

void UDreamWaterBodyComponent::Advance(const FDreamWaterWeather& Weather, float Hours)
{
	if (bHold)
	{
		return;
	}
	State = DreamWater::Model::Advance(State, Rules, Weather, Hours);
	ApplyLevel();
}

void UDreamWaterBodyComponent::ApplyLevel()
{
	const EDreamWaterBand Was = Band;
	Band = DreamWater::Model::BandOf(State.LevelOffset, Rules, Band);
	if (Band != Was)
	{
		OnBandChangedNative.Broadcast(this, Was, Band);
		OnBandChanged.Broadcast(this, Was, Band);
	}
	UpdateCaustics();
	// The actor it is on rides on the level (not by less than half a cm a move).
	AActor* Owner = bMoveOwner ? GetOwner() : nullptr;
	if (Owner && bBaseKnown)
	{
		FVector At = Owner->GetActorLocation();
		const double Want = OwnerBaseZ + State.LevelOffset;
		if (FMath::Abs(At.Z - Want) > 0.5)
		{
			At.Z = Want;
			Owner->SetActorLocation(At);
		}
	}
}

bool UDreamWaterBodyComponent::Covers(FVector Position) const
{
	const FVector Local = GetComponentTransform().InverseTransformPositionNoScale(Position);
	return FMath::Abs(Local.X) <= Extent.X && FMath::Abs(Local.Y) <= Extent.Y;
}

float UDreamWaterBodyComponent::GetDepthBelowSurface(FVector Position) const
{
	if (!Covers(Position) || Position.Z < GetBottomHeight())
	{
		return 0.0f;
	}
	return FMath::Max(GetSurfaceHeight() - float(Position.Z), 0.0f);
}
