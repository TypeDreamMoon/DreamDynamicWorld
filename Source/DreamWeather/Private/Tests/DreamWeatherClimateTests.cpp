// Copyright DreamMoon. All Rights Reserved.

#include "DreamWeather.h"
#include "DreamWeatherClimate.h"
#include "DreamWeatherModule.h"
#include "DreamWeatherPreset.h"
#include "DreamWeatherSelector.h"
#include "DreamWeatherSubsystem.h"
#include "DreamWeatherTemperature.h"
#include "DreamWorldSubsystem.h"

#include "Engine/World.h"
#include "Misc/AutomationTest.h"
#include "UObject/Package.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace DreamWeather::ClimateTests
{
	constexpr EAutomationTestFlags Flags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter;

	struct FWorld
	{
		UWorld* World = nullptr;
		UDreamWorldSubsystem* Clock = nullptr;
		UDreamWeatherSubsystem* Weather = nullptr;

		explicit FWorld(UDreamWeatherSelector* Selector, const FDateTime& When = FDateTime(2026, 1, 15, 6, 0, 0))
		{
			World = UWorld::CreateWorld(EWorldType::Game, false);
			Clock = World ? World->GetSubsystem<UDreamWorldSubsystem>() : nullptr;
			Weather = UDreamWeatherSubsystem::Get(World);
			if (!Clock || !Weather)
			{
				return;
			}
			FActorSpawnParameters Spawn;
			Spawn.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
			ADreamWeather* Actor = World->SpawnActor<ADreamWeather>(Spawn);
			Selector->Rename(nullptr, Actor);
			Actor->Selector = Selector;
			FDreamWorldClockConfig Config;
			Config.StartTime = When;
			Clock->ApplyClockConfig(Config, true);
			Clock->ReportView(FVector(0, 0, 170), FRotator::ZeroRotator, true);
			Clock->Tick(0.016f);
		}

		~FWorld()
		{
			if (World)
			{
				World->DestroyWorld(false);
			}
		}

		bool IsValid() const { return Clock && Weather; }
	};

	UDreamWeatherSelector_Fixed* Clear()
	{
		UDreamWeatherSelector_Fixed* Fixed = NewObject<UDreamWeatherSelector_Fixed>();
		Fixed->Preset = NewObject<UDreamWeatherPreset>(GetTransientPackage());
		Fixed->Preset->Channels.Add(Tags::Channel_CloudCoverage, 0.1f);
		return Fixed;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDreamWeatherTemperatureVolumeTest, "DreamWeather.Temperature.Volume", DreamWeather::ClimateTests::Flags)
bool FDreamWeatherTemperatureVolumeTest::RunTest(const FString& Parameters)
{
	using namespace DreamWeather::ClimateTests;
	FWorld W(Clear());
	if (!TestTrue(TEXT("world"), W.IsValid()))
	{
		return false;
	}
	const FVector Fire(1000.0, 0.0, 100.0);
	const float Outside = W.Weather->GetTemperatureAt(Fire);

	FActorSpawnParameters Spawn;
	Spawn.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	ADreamTemperatureVolume* Campfire = W.World->SpawnActor<ADreamTemperatureVolume>(Fire, FRotator::ZeroRotator, Spawn);
	Campfire->Volume->Temperature = 25.0f;
	Campfire->Volume->Extent = FVector(200.0);
	Campfire->Volume->Falloff = 400.0f;
	TestNearlyEqual(TEXT("by the fire: 25 degrees warmer"), W.Weather->GetTemperatureAt(Fire), Outside + 25.0f, 0.01f);
	const float Near = W.Weather->GetTemperatureAt(Fire + FVector(400.0, 0.0, 0.0));
	TestTrue(FString::Printf(TEXT("a few steps away: warmer still, less so (%.1f)"), Near), Near > Outside + 1.0f && Near < Outside + 24.0f);
	TestNearlyEqual(TEXT("far away: as before"), W.Weather->GetTemperatureAt(Fire + FVector(2000.0, 0.0, 0.0)), Outside, 0.01f);

	// A heated house: 20 degrees inside whatever it is outside, 80% of the way.
	ADreamTemperatureVolume* House = W.World->SpawnActor<ADreamTemperatureVolume>(FVector(-3000.0, 0.0, 150.0), FRotator::ZeroRotator, Spawn);
	House->Volume->Mode = EDreamTemperatureMode::Replace;
	House->Volume->Shape = EDreamTemperatureShape::Box;
	House->Volume->Extent = FVector(300.0, 300.0, 150.0);
	House->Volume->Temperature = 20.0f;
	House->Volume->Weight = 0.8f;
	const float OutsideHouse = W.Weather->GetTemperatureAt(FVector(-3000.0, 1000.0, 150.0));
	TestNearlyEqual(TEXT("inside the house: 80% of the way to 20 degrees"), W.Weather->GetTemperatureAt(FVector(-3000.0, 0.0, 150.0)),
		OutsideHouse + (20.0f - OutsideHouse) * 0.8f, 0.05f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDreamWeatherClimateTest, "DreamWeather.Climate.Koppen", DreamWeather::ClimateTests::Flags)
bool FDreamWeatherClimateTest::RunTest(const FString& Parameters)
{
	using namespace DreamWeather::ClimateTests;
	for (int32 t = 0; t < (int32)EDreamKoppenClimate::Num; ++t)
	{
		const FDreamClimate& C = FDreamClimate::Koppen(EDreamKoppenClimate(t));
		for (int32 s = 0; s < 4; ++s)
		{
			float Odds[FDreamClimate::NumKinds];
			C.GetWeatherOdds(s, Odds);
			float Sum = 0.0f;
			for (float O : Odds)
			{
				TestTrue(TEXT("odds are never negative"), O >= 0.0f);
				Sum += O;
			}
			TestTrue(*FString::Printf(TEXT("climate %d season %d has some weather"), t, s), Sum > 0.1f);
		}
	}
	float Odds[FDreamClimate::NumKinds];
	FDreamClimate::Koppen(EDreamKoppenClimate::BWh).GetWeatherOdds(1, Odds);
	TestTrue(TEXT("a hot desert summer: clear, never snow, dust now and then"), Odds[0] > 0.5f && Odds[5] == 0.0f && Odds[7] > 0.0f);
	FDreamClimate::Koppen(EDreamKoppenClimate::Dfb).GetWeatherOdds(3, Odds);
	TestTrue(TEXT("a continental winter snows rather than rains"), Odds[5] > Odds[3]);
	FDreamClimate::Koppen(EDreamKoppenClimate::Am).GetWeatherOdds(1, Odds);
	TestTrue(TEXT("a monsoon summer: storms"), Odds[4] > Odds[0]);

	float Mean, Seasonal, Diurnal;
	FDreamClimate::Koppen(EDreamKoppenClimate::Af).GetTemperature(Mean, Seasonal, Diurnal);
	TestTrue(FString::Printf(TEXT("the rainforest is warm all year (%.1f +- %.1f)"), Mean, Seasonal), Mean > 25.0f && Seasonal < 2.0f);
	FDreamClimate::Koppen(EDreamKoppenClimate::Dfd).GetTemperature(Mean, Seasonal, Diurnal);
	TestTrue(FString::Printf(TEXT("Yakutsk swings wildly (%.1f +- %.1f)"), Mean, Seasonal), Mean < -5.0f && Seasonal > 25.0f);

	// The climate drives the weather's temperature: a subarctic January morning is bitter.
	UDreamWeatherSelector_Climate* Selector = NewObject<UDreamWeatherSelector_Climate>();
	Selector->Climate = EDreamKoppenClimate::Dfc;
	FWorld W(Selector);
	if (TestTrue(TEXT("world"), W.IsValid()))
	{
		const float T = W.Weather->GetTemperatureAt(FVector(0, 0, 0));
		TestTrue(FString::Printf(TEXT("a Dfc January morning is far below freezing (%.1f)"), T), T < -15.0f);
		// Its forecast draws from the climate's odds (some weather, deterministically).
		const TArray<FDreamWeatherForecastEntry> A = W.Weather->GetForecast(240.0f, 12.0f);
		const TArray<FDreamWeatherForecastEntry> B = W.Weather->GetForecast(240.0f, 12.0f);
		bool bSame = A.Num() == B.Num();
		for (int32 i = 0; bSame && i < A.Num(); ++i)
		{
			bSame = A[i].Preset == B[i].Preset;
		}
		TestTrue(TEXT("the same forecast twice"), bSame && A.Num() > 0);
	}
	return true;
}

#endif
