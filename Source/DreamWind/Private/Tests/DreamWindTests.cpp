// Copyright DreamMoon. All Rights Reserved.
//
// DreamWind.* automation tests. Everything under DreamWind.Shared.* is pure C++ over the shared model
// (the same code the shaders compile), so it runs in milliseconds and needs no world; the Subsystem
// test drives a real UDreamWindSubsystem tick in whichever editor/game world is up.

#include "CoreMinimal.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "DreamWindHlslShim.h"
#include "DreamWindPreset.h"
#include "DreamWindSettings.h"
#include "DreamWindSubsystem.h"
#include "DreamWindTerrainMap.h"
#include "DreamWindTypes.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "HAL/PlatformTime.h"
#include "Misc/AutomationTest.h"

namespace DreamWindTests
{
	using namespace DreamWind::Hlsl;

	static FDreamWindParams MakeParams(float Time = 12.5f, float Seed = 1337.0f)
	{
		return DW_UnpackParams(
			float4(0.8f, 0.6f, 0.0f, 600.0f),
			float4(0.5f, 4000.0f, 0.8f, 0.15f),
			float4(0.35f, 900.0f, Time, Seed));
	}

	static bool NearlyEqual(float3 A, float3 B, float Tolerance)
	{
		return FMath::Abs(A.x - B.x) <= Tolerance && FMath::Abs(A.y - B.y) <= Tolerance && FMath::Abs(A.z - B.z) <= Tolerance;
	}

	static bool IsFinite3(float3 V)
	{
		return FMath::IsFinite(V.x) && FMath::IsFinite(V.y) && FMath::IsFinite(V.z);
	}
}

// -------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDreamWindSharedDeterminismTest, "DreamWind.Shared.Determinism",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FDreamWindSharedDeterminismTest::RunTest(const FString& Parameters)
{
	using namespace DreamWind::Hlsl;
	using namespace DreamWindTests;

	const FDreamWindParams P = MakeParams();
	const float3 Pos(1234.5f, -876.25f, 130.0f);

	const FDreamWindSample A = DW_SampleAnalytic(P, Pos);
	const FDreamWindSample B = DW_SampleAnalytic(P, Pos);
	TestTrue(TEXT("Same inputs, same velocity"), NearlyEqual(A.Velocity, B.Velocity, 0.0f));
	TestEqual(TEXT("Same inputs, same gust"), A.Gust, B.Gust);
	TestTrue(TEXT("Finite"), IsFinite3(A.Velocity) && FMath::IsFinite(A.Gust));

	const FDreamWindParams P2 = MakeParams(12.5f, 42.0f);
	const FDreamWindSample C = DW_SampleAnalytic(P2, Pos);
	TestFalse(TEXT("A different seed gives a different wind"), NearlyEqual(A.Velocity, C.Velocity, 1e-3f));

	const FDreamWindSample D = DW_SampleAnalytic(P, float3(Pos.x + 2000.0f, Pos.y, Pos.z));
	TestFalse(TEXT("A different position gives a different wind"), NearlyEqual(A.Velocity, D.Velocity, 1e-3f));

	// the lattice hash is bit-stable and spreads
	TestEqual(TEXT("lowbias32 is deterministic"), DW_HashUInt(12345u), DW_HashUInt(12345u));
	TestNotEqual(TEXT("lowbias32 spreads neighbours"), DW_HashUInt(1u), DW_HashUInt(2u));
	return true;
}

// -------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDreamWindSharedContinuityTest, "DreamWind.Shared.Continuity",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FDreamWindSharedContinuityTest::RunTest(const FString& Parameters)
{
	using namespace DreamWind::Hlsl;
	using namespace DreamWindTests;

	const FDreamWindParams P = MakeParams(3.7f);
	const float Step = 5.0f;          // cm
	const int32 Steps = 4000;         // 200 m, crosses many lattice cells of both gust and turbulence noise
	const float Bound = P.BaseSpeed * 0.05f;   // a lattice seam would jump by a large fraction of the speed

	float MaxDeltaCheap = 0.0f;
	float MaxDeltaFull = 0.0f;
	float3 Pos(-3000.0f, 250.0f, 80.0f);
	FDreamWindSample PrevCheap = DW_SampleAnalyticCheap(P, Pos);
	FDreamWindSample PrevFull = DW_SampleAnalytic(P, Pos);
	for (int32 i = 0; i < Steps; ++i)
	{
		Pos = float3(Pos.x + Step * 0.8f, Pos.y + Step * 0.6f, Pos.z);
		const FDreamWindSample Cheap = DW_SampleAnalyticCheap(P, Pos);
		const FDreamWindSample Full = DW_SampleAnalytic(P, Pos);
		if (!IsFinite3(Cheap.Velocity) || !IsFinite3(Full.Velocity))
		{
			AddError(FString::Printf(TEXT("Non-finite velocity at step %d"), i));
			return false;
		}
		MaxDeltaCheap = FMath::Max(MaxDeltaCheap, length(Cheap.Velocity - PrevCheap.Velocity));
		MaxDeltaFull = FMath::Max(MaxDeltaFull, length(Full.Velocity - PrevFull.Velocity));
		PrevCheap = Cheap;
		PrevFull = Full;
	}
	AddInfo(FString::Printf(TEXT("max |dv| per %.0f cm step: cheap %.2f, full %.2f cm/s (bound %.1f)"), Step, MaxDeltaCheap, MaxDeltaFull, Bound));
	TestTrue(TEXT("Cheap wind is continuous across lattice cells"), MaxDeltaCheap < Bound);
	TestTrue(TEXT("Full wind is continuous across lattice cells"), MaxDeltaFull < Bound);

	// continuity in time as well
	float MaxDeltaT = 0.0f;
	FDreamWindSample Prev = DW_SampleAnalytic(P, float3(100.0f, 100.0f, 100.0f));
	for (int32 i = 1; i <= 600; ++i)
	{
		const FDreamWindSample S = DW_SampleAnalytic(MakeParams(3.7f + i * (1.0f / 60.0f)), float3(100.0f, 100.0f, 100.0f));
		MaxDeltaT = FMath::Max(MaxDeltaT, length(S.Velocity - Prev.Velocity));
		Prev = S;
	}
	AddInfo(FString::Printf(TEXT("max |dv| per 1/60 s: %.2f cm/s"), MaxDeltaT));
	TestTrue(TEXT("Wind is continuous in time"), MaxDeltaT < P.BaseSpeed * 0.25f);
	return true;
}

