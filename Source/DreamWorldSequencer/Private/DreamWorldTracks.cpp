// Copyright DreamMoon. All Rights Reserved.

#include "DreamWorldTracks.h"
#include "DreamWorldSequencerSubsystem.h"
#include "DreamWeatherChannels.h"
#include "DreamWeatherPreset.h"
#include "DreamWeatherSubsystem.h"
#include "DreamWorldCelestial.h"
#include "DreamWorldSubsystem.h"

#include "Channels/MovieSceneChannelProxy.h"
#include "Engine/World.h"
#include "EntitySystem/MovieSceneSharedPlaybackState.h"
#include "Evaluation/IMovieSceneEvaluationHook.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(DreamWorldTracks)

#define LOCTEXT_NAMESPACE "DreamWorldTracks"

namespace DreamWorldSequencer
{
	static UWorld* WorldOf(const TSharedRef<UE::MovieScene::FSharedPlaybackState>& State)
	{
		const UObject* Context = State->GetPlaybackContext();
		return Context ? Context->GetWorld() : nullptr;
	}

	static bool IsPlayingForward(const UE::MovieScene::FEvaluationHookParams& Params)
	{
		return Params.Context.GetStatus() == EMovieScenePlayerStatus::Playing && Params.Context.GetDirection() == EPlayDirection::Forwards;
	}

	/** The sequence instance a hook runs in (one modifier per section per instance). */
	static const void* InstanceOf(const TSharedRef<UE::MovieScene::FSharedPlaybackState>& State, const UE::MovieScene::FEvaluationHookParams& Params)
	{
		return reinterpret_cast<const void*>(reinterpret_cast<UPTRINT>(&State.Get()) ^ (UPTRINT(Params.SequenceID.GetInternalValue()) << 3));
	}

	static void AddCurve(FMovieSceneChannelProxyData& Channels, FMovieSceneFloatChannel& Channel, FName Name, const FText& Label, int32 Order)
	{
#if WITH_EDITOR
		FMovieSceneChannelMetaData Meta;
		Meta.SetIdentifiers(Name, Label);
		Meta.SortOrder = Order;
		Meta.bCanCollapseToTrack = false;
		Channels.Add(Channel, Meta, TMovieSceneExternalValue<float>::Make());
#else
		Channels.Add(Channel);
#endif
	}
}

// ---- Time --------------------------------------------------------------------------------------

UDreamWorldTimeSection::UDreamWorldTimeSection(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	bRequiresRangedHook = true;
	Hours.SetDefault(12.0f);
	EvalOptions.CompletionMode = EMovieSceneCompletionMode::RestoreState;
}

EMovieSceneChannelProxyType UDreamWorldTimeSection::CacheChannelProxy()
{
	FMovieSceneChannelProxyData Channels;
	DreamWorldSequencer::AddCurve(Channels, Hours, TEXT("Hours"), LOCTEXT("Hours", "Hours"), 0);
	ChannelProxy = MakeShared<FMovieSceneChannelProxy>(MoveTemp(Channels));
	return EMovieSceneChannelProxyType::Static;
}

bool UDreamWorldTimeSection::EvaluateWorldSeconds(FFrameTime Time, const UWorld* World, double& OutSeconds) const
{
	float Value = 0.0f;
	if (!Hours.Evaluate(Time, Value))
	{
		return false;
	}
	FDateTime Day = Date;
	if (!bOverrideDate)
	{
		const UDreamWorldSubsystem* Clock = UDreamWorldSubsystem::Get(World);
		Day = Clock ? Clock->GetClockConfig().StartTime : Date;
	}
	OutSeconds = DreamWorld::Celestial::FromDateTime(Day.GetDate()) + double(FMath::Max(Value, 0.0f)) * 3600.0;
	return true;
}

