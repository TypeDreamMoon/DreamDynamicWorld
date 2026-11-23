// Copyright DreamMoon. All Rights Reserved.
//
// The level editor toolbar's Dream World menu: time, weather, the sky's look, quality, debug views, the wind's
// editor actions, cloud painting, configs, the demo maps and the project settings.

#include "DreamWorldEditorUI.h"
#include "DreamWorldEditorFactories.h"
#include "DreamWeatherPreset.h"
#include "DreamWeatherSubsystem.h"
#include "DreamWindEditorLibrary.h"
#include "DreamWindGlobalActor.h"
#include "DreamWindSubsystem.h"
#include "DreamWorldConfig.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetToolsModule.h"
#include "Editor.h"
#include "EditorViewportClient.h"
#include "Engine/DeveloperSettings.h"
#include "EngineUtils.h"
#include "FileHelpers.h"
#include "Framework/Docking/TabManager.h"
#include "HAL/IConsoleManager.h"
#include "ISettingsModule.h"
#include "LevelEditorViewport.h"
#include "Misc/PackageName.h"
#include "ToolMenus.h"

#define LOCTEXT_NAMESPACE "DreamWorldEditor"

namespace DreamWorldEditorMenuImpl
{
	using namespace DreamWorld::EditorUI;

	static IConsoleVariable* Variable(const TCHAR* Name)
	{
		return IConsoleManager::Get().FindConsoleVariable(Name);
	}

	static FToolUIActionChoice RadioVariable(const TCHAR* Name, int32 Value)
	{
		const FString Key(Name);
		FUIAction Action(
			FExecuteAction::CreateLambda([Key, Value]
			{
				if (IConsoleVariable* V = Variable(*Key))
				{
					V->Set(Value, ECVF_SetByConsole);
					GEditor->RedrawLevelEditingViewports();
				}
			}),
			FCanExecuteAction(),
			FIsActionChecked::CreateLambda([Key, Value]
			{
				const IConsoleVariable* V = Variable(*Key);
				return V && V->GetInt() == Value;
			}));
		return FToolUIActionChoice(Action);
	}

	/** The plugin's debug views: its int console variables named ...Debug or ...DebugDraw (found by name, so ones added
	 *  later show up too). */
	static TArray<FString> DebugVariables()
	{
		TArray<FString> Out;
		for (const TCHAR* Prefix : UDreamWorldConfig::GetConsoleVariablePrefixes())
		{
			IConsoleManager::Get().ForEachConsoleObjectThatStartsWith(FConsoleObjectVisitor::CreateLambda([&Out](const TCHAR* Name, IConsoleObject* Object)
			{
				IConsoleVariable* V = Object ? Object->AsVariable() : nullptr;
				const FString N(Name);
				FString Last = N;
				int32 Dot;
				if (N.FindLastChar(TEXT('.'), Dot))
				{
					Last = N.Mid(Dot + 1);
				}
				if (V && (V->IsVariableInt() || V->IsVariableBool()) && (Last == TEXT("Debug") || Last == TEXT("DebugDraw")))
				{
					Out.AddUnique(N);
				}
			}), Prefix);
		}
		Out.Sort();
		return Out;
	}

	template <typename T>
	static TArray<FAssetData> AssetsOf()
	{
		TArray<FAssetData> Assets;
		FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry").Get().GetAssetsByClass(T::StaticClass()->GetClassPathName(), Assets, true);
		Assets.Sort([](const FAssetData& A, const FAssetData& B) { return A.AssetName.LexicalLess(B.AssetName); });
		return Assets;
	}

	static FVector ViewLocation()
	{
		if (GCurrentLevelEditingViewportClient)
		{
			return GCurrentLevelEditingViewportClient->GetViewLocation();
		}
		return FVector::ZeroVector;
	}

	static void FillTime(UToolMenu* Menu)
	{
		FToolMenuSection& Section = Menu->AddSection("Time", LOCTEXT("TimeSection", "Time of day"));
		struct FTime { const TCHAR* Name; FText Label; float Hours; };
		const FTime Times[] = {
			{ TEXT("Dawn"), LOCTEXT("Dawn", "Dawn (05:45)"), 5.75f },
			{ TEXT("Morning"), LOCTEXT("Morning", "Morning (08:30)"), 8.5f },
			{ TEXT("Noon"), LOCTEXT("Noon", "Noon (12:00)"), 12.0f },
			{ TEXT("Afternoon"), LOCTEXT("Afternoon", "Afternoon (15:30)"), 15.5f },
			{ TEXT("Dusk"), LOCTEXT("Dusk", "Dusk (18:40)"), 18.67f },
			{ TEXT("Night"), LOCTEXT("Night", "Night (23:00)"), 23.0f },
		};
		for (const FTime& Time : Times)
		{
			const float Hours = Time.Hours;
			Section.AddMenuEntry(Time.Name, Time.Label, FText::GetEmpty(), FSlateIcon(), FUIAction(FExecuteAction::CreateLambda([Hours] { SetHours(Hours); })));
		}
	}

