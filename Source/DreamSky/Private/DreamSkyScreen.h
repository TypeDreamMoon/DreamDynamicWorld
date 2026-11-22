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
	 * What the game thread decided for the frame's lens and the air before it (UDreamSkySubsystem::GetScreenWetness,
	 * GetScreenFrost and GetHeatHaze, the style's ScreenDroplets, ScreenFrost and HeatHaze).
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
		/** The hot air's shimmer: x strength, y the band round the horizon (sine), z/w the distance (cm) it starts / is full. */
		FVector4f Haze = FVector4f(0.0f, 0.05f, 5000.0f, 60000.0f);
		/** x how fast it shimmers. */
		FVector4f HazeMotion = FVector4f(1.0f, 0.0f, 0.0f, 0.0f);
	};

	/** Draws the hot air's shimmer, the drops and the frost over the finished picture (DreamSkyScreen.usf) into the pass's output. */
	FScreenPassTexture AddPasses(FRDGBuilder& GraphBuilder, const FSceneView& View, const FPostProcessMaterialInputs& Inputs, const FSettings& Settings);
}
