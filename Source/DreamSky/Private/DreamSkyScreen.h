// Copyright DreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "ScreenPass.h"

class FRDGBuilder;
class FSceneView;
struct FPostProcessMaterialInputs;

namespace DreamSky::Screen
{
	/** What the game thread decided for the frame's lens (UDreamSkySubsystem::GetScreenWetness, the style's ScreenDroplets). */
	struct FSettings
	{
		bool bEnabled = false;
		/** x wetness 0..1, y intensity, z drop size, w how far the drops bend the picture. */
		FVector4f Screen = FVector4f(0.0f, 1.0f, 1.0f, 1.0f);
	};

	/** Draws the drops over the finished picture (DreamSkyScreen.usf) into the pass's output. */
	FScreenPassTexture AddPasses(FRDGBuilder& GraphBuilder, const FSceneView& View, const FPostProcessMaterialInputs& Inputs, const FSettings& Settings);
}