	static void FillWeather(UToolMenu* Menu)
	{
		FToolMenuSection& Section = Menu->AddSection("Weather", LOCTEXT("WeatherSection", "Weather"));
		Section.AddMenuEntry("Schedule", LOCTEXT("Schedule", "Back to the Schedule"), LOCTEXT("ScheduleTip", "Clear a weather forced from here: the weather's schedule takes over."),
			FSlateIcon(), FUIAction(FExecuteAction::CreateLambda([]
			{
				if (UDreamWeatherSubsystem* Weather = UDreamWeatherSubsystem::Get(PreviewWorld()))
				{
					Weather->ClearForcedWeather(2.0f);
				}
			})));
		Section.AddSeparator(NAME_None);
		for (const FAssetData& Asset : AssetsOf<UDreamWeatherPreset>())
		{
			const FSoftObjectPath Path = Asset.GetSoftObjectPath();
			Section.AddMenuEntry(Asset.AssetName, FText::FromName(Asset.AssetName), FText::Format(LOCTEXT("ForceTip", "Force {0} (two seconds' transition)."),
				FText::FromString(Path.ToString())), FSlateIcon(), FUIAction(FExecuteAction::CreateLambda([Path]
			{
				UDreamWeatherSubsystem* Weather = UDreamWeatherSubsystem::Get(PreviewWorld());
				if (UDreamWeatherPreset* Preset = Weather ? Cast<UDreamWeatherPreset>(Path.TryLoad()) : nullptr)
				{
					Weather->ForceWeather(Preset, 2.0f);
				}
			})));
		}
	}

	static void FillLook(UToolMenu* Menu)
	{
		FToolMenuSection& Section = Menu->AddSection("Look", LOCTEXT("LookSection", "Clouds (DreamSky.CloudRender.Look)"));
		Section.AddMenuEntry("LookEach", LOCTEXT("LookEach", "Each Sky's Own"), FText::GetEmpty(), FSlateIcon(), RadioVariable(TEXT("DreamSky.CloudRender.Look"), -1), EUserInterfaceActionType::RadioButton);
		Section.AddMenuEntry("LookToon", LOCTEXT("LookToon", "Toon"), FText::GetEmpty(), FSlateIcon(), RadioVariable(TEXT("DreamSky.CloudRender.Look"), 0), EUserInterfaceActionType::RadioButton);
		Section.AddMenuEntry("LookReal", LOCTEXT("LookReal", "Realistic"), FText::GetEmpty(), FSlateIcon(), RadioVariable(TEXT("DreamSky.CloudRender.Look"), 1), EUserInterfaceActionType::RadioButton);
		Section.AddMenuEntry("LookPaint", LOCTEXT("LookPaint", "Painted"), FText::GetEmpty(), FSlateIcon(), RadioVariable(TEXT("DreamSky.CloudRender.Look"), 2), EUserInterfaceActionType::RadioButton);
	}

	static void FillQuality(UToolMenu* Menu)
	{
		FToolMenuSection& Section = Menu->AddSection("Quality", LOCTEXT("QualitySection", "Quality (r.DreamWorld.Quality)"));
		const FText Names[] = { LOCTEXT("QLow", "Low"), LOCTEXT("QMedium", "Medium"), LOCTEXT("QHigh", "High"), LOCTEXT("QEpic", "Epic"), LOCTEXT("QCinematic", "Cinematic") };
		Section.AddMenuEntry("QualityEngine", LOCTEXT("QEngine", "Follow Engine Scalability"), FText::GetEmpty(), FSlateIcon(), RadioVariable(TEXT("r.DreamWorld.Quality"), -1),
			EUserInterfaceActionType::RadioButton);
		for (int32 Level = 0; Level < 5; ++Level)
		{
			Section.AddMenuEntry(FName(*FString::Printf(TEXT("Quality%d"), Level)), Names[Level], FText::GetEmpty(), FSlateIcon(), RadioVariable(TEXT("r.DreamWorld.Quality"), Level),
				EUserInterfaceActionType::RadioButton);
		}
	}

