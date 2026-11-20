// Copyright DreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "SceneViewExtension.h"

class UDreamSkySubsystem;

/**
 * The sky cloud renderer's hook into a world's views: right after the base pass (before lighting, fog and
 * translucency) it writes into the scene colour's sky pixels -- the far plane and whatever lies beyond the dome's
 * depth -- so the engine's fog, aerial perspective and translucency land on top of what it drew.
 */
class FDreamSkyCloudViewExtension : public FWorldSceneViewExtension
{
public:
	FDreamSkyCloudViewExtension(const FAutoRegister& AutoRegister, UWorld* InWorld, UDreamSkySubsystem* InSubsystem);

	//~ ISceneViewExtension
	virtual void SetupViewFamily(FSceneViewFamily& InViewFamily) override;
	virtual void SetupView(FSceneViewFamily& InViewFamily, FSceneView& InView) override {}
	virtual void BeginRenderViewFamily(FSceneViewFamily& InViewFamily) override {}
	virtual void PostRenderBasePassDeferred_RenderThread(FRDGBuilder& GraphBuilder, FSceneView& InView, const FRenderTargetBindingSlots& RenderTargets, TRDGUniformBufferRef<FSceneTextureUniformParameters> SceneTextures) override;

	void Detach() { Subsystem = nullptr; }

private:
	/** What the game thread decided for the frame; copied to the render thread ahead of the family's rendering. */
	struct FFrameSettings
	{
		/** View depth (cm) beyond which a pixel counts as sky; the far plane always does. */
		float SkyDepth = 1.0e9f;
		int32 DebugMode = 0;
	};

	TWeakObjectPtr<UDreamSkySubsystem> Subsystem;
	FFrameSettings RenderSettings;
};