// -------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDreamWindSharedCalmTest, "DreamWind.Shared.Calm",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FDreamWindSharedCalmTest::RunTest(const FString& Parameters)
{
	using namespace DreamWind::Hlsl;
	FDreamWindParams P = DreamWindTests::MakeParams();
	P.BaseSpeed = 0.0f;
	const FDreamWindSample S = DW_SampleAnalytic(P, float3(50.0f, 60.0f, 70.0f));
	TestTrue(TEXT("Zero speed means zero velocity"), length(S.Velocity) == 0.0f);
	TestTrue(TEXT("Gust factor is never negative"), S.Gust >= 0.0f);

	// gust factor stays in a sane band for the default strength
	const FDreamWindParams Q = DreamWindTests::MakeParams();
	float MinG = 10.0f, MaxG = -10.0f;
	for (int32 i = 0; i < 2000; ++i)
	{
		const FDreamWindSample T = DW_SampleAnalytic(Q, float3(i * 37.0f, i * 11.0f, 0.0f));
		MinG = FMath::Min(MinG, T.Gust);
		MaxG = FMath::Max(MaxG, T.Gust);
	}
	AddInfo(FString::Printf(TEXT("gust factor range over 2000 samples: %.2f .. %.2f"), MinG, MaxG));
	TestTrue(TEXT("Gust factor bounded below"), MinG >= 0.0f);
	TestTrue(TEXT("Gust factor bounded above"), MaxG <= 1.0f + Q.GustStrength * 1.25f + 0.01f);
	return true;
}

