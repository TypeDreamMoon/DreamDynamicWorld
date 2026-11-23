// Copyright DreamMoon. All Rights Reserved.

#include "DreamWorldEditorUI.h"
#include "DreamWorldEditorFactories.h"
#include "DreamWeatherCloudPaint.h"
#include "DreamWorldConfig.h"

#include "Misc/AutomationTest.h"
#include "ToolMenus.h"
#include "UObject/Package.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDreamWorldEditorToolsTest, "DreamWorld.EditorTools", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FDreamWorldEditorToolsTest::RunTest(const FString& Parameters)
{
	// The toolbar's menu: the world's entries, the tools, what it opens.
	UToolMenu* Menu = NewObject<UToolMenu>(GetTransientPackage());
	DreamWorld::EditorUI::FillMenu(Menu);
	auto Entries = [Menu](FName Section)
	{
		const FToolMenuSection* Found = Menu->FindSection(Section);
		return Found ? Found->Blocks.Num() : -1;
	};
	TestEqual(TEXT("the world's entries: time, weather, look, quality, debug views"), Entries(TEXT("DreamWorldWorld")), 5);
	TestEqual(TEXT("the tools: cloud painting, wind, configs"), Entries(TEXT("DreamWorldTools")), 3);
	TestEqual(TEXT("what it opens: the demo maps, the project settings"), Entries(TEXT("DreamWorldOpen")), 2);

	// The Content Browser's factories: a blank painting, a config holding what is in effect.
	UDreamCloudPaintFactory* PaintFactory = NewObject<UDreamCloudPaintFactory>();
	const UDreamCloudPainting* Painting = Cast<UDreamCloudPainting>(PaintFactory->FactoryCreateNew(UDreamCloudPainting::StaticClass(), GetTransientPackage(), NAME_None, RF_Transient, nullptr, GWarn));
	TestTrue(TEXT("a blank painting, 256 across"), Painting && Painting->IsValidPainting() && Painting->Resolution == 256);
	UDreamWorldConfigFactory* ConfigFactory = NewObject<UDreamWorldConfigFactory>();
	const UDreamWorldConfig* Config = Cast<UDreamWorldConfig>(ConfigFactory->FactoryCreateNew(UDreamWorldConfig::StaticClass(), GetTransientPackage(), NAME_None, RF_Transient, nullptr, GWarn));
	TestTrue(TEXT("a config holding every settings object"), Config && Config->Settings.Num() == UDreamWorldConfig::GetSettingsClasses().Num() && Config->Settings.Num() >= 7);
	return true;
}

#endif
