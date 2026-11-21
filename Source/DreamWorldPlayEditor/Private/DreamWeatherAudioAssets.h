// Copyright DreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

namespace DreamWorldPlay
{
	/** /DreamDynamicWorld/Audio: the voice graph, the forest ambience graph and its preset, the two sound classes. */
	bool EnsureAudioAssets(bool bForce);

	/** Writes a package to its file. */
	bool SaveAsset(UObject* Asset);
}