// -------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDreamWindSharedSourcesTest, "DreamWind.Shared.Sources",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FDreamWindSharedSourcesTest::RunTest(const FString& Parameters)
{
	using namespace DreamWind::Hlsl;
	using namespace DreamWindTests;

	const float3 Ambient(300.0f, 0.0f, 0.0f);

	// directional: full weight at the centre, no effect outside the radius
	FDreamWindSourceData Dir;
	Dir.PositionRadius = float4(0.0f, 0.0f, 0.0f, 500.0f);
	Dir.DirectionStrength = float4(0.0f, 1.0f, 0.0f, 1000.0f);
	Dir.Misc = float4(float(DW_SOURCE_DIRECTIONAL), 20.0f, 0.2f, 0.0f);
	TestTrue(TEXT("Directional source at centre replaces the wind"), NearlyEqual(DW_ApplySource(Ambient, Dir, float3(0.0f, 0.0f, 0.0f), 1.0f), float3(0.0f, 1000.0f, 0.0f), 1e-3f));
	TestTrue(TEXT("Directional source inside the inner radius has full weight"), NearlyEqual(DW_ApplySource(Ambient, Dir, float3(90.0f, 0.0f, 0.0f), 1.0f), float3(0.0f, 1000.0f, 0.0f), 1e-3f));
	TestTrue(TEXT("Outside the radius nothing changes"), NearlyEqual(DW_ApplySource(Ambient, Dir, float3(600.0f, 0.0f, 0.0f), 1.0f), Ambient, 1e-4f));
	const float3 Half = DW_ApplySource(Ambient, Dir, float3(0.0f, 0.0f, 0.0f), 0.5f);
	TestTrue(TEXT("Rate factor scales the blend"), NearlyEqual(Half, float3(150.0f, 500.0f, 0.0f), 1e-3f));

	// radial pushes away from the centre
	FDreamWindSourceData Rad = Dir;
	Rad.Misc.x = float(DW_SOURCE_RADIAL);
	const float3 R = DW_ApplySource(float3(0.0f, 0.0f, 0.0f), Rad, float3(50.0f, 0.0f, 0.0f), 1.0f);
	TestTrue(TEXT("Radial source pushes outward"), R.x > 990.0f && FMath::Abs(R.y) < 1e-3f);

	// vortex swirls around the axis
	FDreamWindSourceData Vor = Dir;
	Vor.Misc.x = float(DW_SOURCE_VORTEX);
	Vor.DirectionStrength = float4(0.0f, 0.0f, 1.0f, 1000.0f);
	const float3 V = DW_ApplySource(float3(0.0f, 0.0f, 0.0f), Vor, float3(50.0f, 0.0f, 0.0f), 1.0f);
	TestTrue(TEXT("Vortex around +Z at +X pushes +Y"), V.y > 990.0f && FMath::Abs(V.x) < 1e-3f);

	// a stretched jet: the same fan reaches 4 radii ahead of the centre, still 1 radius behind
	FDreamWindSourceData Jet = Dir;
	Jet.DirectionStrength = float4(1.0f, 0.0f, 0.0f, 1000.0f);
	Jet.Misc.w = 4.0f;
	// footprint 1500/2000 = 0.75 -> past the inner 0.2 the smoothstep gives 0.23 there
	TestTrue(TEXT("Jet: 1500 cm ahead is inside (sphere would be outside)"), DW_EvalSource(Jet, float3(1500.0f, 0.0f, 0.0f)).Weight > 0.15f);
	TestTrue(TEXT("Jet: 2100 cm ahead is outside"), DW_EvalSource(Jet, float3(2100.0f, 0.0f, 0.0f)).Weight <= 1e-5f);
	TestTrue(TEXT("Jet: 600 cm behind is outside (no stretch backwards)"), DW_EvalSource(Jet, float3(-600.0f, 0.0f, 0.0f)).Weight <= 1e-5f);
	TestTrue(TEXT("Jet: 600 cm sideways is outside (radius unchanged)"), DW_EvalSource(Jet, float3(0.0f, 600.0f, 0.0f)).Weight <= 1e-5f);
	TestTrue(TEXT("Jet: the un-stretched sphere is unchanged"), FMath::Abs(DW_EvalSource(Jet, float3(0.0f, 300.0f, 0.0f)).Weight - DW_EvalSource(Dir, float3(0.0f, 300.0f, 0.0f)).Weight) < 1e-5f);
	FDreamWindSourceData RadJet = Jet;
	RadJet.Misc.x = float(DW_SOURCE_RADIAL);
	TestTrue(TEXT("Radial sources ignore Stretch"), DW_EvalSource(RadJet, float3(800.0f, 0.0f, 0.0f)).Weight <= 1e-5f);

	// weight falls off monotonically to zero at the radius
	float PrevW = 2.0f;
	for (float d = 0.0f; d <= 500.0f; d += 25.0f)
	{
		const float W = DW_EvalSource(Dir, float3(d, 0.0f, 0.0f)).Weight;
		TestTrue(FString::Printf(TEXT("weight monotone at %.0f"), d), W <= PrevW + 1e-5f);
		PrevW = W;
	}
	TestTrue(TEXT("Weight is zero at the radius"), DW_EvalSource(Dir, float3(500.0f, 0.0f, 0.0f)).Weight <= 1e-5f);

	// shelter: an oriented box (half extents 400 x 100 x 200, yawed 90 deg so its long side lies along
	// world Y) that calms the wind inside and leaves the outside alone
	FDreamWindSourceData Box;
	Box.PositionRadius = float4(1000.0f, 0.0f, 0.0f, 458.3f);
	Box.DirectionStrength = float4(1.0f, 0.0f, 0.0f, 0.0f);
	Box.Misc = float4(float(DW_SOURCE_SHELTER), 30.0f, 0.5f, 0.0f);
	Box.BoxExtent = float4(400.0f, 100.0f, 200.0f, 0.0f);
	const float SinHalf = FMath::Sin(FMath::DegreesToRadians(45.0f)), CosHalf = FMath::Cos(FMath::DegreesToRadians(45.0f));
	Box.BoxRotation = float4(0.0f, 0.0f, SinHalf, CosHalf); // 90 deg about +Z
	TestTrue(TEXT("Shelter centre is calm"), NearlyEqual(DW_ApplySource(Ambient, Box, float3(1000.0f, 0.0f, 0.0f), 1.0f), float3(0.0f, 0.0f, 0.0f), 1e-3f));
	TestTrue(TEXT("Shelter: inside the inner half along the long (world Y) side is calm"), NearlyEqual(DW_ApplySource(Ambient, Box, float3(1000.0f, 190.0f, 0.0f), 1.0f), float3(0.0f, 0.0f, 0.0f), 1e-3f));
	TestTrue(TEXT("Shelter: rotation moved the short side onto world X (150 cm is outside)"), NearlyEqual(DW_ApplySource(Ambient, Box, float3(1150.0f, 0.0f, 0.0f), 1.0f), Ambient, 1e-4f));
	TestTrue(TEXT("Shelter: 300 cm along world Y is still inside the box (soft part)"), DW_EvalSource(Box, float3(1000.0f, 300.0f, 0.0f)).Weight > 0.4f);
	TestTrue(TEXT("Shelter: past the long side is untouched"), NearlyEqual(DW_ApplySource(Ambient, Box, float3(1000.0f, 450.0f, 0.0f), 1.0f), Ambient, 1e-4f));
	TestTrue(TEXT("Shelter: above the roof is untouched"), NearlyEqual(DW_ApplySource(Ambient, Box, float3(1000.0f, 0.0f, 250.0f), 1.0f), Ambient, 1e-4f));
	// a wind zone: same box, 600 cm/s along +X inside
	Box.DirectionStrength = float4(1.0f, 0.0f, 0.0f, 600.0f);
	TestTrue(TEXT("Wind zone sets its own velocity inside"), NearlyEqual(DW_ApplySource(float3(0.0f, 300.0f, 0.0f), Box, float3(1000.0f, 0.0f, 0.0f), 1.0f), float3(600.0f, 0.0f, 0.0f), 1e-3f));

	// the C++ desc packs into the same record the shader reads: bounding radius, extents, quaternion
	FDreamWindSourceDesc Desc;
	Desc.Type = EDreamWindSourceType::Shelter;
	Desc.Position = FVector(1000.0, 0.0, 0.0);
	Desc.BoxExtent = FVector(400.0, 100.0, 200.0);
	Desc.BoxRotation = FRotator(0.0, 90.0, 0.0);
	Desc.Strength = 0.0f;
	Desc.InnerRadiusFraction = 0.5f;
	const FDreamWindSourceData Packed = UDreamWindSubsystem::PackSource(Desc, 1.0f);
	TestEqual(TEXT("Packed type"), int32(Packed.Misc.x), int32(DW_SOURCE_SHELTER));
	TestTrue(TEXT("Packed bounding radius"), FMath::IsNearlyEqual(Packed.PositionRadius.w, 458.26f, 0.05f));
	TestTrue(TEXT("Packed extents"), Packed.BoxExtent.x == 400.0f && Packed.BoxExtent.y == 100.0f && Packed.BoxExtent.z == 200.0f);
	TestTrue(TEXT("Packed rotation matches the hand-built quaternion"), FMath::Abs(FMath::Abs(Packed.BoxRotation.z) - SinHalf) < 1e-4f && FMath::Abs(FMath::Abs(Packed.BoxRotation.w) - CosHalf) < 1e-4f);
	TestTrue(TEXT("Packed shelter calms its centre"), NearlyEqual(DW_ApplySource(Ambient, Packed, float3(1000.0f, 0.0f, 0.0f), 1.0f), float3(0.0f, 0.0f, 0.0f), 1e-3f));
	TestTrue(TEXT("Packed shelter: world +X 150 is outside after the yaw"), NearlyEqual(DW_ApplySource(Ambient, Packed, float3(1150.0f, 0.0f, 0.0f), 1.0f), Ambient, 1e-4f));
	TestTrue(TEXT("Packed shelter: world +Y 190 is inside"), NearlyEqual(DW_ApplySource(Ambient, Packed, float3(1000.0f, 190.0f, 0.0f), 1.0f), float3(0.0f, 0.0f, 0.0f), 1e-3f));
	return true;
}

