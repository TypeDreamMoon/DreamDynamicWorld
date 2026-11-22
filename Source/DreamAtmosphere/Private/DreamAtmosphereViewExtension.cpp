// Copyright DreamMoon. All Rights Reserved.

#include "DreamAtmosphereViewExtension.h"

#include "HAL/IConsoleManager.h"
#include "PostProcess/PostProcessMaterialInputs.h"
#include "RenderingThread.h"
#include "SceneView.h"
#include "ScreenPass.h"

static TAutoConsoleVariable<int32> CVarDreamAtmosphereAerialPerspective(
	TEXT("r.DreamAtmosphere.AerialPerspective"),
	1,
	TEXT("Lay the atmosphere's aerial perspective over the scene (1, the project settings permitting) or not (0)."),
	ECVF_RenderThreadSafe);

FDreamAtmosphereViewExtension::FDreamAtmosphereViewExtension(const FAutoRegister& AutoRegister, UWorld* InWorld)
	: FWorldSceneViewExtension(AutoRegister, InWorld)
{
}

void FDreamAtmosphereViewExtension::SetFrame_GameThread(const DreamAtmosphere::Aerial::FFrame& InFrame)
{
	TSharedRef<FDreamAtmosphereViewExtension, ESPMode::ThreadSafe> Self = StaticCastSharedRef<FDreamAtmosphereViewExtension>(AsShared());
	ENQUEUE_RENDER_COMMAND(DreamAtmosphereFrame)([Self, InFrame](FRHICommandListImmediate&)
		{
			Self->Frame = InFrame;
		});
}

void FDreamAtmosphereViewExtension::SubscribeToPostProcessingPass(EPostProcessingPass Pass, const FSceneView& InView, FPostProcessingPassDelegateArray& InOutPassCallbacks, bool bIsPassEnabled)
{
	if (Pass == EPostProcessingPass::BeforeDOF && Frame.bEnabled && InView.IsPerspectiveProjection() && CVarDreamAtmosphereAerialPerspective.GetValueOnRenderThread() != 0)
	{
		InOutPassCallbacks.Add(FPostProcessingPassDelegate::CreateRaw(this, &FDreamAtmosphereViewExtension::Draw));
	}
}

FScreenPassTexture FDreamAtmosphereViewExtension::Draw(FRDGBuilder& GraphBuilder, const FSceneView& InView, const FPostProcessMaterialInputs& Inputs)
{
	return DreamAtmosphere::Aerial::AddCompositePass(GraphBuilder, InView, Inputs, Frame);
}
