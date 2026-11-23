// Copyright DreamMoon. All Rights Reserved.

#include "DreamWorldEditorFactories.h"
#include "DreamWorldEditorUI.h"
#include "DreamWeatherCloudPaint.h"
#include "DreamWorldConfig.h"

#include "AssetTypeCategories.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(DreamWorldEditorFactories)

#define LOCTEXT_NAMESPACE "DreamWorldEditor"

UDreamCloudPaintFactory::UDreamCloudPaintFactory()
{
	SupportedClass = UDreamCloudPainting::StaticClass();
	bCreateNew = true;
	bEditAfterNew = false;
}

UObject* UDreamCloudPaintFactory::FactoryCreateNew(UClass* InClass, UObject* InParent, FName InName, EObjectFlags Flags, UObject* Context, FFeedbackContext* Warn)
{
	UDreamCloudPainting* Paint = NewObject<UDreamCloudPainting>(InParent, InClass, InName, Flags | RF_Transactional);
	Paint->Initialize(256);
	return Paint;
}

FText UDreamCloudPaintFactory::GetDisplayName() const
{
	return LOCTEXT("CloudPaintFactory", "Dream Cloud Painting");
}

uint32 UDreamCloudPaintFactory::GetMenuCategories() const
{
	return EAssetTypeCategories::Misc;
}

UDreamWorldConfigFactory::UDreamWorldConfigFactory()
{
	SupportedClass = UDreamWorldConfig::StaticClass();
	bCreateNew = true;
	bEditAfterNew = true;
}

UObject* UDreamWorldConfigFactory::FactoryCreateNew(UClass* InClass, UObject* InParent, FName InName, EObjectFlags Flags, UObject* Context, FFeedbackContext* Warn)
{
	UDreamWorldConfig* Config = NewObject<UDreamWorldConfig>(InParent, InClass, InName, Flags | RF_Transactional);
	Config->Capture(DreamWorld::EditorUI::PreviewWorld());
	return Config;
}

FText UDreamWorldConfigFactory::GetDisplayName() const
{
	return LOCTEXT("ConfigFactory", "Dream World Config");
}

uint32 UDreamWorldConfigFactory::GetMenuCategories() const
{
	return EAssetTypeCategories::Misc;
}

#undef LOCTEXT_NAMESPACE
