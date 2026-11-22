// Copyright DreamMoon. All Rights Reserved.

#include "Materials/Material.h"
#include "Materials/MaterialInterface.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace DreamWorld::SurfaceLookTests
{
	struct FScalar
	{
		const TCHAR* Name;
		float Default;
	};

	struct FVector3
	{
		const TCHAR* Name;
		FLinearColor Default;
	};

	/** The realistic look's parameters (MF_DreamSurfaceWeather, MF_DreamSurfaceWeatherTerrain) and their defaults. */
	const FScalar RealisticScalars[] = {
		{ TEXT("WetValueHard"), 0.85f }, { TEXT("WetValuePorous"), 0.4f }, { TEXT("WetRoughHard"), 0.2f }, { TEXT("WetRoughPorous"), 0.35f },
		{ TEXT("PuddleValue"), 0.35f }, { TEXT("PuddleRoughness"), 0.02f }, { TEXT("RippleStrength"), 0.35f },
		{ TEXT("SnowRoughness"), 0.6f }, { TEXT("SnowCoverGain"), 1.0f }, { TEXT("PrintDarken"), 0.12f },
		{ TEXT("DustAmount"), 0.85f }, { TEXT("DustRoughness"), 0.95f },
	};
	const FVector3 RealisticVectors[] = {
		{ TEXT("SnowColor"), FLinearColor(0.88f, 0.9f, 0.93f, 1.0f) }, { TEXT("DustColor"), FLinearColor(0.55f, 0.46f, 0.34f, 1.0f) },
	};

	/** The toon look's parameters (MF_DreamSurfaceWeatherToon) and their defaults. */
	const FScalar ToonScalars[] = {
		{ TEXT("ToonWetValueDrop"), 0.18f }, { TEXT("ToonWetSaturationBoost"), 0.35f }, { TEXT("ToonWetRoughnessScale"), 0.35f },
		{ TEXT("ToonEdgeWidth"), 0.05f }, { TEXT("ToonRoughnessFloor"), 0.4f },
		{ TEXT("ToonPuddleThreshold"), 0.5f }, { TEXT("ToonPuddleValue"), 0.45f }, { TEXT("ToonPuddleSkyReflection"), 0.55f },
		{ TEXT("ToonPuddleRoughness"), 0.05f }, { TEXT("ToonPuddleRimWidth"), 0.2f }, { TEXT("ToonRippleBrightness"), 0.18f },
		{ TEXT("ToonSnowThreshold"), 0.5f }, { TEXT("ToonSnowRoughness"), 0.7f }, { TEXT("ToonPrintThreshold"), 0.5f },
		{ TEXT("ToonDustThreshold"), 0.4f }, { TEXT("ToonDustAmount"), 0.8f }, { TEXT("ToonDustRoughness"), 0.95f },
	};
	const FVector3 ToonVectors[] = {
		{ TEXT("ToonPuddleRimColor"), FLinearColor(0.12f, 0.12f, 0.12f, 1.0f) }, { TEXT("ToonSnowColor"), FLinearColor(0.9f, 0.93f, 1.0f, 1.0f) },
		{ TEXT("ToonDustColor"), FLinearColor(0.6f, 0.5f, 0.36f, 1.0f) },
	};

	template <int32 NumScalars, int32 NumVectors>
	void CheckLook(FAutomationTestBase& Test, const TCHAR* MaterialPath, const FScalar (&Scalars)[NumScalars], const FVector3 (&Vectors)[NumVectors])
	{
		const UMaterialInterface* Material = LoadObject<UMaterialInterface>(nullptr, MaterialPath);
		if (!Test.TestNotNull(*FString::Printf(TEXT("%s (compile the DreamSurface demo materials)"), MaterialPath), Material))
		{
			return;
		}
		for (const FScalar& Scalar : Scalars)
		{
			float Value = -1.0f;
			const bool bFound = Material->GetScalarParameterDefaultValue(FHashedMaterialParameterInfo(FName(Scalar.Name)), Value);
			Test.TestTrue(*FString::Printf(TEXT("%s: a parameter %s"), *Material->GetName(), Scalar.Name), bFound);
			Test.TestEqual(*FString::Printf(TEXT("%s: %s's default"), *Material->GetName(), Scalar.Name), Value, Scalar.Default, 1.0e-4f);
		}
		for (const FVector3& Vector : Vectors)
		{
			FLinearColor Value(FLinearColor::Black);
			const bool bFound = Material->GetVectorParameterDefaultValue(FHashedMaterialParameterInfo(FName(Vector.Name)), Value);
			Test.TestTrue(*FString::Printf(TEXT("%s: a parameter %s"), *Material->GetName(), Vector.Name), bFound);
			Test.TestTrue(*FString::Printf(TEXT("%s: %s's default"), *Material->GetName(), Vector.Name), Value.Equals(Vector.Default, 1.0e-4f));
		}
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDreamSurfaceLookParametersTest, "DreamWorld.SurfaceLook.Parameters",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FDreamSurfaceLookParametersTest::RunTest(const FString& Parameters)
{
	using namespace DreamWorld::SurfaceLookTests;
	// The weather's look on a surface is set in the editor, per material instance: every shift, edge and colour the
	// material functions apply is a parameter, its default the look as it was drawn before.
	CheckLook(*this, TEXT("/DreamDynamicWorld/Surface/Materials/M_DreamSurfaceDemo.M_DreamSurfaceDemo"), RealisticScalars, RealisticVectors);
	CheckLook(*this, TEXT("/DreamDynamicWorld/Surface/Materials/M_DreamSurfaceTerrainDemo.M_DreamSurfaceTerrainDemo"), RealisticScalars, RealisticVectors);
	CheckLook(*this, TEXT("/DreamDynamicWorld/Surface/Materials/M_DreamSurfaceToonDemo.M_DreamSurfaceToonDemo"), ToonScalars, ToonVectors);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
