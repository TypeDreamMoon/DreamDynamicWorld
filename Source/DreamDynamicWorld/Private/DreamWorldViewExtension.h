// Copyright DreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "SceneViewExtension.h"

class UDreamWorldSubsystem;

/**
 * Tells the world subsystem where the views of its world are (game camera or editor viewport) without a
 * dependency on UnrealEd. Nothing is rendered here.
 */
class FDreamWorldViewExtension : public FWorldSceneViewExtension
{
public:
	FDreamWorldViewExtension(const FAutoRegister& AutoRegister, UWorld* InWorld, UDreamWorldSubsystem* InSubsystem);

	//~ ISceneViewExtension
	virtual void SetupViewFamily(FSceneViewFamily& InViewFamily) override {}
	virtual void SetupView(FSceneViewFamily& InViewFamily, FSceneView& InView) override;
	virtual void BeginRenderViewFamily(FSceneViewFamily& InViewFamily) override {}

	void Detach() { Subsystem = nullptr; }

private:
	TWeakObjectPtr<UDreamWorldSubsystem> Subsystem;
};