// -------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDreamWindSharedPackingTest, "DreamWind.Shared.Packing",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FDreamWindSharedPackingTest::RunTest(const FString& Parameters)
{
	using namespace DreamWind::Hlsl;
	FDreamWindFrameData Frame;
	Frame.P0 = FVector4f(0.6f, 0.8f, 0.0f, 750.0f);
	Frame.P1 = FVector4f(0.4f, 3000.0f, 0.9f, 0.2f);
	Frame.P2 = FVector4f(0.3f, 800.0f, 99.5f, 7.0f);
	Frame.V0 = FVector4f(100.0f, 200.0f, 300.0f, 0.15f);
	Frame.V1 = FVector4f(1.0f / 6400.0f, 1.0f / 6400.0f, 1.0f / 3200.0f, 1.0f);

	const FDreamWindParams P = Frame.GetParams();
	TestEqual(TEXT("BaseDirection.x"), P.BaseDirection.x, 0.6f);
	TestEqual(TEXT("BaseSpeed"), P.BaseSpeed, 750.0f);
	TestEqual(TEXT("GustWavelength"), P.GustWavelength, 3000.0f);
	TestEqual(TEXT("GustFrequency"), P.GustFrequency, 0.2f);
	TestEqual(TEXT("TurbulenceWavelength"), P.TurbulenceWavelength, 800.0f);
	TestEqual(TEXT("Time"), P.Time, 99.5f);
	TestEqual(TEXT("Seed"), P.Seed, 7.0f);

	const FDreamWindVolumeParams V = Frame.GetVolumeParams();
	TestEqual(TEXT("Center.z"), V.Center.z, 300.0f);
	TestEqual(TEXT("FadeFrac"), V.FadeFrac, 0.15f);
	TestEqual(TEXT("Enabled"), V.Enabled, 1.0f);

	// the window: centre maps to uvw 0.5, the border to 0/1, the mask fades over FadeFrac
	const float3 C = DW_VolumeUVW(float3(100.0f, 200.0f, 300.0f), V);
	TestTrue(TEXT("Centre samples the middle of the volume"), FMath::Abs(C.x - 0.5f) < 1e-5f && FMath::Abs(C.z - 0.5f) < 1e-5f);
	TestEqual(TEXT("Mask at centre"), DW_VolumeMask(float3(100.0f, 200.0f, 300.0f), V), 1.0f);
	TestTrue(TEXT("Mask at the border is zero"), DW_VolumeMask(float3(100.0f + 3200.0f, 200.0f, 300.0f), V) <= 1e-5f);
	TestTrue(TEXT("Mask outside is zero"), DW_VolumeMask(float3(100.0f + 5000.0f, 200.0f, 300.0f), V) <= 0.0f);
	return true;
}

