// Copyright DreamMoon. All Rights Reserved.

#include "DreamWorldEditorModule.h"
#include "DreamWorldEditorUI.h"
#include "DreamSky.h"
#include "DreamSkySubsystem.h"
#include "DreamWorldSubsystem.h"

#include "Editor.h"
#include "Framework/Application/SlateApplication.h"
#include "Framework/Docking/TabManager.h"
#include "Styling/AppStyle.h"
#include "Framework/Notifications/NotificationManager.h"
#include "ToolMenus.h"
#include "Widgets/Docking/SDockTab.h"
#include "Widgets/Notifications/SNotificationList.h"
#include "Widgets/Input/SSlider.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "DreamWorldEditor"

namespace DreamWorld::EditorUI
{
	UWorld* PreviewWorld()
	{
		if (!GEditor)
		{
			return nullptr;
		}
		if (GEditor->PlayWorld)
		{
			return GEditor->PlayWorld;
		}
		return GEditor->GetEditorWorldContext().World();
	}

	UWorld* EditedWorld()
	{
		return GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	}

	void Notify(const FText& Message, bool bSuccess)
	{
		FNotificationInfo Info(Message);
		Info.ExpireDuration = 4.0f;
		if (TSharedPtr<SNotificationItem> Item = FSlateNotificationManager::Get().AddNotification(Info))
		{
			Item->SetCompletionState(bSuccess ? SNotificationItem::CS_Success : SNotificationItem::CS_Fail);
		}
	}

	/** The level's sky, when editing (scrubbing then also sets the level's start hour, undoably). */
	static ADreamSky* EditedSky(UWorld* World)
	{
		if (!World || World->IsGameWorld())
		{
			return nullptr;
		}
		const UDreamSkySubsystem* Sky = World->GetSubsystem<UDreamSkySubsystem>();
		return Sky ? Sky->GetSky() : nullptr;
	}

	static TOptional<float> CurrentHours()
	{
		const UWorld* World = PreviewWorld();
		const UDreamWorldSubsystem* Clock = World ? World->GetSubsystem<UDreamWorldSubsystem>() : nullptr;
		return Clock ? TOptional<float>(Clock->GetTimeOfDay()) : TOptional<float>();
	}

	void SetHours(float Hours)
	{
		UWorld* World = PreviewWorld();
		if (ADreamSky* Sky = EditedSky(World))
		{
			Sky->SetTimeOfDayAndClock(Hours, false);
		}
		else if (UDreamWorldSubsystem* Clock = World ? World->GetSubsystem<UDreamWorldSubsystem>() : nullptr)
		{
			Clock->SetTimeOfDay(Hours);
		}
		GEditor->RedrawLevelEditingViewports();
	}

	TSharedRef<SWidget> MakeTimeWidget()
	{
		return SNew(SHorizontalBox)
			.ToolTipText(LOCTEXT("TimeTip", "Dream Dynamic World: time of day. Drag to scrub the sky; in the editor this also sets the level's start hour (undoable)."))
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.Padding(4.0f, 0.0f)
			[
				SNew(STextBlock)
				.MinDesiredWidth(36.0f)
				.Text_Lambda([]
				{
					const TOptional<float> Hours = CurrentHours();
					if (!Hours.IsSet())
					{
						return LOCTEXT("NoClock", "--:--");
					}
					const int32 Minutes = FMath::FloorToInt(Hours.GetValue() * 60.0f);
					return FText::FromString(FString::Printf(TEXT("%02d:%02d"), Minutes / 60, Minutes % 60));
				})
			]
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			[
				SNew(SBox)
				.WidthOverride(160.0f)
				[
					SNew(SSlider)
					.MinValue(0.0f)
					.MaxValue(23.99f)
					.Value_Lambda([] { return CurrentHours().Get(0.0f); })
					.OnMouseCaptureBegin_Lambda([]
					{
						if (ADreamSky* Sky = EditedSky(PreviewWorld()))
						{
							GEditor->BeginTransaction(LOCTEXT("ScrubTime", "Scrub Time of Day"));
							Sky->Modify();
						}
					})
					.OnMouseCaptureEnd_Lambda([]
					{
						if (GEditor->IsTransactionActive())
						{
							GEditor->EndTransaction();
						}
					})
					.OnValueChanged_Lambda([](float Hours) { SetHours(Hours); })
				]
			];
	}
}

void FDreamWorldEditorModule::StartupModule()
{
	UToolMenus::RegisterStartupCallback(FSimpleMulticastDelegate::FDelegate::CreateRaw(this, &FDreamWorldEditorModule::RegisterMenus));
	FGlobalTabmanager::Get()->RegisterNomadTabSpawner(DreamWorld::EditorUI::CloudPaintTabName, FOnSpawnTab::CreateStatic(&DreamWorld::EditorUI::SpawnCloudPaintTab))
		.SetDisplayName(LOCTEXT("CloudPaintTab", "Dream Cloud Paint"))
		.SetTooltipText(LOCTEXT("CloudPaintTabTip", "Paint the cloud cover by hand (Dream World > Paint Cloud Cover)."))
		.SetMenuType(ETabSpawnerMenuType::Hidden);
}

void FDreamWorldEditorModule::ShutdownModule()
{
	if (FSlateApplication::IsInitialized())
	{
		FGlobalTabmanager::Get()->UnregisterNomadTabSpawner(DreamWorld::EditorUI::CloudPaintTabName);
	}
	if (UObjectInitialized())
	{
		UToolMenus::UnRegisterStartupCallback(this);
		UToolMenus::UnregisterOwner(this);
	}
}

void FDreamWorldEditorModule::RegisterMenus()
{
	FToolMenuOwnerScoped OwnerScoped(this);
	UToolMenu* Toolbar = UToolMenus::Get()->ExtendMenu("LevelEditor.LevelEditorToolBar.User");
	FToolMenuSection& Section = Toolbar->FindOrAddSection("DreamWorld");
	Section.AddEntry(FToolMenuEntry::InitWidget("DreamWorldTime", DreamWorld::EditorUI::MakeTimeWidget(), LOCTEXT("TimeLabel", "Time of Day"), true));
	Section.AddEntry(FToolMenuEntry::InitComboButton("DreamWorldMenu", FUIAction(), FNewToolMenuDelegate::CreateStatic(&DreamWorld::EditorUI::FillMenu),
		LOCTEXT("MenuLabel", "Dream World"), LOCTEXT("MenuTip", "Dream Dynamic World: time, weather, the sky's look, quality, debug views, cloud painting, configs."),
		FSlateIcon(FAppStyle::GetAppStyleSetName(), "LevelEditor.Tabs.EnvironmentLightingViewer")));
}

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FDreamWorldEditorModule, DreamWorldEditor)
