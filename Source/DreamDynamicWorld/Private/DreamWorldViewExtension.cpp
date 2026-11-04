// Copyright DreamMoon. All Rights Reserved.

#include "DreamWorldViewExtension.h"
#include "DreamWorldSubsystem.h"
#include "SceneView.h"

FDreamWorldViewExtension::FDreamWorldViewExtension(const FAutoRegister& AutoRegister, UWorld* InWorld, UDreamWorldSubsystem* InSubsystem)
	: FWorldSceneViewExtension(AutoRegister, InWorld)
	, Subsystem(InSubsystem)
{
}

void FDreamWorldViewExtension::SetupView(FSceneViewFamily& InViewFamily, FSceneView& InView)
{
	// Game thread. Orthographic editor views sit at arbitrary far-away points; skip them.
	if (!InView.IsPerspectiveProjection())
	{
		return;
	}
	if (UDreamWorldSubsystem* Clock = Subsystem.Get())
	{
		Clock->ReportView(InView.ViewLocation, InView.ViewRotation, InView.bIsGameView);
	}
}
