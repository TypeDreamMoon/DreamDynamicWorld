// Copyright DreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "DreamWindDistanceProbe.h"
#include "SceneViewExtension.h"

class UDreamWindSubsystem;

/**
 * Tells the subsystem where the views of its world are, so the dynamic volume can follow the camera in game AND in the
 * editor viewport without a dependency on UnrealEd. Also where the wind reads what only a view has: a slice of the
 * global distance field (DistanceProbe), in the view's post-processing, where the renderer has updated it.
 */
class FDreamWindViewExtension : public FWorldSceneViewExtension
{
public:
	FDreamWindViewExtension(const FAutoRegister& AutoRegister, UWorld* InWorld, UDreamWindSubsystem* InSubsystem);

	//~ ISceneViewExtension
	virtual void SetupViewFamily(FSceneViewFamily& InViewFamily) override {}
	virtual void SetupView(FSceneViewFamily& InViewFamily, FSceneView& InView) override;
	virtual void BeginRenderViewFamily(FSceneViewFamily& InViewFamily) override {}
	virtual void PrePostProcessPass_RenderThread(FRDGBuilder& GraphBuilder, const FSceneView& InView, const FPostProcessingInputs& Inputs) override;

	void Detach() { Subsystem = nullptr; }

	/** Game thread: write a slice of the global distance field in the next perspective view of the world. */
	void QueueDistanceProbe(const DreamWind::DistanceField::FProbeRequest& Request);

	/** How many probes have been written (any thread). */
	uint32 GetDistanceProbeCount() const { return ProbesWritten.load(); }

private:
	TWeakObjectPtr<UDreamWindSubsystem> Subsystem;
	/** Render thread. */
	TOptional<DreamWind::DistanceField::FProbeRequest> PendingProbe;
	std::atomic<uint32> ProbesWritten{ 0 };
};