	static void FillDebug(UToolMenu* Menu)
	{
		FToolMenuSection& Section = Menu->AddSection("Debug", LOCTEXT("DebugSection", "Debug views"));
		for (const FString& Name : DebugVariables())
		{
			FUIAction Action(
				FExecuteAction::CreateLambda([Name]
				{
					if (IConsoleVariable* V = Variable(*Name))
					{
						V->Set(V->GetInt() != 0 ? 0 : 1, ECVF_SetByConsole);
						GEditor->RedrawLevelEditingViewports();
					}
				}),
				FCanExecuteAction(),
				FIsActionChecked::CreateLambda([Name]
				{
					const IConsoleVariable* V = Variable(*Name);
					return V && V->GetInt() != 0;
				}));
			const IConsoleVariable* V = Variable(*Name);
			Section.AddMenuEntry(FName(*Name), FText::FromString(Name), V ? FText::FromString(V->GetHelp()) : FText::GetEmpty(), FSlateIcon(), Action,
				EUserInterfaceActionType::ToggleButton);
		}
	}

	static void FillWind(UToolMenu* Menu)
	{
		FToolMenuSection& Section = Menu->AddSection("Wind", LOCTEXT("WindSection", "Wind"));
		Section.AddMenuEntry("WindRecapture", LOCTEXT("WindRecapture", "Re-capture Obstacles Around the View"),
			LOCTEXT("WindRecaptureTip", "Traces the buildings and terrain again around the viewport (UDreamWindSubsystem::MarkObstaclesDirty), after moving them."),
			FSlateIcon(), FUIAction(FExecuteAction::CreateLambda([]
			{
				if (UDreamWindSubsystem* Wind = PreviewWorld() ? PreviewWorld()->GetSubsystem<UDreamWindSubsystem>() : nullptr)
				{
					Wind->MarkObstaclesDirty(FBox::BuildAABB(ViewLocation(), FVector(10000.0, 10000.0, 5000.0)));
					Notify(LOCTEXT("WindRecaptured", "The wind traces the obstacles around the view again."));
				}
			})));
		Section.AddMenuEntry("WindBake", LOCTEXT("WindBake", "Bake the Terrain Wind Map"),
			LOCTEXT("WindBakeTip", "Bakes the level's Dream Wind actor's terrain map (the low quality levels' terrain wind), over its TerrainBakeBounds."),
			FSlateIcon(), FUIAction(FExecuteAction::CreateLambda([]
			{
				UWorld* World = EditedWorld();
				ADreamWindGlobal* Actor = nullptr;
				if (World)
				{
					for (TActorIterator<ADreamWindGlobal> It(World); It; ++It)
					{
						Actor = *It;
						break;
					}
				}
				const bool bOk = Actor && UDreamWindEditorLibrary::BakeTerrainMapForActor(Actor);
				Notify(bOk ? LOCTEXT("WindBaked", "The terrain wind map is baked.") : LOCTEXT("WindNotBaked", "No terrain wind map baked: no Dream Wind actor in the level, or the bake failed (see the log)."), bOk);
			})));
	}