// -------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDreamWindSharedTerrainTest, "DreamWind.Shared.Terrain",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FDreamWindSharedTerrainTest::RunTest(const FString& Parameters)
{
	using namespace DreamWind::Hlsl;
	using namespace DreamWindTests;

	// a 1000 x 1000 map with its corner at (100, 200)
	const FDreamWindTerrainParams T = DW_UnpackTerrain(float4(100.0f, 200.0f, 0.001f, 0.001f), float4(1.0f, 1.0f, 1.0f, 0.0f), float4(0.0f, 0.0f, 0.2f, 0.0f));
	FDreamWindSample S;
	S.Velocity = float3(400.0f, 0.0f, 20.0f);
	S.Gust = 1.0f;

	// inside / outside
	TestEqual(TEXT("uv of the corner"), DW_TerrainUV(float3(100.0f, 200.0f, 0.0f), T).x, 0.0f);
	TestEqual(TEXT("inside"), DW_TerrainInside(float2(0.5f, 0.5f), T), 1.0f);
	TestEqual(TEXT("outside"), DW_TerrainInside(float2(1.5f, 0.5f), T), 0.0f);
	FDreamWindTerrainParams Off = T;
	Off.Enabled = 0.0f;
	TestEqual(TEXT("disabled map counts as outside"), DW_TerrainInside(float2(0.5f, 0.5f), Off), 0.0f);

	// speed multiplier scales the whole velocity, steering bends the horizontal part without changing its speed
	const FDreamWindSample Faster = DW_ApplyTerrain(S, float4(1.5f, 0.0f, 0.0f, 0.5f), T, 1.0f);
	TestTrue(TEXT("multiplier 1.5 scales speed"), FMath::Abs(Faster.Velocity.x - 600.0f) < 1e-3f && FMath::Abs(Faster.Velocity.z - 30.0f) < 1e-3f);
	const FDreamWindSample Steered = DW_ApplyTerrain(S, float4(1.0f, 0.0f, 1.0f, 0.5f), T, 1.0f);
	TestTrue(TEXT("steer +Y bends toward +Y"), Steered.Velocity.y > 200.0f && Steered.Velocity.x < 400.0f && Steered.Velocity.x > 0.0f);
	TestTrue(TEXT("steering keeps the horizontal speed"), FMath::Abs(FMath::Sqrt(Steered.Velocity.x * Steered.Velocity.x + Steered.Velocity.y * Steered.Velocity.y) - 400.0f) < 1e-2f);
	FDreamWindTerrainParams NoSpeed = T;
	NoSpeed.SpeedScale = 0.0f;
	const FDreamWindSample Unscaled = DW_ApplyTerrain(S, float4(1.5f, 0.0f, 0.0f, 0.5f), NoSpeed, 1.0f);
	TestTrue(TEXT("SpeedScale 0 ignores the multiplier"), FMath::Abs(Unscaled.Velocity.x - 400.0f) < 1e-3f);
	const FDreamWindSample Untouched = DW_ApplyTerrain(S, float4(1.5f, 1.0f, 1.0f, 0.5f), T, 0.0f);
	TestTrue(TEXT("outside the map nothing changes"), FMath::Abs(Untouched.Velocity.x - 400.0f) < 1e-6f && FMath::Abs(Untouched.Velocity.y) < 1e-6f);
	return true;
}

