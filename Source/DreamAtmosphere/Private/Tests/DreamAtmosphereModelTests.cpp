// Copyright DreamMoon. All Rights Reserved.

#include "DreamAtmosphereHlslShim.h"
#include "DreamAtmosphereTypes.h"

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace DreamAtmosphere::ModelTests
{
	constexpr EAutomationTestFlags Flags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter;

	DreamWind::Hlsl::FDSA_Atmosphere Earth()
	{
		FVector4f P[5];
		FDreamAtmosphereParameters().Pack(P);
		using DreamWind::Hlsl::ToHlsl;
		return DreamWind::Hlsl::DSA_Unpack(ToHlsl(P[0]), ToHlsl(P[1]), ToHlsl(P[2]), ToHlsl(P[3]), ToHlsl(P[4]));
	}

	float Luma(const DreamWind::Hlsl::float3& C)
	{
		return 0.2126f * C.x + 0.7152f * C.y + 0.0722f * C.z;
	}

	/** A direction ElevationDeg over the horizon, AzimuthDeg round from +x. */
	DreamWind::Hlsl::float3 Toward(float ElevationDeg, float AzimuthDeg)
	{
		const float E = FMath::DegreesToRadians(ElevationDeg), Az = FMath::DegreesToRadians(AzimuthDeg);
		return DreamWind::Hlsl::float3(FMath::Cos(E) * FMath::Cos(Az), FMath::Cos(E) * FMath::Sin(Az), FMath::Sin(E));
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDreamAtmosphereTransmittanceTest, "DreamAtmosphere.Model.Transmittance", DreamAtmosphere::ModelTests::Flags)
bool FDreamAtmosphereTransmittanceTest::RunTest(const FString& Parameters)
{
	using namespace DreamWind::Hlsl;
	using namespace DreamAtmosphere::ModelTests;
	const FDSA_Atmosphere A = Earth();
	const float Ground = A.BottomRadius;

	// Straight up: the optical depths in closed form (Rayleigh and haze exponentials over 100 km, the ozone's tent of
	// 30 km at its peak value): 0.0615, 0.1420, 0.2714.
	const float3 Up = DSA_TransmittanceToTopDirect(A, Ground, 1.0f);
	const float3 Expected(FMath::Exp(-0.061494f), FMath::Exp(-0.142007f), FMath::Exp(-0.271403f));
	TestTrue(FString::Printf(TEXT("the sun overhead comes through (%.4f, %.4f, %.4f), as in closed form (%.4f, %.4f, %.4f)"), Up.x, Up.y, Up.z, Expected.x, Expected.y, Expected.z),
		FMath::IsNearlyEqual(Up.x, Expected.x, 0.005f) && FMath::IsNearlyEqual(Up.y, Expected.y, 0.005f) && FMath::IsNearlyEqual(Up.z, Expected.z, 0.005f));

	// At the horizon the long path through the air leaves the red: the setting sun.
	const float3 Horizon = DSA_TransmittanceToTopDirect(A, Ground, 0.0f);
	TestTrue(FString::Printf(TEXT("the sun on the horizon is red (%.4f, %.4f, %.5f)"), Horizon.x, Horizon.y, Horizon.z), Horizon.x > Horizon.y && Horizon.y > Horizon.z);
	TestTrue(TEXT("and much dimmer than overhead"), Horizon.x < 0.5f * Up.x && Horizon.z < 0.05f * Up.z);

	// Higher up there is less air to cross.
	TestTrue(TEXT("less air over a mountain"), DSA_TransmittanceToTopDirect(A, Ground + 4.0f, 0.2f).z > DSA_TransmittanceToTopDirect(A, Ground, 0.2f).z);

	// The earth's shadow: a light under the horizon reaches nothing on the ground; one over it does.
	const float3 Under = DSA_LightTransmittance(A, Ground, -0.05f);
	TestTrue(TEXT("no light from under the horizon"), Under.x == 0.0f && Under.y == 0.0f && Under.z == 0.0f);
	TestTrue(TEXT("light from over it"), DSA_LightTransmittance(A, Ground, 0.05f).x > 0.0f);
	TestTrue(TEXT("a light just under the horizon still lights the air 10 km up"), DSA_LightTransmittance(A, Ground + 10.0f, -0.05f).x > 0.0f);

	// The table's coordinates round trip over its range.
	int32 Off = 0;
	for (int32 j = 0; j <= 8; ++j)
	{
		for (int32 i = 0; i <= 8; ++i)
		{
			const float2 Uv(float(i) / 8.0f, float(j) / 8.0f);
			const float2 RMu = DSA_TransmittanceRMu(A, Uv);
			const float2 Back = DSA_TransmittanceUv(A, RMu.x, RMu.y);
			Off += FMath::Abs(Back.x - Uv.x) < 1.0e-3f && FMath::Abs(Back.y - Uv.y) < 1.0e-3f ? 0 : 1;
		}
	}
	TestEqual(TEXT("the transmittance table's coordinates round trip"), Off, 0);

	// From on the ground a ray down meets it at once (not across the planet), one up runs to the top.
	// (A metre or so is the rounding of a radius of thousands of km in single precision.)
	TestTrue(TEXT("from the ground, down, no air"), DSA_RayLength(A, Ground, -0.3f) < 0.01f);
	TestTrue(TEXT("from 2 km up, down, the slant to the ground"), FMath::IsNearlyEqual(DSA_RayLength(A, Ground + 2.0f, -1.0f), 2.0f, 1.0e-2f));
	TestTrue(TEXT("from the ground, up, the air's height"), FMath::IsNearlyEqual(DSA_RayLength(A, Ground, 1.0f), 100.0f, 1.0e-2f));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDreamAtmosphereSkyTableMappingTest, "DreamAtmosphere.Model.SkyTableMapping", DreamAtmosphere::ModelTests::Flags)
bool FDreamAtmosphereSkyTableMappingTest::RunTest(const FString& Parameters)
{
	using namespace DreamWind::Hlsl;
	int32 Off = 0, Vectors = 0, Range = 0;
	for (int32 k = 0; k <= 6; ++k)
	{
		for (int32 j = 0; j <= 6; ++j)
		{
			for (int32 i = 0; i <= 6; ++i)
			{
				const float3 Uvw(float(i) / 6.0f, float(j) / 6.0f, float(k) / 6.0f);
				const float3 D = DSA_SkyDirections(Uvw);
				const float3 Back = DSA_SkyUvw(D.x, D.y, D.z);
				Off += FMath::Abs(Back.x - Uvw.x) < 1.0e-4f && FMath::Abs(Back.y - Uvw.y) < 1.0e-4f && FMath::Abs(Back.z - Uvw.z) < 1.0e-4f ? 0 : 1;
				// The vectors stand for those: unit, at their elevations, the right angle apart round the horizon.
				const float3 V = DSA_SkyViewVector(D), L = DSA_SkyLightVector(D);
				const float HV = FMath::Sqrt(V.x * V.x + V.y * V.y), HL = FMath::Sqrt(L.x * L.x + L.y * L.y);
				const bool bAround = HV < 1.0e-3f || HL < 1.0e-3f || FMath::IsNearlyEqual((V.x * L.x + V.y * L.y) / (HV * HL), D.y, 1.0e-3f);
				Vectors += FMath::IsNearlyEqual(length(V), 1.0f, 1.0e-4f) && FMath::IsNearlyEqual(length(L), 1.0f, 1.0e-4f)
					&& FMath::IsNearlyEqual(V.z, D.x, 1.0e-4f) && FMath::IsNearlyEqual(L.z, D.z, 1.0e-4f) && bAround ? 0 : 1;
				Range += D.x >= -1.0f && D.x <= 1.0f && D.z >= DSA_SKY_LOWEST_LIGHT - 1.0e-5f && D.z <= 1.0f ? 0 : 1;
			}
		}
	}
	TestEqual(TEXT("the sky table's coordinates round trip"), Off, 0);
	TestEqual(TEXT("its view and light vectors are the directions"), Vectors, 0);
	TestEqual(TEXT("within the table's range (the light from 20 degrees under the horizon)"), Range, 0);
	// Dense at the horizon: the first tenth of the view's elevation above it takes some 30% of the table's half.
	TestTrue(TEXT("the table is dense at the horizon"), DSA_SkyUvw(0.1f, 1.0f, 0.5f).x - 0.5f > 0.15f);
	TestTrue(TEXT("a light deeper under the horizon is held at the table's edge"), FMath::IsNearlyEqual(DSA_SkyUvw(0.0f, 1.0f, -0.9f).z, 0.0f, 1.0e-6f));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDreamAtmosphereSkyTest, "DreamAtmosphere.Model.Sky", DreamAtmosphere::ModelTests::Flags)
bool FDreamAtmosphereSkyTest::RunTest(const FString& Parameters)
{
	using namespace DreamWind::Hlsl;
	using namespace DreamAtmosphere::ModelTests;
	const FDSA_Atmosphere A = Earth();
	constexpr int32 Steps = 24;
	auto Sky = [&](float ViewElevation, float ViewAzimuth, float SunElevation) { return DSA_SkyLuminance(A, 0.0f, Toward(ViewElevation, ViewAzimuth), Toward(SunElevation, 0.0f), Steps); };

	// The multiple scattering's series converges: each event passes on less than all of its light.
	const float3 Multi = DSA_MultiScatteringDirect(A, A.BottomRadius, 0.5f);
	TestTrue(FString::Printf(TEXT("light scattered many times is there and finite (%.4f, %.4f, %.4f)"), Multi.x, Multi.y, Multi.z),
		Multi.x > 0.0f && Multi.z > Multi.x && Multi.z < 10.0f);

	// Noon: a blue zenith, about as bright as clear skies are for their sun (zenith luminance some 3-8 % of the sun's
	// illuminance), and a paler, brighter horizon.
	const float3 Zenith = Sky(90.0f, 0.0f, 60.0f);
	TestTrue(FString::Printf(TEXT("the noon zenith is blue (%.4f, %.4f, %.4f)"), Zenith.x, Zenith.y, Zenith.z), Zenith.z > Zenith.y && Zenith.y > Zenith.x);
	TestTrue(FString::Printf(TEXT("and as bright as a clear sky (%.3f of the sun)"), Luma(Zenith)), Luma(Zenith) > 0.01f && Luma(Zenith) < 0.2f);
	const float3 Horizon = Sky(2.0f, 90.0f, 60.0f);
	TestTrue(FString::Printf(TEXT("the horizon brighter (%.3f)"), Luma(Horizon)), Luma(Horizon) > Luma(Zenith));
	TestTrue(TEXT("and paler"), Horizon.z / Horizon.x < Zenith.z / Zenith.x);
	TestTrue(TEXT("brightest about the sun (the haze's glow)"), Luma(Sky(55.0f, 0.0f, 60.0f)) > 2.0f * Luma(Sky(55.0f, 180.0f, 60.0f)));

	// Sunset: red at the horizon toward the sun, bluer away from it.
	const float3 TowardSun = Sky(1.0f, 0.0f, 2.0f);
	const float3 AwayFromSun = Sky(1.0f, 180.0f, 2.0f);
	TestTrue(FString::Printf(TEXT("the sunset horizon is warm (%.4f, %.4f, %.4f)"), TowardSun.x, TowardSun.y, TowardSun.z), TowardSun.x > TowardSun.z);
	TestTrue(TEXT("warmer toward the sun than away"), TowardSun.x / TowardSun.z > AwayFromSun.x / AwayFromSun.z);

	// The sky dims as the sun sinks, and keeps a glow after it sets until it is far under.
	const float Noon = Luma(Zenith);
	const float Low = Luma(Sky(90.0f, 0.0f, 10.0f));
	const float Set = Luma(Sky(90.0f, 0.0f, 0.0f));
	const float Civil = Luma(Sky(90.0f, 0.0f, -6.0f));
	const float Night = Luma(Sky(90.0f, 0.0f, -18.0f));
	TestTrue(FString::Printf(TEXT("dimmer as the sun sinks (%.4f, %.4f, %.4f)"), Noon, Low, Set), Noon > Low && Low > Set);
	TestTrue(FString::Printf(TEXT("a glow at civil twilight (%.5f of noon)"), Civil / Noon), Civil > 0.0f && Civil < 0.05f * Noon);
	TestTrue(FString::Printf(TEXT("dark by astronomical night (%.6f of noon)"), Night / Noon), Night < 0.002f * Noon);
	// The ozone keeps the twilight zenith blue.
	const float3 Twilight = Sky(90.0f, 0.0f, -4.0f);
	TestTrue(FString::Printf(TEXT("the twilight zenith stays blue (%.6f, %.6f, %.6f)"), Twilight.x, Twilight.y, Twilight.z), Twilight.z > Twilight.x);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