	static void FillConfigs(UToolMenu* Menu)
	{
		FToolMenuSection& Section = Menu->AddSection("Configs", LOCTEXT("ConfigsSection", "Configs"));
		Section.AddMenuEntry("ConfigSave", LOCTEXT("ConfigSave", "Save Current as New Config..."),
			LOCTEXT("ConfigSaveTip", "A Dream World Config asset holding every Dream settings object, the plugin's console variables set by hand, the time of day and the weather."),
			FSlateIcon(), FUIAction(FExecuteAction::CreateLambda([]
			{
				IAssetTools& Tools = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools").Get();
				Tools.CreateAssetWithDialog(TEXT("DWC_Config"), TEXT("/Game/DreamWorld/Configs"), UDreamWorldConfig::StaticClass(), NewObject<UDreamWorldConfigFactory>());
			})));
		UDreamWorldConfig* Last = UDreamWorldConfig::GetLastApplied();
		const FText LastName = Last ? FText::FromString(Last->GetName()) : LOCTEXT("NoConfig", "(none applied)");
		Section.AddMenuEntry("ConfigUpdate", FText::Format(LOCTEXT("ConfigUpdate", "Update {0} from Current"), LastName),
			LOCTEXT("ConfigUpdateTip", "Captures what is in effect now into the config applied last."), FSlateIcon(),
			FUIAction(FExecuteAction::CreateLambda([]
			{
				if (UDreamWorldConfig* Config = UDreamWorldConfig::GetLastApplied())
				{
					Config->Capture(PreviewWorld());
					Config->MarkPackageDirty();
					Notify(FText::Format(LOCTEXT("ConfigUpdated", "{0} holds what is in effect now (save it to keep it)."), FText::FromString(Config->GetName())));
				}
			}), FCanExecuteAction::CreateLambda([] { return UDreamWorldConfig::GetLastApplied() != nullptr; })));
		Section.AddMenuEntry("ConfigDiff", FText::Format(LOCTEXT("ConfigDiff", "Log Differences from {0}"), LastName),
			LOCTEXT("ConfigDiffTip", "Lists in the output log what differs between the config applied last and what is in effect now."), FSlateIcon(),
			FUIAction(FExecuteAction::CreateLambda([]
			{
				if (const UDreamWorldConfig* Config = UDreamWorldConfig::GetLastApplied())
				{
					const TArray<FString> Lines = Config->DescribeDifferences(PreviewWorld());
					for (const FString& Line : Lines)
					{
						UE_LOG(LogTemp, Display, TEXT("%s: %s"), *Config->GetName(), *Line);
					}
					Notify(FText::Format(LOCTEXT("ConfigDiffDone", "{0} differences from {1} (in the output log)."), FText::AsNumber(Lines.Num()), FText::FromString(Config->GetName())));
				}
			}), FCanExecuteAction::CreateLambda([] { return UDreamWorldConfig::GetLastApplied() != nullptr; })));
		Section.AddMenuEntry("ConfigWrite", LOCTEXT("ConfigWrite", "Write Current Settings to the Project"),
			LOCTEXT("ConfigWriteTip", "Writes every Dream settings object as it is now to the project's config files (DefaultGame.ini and the like)."), FSlateIcon(),
			FUIAction(FExecuteAction::CreateLambda([]
			{
				int32 Written = 0;
				for (UClass* Class : UDreamWorldConfig::GetSettingsClasses())
				{
					Written += Class->GetDefaultObject()->TryUpdateDefaultConfigFile() ? 1 : 0;
				}
				Notify(FText::Format(LOCTEXT("ConfigWritten", "{0} settings objects written to the project's config files."), FText::AsNumber(Written)));
			})));
		FToolMenuSection& Apply = Menu->AddSection("ConfigApply", LOCTEXT("ConfigApplySection", "Apply (this session)"));
		for (const FAssetData& Asset : AssetsOf<UDreamWorldConfig>())
		{
			const FSoftObjectPath Path = Asset.GetSoftObjectPath();
			Apply.AddMenuEntry(Asset.AssetName, FText::FromName(Asset.AssetName), FText::Format(LOCTEXT("ApplyTip", "Apply {0}. Write Current Settings to the Project keeps it."),
				FText::FromString(Path.ToString())), FSlateIcon(), FUIAction(FExecuteAction::CreateLambda([Path]
			{
				if (UDreamWorldConfig* Config = Cast<UDreamWorldConfig>(Path.TryLoad()))
				{
					const int32 Changed = Config->Apply(PreviewWorld(), false);
					GEditor->RedrawLevelEditingViewports();
					Notify(FText::Format(LOCTEXT("Applied", "{0} applied: {1} values changed."), FText::FromString(Config->GetName()), FText::AsNumber(Changed)));
				}
			})));
		}
	}

	static void FillMaps(UToolMenu* Menu)
	{
		FToolMenuSection& Section = Menu->AddSection("Maps", LOCTEXT("MapsSection", "Demo maps"));
		const TCHAR* const Maps[] = {
			TEXT("/DreamDynamicWorld/Showcase/Maps/Lvl_DreamWorldShowcase"), TEXT("/DreamDynamicWorld/Showcase/Maps/Lvl_DreamWorldShowcaseDusk"),
			TEXT("/DreamDynamicWorld/Play/Maps/Lvl_DreamWorldPlay"), TEXT("/DreamDynamicWorld/Sky/Maps/Lvl_DreamSkyTest"),
			TEXT("/DreamDynamicWorld/Surface/Maps/Lvl_DreamSurfaceTest"), TEXT("/DreamDynamicWorld/Wind/Maps/Lvl_DreamWindTest"),
			TEXT("/DreamDynamicWorld/Wind/Maps/Lvl_DreamWindFieldTest"), TEXT("/DreamDynamicWorld/Cloud/Maps/Lvl_DreamCloudVolumeTest"),
		};
		for (const TCHAR* Map : Maps)
		{
			const FString Package(Map);
			if (!FPackageName::DoesPackageExist(Package))
			{
				continue;
			}
			Section.AddMenuEntry(FName(Map), FText::FromString(FPackageName::GetShortName(Package)), FText::FromString(Package), FSlateIcon(),
				FUIAction(FExecuteAction::CreateLambda([Package]
				{
					if (FEditorFileUtils::SaveDirtyPackages(true, true, true))
					{
						FEditorFileUtils::LoadMap(FPackageName::LongPackageNameToFilename(Package, FPackageName::GetMapPackageExtension()), false, true);
					}
				})));
		}
	}

