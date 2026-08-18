// Copyright DreamMoon. All Rights Reserved.

#include "DreamWindViewExtension.h"
#include "DreamWindSubsystem.h"
#include "SceneView.h"

FDreamWindViewExtension::FDreamWindViewExtension(const FAutoRegister& AutoRegister, UWorld* InWorld, UDreamWindSubsystem* InSubsystem)
	: FWorldSceneViewExtension(AutoRegister, InWorld)
	, Subsystem(InSubsystem)
{
}

void FDreamWindViewExtension::SetupView(FSceneViewFamily& InViewFamily, FSceneView& InView)
{
	// Game thread. Orthographic editor views would drag the volume window off to infinity, skip them.
	if (!InView.IsPerspectiveProjection())
	{
		return;
	}
	if (UDreamWindSubsystem* Wind = Subsystem.Get())
	{
		Wind->ReportViewOrigin(InView.ViewLocation, InView.bIsGameView);
	}
}
