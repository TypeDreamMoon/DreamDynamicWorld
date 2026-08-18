// Copyright DreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "SceneViewExtension.h"

class UDreamWindSubsystem;

/**
 * Tells the subsystem where the views of its world are, so the dynamic volume can follow the camera
 * in game AND in the editor viewport without a dependency on UnrealEd. Nothing is rendered here.
 */
class FDreamWindViewExtension : public FWorldSceneViewExtension
{
public:
	FDreamWindViewExtension(const FAutoRegister& AutoRegister, UWorld* InWorld, UDreamWindSubsystem* InSubsystem);

	//~ ISceneViewExtension
	virtual void SetupViewFamily(FSceneViewFamily& InViewFamily) override {}
	virtual void SetupView(FSceneViewFamily& InViewFamily, FSceneView& InView) override;
	virtual void BeginRenderViewFamily(FSceneViewFamily& InViewFamily) override {}

	void Detach() { Subsystem = nullptr; }

private:
	TWeakObjectPtr<UDreamWindSubsystem> Subsystem;
};
