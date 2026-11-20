// Copyright DreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

DECLARE_LOG_CATEGORY_EXTERN(LogDreamCloud, Log, All);

namespace DreamCloud::Names
{
	/** The material every cloud volume draws with (its parameters set per volume). */
	inline const TCHAR* VolumeMaterialPath = TEXT("/DreamDynamicWorld/Cloud/M_DreamCloudVolume.M_DreamCloudVolume");
	/** The 3D noise its edges are eroded with (shared with the sky's clouds). */
	inline const TCHAR* NoisePath = TEXT("/DreamDynamicWorld/Sky/T_DreamCloudNoise.T_DreamCloudNoise");
}
