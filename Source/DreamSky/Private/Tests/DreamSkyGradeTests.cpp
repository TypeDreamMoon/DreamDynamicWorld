// Copyright DreamMoon. All Rights Reserved.

#include "DreamAtmosphereSettings.h"
#include "DreamSkyGrade.h"
#include "DreamSkyStyle.h"
#include "DreamWorldTypes.h"

#include "Misc/AutomationTest.h"
#include "UObject/Package.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace DreamSkyGradeTests
{
	constexpr EAutomationTestFlags Flags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter;

	float Luma(const FLinearColor& C)
	{
		return 0.2126f * C.R + 0.7152f * C.G + 0.0722f * C.B;
	}

	bool Same(const FLinearColor& A, const FLinearColor& B, float Tolerance)
	{
		return FMath::IsNearlyEqual(A.R, B.R, Tolerance) && FMath::IsNearlyEqual(A.G, B.G, Tolerance) && FMath::IsNearlyEqual(A.B, B.B, Tolerance);
	}

	void Key(FRuntimeFloatCurve& Curve, float Value)
	{
		Curve.GetRichCurve()->Reset();
		Curve.GetRichCurve()->AddKey(0.0f, Value);
	}

	void Key(FRuntimeCurveLinearColor& Curve, const FLinearColor& Value)
	{
		const float V[4] = { Value.R, Value.G, Value.B, 1.0f };
		for (int32 c = 0; c < 4; ++c)
		{
			Curve.ColorCurves[c].Reset();
			Curve.ColorCurves[c].AddKey(0.0f, V[c]);
		}
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDreamSkyGradeTest, "DreamSky.Grade", DreamSkyGradeTests::Flags)
bool FDreamSkyGradeTest::RunTest(const FString& Parameters)
{
	using namespace DreamSkyGradeTests;
	const FDreamWorldEnvironment Calm;
	const FLinearColor Sky(0.18f, 0.32f, 0.74f), Warm(0.9f, 0.4f, 0.15f);

	// A new grade leaves the air's colours as they are, anywhere in the sky, at any hour.
	const FDreamSkyGrade Identity;
	int32 Changed = 0;
	for (const float Sun : { -15.0f, -5.0f, 0.0f, 10.0f, 60.0f })
	{
		const FDreamSkyGradeValues V = Identity.Evaluate(Sun, Calm);
		for (const float Z : { -0.2f, 0.0f, 0.3f, 1.0f })
		{
			Changed += Same(V.Apply(Sky, Z), Sky, 1.0e-4f) && Same(V.Apply(Warm, Z), Warm, 1.0e-4f) ? 0 : 1;
		}
	}
	TestEqual(TEXT("a new grade is the identity"), Changed, 0);
	TestEqual(TEXT("and shows the physical sky (no gradient)"), Identity.Evaluate(30.0f, Calm).GradientBlend, 0.0f);

	// Exposure, saturation, hue, contrast.
	FDreamSkyGrade G;
	Key(G.Exposure, 1.0f);
	TestTrue(TEXT("+1 EV doubles"), Same(G.Evaluate(30.0f, Calm).Apply(Sky, 0.5f), Sky * 2.0f, 1.0e-4f));
	G = FDreamSkyGrade();
	Key(G.Saturation, 0.0f);
	const FLinearColor Grey = G.Evaluate(30.0f, Calm).Apply(Sky, 0.5f);
	TestTrue(TEXT("no saturation: a grey of the same luminance"), FMath::IsNearlyEqual(Grey.R, Grey.B, 1.0e-4f) && FMath::IsNearlyEqual(Luma(Grey), Luma(Sky), 1.0e-4f));
	G = FDreamSkyGrade();
	Key(G.HueShift, 120.0f);
	const FLinearColor Turned = G.Evaluate(30.0f, Calm).Apply(FLinearColor(1.0f, 0.0f, 0.0f), 0.5f);
	TestTrue(FString::Printf(TEXT("a third of a turn takes red to green (%.3f, %.3f, %.3f)"), Turned.R, Turned.G, Turned.B), Turned.G > 0.9f && Turned.R < 0.1f);
	G = FDreamSkyGrade();
	Key(G.Contrast, 1.5f);
	const FDreamSkyGradeValues Contrasty = G.Evaluate(30.0f, Calm);
	TestTrue(TEXT("contrast keeps the middle grey"), Same(Contrasty.Apply(FLinearColor(0.18f, 0.18f, 0.18f), 0.5f), FLinearColor(0.18f, 0.18f, 0.18f), 1.0e-4f));
	TestTrue(TEXT("and spreads around it"), Contrasty.Apply(FLinearColor(0.5f, 0.5f, 0.5f), 0.5f).R > 0.5f && Contrasty.Apply(FLinearColor(0.05f, 0.05f, 0.05f), 0.5f).R < 0.05f);

	// The tints: the horizon's at the horizon, the zenith's overhead, a blend between.
	G = FDreamSkyGrade();
	Key(G.ZenithTint, FLinearColor(0.8f, 1.0f, 1.2f));
	Key(G.HorizonTint, FLinearColor(1.2f, 1.0f, 0.8f));
	const FDreamSkyGradeValues Tinted = G.Evaluate(30.0f, Calm);
	const FLinearColor White(1.0f, 1.0f, 1.0f);
	TestTrue(TEXT("the horizon's tint at the horizon"), Same(Tinted.Apply(White, 0.0f), FLinearColor(1.2f, 1.0f, 0.8f), 1.0e-4f));
	TestTrue(TEXT("and below it"), Same(Tinted.Apply(White, -0.3f), FLinearColor(1.2f, 1.0f, 0.8f), 1.0e-4f));
	TestTrue(TEXT("the zenith's overhead"), Same(Tinted.Apply(White, 1.0f), FLinearColor(0.8f, 1.0f, 1.2f), 1.0e-4f));
	const FLinearColor Between = Tinted.Apply(White, 0.3f);
	TestTrue(TEXT("between them part way up"), Between.R < 1.2f && Between.R > 0.8f);

	// Curves follow the sun.
	G = FDreamSkyGrade();
	G.Saturation.GetRichCurve()->Reset();
	G.Saturation.GetRichCurve()->AddKey(0.0f, 1.6f);
	G.Saturation.GetRichCurve()->AddKey(30.0f, 1.0f);
	TestTrue(TEXT("a richer sky at sunset than at noon"), G.Evaluate(0.0f, Calm).Saturation > G.Evaluate(45.0f, Calm).Saturation);

	// The afterglow: the twilight scaled, the day untouched.
	G = FDreamSkyGrade();
	G.Afterglow = 2.0f;
	TestTrue(TEXT("a doubled afterglow in the twilight"), FMath::IsNearlyEqual(G.Evaluate(-5.0f, Calm).ExposureScale, 2.0f, 1.0e-3f));
	TestTrue(TEXT("not by day"), FMath::IsNearlyEqual(G.Evaluate(20.0f, Calm).ExposureScale, 1.0f, 1.0e-4f));
	TestTrue(TEXT("nor deep in the night"), FMath::IsNearlyEqual(G.Evaluate(-20.0f, Calm).ExposureScale, 1.0f, 1.0e-4f));

	// The weather: an overcast greys and dims, fog greys, dust tints; a calm (inactive) environment does nothing.
	FDreamWorldEnvironment Overcast;
	Overcast.bActive = true;
	Overcast.CloudCoverage = 1.0f;
	const FDreamSkyGradeValues Grey2 = Identity.Evaluate(30.0f, Overcast);
	TestTrue(TEXT("an overcast greys"), FMath::IsNearlyEqual(Grey2.Saturation, Identity.OvercastSaturation, 1.0e-4f));
	TestTrue(TEXT("and dims"), Grey2.ExposureScale < 1.0f);
	FDreamWorldEnvironment Dusty;
	Dusty.bActive = true;
	Dusty.Dust = 1.0f;
	const FDreamSkyGradeValues Ochre = Identity.Evaluate(30.0f, Dusty);
	TestTrue(TEXT("dust tints the sky ochre"), Ochre.Tint.R > Ochre.Tint.B);
	FDreamWorldEnvironment Foggy;
	Foggy.bActive = true;
	Foggy.Fog = 1.0f;
	TestTrue(TEXT("fog greys"), Identity.Evaluate(30.0f, Foggy).Saturation < 1.0f);
	FDreamWorldEnvironment Off = Overcast;
	Off.bActive = false;
	TestEqual(TEXT("an inactive weather leaves the grade alone"), Identity.Evaluate(30.0f, Off).Saturation, 1.0f);

	// The packed form the shaders read.
	FVector4f P[4];
	Ochre.Pack(P);
	TestTrue(TEXT("packed as the shared function reads it"), P[1].X == Ochre.Tint.R && P[1].W == Ochre.TintSpread && P[0].X == Ochre.ExposureScale);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDreamSkyStyleAtmosphereTest, "DreamSky.Style.AtmosphereAndGrade", DreamSkyGradeTests::Flags)
bool FDreamSkyStyleAtmosphereTest::RunTest(const FString& Parameters)
{
	const UDreamSkyStyle* Style = NewObject<UDreamSkyStyle>(GetTransientPackage());
	TestTrue(TEXT("a new style's air is the earth's, as the project settings'"), Style->Atmosphere.Equals(FDreamAtmosphereParameters()));
	TestEqual(TEXT("and it shows the physical sky"), Style->Grade.GradientBlend, 0.0f);
	const FDreamSkyGradeValues V = Style->Grade.Evaluate(30.0f, FDreamWorldEnvironment());
	TestTrue(TEXT("graded as the air has it"), FMath::IsNearlyEqual(V.ExposureScale, 1.0f, 1.0e-5f) && V.Saturation == 1.0f && V.Contrast == 1.0f);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