// -------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDreamWindSharedOcclusionTest, "DreamWind.Shared.Occlusion",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FDreamWindSharedOcclusionTest::RunTest(const FString& Parameters)
{
	using namespace DreamWind::Hlsl;

	// a 6400 x 6400 cm map at (0,0), 64 texels of 100 cm, ground at z = 0 (HeightBase 0) with a
	// 300 cm high, one-texel-thick wall along Y at x = 3200..3300
	UDreamWindTerrainMap* Asset = NewObject<UDreamWindTerrainMap>(GetTransientPackage());
	Asset->Version = UDreamWindTerrainMap::CurrentVersion;
	Asset->Bounds = FBox2D(FVector2D(0, 0), FVector2D(6400, 6400));
	Asset->Width = Asset->Height = 64;
	Asset->HeightRange = FVector2D(0, 300);
	Asset->Texels.Init(FVector4f(1, 0, 0, 0), 64 * 64);
	for (int32 y = 0; y < 64; ++y)
	{
		Asset->Texels[y * 64 + 32].W = 300.0f;
	}
	FDreamWindTerrainCpuData Map;
	Map.BuildFromMap(*Asset);
	TestEqual(TEXT("atlas holds the map and its max chain"), Map.Texels.Num(), 64 * 64 * 2);
	TestEqual(TEXT("texel size"), Map.TexelSize(), 100.0f);
	TestTrue(TEXT("level 0 keeps the wall"), Map.HeightAt(3250.0, 1000.0, 0) > 299.0f && Map.HeightAt(3350.0, 1000.0, 0) < 1.0f);
	TestTrue(TEXT("level 1 max keeps the wall over 2 texels"), Map.HeightAt(3350.0, 1000.0, 1) > 299.0f && Map.HeightAt(3450.0, 1000.0, 1) < 1.0f);
	TestTrue(TEXT("level 4 max keeps the wall over 16 texels"), Map.HeightAt(4000.0, 1000.0, 4) > 299.0f && Map.HeightAt(4900.0, 1000.0, 4) < 1.0f);
	TestTrue(TEXT("bilinear level 0 still blends the wall for the modulation fetch"), Map.Sample(3300.0, 1000.0).W > 100.0f && Map.Sample(3300.0, 1000.0).W < 200.0f);
	FDreamWindTerrainParams T = DW_UnpackTerrain(float4(0.0f, 0.0f, 1.0f / 6400.0f, 1.0f / 6400.0f), float4(1.0f, 1.0f, 1.0f, 100.0f), float4(1.0f, 3000.0f, 0.2f, 0.0f));
	TestEqual(TEXT("first tap reads level 0"), DW_OcclusionTapLevel(1, T), 0);
	TestTrue(TEXT("last tap reads a coarser level"), DW_OcclusionTapLevel(DW_OCCLUSION_TAPS, T) >= 2);
	for (int32 k = 2; k <= DW_OCCLUSION_TAPS; ++k)
	{
		const float Gap = DW_OcclusionTapDistance(k, T) - DW_OcclusionTapDistance(k - 1, T);
		TestTrue(FString::Printf(TEXT("tap %d reads a level whose texel covers the gap to tap %d"), k, k - 1), 100.0f * float(1 << DW_OcclusionTapLevel(k, T)) >= Gap - 1e-3f);
	}
	// texel uv: the centre of the containing texel, at every level
	const float2 X0 = DW_TerrainAtlasTexelUV(float2(3250.0f / 6400.0f, 0.3f), 0, T);
	TestTrue(TEXT("texel uv level 0 = centre of texel 32"), FMath::Abs(X0.x - 32.5f / 64.0f) < 1e-5f);
	const float2 X1 = DW_TerrainAtlasTexelUV(float2(3350.0f / 6400.0f, 0.3f), 1, T);
	TestTrue(TEXT("texel uv level 1 = centre of texel 16 in the level-1 strip"), FMath::Abs(X1.x - 16.5f / 64.0f) < 1e-5f && X1.y > 0.5f && X1.y < 0.75f);
	// atlas uv: level 0 fills the top half, level 1 starts at v = 0.5, u shrinks by 2
	const float2 A0 = DW_TerrainAtlasUV(float2(0.5f, 0.5f), 0, T);
	const float2 A1 = DW_TerrainAtlasUV(float2(0.5f, 0.5f), 1, T);
	TestTrue(TEXT("atlas uv level 0"), FMath::Abs(A0.x - 0.5f) < 1e-5f && FMath::Abs(A0.y - 0.25f) < 1e-5f);
	TestTrue(TEXT("atlas uv level 1"), FMath::Abs(A1.x - 0.25f) < 1e-5f && FMath::Abs(A1.y - 0.625f) < 1e-5f);

	const float3 WindX(500.0f, 0.0f, 0.0f);      // blowing toward +X: the wall's lee is x > 3300
	const float3 WindNegX(-500.0f, 0.0f, 0.0f);

	// helpers
	TestTrue(TEXT("tap distances grow quadratically to the range"), FMath::Abs(DW_OcclusionTapDistance(DW_OCCLUSION_TAPS, T) - 3000.0f) < 1e-3f && DW_OcclusionTapDistance(1, T) < 30.0f);
	TestTrue(TEXT("upwind tap position"), FMath::Abs(DW_OcclusionTapXY(float3(1000.0f, 0.0f, 0.0f), float2(-1.0f, 0.0f), DW_OCCLUSION_TAPS, T).x - (-2000.0f)) < 1e-3f);
	TestTrue(TEXT("accumulate: right behind an obstacle 100 above = full shadow"), DW_OcclusionAccumulate(0.0f, 100.0f, 0.0f, 0.0f, T) > 0.999f);
	TestTrue(TEXT("accumulate: at half the reach = half shadow"), FMath::Abs(DW_OcclusionAccumulate(0.0f, 100.0f, 0.0f, 250.0f, T) - 0.5f) < 1e-3f);
	TestTrue(TEXT("accumulate: beyond the reach = nothing"), DW_OcclusionAccumulate(0.0f, 100.0f, 0.0f, 600.0f, T) <= 1e-5f);
	TestTrue(TEXT("accumulate: obstacle below the sample = nothing"), DW_OcclusionAccumulate(0.0f, -10.0f, 0.0f, 10.0f, T) <= 1e-5f);
	TestTrue(TEXT("accumulate keeps the max"), DW_OcclusionAccumulate(0.7f, 100.0f, 0.0f, 400.0f, T) == 0.7f);
	TestEqual(TEXT("self: inside a solid"), DW_OcclusionSelf(300.0f, 50.0f), 1.0f);
	TestEqual(TEXT("self: on the ground"), DW_OcclusionSelf(0.0f, 0.0f), 0.0f);
	TestTrue(TEXT("self: fades in over the first half metre"), DW_OcclusionSelf(30.0f, 0.0f) > 0.4f && DW_OcclusionSelf(30.0f, 0.0f) < 0.7f);
	TestTrue(TEXT("inactive when off"), !DW_OcclusionActive(DW_UnpackTerrain(float4(0.0f, 0.0f, 1.0f, 1.0f), float4(1.0f, 1.0f, 1.0f, 0.0f), float4(0.0f, 3000.0f, 0.2f, 0.0f)), 500.0f));

	// the CPU loop against the wall (samples 50 cm above the ground; the wall rises 250 above them,
	// so it shades out to 1250 cm downwind)
	// (the wall texel spans x = 3200..3300; both "just behind" samples sit 100 cm past its edge)
	const float ShadowJustBehind = Map.OcclusionShadow(T, float3(3400.0f, 1000.0f, 50.0f), WindX);
	const float ShadowFar = Map.OcclusionShadow(T, float3(3400.0f + 800.0f, 1000.0f, 50.0f), WindX);
	const float ShadowBeyond = Map.OcclusionShadow(T, float3(3400.0f + 1600.0f, 1000.0f, 50.0f), WindX);
	const float ShadowInFront = Map.OcclusionShadow(T, float3(3000.0f, 1000.0f, 50.0f), WindX);
	const float ShadowOtherSide = Map.OcclusionShadow(T, float3(3100.0f, 1000.0f, 50.0f), WindNegX);
	const float ShadowAboveWall = Map.OcclusionShadow(T, float3(3400.0f, 1000.0f, 400.0f), WindX);
	const float ShadowInside = Map.OcclusionShadow(T, float3(3250.0f, 1000.0f, 50.0f), WindX);
	AddInfo(FString::Printf(TEXT("shadow just behind %.2f, 8 m behind %.2f, 16 m behind %.2f, in front %.2f, other side (wind flipped) %.2f, above %.2f, inside %.2f"),
		ShadowJustBehind, ShadowFar, ShadowBeyond, ShadowInFront, ShadowOtherSide, ShadowAboveWall, ShadowInside));
	// (bilinear taps half a texel apart see at least ~3/4 of a one-texel wall)
	TestTrue(TEXT("strong shadow just behind the wall"), ShadowJustBehind > 0.7f);
	TestTrue(TEXT("weaker shadow further downwind"), ShadowFar < ShadowJustBehind && ShadowFar > 0.15f);
	TestTrue(TEXT("no shadow beyond the reach"), ShadowBeyond <= 1e-4f);
	TestTrue(TEXT("no shadow in front (upwind) of the wall"), ShadowInFront <= 1e-4f);
	TestTrue(TEXT("flip the wind and the other side is shaded"), ShadowOtherSide > 0.7f);
	TestTrue(TEXT("both sides read about the same shadow at the same distance"), FMath::Abs(ShadowOtherSide - ShadowJustBehind) < 0.05f);
	// no gaps: walking downwind from the wall the shadow fades without holes (the bilinear taps ripple
	// by a few percent as they sweep across the wall's texel; that is the accepted cost of smoothness)
	float Prev = 2.0f, MaxRise = 0.0f;
	for (float x = 3400.0f; x <= 5200.0f; x += 25.0f)
	{
		const float Sh = Map.OcclusionShadow(T, float3(x, 1000.0f, 50.0f), WindX);
		if (x < 4400.0f)   // the wall (top 300, sample at 50) reaches 250 / 0.2 = 1250 cm past its centre at 3250
		{
			TestTrue(FString::Printf(TEXT("no hole in the shadow at x=%.0f (%.3f)"), x, Sh), Sh > 0.0f);
		}
		MaxRise = FMath::Max(MaxRise, Sh - Prev);
		Prev = Sh;
	}
	AddInfo(FString::Printf(TEXT("largest downwind rise (ripple) %.3f"), MaxRise));
	TestTrue(TEXT("ripple stays small"), MaxRise < 0.12f);
	TestTrue(TEXT("nothing above the wall top"), ShadowAboveWall <= 1e-4f);
	TestTrue(TEXT("inside the wall = full"), ShadowInside >= 1.0f);

	// through ApplyAll: the velocity magnitude drops, the direction stays
	FDreamWindSample S;
	S.Velocity = WindX;
	S.Gust = 1.0f;
	const FDreamWindSample Behind = Map.ApplyAll(S, T, float3(3400.0f, 1000.0f, 50.0f));
	TestTrue(TEXT("ApplyAll damps the wind behind the wall"), Behind.Velocity.x < 160.0f && Behind.Velocity.x >= 0.0f && FMath::Abs(Behind.Velocity.y) < 1e-3f);
	T.OcclusionStrength = 0.5f;
	const FDreamWindSample Half = Map.ApplyAll(S, T, float3(3250.0f, 1000.0f, 50.0f));
	TestTrue(TEXT("OcclusionStrength scales the damping"), FMath::Abs(Half.Velocity.x - 250.0f) < 1e-2f);
	return true;
}

