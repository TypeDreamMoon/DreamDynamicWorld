// Copyright DreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "MovieSceneNameableTrack.h"
#include "Channels/MovieSceneFloatChannel.h"
#include "Sections/MovieSceneHookSection.h"
#include "DreamWeatherTypes.h"
#include "DreamWorldTracks.generated.h"

class UDreamWeatherPreset;
class UWorld;

namespace UE::MovieScene { struct FSharedPlaybackState; }

/**
 * Time of day: the Hours curve (0..24, beyond 24 into the next days) sets the world clock while the section is
 * active; the clock does not run on its own meanwhile. Playing moves it as if it ran (the hour's events fire);
 * scrubbing or a cut jumps it.
 */
UCLASS(MinimalAPI)
class UDreamWorldTimeSection : public UMovieSceneHookSection
{
	GENERATED_BODY()

public:
	UDreamWorldTimeSection(const FObjectInitializer& ObjectInitializer);

	/** Hours since midnight of Date. */
	UPROPERTY()
	FMovieSceneFloatChannel Hours;

	/** The day the hours count from (otherwise the clock's start date). */
	UPROPERTY(EditAnywhere, Category = "Time")
	bool bOverrideDate = false;

	UPROPERTY(EditAnywhere, Category = "Time", meta = (EditCondition = "bOverrideDate"))
	FDateTime Date = FDateTime(2026, 6, 21);

	/** The world time the section puts the clock at, at a sequence time (false when the curve is empty). */
	DREAMWORLDSEQUENCER_API bool EvaluateWorldSeconds(FFrameTime Time, const UWorld* World, double& OutSeconds) const;

protected:
	virtual EMovieSceneChannelProxyType CacheChannelProxy() override;
	virtual void Begin(TSharedRef<UE::MovieScene::FSharedPlaybackState> SharedPlaybackState, const UE::MovieScene::FEvaluationHookParams& Params) const override;
	virtual void Update(TSharedRef<UE::MovieScene::FSharedPlaybackState> SharedPlaybackState, const UE::MovieScene::FEvaluationHookParams& Params) const override;
	virtual void End(TSharedRef<UE::MovieScene::FSharedPlaybackState> SharedPlaybackState, const UE::MovieScene::FEvaluationHookParams& Params) const override;
};

/** The time-of-day track. */
UCLASS(MinimalAPI)
class UDreamWorldTimeTrack : public UMovieSceneNameableTrack
{
	GENERATED_BODY()

public:
	UDreamWorldTimeTrack();

	virtual void AddSection(UMovieSceneSection& Section) override { Sections.Add(&Section); }
	virtual bool SupportsType(TSubclassOf<UMovieSceneSection> SectionClass) const override;
	virtual UMovieSceneSection* CreateNewSection() override;
	virtual const TArray<UMovieSceneSection*>& GetAllSections() const override { return Sections; }
	virtual bool HasSection(const UMovieSceneSection& Section) const override { return Sections.Contains(&Section); }
	virtual bool IsEmpty() const override { return Sections.Num() == 0; }
	virtual void RemoveAllAnimationData() override { Sections.Reset(); }
	virtual void RemoveSection(UMovieSceneSection& Section) override { Sections.Remove(&Section); }
	virtual void RemoveSectionAt(int32 SectionIndex) override { Sections.RemoveAt(SectionIndex); }
	virtual bool SupportsMultipleRows() const override { return false; }
#if WITH_EDITORONLY_DATA
	virtual FText GetDefaultDisplayName() const override;
#endif

private:
	UPROPERTY()
	TArray<TObjectPtr<UMovieSceneSection>> Sections;
};

/**
 * A stretch of one weather: forces Preset while active, at the section's weight eased in and out (the easing is the
 * transition; overlap two sections to crossfade, the lower row first). Applied through the weather's modifier stack.
 */
