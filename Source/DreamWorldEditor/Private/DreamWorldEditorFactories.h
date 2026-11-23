// Copyright DreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Factories/Factory.h"
#include "DreamWorldEditorFactories.generated.h"

/** Content Browser > Dream World > Cloud Paint: a blank painting (256 x 256). */
UCLASS(hidecategories = Object)
class UDreamCloudPaintFactory : public UFactory
{
	GENERATED_BODY()

public:
	UDreamCloudPaintFactory();
	virtual UObject* FactoryCreateNew(UClass* InClass, UObject* InParent, FName InName, EObjectFlags Flags, UObject* Context, FFeedbackContext* Warn) override;
	virtual FText GetDisplayName() const override;
	virtual uint32 GetMenuCategories() const override;
	virtual bool ShouldShowInNewMenu() const override { return true; }
};

/** Content Browser > Dream World > World Config: a config holding what is in effect now (UDreamWorldConfig::Capture). */
UCLASS(hidecategories = Object)
class UDreamWorldConfigFactory : public UFactory
{
	GENERATED_BODY()

public:
	UDreamWorldConfigFactory();
	virtual UObject* FactoryCreateNew(UClass* InClass, UObject* InParent, FName InName, EObjectFlags Flags, UObject* Context, FFeedbackContext* Warn) override;
	virtual FText GetDisplayName() const override;
	virtual uint32 GetMenuCategories() const override;
	virtual bool ShouldShowInNewMenu() const override { return true; }
};