// -------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDreamWindPerfCpuSampleTest, "DreamWind.Perf.CpuSample",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FDreamWindPerfCpuSampleTest::RunTest(const FString& Parameters)
{
	using namespace DreamWind::Hlsl;
	const FDreamWindParams P = DreamWindTests::MakeParams();
	const int32 N = 200000;
	volatile float Sink = 0.0f;

	double T0 = FPlatformTime::Seconds();
	for (int32 i = 0; i < N; ++i)
	{
		Sink += DW_SampleAnalyticCheap(P, float3(i * 3.1f, i * 1.7f, 100.0f)).Velocity.x;
	}
	const double CheapUs = (FPlatformTime::Seconds() - T0) * 1e6 / N;

	T0 = FPlatformTime::Seconds();
	for (int32 i = 0; i < N; ++i)
	{
		Sink += DW_SampleAnalytic(P, float3(i * 3.1f, i * 1.7f, 100.0f)).Velocity.x;
	}
	const double FullUs = (FPlatformTime::Seconds() - T0) * 1e6 / N;

	AddInfo(FString::Printf(TEXT("CPU sample: cheap %.3f us, full %.3f us (n=%d, sink %.1f)"), CheapUs, FullUs, N, (float)Sink));
	TestTrue(TEXT("Cheap sample under 20 us"), CheapUs < 20.0);
	TestTrue(TEXT("Full sample under 40 us"), FullUs < 40.0);
	return true;
}