UCLASS(MinimalAPI)
class UDreamWeatherPresetSection : public UMovieSceneHookSection
{
	GENERATED_BODY()

public:
	UDreamWeatherPresetSection(const FObjectInitializer& ObjectInitializer);

	UPROPERTY(EditAnywhere, Category = "Weather")
	TObjectPtr<UDreamWeatherPreset> Preset;

	UPROPERTY(EditAnywhere, Category = "Weather", meta = (ClampMin = "0", ClampMax = "1"))
	float Weight = 1.0f;

	/** Modifiers of a weather track apply above this priority (plus the row), over the project's own. */
	UPROPERTY(EditAnywhere, Category = "Weather")
	int32 BasePriority = 1000;

	/** The modifier the section stands for at a sequence time (weight from the easing). */
	DREAMWORLDSEQUENCER_API FDreamWeatherModifierDesc MakeModifier(FFrameTime Time) const;

protected:
	virtual void Update(TSharedRef<UE::MovieScene::FSharedPlaybackState> SharedPlaybackState, const UE::MovieScene::FEvaluationHookParams& Params) const override;
	virtual void End(TSharedRef<UE::MovieScene::FSharedPlaybackState> SharedPlaybackState, const UE::MovieScene::FEvaluationHookParams& Params) const override;
};

/** A weather channel's curve on a channel section, by the channel's tag (any channel: built-in or the project's own). */
USTRUCT()
struct FDreamWeatherChannelCurve
{
	GENERATED_BODY()

	UPROPERTY()
	FGameplayTag Channel;

	UPROPERTY()
	FMovieSceneFloatChannel Curve;
};

/**
 * Weather channels by curve: each keyed channel is pulled to its curve while the section is active (Blend, at the
 * section's eased weight); unkeyed ones are left to the weather. Every channel the weather has can be keyed -- the
 * built-in ones and the project's own (Project Settings > Dream Weather) -- each by its tag.
 */
UCLASS(MinimalAPI)
class UDreamWeatherChannelSection : public UMovieSceneHookSection
{
	GENERATED_BODY()

public:
	UDreamWeatherChannelSection(const FObjectInitializer& ObjectInitializer);

	UPROPERTY() FMovieSceneFloatChannel CloudCoverage;
	UPROPERTY() FMovieSceneFloatChannel CloudDarkness;
	UPROPERTY() FMovieSceneFloatChannel Precipitation;
	UPROPERTY() FMovieSceneFloatChannel SnowRatio;
	UPROPERTY() FMovieSceneFloatChannel Fog;
	UPROPERTY() FMovieSceneFloatChannel Dust;
	UPROPERTY() FMovieSceneFloatChannel Wind;
	UPROPERTY() FMovieSceneFloatChannel Lightning;
	UPROPERTY() FMovieSceneFloatChannel Temperature;
	UPROPERTY() FMovieSceneFloatChannel Wetness;
	UPROPERTY() FMovieSceneFloatChannel SnowCover;

	UPROPERTY(EditAnywhere, Category = "Weather")
	EDreamWeatherBlend Blend = EDreamWeatherBlend::Override;

	UPROPERTY(EditAnywhere, Category = "Weather")
	int32 BasePriority = 1000;

	/** The modifier the section stands for at a sequence time. */
	DREAMWORLDSEQUENCER_API FDreamWeatherModifierDesc MakeModifier(FFrameTime Time) const;

	/** Every channel's curve, in the weather's order (FDreamWeatherChannels): the first eleven built-ins' are the fields
	 *  above (as sequences saved before the others were added keep them), the rest MoreChannels' by tag. */
	TArray<TPair<FGameplayTag, const FMovieSceneFloatChannel*>> GetCurves() const;

	/** A channel's curve by its tag (made for a channel the section has none of yet); null for a tag the weather does
	 *  not have. */
	DREAMWORLDSEQUENCER_API FMovieSceneFloatChannel* GetOrAddCurve(const FGameplayTag& Channel);

