// Copyright DreamMoon. All Rights Reserved.

#include "DreamWindQualityTier.h"
#include "DreamWindSettings.h"
#include "DreamWorldQuality.h"
#include "Misc/App.h"
#include "RHIDefinitions.h"
#include "RHIGlobals.h"

namespace DreamWindQualityTierImpl
{
	/** A grid size scaled by Num / Den, in whole thread groups (4). */
	static FIntVector Scaled(const FIntVector& Res, int32 Num, int32 Den)
	{
		auto One = [Num, Den](int32 V) { return FMath::Max(FMath::DivideAndRoundUp(V * Num, Den * 4) * 4, 8); };
		return FIntVector(One(Res.X), One(Res.Y), One(Res.Z));
	}
}

FDreamWindQualityTier FDreamWindQualityTier::Resolve(const UDreamWindSettings& Settings, int32 InLevel)
{
	using namespace DreamWindQualityTierImpl;
	// (Compute shaders and a renderer, or the analytic wind and the baked map only.)
	const bool bCanCompute = FApp::CanEverRender() && GMaxRHIFeatureLevel >= ERHIFeatureLevel::SM5;
	FDreamWindQualityTier T;
	T.Level = bCanCompute ? FMath::Clamp(InLevel, 0, 4) : 0;

	const FIntVector Res = Settings.VolumeResolution;
	const float Voxel = FMath::Max(Settings.VolumeVoxelSize, 1.0f);
	T.bRuntimeTerrain = true;
	T.bVolume = true;
	T.bFar = Settings.bVolumeFarLevel;
	T.NearResolution = Res;
	T.FarResolution = Res;
	T.NearCell = Voxel;
	T.FarCell = Voxel * FMath::Max(Settings.VolumeFarVoxelScale, 1.0f);
	T.NearIterations = FMath::Max(Settings.FluidPressureIterations, 1);
	T.FarIterations = FMath::Max(Settings.FluidFarPressureIterations, 0);
	T.bMacCormack = Settings.bFluidMacCormack;
	T.Vorticity = Settings.FluidVorticity;
	T.FarInterval = FMath::Clamp(Settings.FluidFarStepInterval, 1, 4);

	switch (T.Level)
	{
	case 0:
		T.bRuntimeTerrain = false;
		T.bBakedShadow = false;
		T.bVolume = false;
		T.bFar = false;
		break;
	case 1:
		// One grid at the far level's cells, half as tall: walls and wakes at the scale of buildings, none of people.
		T.bFar = false;
		T.NearCell = T.FarCell;
		T.NearResolution = FIntVector(Res.X, Res.Y, FMath::Max(Res.Z / 2, 8));
		T.NearIterations = FMath::Max(T.FarIterations, 1);
		T.bMacCormack = false;
		T.Vorticity = 0.0f;
		break;
	case 2:
		// The same grids (a change to or from epic needs no fade), a little less work on each.
		T.NearIterations = FMath::Max(T.NearIterations * 3 / 4, 1);
		T.FarInterval = FMath::Min(T.FarInterval + 1, 4);
		break;
	case 4:
		T.NearCell = Voxel * 0.5f;
		T.NearResolution = Scaled(Res, 2, 1);
		T.NearIterations = T.NearIterations * 2;
		T.FarResolution = Scaled(Res, 3, 2);
		T.FarInterval = 1;
		break;
	default:
		break;
	}
	return T;
}

int32 FDreamWindQualityTier::CurrentLevel()
{
	return UDreamWorldQuality::GetLevel(EDreamWorldQualityGroup::Wind);
}