// -------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDreamWindSubsystemTickTest, "DreamWind.Subsystem.TickAndImpulse",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FDreamWindSubsystemTickTest::RunTest(const FString& Parameters)
{
	UWorld* World = nullptr;
	if (GEngine)
	{
		for (const FWorldContext& Context : GEngine->GetWorldContexts())
		{
			if (Context.World() && (Context.WorldType == EWorldType::Editor || Context.WorldType == EWorldType::Game || Context.WorldType == EWorldType::PIE))
			{
				World = Context.World();
				break;
			}
		}
	}
	if (!World)
	{
		AddInfo(TEXT("No editor/game world - skipping"));
		return true;
	}
	UDreamWindSubsystem* Wind = World->GetSubsystem<UDreamWindSubsystem>();
	if (!TestNotNull(TEXT("Subsystem exists"), Wind))
	{
		return false;
	}

	FDreamWindGlobalWind G;
	G.Direction = FVector(1, 0, 0);
	G.Speed = 500.0f;
	G.GustStrength = 0.0f;
	G.TurbulenceStrength = 0.0f;
	Wind->SetGlobalWind(G, 0.0f);
	Wind->Tick(1.0f / 60.0f);

	const FDreamWindSampleResult Calm = Wind->SampleWind(FVector(100000, 100000, 100), EDreamWindQuality::Cheap);
	TestTrue(TEXT("Steady wind blows +X at the set speed"), FMath::Abs(Calm.Velocity.X - 500.0) < 1.0 && FMath::Abs(Calm.Velocity.Y) < 1.0);

	const FVector Centre(100000, 100000, 100);
	const FDreamWindImpulseHandle Handle = Wind->AddRadialImpulse(Centre, 800.0f, 3000.0f, 5.0f);
	TestTrue(TEXT("Impulse handle valid"), Handle.IsValid());
	Wind->Tick(1.0f / 60.0f);
	TestEqual(TEXT("One live source after the tick"), Wind->GetLiveSourceCount(), 1);

	const FDreamWindSampleResult Near = Wind->SampleWind(Centre + FVector(100, 0, 0), EDreamWindQuality::Cheap);
	TestTrue(TEXT("Radial impulse pushes outward"), Near.Velocity.X > 2500.0);

	Wind->CancelImpulse(Handle);
	Wind->Tick(1.0f / 60.0f);
	TestEqual(TEXT("No live sources after cancel"), Wind->GetLiveSourceCount(), 0);

	// a shelter component desc through the live path: calm inside, untouched outside
	{
		FDreamWindSourceDesc Shelter;
		Shelter.Type = EDreamWindSourceType::Shelter;
		Shelter.Position = Centre + FVector(3000, 0, 0);
		Shelter.BoxExtent = FVector(400, 400, 300);
		Shelter.BoxRotation = FRotator(0, 45, 0);
		Shelter.Strength = 0.0f;
		Shelter.InnerRadiusFraction = 0.6f;
		const FDreamWindImpulseHandle ShelterHandle = Wind->AddImpulse(Shelter, 5.0f, 0.0f);
		Wind->Tick(1.0f / 60.0f);
		TestEqual(TEXT("Shelter impulse is a live source"), Wind->GetLiveSourceCount(), 1);
		const FDreamWindSampleResult Inside = Wind->SampleWind(Shelter.Position, EDreamWindQuality::Cheap);
		TestTrue(TEXT("Shelter interior is calm"), Inside.Speed < 1.0f);
		const FDreamWindSampleResult Outside = Wind->SampleWind(Shelter.Position + FVector(0, 0, 900), EDreamWindQuality::Cheap);
		TestTrue(TEXT("Above the shelter the wind is untouched"), FMath::Abs(Outside.Velocity.X - 500.0) < 1.0);
		Wind->CancelImpulse(ShelterHandle);
		Wind->Tick(1.0f / 60.0f);
	}

	// presets: the shipped storm preset blends in (0 s = snap) and keeps the current direction on request
	if (const UDreamWindPreset* Storm = LoadObject<UDreamWindPreset>(nullptr, TEXT("/DreamDynamicWorld/Wind/Presets/DWP_Storm.DWP_Storm")))
	{
		Wind->SetGlobalWindDirectionAndSpeed(FVector(0, 1, 0), 500.0f, 0.0f);
		Wind->ApplyPresetKeepDirection(Storm, 0.0f);
		Wind->Tick(1.0f / 60.0f);
		TestTrue(TEXT("Preset speed applied"), FMath::IsNearlyEqual(Wind->GetGlobalWind().Speed, Storm->Wind.Speed));
		TestTrue(TEXT("Direction kept"), Wind->GetGlobalWindDirection().Y > 0.99);
		Wind->ApplyPreset(Storm, 0.0f);
		Wind->Tick(1.0f / 60.0f);
		TestTrue(TEXT("Preset direction applied"), Wind->GetGlobalWindDirection().X > 0.99);
	}
	else
	{
		AddInfo(TEXT("DWP_Storm not found - preset check skipped"));
	}

	// restore the project default so the session is not left with the test wind
	Wind->SetGlobalWind(UDreamWindSettings::Get()->DefaultWind, 0.0f);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
