// Copyright DreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "DreamWindDistanceProbe.h"
#include "SceneViewExtension.h"

class FDreamWindFluid;
class UDreamWindSubsystem;

/**
 * Tells the subsystem where the views of its world are, so the dynamic volume can follow the camera in game AND in the
 * editor viewport without a dependency on UnrealEd. Also where the wind reads what only a view has, in the view's
 * post-processing where the renderer has updated it: the global distance field, for the fluid's walls and for a slice
 * of it (DistanceProbe).
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

	/** Game thread: the wind fluid whose walls the views look up in the global distance field (null: none). */
	void SetFluid(const TSharedPtr<FDreamWindFluid, ESPMode::ThreadSafe>& InFluid);

private:
	TWeakObjectPtr<UDreamWindSubsystem> Subsystem;
	/** Render thread. */
	TSharedPtr<FDreamWindFluid, ESPMode::ThreadSafe> Fluid;
	/** Render thread. */
	TOptional<DreamWind::DistanceField::FProbeRequest> PendingProbe;
	std::atomic<uint32> ProbesWritten{ 0 };
};
