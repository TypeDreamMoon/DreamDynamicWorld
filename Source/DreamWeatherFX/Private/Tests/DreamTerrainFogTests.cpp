// Copyright DreamMoon. All Rights Reserved.

#include "DreamTerrainFog.h"
#include "DreamGroundFog.h"
#include "DreamWeather.h"
#include "DreamWeatherModule.h"
#include "DreamWeatherPreset.h"
#include "DreamWeatherSelector.h"
#include "DreamWindSubsystem.h"
#include "DreamWorldSubsystem.h"

#include "Engine/Texture2D.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "UObject/Package.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace DreamWeatherFX::TerrainFogTests
{
	constexpr EAutomationTestFlags Flags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter;

	FDreamTerrainFogAir Night()
	{
		// A clear, calm, humid autumn night.
		FDreamTerrainFogAir Air;
		Air.Temperature = 8.0f;
		Air.Humidity = 0.85f;
		Air.Cloud = 0.0f;
		Air.WindSpeed = 1.0f;
		Air.SunElevation = -15.0f;
		return Air;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDreamTerrainFogModelTest, "DreamWeather.FX.TerrainFog.Model", DreamWeatherFX::TerrainFogTests::Flags)
bool FDreamTerrainFogModelTest::RunTest(const FString& Parameters)
{
	using namespace DreamWeatherFX::TerrainFog;
	using DreamWeatherFX::TerrainFogTests::Night;
	const FDreamTerrainFogRules Rules;

	// Radiation fog wants a humid, clear, calm night that is not too cold.
	TestTrue(TEXT("a clear, calm, humid night fogs"), RadiationPotential(Night(), Rules) > 0.9f);
	auto With = [](TFunctionRef<void(FDreamTerrainFogAir&)> Change) { FDreamTerrainFogAir Air = Night(); Change(Air); return Air; };
	TestTrue(TEXT("not in a wind"), RadiationPotential(With([](FDreamTerrainFogAir& A) { A.WindSpeed = 6.0f; }), Rules) < 0.05f);
	TestTrue(TEXT("not under cloud"), RadiationPotential(With([](FDreamTerrainFogAir& A) { A.Cloud = 1.0f; }), Rules) < 0.05f);
	TestTrue(TEXT("not in dry air"), RadiationPotential(With([](FDreamTerrainFogAir& A) { A.Humidity = 0.5f; }), Rules) < 0.05f);
	TestTrue(TEXT("not in a hard frost"), RadiationPotential(With([](FDreamTerrainFogAir& A) { A.Temperature = -20.0f; }), Rules) < 0.05f);
	TestTrue(TEXT("not with the sun up"), RadiationPotential(With([](FDreamTerrainFogAir& A) { A.SunElevation = 20.0f; }), Rules) < 0.05f);
	TestTrue(TEXT("not in the rain"), RadiationPotential(With([](FDreamTerrainFogAir& A) { A.Precipitation = 0.8f; }), Rules) < 0.05f);

	// Through the night it fills in.
	FDreamTerrainFogState State;
	for (int32 i = 0; i < 54; ++i)
	{
		State = Advance(State, Night(), Rules, 1.0f / 6.0f);
	}
	AddInfo(FString::Printf(TEXT("after nine hours of night: %.2f"), State.Radiation));
	TestTrue(TEXT("full by morning"), State.Radiation > 0.9f);

	// After sunrise (the sun climbing ten degrees an hour) it lingers a little, then burns off.
	float HalfHour = -1.0f, TwoHours = -1.0f;
	for (int32 i = 1; i <= 18; ++i)
	{
		FDreamTerrainFogAir Morning = Night();
		Morning.SunElevation = 10.0f * i / 6.0f;
		State = Advance(State, Morning, Rules, 1.0f / 6.0f);
		HalfHour = i == 3 ? State.Radiation : HalfHour;
		TwoHours = i == 12 ? State.Radiation : TwoHours;
	}
	AddInfo(FString::Printf(TEXT("after sunrise: %.2f at half an hour, %.2f at two hours"), HalfHour, TwoHours));
	TestTrue(TEXT("still there half an hour after sunrise"), HalfHour > 0.5f);
	TestTrue(TEXT("gone two hours after"), TwoHours < 0.1f);

	// A wind getting up at night stirs it away.
	State.Radiation = 1.0f;
	for (int32 i = 0; i < 12; ++i)
	{
		State = Advance(State, With([](FDreamTerrainFogAir& A) { A.WindSpeed = 8.0f; }), Rules, 1.0f / 6.0f);
	}
	TestTrue(TEXT("a night wind clears it within two hours"), State.Radiation < 0.2f);

	// Advection fog: moist air the wind brings over a colder surface, at any hour; only where the rules allow it.
	FDreamTerrainFogRules Coast = Rules;
	Coast.bAdvection = true;
	Coast.ColdSurfaceTemperature = 6.0f;
	FDreamTerrainFogAir SeaAir;
	SeaAir.Temperature = 14.0f;
	SeaAir.Humidity = 0.9f;
	SeaAir.Cloud = 0.3f;
	SeaAir.WindSpeed = 4.0f;
	SeaAir.SunElevation = 40.0f;
	TestTrue(TEXT("advection: warm moist air in a breeze over cold water"), AdvectionPotential(SeaAir, Coast) > 0.9f);
	TestTrue(TEXT("advection: not unless the rules allow it"), AdvectionPotential(SeaAir, Rules) == 0.0f);
	TestTrue(TEXT("advection: not in still air"), AdvectionPotential(With([](FDreamTerrainFogAir& A) { A = FDreamTerrainFogAir(); A.Temperature = 14.0f; A.Humidity = 0.9f; A.WindSpeed = 0.0f; }), Coast) < 0.05f);
	FDreamTerrainFogAir Gale = SeaAir;
	Gale.WindSpeed = 12.0f;
	TestTrue(TEXT("advection: a gale lifts it"), AdvectionPotential(Gale, Coast) < 0.05f);
	FDreamTerrainFogAir ColdAir = SeaAir;
	ColdAir.Temperature = 6.0f;
	TestTrue(TEXT("advection: not when the air is no warmer than the water"), AdvectionPotential(ColdAir, Coast) < 0.05f);
	FDreamTerrainFogState Sea;
	for (int32 i = 0; i < 12; ++i)
	{
		Sea = Advance(Sea, SeaAir, Coast, 1.0f / 6.0f);
	}
	AddInfo(FString::Printf(TEXT("advection after two hours at midday: %.2f"), Sea.Advection));
	TestTrue(TEXT("advection: in by midday"), Sea.Advection > 0.5f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDreamTerrainFogActorTest, "DreamWeather.FX.TerrainFog.Actor", DreamWeatherFX::TerrainFogTests::Flags)
bool FDreamTerrainFogActorTest::RunTest(const FString& Parameters)
{
	using namespace DreamWeather::Tags;
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
	if (!TestNotNull(TEXT("world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(false); };
	UDreamWorldSubsystem* Clock = World->GetSubsystem<UDreamWorldSubsystem>();
	UDreamWindSubsystem* Wind = UDreamWindSubsystem::Get(World);
	auto Frame = [Clock, Wind](float Seconds)
	{
		// The clock, then the wind the weather drives.
		Clock->Tick(Seconds);
		if (Wind)
		{
			Wind->Tick(Seconds);
		}
		Clock->Tick(0.0f);
	};
	UDreamWeatherPreset* Clear = NewObject<UDreamWeatherPreset>(GetTransientPackage(), MakeUniqueObjectName(GetTransientPackage(), UDreamWeatherPreset::StaticClass(), TEXT("ClearCalm")));
	Clear->Channels.Add(Channel_CloudCoverage, 0.0f);
	Clear->Channels.Add(Channel_Wind, 0.0f);
	UDreamWeatherSelector_Fixed* Fixed = NewObject<UDreamWeatherSelector_Fixed>();
	Fixed->Preset = Clear;
	FActorSpawnParameters Spawn;
	Spawn.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	ADreamWeather* Weather = World->SpawnActor<ADreamWeather>(Spawn);
	Fixed->Rename(nullptr, Weather);
	Weather->Selector = Fixed;
	FDreamWorldClockConfig Config;
	Config.StartTime = FDateTime(2026, 10, 15, 4, 30, 0);
	Config.bAnimate = false;
	Clock->ApplyClockConfig(Config, true);
	Clock->ReportView(FVector(0, 0, 200), FRotator::ZeroRotator, true);
	Frame(0.016f);
	Frame(0.5f);

	// A bowl two metres deep, 20 m across.
	ADreamTerrainFog* Fog = World->SpawnActor<ADreamTerrainFog>(FVector(0, 0, 0), FRotator::ZeroRotator, Spawn);
	Fog->Extent = FVector(1000.0, 1000.0, 500.0);
	Fog->Cells = 16;
	Fog->PoolDepth = 150.0f;
	Fog->MistThickness = 40.0f;
	Fog->Humidity = 0.9f;
	TArray<float> Bowl;
	for (int32 Y = 0; Y < 16; ++Y)
	{
		for (int32 X = 0; X < 16; ++X)
		{
			const FVector2D P((X + 0.5) / 16.0 * 2.0 - 1.0, (Y + 0.5) / 16.0 * 2.0 - 1.0);
			Bowl.Add(float(200.0 * FMath::Min(P.SizeSquared(), 1.0) - 100.0));
		}
	}
	Fog->SetTerrainHeights(Bowl);
	TestNearlyEqual(TEXT("the ground read back at the middle"), Fog->GetGroundHeightAt(FVector(0, 0, 0)), -100.0f + 200.0f * 2.0f * FMath::Square(1.0f / 16.0f), 2.0f);

	// The hollow: the bowl's bottom lies near two metres under its brim (where it would spill over), its rim none.
	AddInfo(FString::Printf(TEXT("hollow %.0f cm at the bottom, %.0f cm on the rim"), Fog->GetHollowAt(FVector(0, 0, 0)), Fog->GetHollowAt(FVector(950, 950, 0))));
	TestTrue(TEXT("the bottom is a hollow"), Fog->GetHollowAt(FVector(0, 0, 0)) > 150.0f);
	TestTrue(TEXT("the rim is not"), Fog->GetHollowAt(FVector(950, 950, 0)) < 5.0f);

	// Before dawn after a clear, calm night: the bowl is full of fog, deepest at the bottom.
	Fog->Settle();
	const FDreamTerrainFogAir Air = Fog->GetAir();
	const FDreamTerrainFogState Dawn = Fog->GetFogState();
	AddInfo(FString::Printf(TEXT("04:30: %.1f C, humidity %.2f, wind %.1f m/s, sun %.1f deg -> radiation %.2f"), Air.Temperature, Air.Humidity, Air.WindSpeed, Air.SunElevation, Dawn.Radiation));
	TestTrue(TEXT("fog by dawn"), Dawn.Radiation > 0.6f);
	const float Middle = Fog->GetFogDepthAt(FVector(0, 0, 0));
	const float Rim = Fog->GetFogDepthAt(FVector(950, 950, 0));
	AddInfo(FString::Printf(TEXT("fog %.0f cm deep at the bottom, %.0f cm on the rim"), Middle, Rim));
	TestTrue(TEXT("deep at the bottom"), Middle > 100.0f);
	TestTrue(TEXT("only mist on the rim"), Rim <= Fog->MistThickness + 1.0f);
	TestTrue(TEXT("a point low in the bowl is in the fog"), Fog->IsInFog(FVector(0, 0, -60)));
	TestFalse(TEXT("one well above it is not"), Fog->IsInFog(FVector(0, 0, 400)));
	TestFalse(TEXT("nor one outside the box"), Fog->IsInFog(FVector(3000, 0, -60)));

	// Drawn marched (the default): the fog volume shows, its layer the grid, its box round the fog.
	IConsoleVariable* Volumetric = IConsoleManager::Get().FindConsoleVariable(TEXT("DreamWeather.Fog.Volumetric"));
	const int32 WasVolumetric = Volumetric ? Volumetric->GetInt() : 1;
	ON_SCOPE_EXIT { if (Volumetric) { Volumetric->Set(WasVolumetric, ECVF_SetByScalability); } };
	auto SetVolumetric = [Volumetric, Fog](bool bOn)
	{
		if (Volumetric)
		{
			Volumetric->Set(bOn ? 1 : 0, ECVF_SetByScalability);
		}
		Fog->Settle();
	};
	SetVolumetric(true);
	if (TestNotNull(TEXT("the fog volume"), Fog->FogVolume.Get()))
	{
		const FDreamGroundFogParams& P = Fog->FogVolume->GetFogParams();
		AddInfo(FString::Printf(TEXT("marched: radiation %.2f, box %.0f..%.0f cm, pooled fog %.3f/m, mist %.3f/m, noise %.0f cm"),
			P.Radiation, P.BoxLow, P.BoxHigh, P.PoolExtinction, P.MistExtinction, P.NoiseSize));
		TestTrue(TEXT("marched: the fog volume draws at dawn"), Fog->FogVolume->IsVisible());
		TestEqual(TEXT("marched: no local fog volume draws"), Fog->GetVisiblePatchCount(), 0);
		TestNearlyEqual(TEXT("marched: the fog's state"), P.Radiation, Dawn.Radiation, 1.0e-4f);
		TestTrue(TEXT("marched: the box reaches from the lowest ground over the fog's top"),
			P.BoxLow <= Fog->GetGroundHeightAt(FVector(0, 0, 0)) && P.BoxHigh >= Fog->GetGroundHeightAt(FVector(0, 0, 0)) + Middle);
		TestTrue(TEXT("marched: its layer is the grid"), Fog->FogVolume->GetLayerMap() && Fog->FogVolume->GetLayerMap()->GetSizeX() == 16);
		TestNotNull(TEXT("marched: its material (compile M_DreamGroundFog)"), Fog->FogVolume->GetFogMaterial());
	}

	// Drawn with local fog volumes (the low quality level's): on the ground under the fog.
	SetVolumetric(false);
	TestFalse(TEXT("volumes: the fog volume does not draw"), Fog->FogVolume && Fog->FogVolume->IsVisible());
	const int32 DawnPatches = Fog->GetVisiblePatchCount();
	bool bOnTheFog = DawnPatches > 0;
	for (int32 i = 0; i < Fog->Patches; ++i)
	{
		const FVector P = Fog->GetPatchLocation(i);
		if (Fog->GetPatchCount() > i && P != FVector::ZeroVector)
		{
			bOnTheFog &= FMath::Abs(P.Z - Fog->GetGroundHeightAt(P)) < 30.0f && Fog->GetFogDepthAt(P) > Fog->MistThickness;
		}
	}
	AddInfo(FString::Printf(TEXT("%d of %d patches drawing"), DawnPatches, Fog->GetPatchCount()));
	TestTrue(TEXT("the patches lie on the ground under the fog"), bOnTheFog);

	// Mid-morning: burnt off.
	Config.StartTime = FDateTime(2026, 10, 15, 10, 30, 0);
	Clock->ApplyClockConfig(Config, true);
	Frame(0.016f);
	Fog->Refresh();
	const FDreamTerrainFogState Late = Fog->GetFogState();
	AddInfo(FString::Printf(TEXT("10:30: sun %.1f deg -> radiation %.2f, %d patches drawing"), Fog->GetAir().SunElevation, Late.Radiation, Fog->GetVisiblePatchCount()));
	TestTrue(TEXT("gone by mid-morning (the clock's jump settles it)"), Late.Radiation < 0.1f);
	TestTrue(TEXT("fewer patches"), Fog->GetVisiblePatchCount() < DawnPatches);
	SetVolumetric(true);
	TestTrue(TEXT("marched: next to nothing by mid-morning"), !Fog->FogVolume->IsVisible() || Fog->FogVolume->GetFogParams().Radiation < 0.01f);

	// Each terrain fog draws from its own layer (a second one in the same world keeps its own map).
	ADreamTerrainFog* Other = World->SpawnActor<ADreamTerrainFog>(FVector(5000, 0, 0), FRotator::ZeroRotator, Spawn);
	Other->Extent = FVector(1000.0, 1000.0, 500.0);
	Other->Cells = 8;
	TArray<float> Flat;
	Flat.Init(300.0f, 64);
	Other->SetTerrainHeights(Flat);
	TestTrue(TEXT("two terrain fogs, two layers"), Other->FogVolume && Fog->FogVolume && Other->FogVolume->GetLayerMap() != Fog->FogVolume->GetLayerMap()
		&& Fog->FogVolume->GetLayerMap() && Fog->FogVolume->GetLayerMap()->GetSizeX() == 16);

	// Held: full fog whatever the air.
	Fog->bForce = true;
	Fog->ForcedState.Radiation = 1.0f;
	TestNearlyEqual(TEXT("held full: the pool's depth and the mist at the bottom"), Fog->GetFogDepthAt(FVector(0, 0, 0)), 150.0f + 40.0f, 2.0f);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
