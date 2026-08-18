// Copyright DreamMoon. All Rights Reserved.
//
// DreamWind.* automation tests. Everything under DreamWind.Shared.* is pure C++ over the shared model
// (the same code the shaders compile), so it runs in milliseconds and needs no world; the Subsystem
// test drives a real UDreamWindSubsystem tick in whichever editor/game world is up.

#include "CoreMinimal.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "DreamWindHlslShim.h"
#include "DreamWindSettings.h"
#include "DreamWindSubsystem.h"
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

	// weight falls off monotonically to zero at the radius
	float PrevW = 2.0f;
	for (float d = 0.0f; d <= 500.0f; d += 25.0f)
	{
		const float W = DW_EvalSource(Dir, float3(d, 0.0f, 0.0f)).Weight;
		TestTrue(FString::Printf(TEXT("weight monotone at %.0f"), d), W <= PrevW + 1e-5f);
		PrevW = W;
	}
	TestTrue(TEXT("Weight is zero at the radius"), DW_EvalSource(Dir, float3(500.0f, 0.0f, 0.0f)).Weight <= 1e-5f);
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
	const FDreamWindTerrainParams T = DW_UnpackTerrain(float4(100.0f, 200.0f, 0.001f, 0.001f), float4(1.0f, 1.0f, 1.0f, 0.0f));
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

	// restore the project default so the session is not left with the test wind
	Wind->SetGlobalWind(UDreamWindSettings::Get()->DefaultWind, 0.0f);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
