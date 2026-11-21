// Copyright DreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "MovieSceneTrackEditor.h"

/** Adds and shows the time-of-day and weather tracks in Sequencer. */
class FDreamWorldTrackEditor : public FMovieSceneTrackEditor
{
public:
	explicit FDreamWorldTrackEditor(TSharedRef<ISequencer> InSequencer);

	static TSharedRef<ISequencerTrackEditor> Create(TSharedRef<ISequencer> InSequencer);

	//~ ISequencerTrackEditor
	virtual FText GetDisplayName() const override;
	virtual void BuildAddTrackMenu(FMenuBuilder& MenuBuilder) override;
	virtual TSharedPtr<SWidget> BuildOutlinerEditWidget(const FGuid& ObjectBinding, UMovieSceneTrack* Track, const FBuildEditWidgetParams& Params) override;
	virtual TSharedRef<ISequencerSection> MakeSectionInterface(UMovieSceneSection& SectionObject, UMovieSceneTrack& Track, FGuid ObjectBinding) override;
	virtual bool SupportsType(TSubclassOf<UMovieSceneTrack> Type) const override;
	virtual bool SupportsSequence(UMovieSceneSequence* InSequence) const override;

private:
	void AddDreamTrack(UClass* TrackClass);
	void AddSection(UMovieSceneTrack* Track, UClass* SectionClass, int32 RowIndex);
};