void UDreamWorldTimeSection::Begin(TSharedRef<UE::MovieScene::FSharedPlaybackState> SharedPlaybackState, const UE::MovieScene::FEvaluationHookParams& Params) const
{
	if (UDreamWorldSubsystem* Clock = UDreamWorldSubsystem::Get(DreamWorldSequencer::WorldOf(SharedPlaybackState)))
	{
		Clock->BeginExternalDrive(this);
	}
}

void UDreamWorldTimeSection::Update(TSharedRef<UE::MovieScene::FSharedPlaybackState> SharedPlaybackState, const UE::MovieScene::FEvaluationHookParams& Params) const
{
	UWorld* World = DreamWorldSequencer::WorldOf(SharedPlaybackState);
	UDreamWorldSubsystem* Clock = UDreamWorldSubsystem::Get(World);
	double Seconds = 0.0;
	if (Clock && EvaluateWorldSeconds(Params.Context.GetTime(), World, Seconds))
	{
		Clock->BeginExternalDrive(this);
		// Playing runs the clock there (an hour's worth of curve per frame at most); scrubbing and cuts jump it.
		Clock->DriveWorldSeconds(Seconds, DreamWorldSequencer::IsPlayingForward(Params) ? 3600.0 : 0.0);
	}
}

void UDreamWorldTimeSection::End(TSharedRef<UE::MovieScene::FSharedPlaybackState> SharedPlaybackState, const UE::MovieScene::FEvaluationHookParams& Params) const
{
	if (UDreamWorldSubsystem* Clock = UDreamWorldSubsystem::Get(DreamWorldSequencer::WorldOf(SharedPlaybackState)))
	{
		Clock->EndExternalDrive(this);
	}
}

UDreamWorldTimeTrack::UDreamWorldTimeTrack()
{
#if WITH_EDITORONLY_DATA
	TrackTint = FColor(200, 160, 60, 150);
#endif
}

bool UDreamWorldTimeTrack::SupportsType(TSubclassOf<UMovieSceneSection> SectionClass) const
{
	return SectionClass == UDreamWorldTimeSection::StaticClass();
}

UMovieSceneSection* UDreamWorldTimeTrack::CreateNewSection()
{
	return NewObject<UDreamWorldTimeSection>(this, NAME_None, RF_Transactional);
}

#if WITH_EDITORONLY_DATA
FText UDreamWorldTimeTrack::GetDefaultDisplayName() const
{
	return LOCTEXT("TimeTrack", "Time of Day");
}
#endif

// ---- Weather presets ---------------------------------------------------------------------------

UDreamWeatherPresetSection::UDreamWeatherPresetSection(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	bRequiresRangedHook = true;
	bSupportsInfiniteRange = true;
}

FDreamWeatherModifierDesc UDreamWeatherPresetSection::MakeModifier(FFrameTime Time) const
{
	FDreamWeatherModifierDesc Desc;
	Desc.Source = TEXT("Sequencer");
	Desc.Preset = Preset;
	Desc.Blend = EDreamWeatherBlend::Override;
	Desc.Weight = FMath::Clamp(Weight, 0.0f, 1.0f) * EvaluateEasing(Time);
	Desc.Priority = BasePriority + GetRowIndex();
	Desc.BlendInSeconds = 0.0f;
	return Desc;
}

void UDreamWeatherPresetSection::Update(TSharedRef<UE::MovieScene::FSharedPlaybackState> SharedPlaybackState, const UE::MovieScene::FEvaluationHookParams& Params) const
{
	if (UDreamWorldSequencerSubsystem* Held = UDreamWorldSequencerSubsystem::Get(DreamWorldSequencer::WorldOf(SharedPlaybackState)))
	{
		Held->ApplyModifier(this, DreamWorldSequencer::InstanceOf(SharedPlaybackState, Params), MakeModifier(Params.Context.GetTime()));
	}
}

