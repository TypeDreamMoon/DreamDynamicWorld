// Copyright DreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

class FRDGBuilder;
class FRDGTexture;
class FTextureResource;
class FSceneView;
class UDreamAtmosphereTables;
struct FPostProcessMaterialInputs;
struct FScreenPassTexture;

/**
 * The aerial perspective (Shaders/Private/DreamAtmosphereAerial.usf): the air between the eye and the scene, a froxel
 * volume in front of the camera from the atmosphere's model and tables, laid over the opaque scene by its depth.
 */
namespace DreamAtmosphere::Aerial
{
	/** What a frame of it needs that is not the view's: the air, its tables, the lights, the volume's shape. */
	struct DREAMATMOSPHERE_API FFrame
	{
		FVector4f Atmosphere[5];
		FTextureResource* Transmittance = nullptr;
		FTextureResource* MultiScattering = nullptr;
		FIntPoint TransmittanceSize = FIntPoint(1, 1);
		FIntPoint MultiScatteringSize = FIntPoint(1, 1);
		FVector3f SunDirection = FVector3f(0.0f, 0.0f, 1.0f);
		FVector3f SunLight = FVector3f::ZeroVector;
		FVector3f MoonDirection = FVector3f(0.0f, 0.0f, -1.0f);
		FVector3f MoonLight = FVector3f::ZeroVector;
		float MaxDistanceKm = 64.0f;
		float DistanceScale = 1.0f;
		float SkyDistanceKm = 500.0f;
		float SeaLevel = 0.0f;
		FIntVector VolumeSize = FIntVector(32, 32, 32);
		int32 Steps = 8;
		bool bEnabled = false;
	};

	/** The camera, as the volume sees it: rays through the screen, and where it stands over the planet. */
	struct DREAMATMOSPHERE_API FCamera
	{
		FMatrix44f ClipToTranslatedWorld = FMatrix44f::Identity;
		FVector3f TranslatedCamera = FVector3f::ZeroVector;
		FVector3f CameraKm = FVector3f::ZeroVector;

		static FCamera FromView(const FSceneView& View, const FFrame& Frame);
		static FCamera FromMatrices(const FMatrix& ViewProjection, const FVector& Location, const FFrame& Frame);
	};

	/** The volume for this camera (an RDG texture, rgb inscattered light per unit of pre-exposure, a transmittance). */
	DREAMATMOSPHERE_API FRDGTexture* AddVolumePass(FRDGBuilder& GraphBuilder, const FFrame& Frame, const FCamera& Camera);

	/** The volume laid over the scene colour by the scene depth (the post-processing pass's inputs); returns the colour. */
	FScreenPassTexture AddCompositePass(FRDGBuilder& GraphBuilder, const FSceneView& View, const FPostProcessMaterialInputs& Inputs, const FFrame& Frame);

	/** The volume's depth slice Z for this camera, read back (flushes rendering): for tests. */
	DREAMATMOSPHERE_API bool ReadVolumeSlice(const FFrame& Frame, const FCamera& Camera, int32 Z, TArray<FLinearColor>& OutPixels);

	/** The frame for these tables and the project settings, lights from the clock's sun and moon. */
	DREAMATMOSPHERE_API FFrame MakeFrame(const UDreamAtmosphereTables* Tables, const FVector& SunDirection, const FVector& MoonDirection, float MoonIllumination);
}
