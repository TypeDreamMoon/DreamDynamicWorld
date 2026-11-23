// Copyright DreamMoon. All Rights Reserved.

#include "DreamWindViewExtension.h"
#include "DreamWindFluid.h"
#include "DreamWindSubsystem.h"
#include "RenderingThread.h"
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

void FDreamWindViewExtension::QueueDistanceProbe(const DreamWind::DistanceField::FProbeRequest& Request)
{
	TSharedRef<FDreamWindViewExtension, ESPMode::ThreadSafe> Self = StaticCastSharedRef<FDreamWindViewExtension>(AsShared());
	ENQUEUE_RENDER_COMMAND(DreamWindQueueDistanceProbe)([Self, Request](FRHICommandListImmediate&)
	{
		Self->PendingProbe = Request;
	});
}

void FDreamWindViewExtension::SetFluid(const TSharedPtr<FDreamWindFluid, ESPMode::ThreadSafe>& InFluid)
{
	TSharedRef<FDreamWindViewExtension, ESPMode::ThreadSafe> Self = StaticCastSharedRef<FDreamWindViewExtension>(AsShared());
	ENQUEUE_RENDER_COMMAND(DreamWindSetFluid)([Self, InFluid](FRHICommandListImmediate&)
	{
		Self->Fluid = InFluid;
	});
}

void FDreamWindViewExtension::PrePostProcessPass_RenderThread(FRDGBuilder& GraphBuilder, const FSceneView& InView, const FPostProcessingInputs& Inputs)
{
	// (A scene capture's view is perspective too, but has its own distance field relevance: the main views only.)
	if (!InView.IsPerspectiveProjection() || InView.bIsSceneCapture || InView.bIsReflectionCapture)
	{
		return;
	}
	if (Fluid.IsValid())
	{
		Fluid->AddWallPasses_RenderThread(GraphBuilder, InView);
	}
	if (PendingProbe.IsSet())
	{
		DreamWind::DistanceField::AddProbePass(GraphBuilder, InView, PendingProbe.GetValue());
		PendingProbe.Reset();
		ProbesWritten.fetch_add(1);
	}
}
