// Copyright DreamMoon. All Rights Reserved.

#include "DreamWorldTrackEditors.h"
#include "DreamWeatherPreset.h"
#include "DreamWorldTracks.h"

#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "ISequencer.h"
#include "ISequencerSection.h"
#include "MovieScene.h"
#include "MovieSceneSequence.h"
#include "MVVM/Views/ViewUtilities.h"
#include "ScopedTransaction.h"
#include "SequencerSectionPainter.h"
#include "Styling/AppStyle.h"

#define LOCTEXT_NAMESPACE "DreamWorldTrackEditor"

namespace DreamWorldPlay
{
	/** A section as a coloured bar with its title: the preset, "Channels" or "Lightning", or the time span. */
	class FDreamWorldSection : public ISequencerSection, public TSharedFromThis<FDreamWorldSection>
	{
	public:
		explicit FDreamWorldSection(UMovieSceneSection& InSection) : Section(&InSection) {}

		virtual UMovieSceneSection* GetSectionObject() override { return Section.Get(); }

		virtual FText GetSectionTitle() const override
		{
			if (const UDreamWeatherPresetSection* Preset = Cast<UDreamWeatherPresetSection>(Section.Get()))
			{
				return Preset->Preset ? FText::FromString(Preset->Preset->GetName()) : LOCTEXT("NoPreset", "(no preset)");
			}
			if (Section.IsValid() && Section->IsA<UDreamWeatherChannelSection>())
			{
				return LOCTEXT("Channels", "Channels");
			}
			if (Section.IsValid() && Section->IsA<UDreamLightningSection>())
			{
				return LOCTEXT("Lightning", "Lightning");
			}
			return LOCTEXT("Time", "Time of day");
		}

		virtual int32 OnPaintSection(FSequencerSectionPainter& Painter) const override
		{
			return Painter.PaintSectionBackground();
		}

	private:
		TWeakObjectPtr<UMovieSceneSection> Section;
	};
}

FDreamWorldTrackEditor::FDreamWorldTrackEditor(TSharedRef<ISequencer> InSequencer)
	: FMovieSceneTrackEditor(InSequencer)
{
}

TSharedRef<ISequencerTrackEditor> FDreamWorldTrackEditor::Create(TSharedRef<ISequencer> InSequencer)
{
	return MakeShared<FDreamWorldTrackEditor>(InSequencer);
}

FText FDreamWorldTrackEditor::GetDisplayName() const
{
	return LOCTEXT("DisplayName", "Dream World");
}

void FDreamWorldTrackEditor::BuildAddTrackMenu(FMenuBuilder& MenuBuilder)
{
	MenuBuilder.AddMenuEntry(LOCTEXT("AddTime", "Time of Day Track"), LOCTEXT("AddTimeTip", "Drives the world clock: drag the curve to set the hour."),
		FSlateIcon(), FUIAction(FExecuteAction::CreateRaw(this, &FDreamWorldTrackEditor::AddDreamTrack, UDreamWorldTimeTrack::StaticClass())));
	MenuBuilder.AddMenuEntry(LOCTEXT("AddWeather", "Weather Track"), LOCTEXT("AddWeatherTip", "Forces weather presets, shapes weather channels with curves, strikes lightning on keys."),
		FSlateIcon(), FUIAction(FExecuteAction::CreateRaw(this, &FDreamWorldTrackEditor::AddDreamTrack, UDreamWeatherTrack::StaticClass())));
}