void UDreamWeatherPresetSection::End(TSharedRef<UE::MovieScene::FSharedPlaybackState> SharedPlaybackState, const UE::MovieScene::FEvaluationHookParams& Params) const
{
	if (UDreamWorldSequencerSubsystem* Held = UDreamWorldSequencerSubsystem::Get(DreamWorldSequencer::WorldOf(SharedPlaybackState)))
	{
		Held->ReleaseModifier(this, DreamWorldSequencer::InstanceOf(SharedPlaybackState, Params));
	}
}

// ---- Weather channels --------------------------------------------------------------------------

UDreamWeatherChannelSection::UDreamWeatherChannelSection(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	bRequiresRangedHook = true;
	bSupportsInfiniteRange = true;
}

namespace DreamWorldSequencer
{
	/** How many of the weather's built-in channels the channel section's own fields hold (the first ones, in order). */
	static constexpr int32 FieldChannels = 11;
}

TArray<TPair<FGameplayTag, const FMovieSceneFloatChannel*>> UDreamWeatherChannelSection::GetCurves() const
{
	const FMovieSceneFloatChannel* Fields[DreamWorldSequencer::FieldChannels] = { &CloudCoverage, &CloudDarkness, &Precipitation, &SnowRatio, &Fog, &Dust,
		&Wind, &Lightning, &Temperature, &Wetness, &SnowCover };
	TArray<TPair<FGameplayTag, const FMovieSceneFloatChannel*>> Out;
	const TArray<FDreamWeatherChannelDef>& Defs = FDreamWeatherChannels::Get();
	for (int32 i = 0; i < Defs.Num(); ++i)
	{
		if (i < DreamWorldSequencer::FieldChannels)
		{
			Out.Emplace(Defs[i].Channel, Fields[i]);
			continue;
		}
		for (const FDreamWeatherChannelCurve& More : MoreChannels)
		{
			if (More.Channel == Defs[i].Channel)
			{
				Out.Emplace(Defs[i].Channel, &More.Curve);
				break;
			}
		}
	}
	return Out;
}

FMovieSceneFloatChannel* UDreamWeatherChannelSection::GetOrAddCurve(const FGameplayTag& Channel)
{
	const int32 Index = FDreamWeatherChannels::IndexOf(Channel);
	if (Index == INDEX_NONE)
	{
		return nullptr;
	}
	if (Index < DreamWorldSequencer::FieldChannels)
	{
		FMovieSceneFloatChannel* Fields[DreamWorldSequencer::FieldChannels] = { &CloudCoverage, &CloudDarkness, &Precipitation, &SnowRatio, &Fog, &Dust,
			&Wind, &Lightning, &Temperature, &Wetness, &SnowCover };
		return Fields[Index];
	}
	for (FDreamWeatherChannelCurve& More : MoreChannels)
	{
		if (More.Channel == Channel)
		{
			return &More.Curve;
		}
	}
	FDreamWeatherChannelCurve& Added = MoreChannels.AddDefaulted_GetRef();
	Added.Channel = Channel;
	ChannelProxy.Reset();
	return &Added.Curve;
}

void UDreamWeatherChannelSection::PostLoad()
{
	Super::PostLoad();
	// (Every channel the weather has now gets a curve, so a channel added to the project can be keyed.)
	for (const FDreamWeatherChannelDef& Def : FDreamWeatherChannels::Get())
	{
		GetOrAddCurve(Def.Channel);
	}
}

EMovieSceneChannelProxyType UDreamWeatherChannelSection::CacheChannelProxy()
{
	// (A curve for every channel the weather has, the project's own too; they can change, so the proxy is dynamic.)
	for (const FDreamWeatherChannelDef& Def : FDreamWeatherChannels::Get())
	{
		GetOrAddCurve(Def.Channel);
	}
	FMovieSceneChannelProxyData Channels;
	const TArray<TPair<FGameplayTag, const FMovieSceneFloatChannel*>> Curves = GetCurves();
	for (int32 i = 0; i < Curves.Num(); ++i)
	{
		const FName Name = Curves[i].Key.GetTagName();
		FString Label = Name.ToString();
		Label.RemoveFromStart(TEXT("Dream.Weather.Channel."));
		DreamWorldSequencer::AddCurve(Channels, const_cast<FMovieSceneFloatChannel&>(*Curves[i].Value), Name, FText::FromString(Label), i);
	}
	ChannelProxy = MakeShared<FMovieSceneChannelProxy>(MoveTemp(Channels));
	return EMovieSceneChannelProxyType::Dynamic;
}

