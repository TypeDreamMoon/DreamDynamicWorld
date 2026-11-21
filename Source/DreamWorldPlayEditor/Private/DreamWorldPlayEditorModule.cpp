// Copyright DreamMoon. All Rights Reserved.

#include "DreamWorldTrackEditors.h"

#include "ISequencerModule.h"
#include "Modules/ModuleManager.h"

class FDreamWorldPlayEditorModule : public IModuleInterface
{
public:
	virtual void StartupModule() override
	{
		ISequencerModule& Sequencer = FModuleManager::LoadModuleChecked<ISequencerModule>(TEXT("Sequencer"));
		TrackEditor = Sequencer.RegisterTrackEditor(FOnCreateTrackEditor::CreateStatic(&FDreamWorldTrackEditor::Create));
	}

	virtual void ShutdownModule() override
	{
		if (ISequencerModule* Sequencer = FModuleManager::GetModulePtr<ISequencerModule>(TEXT("Sequencer")))
		{
			Sequencer->UnRegisterTrackEditor(TrackEditor);
		}
	}

private:
	FDelegateHandle TrackEditor;
};

IMPLEMENT_MODULE(FDreamWorldPlayEditorModule, DreamWorldPlayEditor)