TSharedPtr<SWidget> FDreamWorldTrackEditor::BuildOutlinerEditWidget(const FGuid& ObjectBinding, UMovieSceneTrack* Track, const FBuildEditWidgetParams& Params)
{
	const int32 Row = Params.TrackInsertRowIndex;
	TWeakObjectPtr<UMovieSceneTrack> WeakTrack = Track;
	if (Track && Track->IsA<UDreamWorldTimeTrack>())
	{
		return UE::Sequencer::MakeAddButton(LOCTEXT("Section", "Section"), FOnClicked::CreateLambda([this, WeakTrack, Row]()
		{
			AddSection(WeakTrack.Get(), UDreamWorldTimeSection::StaticClass(), Row);
			return FReply::Handled();
		}), Params.ViewModel);
	}
	return UE::Sequencer::MakeAddButton(LOCTEXT("Section", "Section"), FOnGetContent::CreateLambda([this, WeakTrack, Row]()
	{
		FMenuBuilder Menu(true, nullptr);
		const TPair<FText, UClass*> Kinds[] = {
			{ LOCTEXT("PresetSection", "Weather Preset"), UDreamWeatherPresetSection::StaticClass() },
			{ LOCTEXT("ChannelSection", "Weather Channels (curves)"), UDreamWeatherChannelSection::StaticClass() },
			{ LOCTEXT("LightningSection", "Lightning (keys)"), UDreamLightningSection::StaticClass() } };
		for (const TPair<FText, UClass*>& Kind : Kinds)
		{
			UClass* Class = Kind.Value;
			Menu.AddMenuEntry(Kind.Key, FText(), FSlateIcon(), FUIAction(FExecuteAction::CreateLambda([this, WeakTrack, Row, Class]()
			{
				AddSection(WeakTrack.Get(), Class, Row);
			})));
		}
		return Menu.MakeWidget();
	}), Params.ViewModel);
}

TSharedRef<ISequencerSection> FDreamWorldTrackEditor::MakeSectionInterface(UMovieSceneSection& SectionObject, UMovieSceneTrack& Track, FGuid ObjectBinding)
{
	return MakeShared<DreamWorldPlay::FDreamWorldSection>(SectionObject);
}

bool FDreamWorldTrackEditor::SupportsType(TSubclassOf<UMovieSceneTrack> Type) const
{
	return Type == UDreamWorldTimeTrack::StaticClass() || Type == UDreamWeatherTrack::StaticClass();
}

bool FDreamWorldTrackEditor::SupportsSequence(UMovieSceneSequence* InSequence) const
{
	return InSequence && InSequence->IsTrackSupported(UDreamWeatherTrack::StaticClass()) == ETrackSupport::Supported;
}

void FDreamWorldTrackEditor::AddDreamTrack(UClass* TrackClass)
{
	UMovieScene* MovieScene = GetFocusedMovieScene();
	if (!MovieScene || MovieScene->IsReadOnly())
	{
		return;
	}
	const FScopedTransaction Transaction(LOCTEXT("AddTrackTransaction", "Add Dream World Track"));
	MovieScene->Modify();
	UMovieSceneTrack* Track = MovieScene->AddTrack(TrackClass);
	if (!Track)
	{
		return;
	}
	AddSection(Track, TrackClass == UDreamWorldTimeTrack::StaticClass() ? UDreamWorldTimeSection::StaticClass() : UDreamWeatherPresetSection::StaticClass(), 0);
	if (GetSequencer().IsValid())
	{
		GetSequencer()->OnAddTrack(Track, FGuid());
	}
}

void FDreamWorldTrackEditor::AddSection(UMovieSceneTrack* Track, UClass* SectionClass, int32 RowIndex)
{
	TSharedPtr<ISequencer> Sequencer = GetSequencer();
	if (!Track || !SectionClass || !Sequencer.IsValid())
	{
		return;
	}
	const FScopedTransaction Transaction(LOCTEXT("AddSectionTransaction", "Add Section"));
	Track->Modify();
	UMovieSceneSection* Section = NewObject<UMovieSceneSection>(Track, SectionClass, NAME_None, RF_Transactional);
	UMovieScene* MovieScene = Track->GetTypedOuter<UMovieScene>();
	if (MovieScene && !Section->IsA<UDreamLightningSection>())
	{
		Section->SetRange(MovieScene->GetPlaybackRange());
	}
	Section->SetRowIndex(RowIndex);
	if (UDreamWorldTimeSection* Time = Cast<UDreamWorldTimeSection>(Section))
	{
		// A day from dawn to dusk over the playback range, to edit from.
		const TRange<FFrameNumber> Range = Section->GetRange();
		if (Range.HasLowerBound() && Range.HasUpperBound())
		{
			Time->Hours.AddCubicKey(Range.GetLowerBoundValue(), 6.0f);
			Time->Hours.AddCubicKey(Range.GetUpperBoundValue(), 20.0f);
		}
	}
	Track->AddSection(*Section);
	Sequencer->NotifyMovieSceneDataChanged(EMovieSceneDataChangeType::MovieSceneStructureItemAdded);
}

#undef LOCTEXT_NAMESPACE