	/** The curves of the channels past the first eleven built-ins, by tag. */
	UPROPERTY()
	TArray<FDreamWeatherChannelCurve> MoreChannels;

	virtual void PostLoad() override;

protected:
	virtual EMovieSceneChannelProxyType CacheChannelProxy() override;
	virtual void Update(TSharedRef<UE::MovieScene::FSharedPlaybackState> SharedPlaybackState, const UE::MovieScene::FEvaluationHookParams& Params) const override;
	virtual void End(TSharedRef<UE::MovieScene::FSharedPlaybackState> SharedPlaybackState, const UE::MovieScene::FEvaluationHookParams& Params) const override;
};

/**
 * Lightning on cue: every key of Strikes is a strike (its value the intensity, 0..1) when the sequence plays
 * forward through it, at Location (relative to the view and turned with it, by default), through
 * UDreamWeatherSubsystem::StrikeLightning: the flash, the bolt, the thunder after its delay.
 */
UCLASS(MinimalAPI)
class UDreamLightningSection : public UMovieSceneHookSection
{
	GENERATED_BODY()

public:
	UDreamLightningSection(const FObjectInitializer& ObjectInitializer);

	UPROPERTY()
	FMovieSceneFloatChannel Strikes;

	/** Where the strikes land: relative to the view (x ahead, y right) when bRelativeToView, else in the world. */
	UPROPERTY(EditAnywhere, Category = "Lightning")
	FVector Location = FVector(150000.0, 0.0, 0.0);

	UPROPERTY(EditAnywhere, Category = "Lightning")
	bool bRelativeToView = true;

	/** Each strike lands up to this far (cm) from Location, the same way on every play. */
	UPROPERTY(EditAnywhere, Category = "Lightning", meta = (ClampMin = "0"))
	float Scatter = 30000.0f;

	/** Where key Index strikes when the view is at ViewLocation looking along ViewRotation. */
	DREAMWORLDSEQUENCER_API FVector StrikeLocation(int32 Index, const FVector& ViewLocation, const FRotator& ViewRotation) const;

protected:
	virtual EMovieSceneChannelProxyType CacheChannelProxy() override;
	virtual TArrayView<const FFrameNumber> GetTriggerTimes() const override { return Strikes.GetData().GetTimes(); }
	virtual void Trigger(TSharedRef<UE::MovieScene::FSharedPlaybackState> SharedPlaybackState, const UE::MovieScene::FEvaluationHookParams& Params) const override;
};

/** The weather track: preset, channel and lightning sections, on as many rows as needed. */
UCLASS(MinimalAPI)
class UDreamWeatherTrack : public UMovieSceneNameableTrack
{
	GENERATED_BODY()

public:
	UDreamWeatherTrack();

	virtual void AddSection(UMovieSceneSection& Section) override { Sections.Add(&Section); }
	virtual bool SupportsType(TSubclassOf<UMovieSceneSection> SectionClass) const override;
	virtual UMovieSceneSection* CreateNewSection() override;
	virtual const TArray<UMovieSceneSection*>& GetAllSections() const override { return Sections; }
	virtual bool HasSection(const UMovieSceneSection& Section) const override { return Sections.Contains(&Section); }
	virtual bool IsEmpty() const override { return Sections.Num() == 0; }
	virtual void RemoveAllAnimationData() override { Sections.Reset(); }
	virtual void RemoveSection(UMovieSceneSection& Section) override { Sections.Remove(&Section); }
	virtual void RemoveSectionAt(int32 SectionIndex) override { Sections.RemoveAt(SectionIndex); }
	virtual bool SupportsMultipleRows() const override { return true; }
#if WITH_EDITORONLY_DATA
	virtual FText GetDefaultDisplayName() const override;
#endif

private:
	UPROPERTY()
	TArray<TObjectPtr<UMovieSceneSection>> Sections;
};
