// Copyright DreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

class SDockTab;
class SWidget;
class UToolMenu;
class UWorld;
class FSpawnTabArgs;

/** The level editor's Dream World tools: the toolbar's time slider and menu, the cloud painting tab. */
namespace DreamWorld::EditorUI
{
	/** The world the tools drive: the one being played in, else the level being edited. */
	UWorld* PreviewWorld();

	/** The level being edited (never a play world). */
	UWorld* EditedWorld();

	/** Sets the time of day (in the editor also the level's start hour, undoably). */
	void SetHours(float Hours);

	/** The toolbar's time of day slider. */
	TSharedRef<SWidget> MakeTimeWidget();

	/** The toolbar's Dream World menu. */
	void FillMenu(UToolMenu* Menu);

	/** The cloud painting tab. */
	extern const FName CloudPaintTabName;
	TSharedRef<SDockTab> SpawnCloudPaintTab(const FSpawnTabArgs& Args);
	/** The tab's content. */
	TSharedRef<SWidget> MakeCloudPaintPanel();

	/** Shows a short notification at the editor's corner. */
	void Notify(const FText& Message, bool bSuccess = true);
}
