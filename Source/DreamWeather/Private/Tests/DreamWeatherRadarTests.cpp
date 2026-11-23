// Copyright DreamMoon. All Rights Reserved.

#include "DreamWeatherRadar.h"
#include "DreamWeatherSubsystem.h"

#include "Engine/TextureRenderTarget2D.h"
#include "Engine/World.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDreamWeatherRadarTest, "DreamWeather.Radar", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FDreamWeatherRadarTest::RunTest(const FString& Parameters)
{
	// The precipitation's steps.
	const FDreamWeatherRadarLook Look;
	TestTrue(TEXT("dry: none"), UDreamWeatherRadar::LevelFor(Look, 0.0f) == EDreamWeatherRadarLevel::None);
	TestTrue(TEXT("a drizzle: light"), UDreamWeatherRadar::LevelFor(Look, 0.1f) == EDreamWeatherRadarLevel::Light);
	TestTrue(TEXT("moderate"), UDreamWeatherRadar::LevelFor(Look, 0.4f) == EDreamWeatherRadarLevel::Moderate);
	TestTrue(TEXT("heavy"), UDreamWeatherRadar::LevelFor(Look, 0.7f) == EDreamWeatherRadarLevel::Heavy);
	TestTrue(TEXT("a cloudburst: extreme"), UDreamWeatherRadar::LevelFor(Look, 0.95f) == EDreamWeatherRadarLevel::Extreme);

	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
	if (!TestNotNull(TEXT("world"), World))
	{
		return false;
	}
	UDreamWeatherRadar* Radar = UDreamWeatherRadar::Get(World);
	if (TestNotNull(TEXT("the radar"), Radar))
	{
		// Held at a point: the point at the middle, north up and east right, the range at the edge.
		Radar->SetCenter(FVector(100000.0, 200000.0, 0.0));
		FDreamWeatherRadarLook L = Radar->GetLook();
		L.RangeKm = 20.0f;
		Radar->SetLook(L);
		TestTrue(TEXT("the middle at the middle"), Radar->WorldToRadar(FVector(100000.0, 200000.0, 50.0)).Equals(FVector2D(0.5, 0.5), 1e-6));
		TestTrue(TEXT("10 km north: halfway up"), Radar->WorldToRadar(FVector(1100000.0, 200000.0, 0.0)).Equals(FVector2D(0.5, 0.25), 1e-6));
		TestTrue(TEXT("20 km east: the right edge"), Radar->WorldToRadar(FVector(100000.0, 2200000.0, 0.0)).Equals(FVector2D(1.0, 0.5), 1e-6));
		const FVector Back = Radar->RadarToWorld(FVector2D(0.2, 0.7));
		TestTrue(TEXT("and back"), Radar->WorldToRadar(Back).Equals(FVector2D(0.2, 0.7), 1e-6));
		// The texture made on asking, at the look's size.
		UTextureRenderTarget2D* Texture = Radar->GetRadarTexture();
		TestTrue(TEXT("its texture"), Texture != nullptr);
		// What it shows where there is no weather: nothing; no rain to find.
		const FDreamWeatherRadarSample Here = Radar->SampleRadar(FVector::ZeroVector);
		TestTrue(TEXT("no weather here: nothing"), Here.Level == EDreamWeatherRadarLevel::None);
		FVector Found;
		float Km = 0.0f;
		TestFalse(TEXT("no rain to find"), Radar->FindNearestPrecipitation(FVector::ZeroVector, 5.0f, 0.1f, Found, Km));
		// A surface showing it takes its look.
		if (UMaterialInterface* Material = UDreamWeatherRadar::GetRadarMaterial())
		{
			UMaterialInstanceDynamic* Instance = UMaterialInstanceDynamic::Create(Material, GetTransientPackage());
			Radar->ApplyTo(Instance);
			FLinearColor View;
			TestTrue(TEXT("its middle and range set on a surface"), Instance->GetVectorParameterValue(FHashedMaterialParameterInfo(TEXT("RadarView")), View)
				&& FMath::IsNearlyEqual(View.B, 2000000.0f, 1.0f) && FMath::IsNearlyEqual(View.R, 100000.0f, 1.0f));
		}
		else
		{
			AddError(TEXT("M_DreamWeatherRadar is missing"));
		}
	}
	World->DestroyWorld(false);
	return true;
}

#endif
