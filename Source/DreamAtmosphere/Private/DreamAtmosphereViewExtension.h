// Copyright DreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "DreamAtmosphereAerial.h"
#include "SceneViewExtension.h"

/**
 * Lays the aerial perspective over the scene, in the post processing before the depth of field. (No extension hook comes
 * between the lighting and the translucency, where the engine's own fog goes: near translucency takes the air of what
 * stands behind it, which is little that near.) Runs before the sky's own passes there (its light shafts are in the air,
 * in front of the scene).
 */
class FDreamAtmosphereViewExtension : public FWorldSceneViewExtension
{
public:
	FDreamAtmosphereViewExtension(const FAutoRegister& AutoRegister, UWorld* InWorld);

	//~ ISceneViewExtension
	virtual void SetupViewFamily(FSceneViewFamily& InViewFamily) override {}
	virtual void SetupView(FSceneViewFamily& InViewFamily, FSceneView& InView) override {}
	virtual void BeginRenderViewFamily(FSceneViewFamily& InViewFamily) override {}
	virtual int32 GetPriority() const override { return 10; }
	virtual void SubscribeToPostProcessingPass(EPostProcessingPass Pass, const FSceneView& InView, FPostProcessingPassDelegateArray& InOutPassCallbacks, bool bIsPassEnabled) override;

	/** The next frames' air, lights and volume (from the game thread). */
	void SetFrame_GameThread(const DreamAtmosphere::Aerial::FFrame& InFrame);

private:
	FScreenPassTexture Draw(FRDGBuilder& GraphBuilder, const FSceneView& InView, const FPostProcessMaterialInputs& Inputs);

	/** Render thread. */
	DreamAtmosphere::Aerial::FFrame Frame;
};