	static void FillSettings(UToolMenu* Menu)
	{
		FToolMenuSection& Section = Menu->AddSection("Settings", LOCTEXT("SettingsSection", "Project settings"));
		for (UClass* Class : UDreamWorldConfig::GetSettingsClasses())
		{
			const UDeveloperSettings* Settings = Class->GetDefaultObject<UDeveloperSettings>();
			const FName Container = Settings->GetContainerName();
			const FName Category = Settings->GetCategoryName();
			const FName SectionName = Settings->GetSectionName();
			Section.AddMenuEntry(Class->GetFName(), Settings->GetSectionText(), FText::GetEmpty(), FSlateIcon(), FUIAction(FExecuteAction::CreateLambda([Container, Category, SectionName]
			{
				if (ISettingsModule* Module = FModuleManager::GetModulePtr<ISettingsModule>("Settings"))
				{
					Module->ShowViewer(Container, Category, SectionName);
				}
			})));
		}
	}
}

void DreamWorld::EditorUI::FillMenu(UToolMenu* Menu)
{
	using namespace DreamWorldEditorMenuImpl;
	FToolMenuSection& World = Menu->AddSection("DreamWorldWorld", LOCTEXT("WorldSection", "World"));
	World.AddSubMenu("Time", LOCTEXT("TimeMenu", "Time of Day"), LOCTEXT("TimeMenuTip", "Jump to a time of day (the toolbar's slider scrubs)."), FNewToolMenuDelegate::CreateStatic(&FillTime));
	World.AddSubMenu("Weather", LOCTEXT("WeatherMenu", "Weather"), LOCTEXT("WeatherMenuTip", "Force a weather preset, or hand the weather back to its schedule."), FNewToolMenuDelegate::CreateStatic(&FillWeather));
	World.AddSubMenu("Look", LOCTEXT("LookMenu", "Cloud Look"), LOCTEXT("LookMenuTip", "How the sky's clouds are drawn, for every sky."), FNewToolMenuDelegate::CreateStatic(&FillLook));
	World.AddSubMenu("Quality", LOCTEXT("QualityMenu", "Quality"), LOCTEXT("QualityMenuTip", "Dream Dynamic World's quality level, all its groups."), FNewToolMenuDelegate::CreateStatic(&FillQuality));
	World.AddSubMenu("Debug", LOCTEXT("DebugMenu", "Debug Views"), LOCTEXT("DebugMenuTip", "The plugin's debug views (its ...Debug console variables)."), FNewToolMenuDelegate::CreateStatic(&FillDebug));

	FToolMenuSection& Tools = Menu->AddSection("DreamWorldTools", LOCTEXT("ToolsSection", "Tools"));
	Tools.AddMenuEntry("CloudPaint", LOCTEXT("CloudPaint", "Paint Cloud Cover..."), LOCTEXT("CloudPaintTip", "Paint where the clouds are, by hand, into the level's cloud painting."),
		FSlateIcon(), FUIAction(FExecuteAction::CreateLambda([] { FGlobalTabmanager::Get()->TryInvokeTab(CloudPaintTabName); })));
	Tools.AddSubMenu("Wind", LOCTEXT("WindMenu", "Wind"), LOCTEXT("WindMenuTip", "The wind's editor actions."), FNewToolMenuDelegate::CreateStatic(&FillWind));
	Tools.AddSubMenu("Configs", LOCTEXT("ConfigsMenu", "Configs"), LOCTEXT("ConfigsMenuTip", "Save and switch whole sets of Dream Dynamic World settings."), FNewToolMenuDelegate::CreateStatic(&FillConfigs));

	FToolMenuSection& Open = Menu->AddSection("DreamWorldOpen", LOCTEXT("OpenSection", "Open"));
	Open.AddSubMenu("Maps", LOCTEXT("MapsMenu", "Demo Maps"), LOCTEXT("MapsMenuTip", "The plugin's showcase and test maps."), FNewToolMenuDelegate::CreateStatic(&FillMaps));
	Open.AddSubMenu("Settings", LOCTEXT("SettingsMenu", "Project Settings"), LOCTEXT("SettingsMenuTip", "Each of the plugin's project settings pages."), FNewToolMenuDelegate::CreateStatic(&FillSettings));
}

#undef LOCTEXT_NAMESPACE
