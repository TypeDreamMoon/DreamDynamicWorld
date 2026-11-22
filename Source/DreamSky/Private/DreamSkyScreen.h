// Copyright DreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "ScreenPass.h"

class FRDGBuilder;
class FSceneView;
struct FPostProcessMaterialInputs;

namespace DreamSky::Screen
{
	/**
	 * What the game thread decided for the frame's lens (UDreamSkySubsystem::GetScreenWetness and GetScreenFrost, the
	 * style's ScreenDroplets and ScreenFrost).
	 */
	struct FSettings
	{
		bool bEnabled = false;
		/** x wetness 0..1, y intensity, z drop size, w how far the drops bend the picture. */
		FVector4f Screen = FVector4f(0.0f, 1.0f, 1.0f, 1.0f);
		/** x how frosted 0..1, y intensity, z crystal size, w how far the frost blurs the picture. */
		FVector4f Frost = FVector4f(0.0f, 1.0f, 1.0f, 1.0f);
		/** rgb the frost's colour, w how far its crystals bend the picture. */
		FVector4f FrostTint = FVector4f(1.0f, 1.0f, 1.0f, 1.0f);
	};

	/** Draws the drops and the frost over the finished picture (DreamSkyScreen.usf) into the pass's output. */
	FScreenPassTexture AddPasses(FRDGBuilder& GraphBuilder, const FSceneView& View, const FPostProcessMaterialInputs& Inputs, const FSettings& Settings);
}
