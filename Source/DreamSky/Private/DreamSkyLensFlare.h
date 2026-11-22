// Copyright DreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "ScreenPass.h"

class FRDGBuilder;
class FSceneView;

namespace DreamSky::LensFlare
{
	/** What the game thread decided for the frame's flare (FDreamSkyLensFlare, the look's scale, the console's). */
	struct FSettings
	{
		bool bEnabled = false;
		/** Toward the sun; w = the drawn disk's angular radius (rad). */
		FVector4f SunDirection = FVector4f(0.0f, 0.0f, 1.0f, 0.01f);
		/** x intensity, y threshold, z preset (EDreamSkyLensFlarePreset). */
		FVector4f Flare = FVector4f(0.0f, 1.0f, 0.0f, 0.0f);
		FVector4f Tint = FVector4f(1.0f, 1.0f, 1.0f, 1.0f);
	};

	/** Measures the sun on screen in SceneColor and adds its flare over it (DreamSkyLensFlare.usf). */
	void AddPasses(FRDGBuilder& GraphBuilder, const FSceneView& View, const FScreenPassTexture& SceneColor, const FSettings& Settings);
}