FDreamWeatherModifierDesc UDreamWeatherChannelSection::MakeModifier(FFrameTime Time) const
{
	FDreamWeatherModifierDesc Desc;
	Desc.Source = TEXT("Sequencer");
	Desc.Blend = Blend;
	Desc.Weight = EvaluateEasing(Time);
	Desc.Priority = BasePriority + GetRowIndex();
	Desc.BlendInSeconds = 0.0f;
	for (const TPair<FGameplayTag, const FMovieSceneFloatChannel*>& Curve : GetCurves())
	{
		float Value = 0.0f;
		// Only what has keys (or a default set by hand) applies; the rest stays the weather's.
		if ((Curve.Value->GetNumKeys() > 0 || Curve.Value->GetDefault().IsSet()) && Curve.Value->Evaluate(Time, Value))
		{
			Desc.Channels.Add(Curve.Key, Value);
		}
	}
	return Desc;
}

void UDreamWeatherChannelSection::Update(TSharedRef<UE::MovieScene::FSharedPlaybackState> SharedPlaybackState, const UE::MovieScene::FEvaluationHookParams& Params) const
{
	if (UDreamWorldSequencerSubsystem* Held = UDreamWorldSequencerSubsystem::Get(DreamWorldSequencer::WorldOf(SharedPlaybackState)))
	{
		Held->ApplyModifier(this, DreamWorldSequencer::InstanceOf(SharedPlaybackState, Params), MakeModifier(Params.Context.GetTime()));
	}
}

void UDreamWeatherChannelSection::End(TSharedRef<UE::MovieScene::FSharedPlaybackState> SharedPlaybackState, const UE::MovieScene::FEvaluationHookParams& Params) const
{
	if (UDreamWorldSequencerSubsystem* Held = UDreamWorldSequencerSubsystem::Get(DreamWorldSequencer::WorldOf(SharedPlaybackState)))
	{
		Held->ReleaseModifier(this, DreamWorldSequencer::InstanceOf(SharedPlaybackState, Params));
	}
}

// ---- Lightning ---------------------------------------------------------------------------------

UDreamLightningSection::UDreamLightningSection(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	bRequiresTriggerHooks = true;
	SetRange(TRange<FFrameNumber>::All());
}

EMovieSceneChannelProxyType UDreamLightningSection::CacheChannelProxy()
{
	FMovieSceneChannelProxyData Channels;
	DreamWorldSequencer::AddCurve(Channels, Strikes, TEXT("Strikes"), LOCTEXT("Strikes", "Strikes"), 0);
	ChannelProxy = MakeShared<FMovieSceneChannelProxy>(MoveTemp(Channels));
	return EMovieSceneChannelProxyType::Static;
}

FVector UDreamLightningSection::StrikeLocation(int32 Index, const FVector& ViewLocation, const FRotator& ViewRotation) const
{
	// The same scatter for a key on every play: drawn from the key's index and time.
	const TArrayView<const FFrameNumber> Times = Strikes.GetData().GetTimes();
	const uint32 Hash = HashCombine(GetTypeHash(Index), Times.IsValidIndex(Index) ? GetTypeHash(Times[Index].Value) : 0u);
	FRandomStream Random(static_cast<int32>(Hash));
	const FVector2D Offset = FVector2D(Random.FRandRange(-1.0, 1.0), Random.FRandRange(-1.0, 1.0)) * Scatter;
	FVector Local = Location + FVector(Offset.X, Offset.Y, 0.0);
	if (!bRelativeToView)
	{
		return Local;
	}
	return ViewLocation + FRotator(0.0, ViewRotation.Yaw, 0.0).RotateVector(Local);
}

