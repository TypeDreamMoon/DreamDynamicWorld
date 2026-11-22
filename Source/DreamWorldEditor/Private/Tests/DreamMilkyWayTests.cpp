// Copyright DreamMoon. All Rights Reserved.
//
// The Milky Way Setup makes from noise (DreamMilkyWay.h): its frame on the celestial sphere, and the shape of the band
// -- brightest toward the centre, dark at the galactic poles, the Great Rift's dust north of the plane toward Cygnus.

#include "CoreMinimal.h"
#include "DreamMilkyWay.h"

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace DreamMilkyWayTests
{
	static constexpr EAutomationTestFlags Flags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter;

	FVector3d Equatorial(double RaDeg, double DecDeg)
	{
		const double Ra = FMath::DegreesToRadians(RaDeg), Dec = FMath::DegreesToRadians(DecDeg);
		return FVector3d(FMath::Cos(Dec) * FMath::Cos(Ra), FMath::Cos(Dec) * FMath::Sin(Ra), FMath::Sin(Dec));
	}

	/** Galactic longitude and latitude (degrees) of an equatorial direction. */
	FVector2d Galactic(const FVector3d& E)
	{
		const FVector3d G(FVector3d::DotProduct(E, DreamMilkyWay::CentreAxis()), FVector3d::DotProduct(E, DreamMilkyWay::NinetyAxis()),
			FVector3d::DotProduct(E, DreamMilkyWay::PoleAxis()));
		return FVector2d(FMath::Fmod(FMath::RadiansToDegrees(FMath::Atan2(G.Y, G.X)) + 360.0, 360.0), FMath::RadiansToDegrees(FMath::Asin(G.Z)));
	}

	float Brightness(double L, double B, float* Dust = nullptr)
	{
		FLinearColor Light;
		float D = 0.0f;
		DreamMilkyWay::Evaluate(L, B, Light, D);
		if (Dust)
		{
			*Dust = D;
		}
		return Light.GetLuminance();
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDreamMilkyWayFrameTest, "DreamWorld.MilkyWay.Frame", DreamMilkyWayTests::Flags)
bool FDreamMilkyWayFrameTest::RunTest(const FString& Parameters)
{
	using namespace DreamMilkyWayTests;
	// (Deneb, in Cygnus, at about l 84 b +2; Betelgeuse, toward the anticentre, at about l 200 b -9.)
	const FVector2d Deneb = Galactic(Equatorial(310.358, 45.280));
	TestNearlyEqual(TEXT("Deneb's galactic longitude"), Deneb.X, 84.28, 0.3);
	TestNearlyEqual(TEXT("and latitude"), Deneb.Y, 2.0, 0.3);
	const FVector2d Betelgeuse = Galactic(Equatorial(88.793, 7.407));
	TestNearlyEqual(TEXT("Betelgeuse's galactic longitude"), Betelgeuse.X, 199.79, 0.3);
	TestNearlyEqual(TEXT("and latitude"), Betelgeuse.Y, -8.96, 0.3);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDreamMilkyWayShapeTest, "DreamWorld.MilkyWay.Shape", DreamMilkyWayTests::Flags)
bool FDreamMilkyWayShapeTest::RunTest(const FString& Parameters)
{
	using namespace DreamMilkyWayTests;
	const float Centre = Brightness(0.0, -3.0);
	const float Anticentre = Brightness(180.0, 0.0);
	const float Pole = Brightness(0.0, 85.0);
	TestTrue(FString::Printf(TEXT("brightest toward the centre (%.3f against %.3f)"), Centre, Anticentre), Centre > 2.0f * Anticentre);
	TestTrue(FString::Printf(TEXT("the band against the galactic pole (%.4f against %.4f)"), Anticentre, Pole), Anticentre > 10.0f * Pole);
	float Rift = 0.0f, Away = 0.0f;
	Brightness(40.0, 2.5, &Rift);
	Brightness(-140.0, 8.0, &Away);
	TestTrue(FString::Printf(TEXT("the Great Rift's dust north of the plane toward Cygnus (%.2f against %.2f)"), Rift, Away), Rift > 0.5f && Rift > 4.0f * Away);
	TArray<FColor> Texels;
	DreamMilkyWay::Build(256, 128, Texels);
	int32 Brightest = 0;
	for (int32 Index = 1; Index < Texels.Num(); ++Index)
	{
		Brightest = Texels[Index].R + Texels[Index].G > Texels[Brightest].R + Texels[Brightest].G ? Index : Brightest;
	}
	const int32 X = Brightest % 256, Y = Brightest / 256;
	TestTrue(FString::Printf(TEXT("the map's brightest texel near its middle, the centre (%d, %d)"), X, Y), FMath::Abs(X - 128) < 16 && FMath::Abs(Y - 64) < 12);
	return true;
}

#endif
