// Copyright DreamMoon. All Rights Reserved.

#include "DreamWindEditorLibrary.h"
#include "DreamWindModule.h"
#include "DreamWindSettings.h"

#include "DreamWindSetupCommandlet.h"
#include "Framework/Notifications/NotificationManager.h"
#include "ISettingsModule.h"
#include "Modules/ModuleManager.h"
#include "ToolMenus.h"
#include "Widgets/Notifications/SNotificationList.h"

#define LOCTEXT_NAMESPACE "DreamWindEditor"

class FDreamWindEditorModule : public IModuleInterface
{
public:
	virtual void StartupModule() override
	{
		UToolMenus::RegisterStartupCallback(FSimpleMulticastDelegate::FDelegate::CreateRaw(this, &FDreamWindEditorModule::RegisterMenus));
	}

	virtual void ShutdownModule() override
	{
		UToolMenus::UnRegisterStartupCallback(this);
		UToolMenus::UnregisterOwner(this);
	}

private:
	void RegisterMenus()
	{
		FToolMenuOwnerScoped OwnerScoped(this);
		UToolMenu* ToolsMenu = UToolMenus::Get()->ExtendMenu("LevelEditor.MainMenu.Tools");
		if (!ToolsMenu)
		{
			return;
		}
		FToolMenuSection& Section = ToolsMenu->FindOrAddSection("DreamWind", LOCTEXT("DreamWindSection", "DreamWind"));
		Section.AddMenuEntry(
			"DreamWindSetupAssets",
			LOCTEXT("SetupAssets", "DreamWind: Setup Assets"),
			LOCTEXT("SetupAssetsTooltip", "Create or repair the wind Material Parameter Collection and volume render target at the paths in Project Settings > Dream Wind."),
			FSlateIcon(),
			FUIAction(FExecuteAction::CreateLambda([]()
			{
				const bool bOk = UDreamWindEditorLibrary::EnsureAssets(true);
				FNotificationInfo Info(bOk ? LOCTEXT("SetupOk", "DreamWind assets are ready.") : LOCTEXT("SetupFail", "DreamWind asset setup failed - see the log."));
				Info.ExpireDuration = 4.0f;
				FSlateNotificationManager::Get().AddNotification(Info)->SetCompletionState(bOk ? SNotificationItem::CS_Success : SNotificationItem::CS_Fail);
			})));
		Section.AddMenuEntry(
			"DreamWindSettings",
			LOCTEXT("OpenSettings", "DreamWind: Project Settings"),
			LOCTEXT("OpenSettingsTooltip", "Open Project Settings > DreamPlugin > Dream Wind."),
			FSlateIcon(),
			FUIAction(FExecuteAction::CreateLambda([]()
			{
				if (ISettingsModule* SettingsModule = FModuleManager::GetModulePtr<ISettingsModule>("Settings"))
				{
					SettingsModule->ShowViewer("Project", "DreamPlugin", "Dream Wind");
				}
			})));
	}
};

IMPLEMENT_MODULE(FDreamWindEditorModule, DreamWindEditor)

#undef LOCTEXT_NAMESPACE