void UDreamLightningSection::Trigger(TSharedRef<UE::MovieScene::FSharedPlaybackState> SharedPlaybackState, const UE::MovieScene::FEvaluationHookParams& Params) const
{
	if (!DreamWorldSequencer::IsPlayingForward(Params) || Params.Context.IsSilent())
	{
		return;
	}
	UWorld* World = DreamWorldSequencer::WorldOf(SharedPlaybackState);
	UDreamWeatherSubsystem* Weather = UDreamWeatherSubsystem::Get(World);
	const UDreamWorldSubsystem* Clock = UDreamWorldSubsystem::Get(World);
	const TArrayView<const FMovieSceneFloatValue> Values = Strikes.GetData().GetValues();
	if (!Weather || !Values.IsValidIndex(Params.TriggerIndex))
	{
		return;
	}
	const FVector View = Clock ? Clock->GetViewLocation() : FVector::ZeroVector;
	const FRotator Rotation = Clock ? Clock->GetViewRotation() : FRotator::ZeroRotator;
	Weather->StrikeLightning(StrikeLocation(Params.TriggerIndex, View, Rotation), FMath::Clamp(Values[Params.TriggerIndex].Value, 0.0f, 1.0f));
}

// ---- The weather track -------------------------------------------------------------------------

UDreamWeatherTrack::UDreamWeatherTrack()
{
#if WITH_EDITORONLY_DATA
	TrackTint = FColor(70, 120, 200, 150);
#endif
}

bool UDreamWeatherTrack::SupportsType(TSubclassOf<UMovieSceneSection> SectionClass) const
{
	return SectionClass == UDreamWeatherPresetSection::StaticClass() || SectionClass == UDreamWeatherChannelSection::StaticClass()
		|| SectionClass == UDreamLightningSection::StaticClass();
}

UMovieSceneSection* UDreamWeatherTrack::CreateNewSection()
{
	return NewObject<UDreamWeatherPresetSection>(this, NAME_None, RF_Transactional);
}

#if WITH_EDITORONLY_DATA
FText UDreamWeatherTrack::GetDefaultDisplayName() const
{
	return LOCTEXT("WeatherTrack", "Weather");
}
#endif

// ---- Held modifiers ----------------------------------------------------------------------------

UDreamWorldSequencerSubsystem* UDreamWorldSequencerSubsystem::Get(const UObject* WorldContextObject)
{
	const UWorld* World = WorldContextObject ? WorldContextObject->GetWorld() : nullptr;
	return World ? World->GetSubsystem<UDreamWorldSequencerSubsystem>() : nullptr;
}

void UDreamWorldSequencerSubsystem::ApplyModifier(const UObject* Section, const void* Instance, const FDreamWeatherModifierDesc& Desc)
{
	UDreamWeatherSubsystem* Weather = UDreamWeatherSubsystem::Get(this);
	if (!Weather)
	{
		return;
	}
	const TPair<const void*, const void*> Key(Section, Instance);
	FDreamWeatherModifierHandle* Handle = Held.Find(Key);
	if (Handle && Weather->UpdateModifier(*Handle, Desc))
	{
		Weather->SetModifierWeight(*Handle, Desc.Weight, 0.0f);
		return;
	}
	Held.Add(Key, Weather->PushModifier(Desc));
}

void UDreamWorldSequencerSubsystem::ReleaseModifier(const UObject* Section, const void* Instance)
{
	FDreamWeatherModifierHandle Handle;
	if (Held.RemoveAndCopyValue(TPair<const void*, const void*>(Section, Instance), Handle))
	{
		if (UDreamWeatherSubsystem* Weather = UDreamWeatherSubsystem::Get(this))
		{
			Weather->RemoveModifier(Handle, 0.0f);
		}
	}
}

#undef LOCTEXT_NAMESPACE
