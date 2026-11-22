// Copyright DreamMoon. All Rights Reserved.

#include "DreamAtmosphereSettings.h"
#include "DreamAtmosphereSubsystem.h"
#include "DreamAtmosphereTables.h"
#include "DreamSky.h"
#include "DreamSkyStyle.h"
#include "DreamSkySubsystem.h"
#include "DreamWorldSubsystem.h"

#include "Engine/World.h"
#include "HAL/IConsoleManager.h"
#include "Misc/App.h"
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "RenderingThread.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace DreamSkyAtmosphereTests
{
	constexpr EAutomationTestFlags Flags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter;

	float Luma(const FLinearColor& C)
	{
		return 0.2126f * C.R + 0.7152f * C.G + 0.0722f * C.B;
	}

	void SetBlend(float Value)
	{
		if (IConsoleVariable* Var = IConsoleManager::Get().FindConsoleVariable(TEXT("r.DreamSky.GradientBlendOverride")))
		{
			Var->Set(Value, ECVF_SetByCode);
		}
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDreamSkyAtmosphereBridgeTest, "DreamSky.Atmosphere.PhysicalSky", DreamSkyAtmosphereTests::Flags)
bool FDreamSkyAtmosphereBridgeTest::RunTest(const FString& Parameters)
{
	using namespace DreamSkyAtmosphereTests;
	if (!FApp::CanEverRender() || GUsingNullRHI)
	{
		AddInfo(TEXT("No renderer (-nullrhi): no atmosphere tables, the sky keeps the style's curves. Run with a renderer (-RenderOffscreen)."));
		return true;
	}
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
	if (!TestNotNull(TEXT("world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT
	{
		SetBlend(-1.0f);
		World->DestroyWorld(false);
	};
	UDreamWorldSubsystem* Clock = World->GetSubsystem<UDreamWorldSubsystem>();
	UDreamSkySubsystem* SkySubsystem = World->GetSubsystem<UDreamSkySubsystem>();
	UDreamAtmosphereSubsystem* Atmosphere = UDreamAtmosphereSubsystem::Get(World);
	FActorSpawnParameters Spawn;
	Spawn.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	ADreamSky* Sky = World->SpawnActor<ADreamSky>(Spawn);
	if (!TestNotNull(TEXT("clock"), Clock) || !TestNotNull(TEXT("sky"), SkySubsystem) || !TestNotNull(TEXT("atmosphere"), Atmosphere) || !TestNotNull(TEXT("sky actor"), Sky))
	{
		return false;
	}
	const UDreamSkyStyle* Style = Sky->GetEffectiveStyle();
	if (!TestNotNull(TEXT("style"), Style))
	{
		return false;
	}
	FDreamWorldClockConfig Config;
	Config.StartTime = FDateTime(2026, 6, 21, 14, 0, 0);
	Config.bAnimate = false;
	Clock->ApplyClockConfig(Config, true);
	auto Settle = [&]()
	{
		for (int32 i = 0; i < 3; ++i)
		{
			Atmosphere->Tick(0.0f);
			Clock->Tick(0.0f);
		}
		FlushRenderingCommands();
	};

	// The style's air reaches the atmosphere, its tables are built and read back.
	Settle();
	TestTrue(TEXT("the atmosphere takes the style's air"), Atmosphere->GetParameters().Equals(Style->Atmosphere));
	if (!TestTrue(TEXT("and its sky is ready"), Atmosphere->IsSkyReady()))
	{
		return false;
	}

	// The gradient all the way: the style's curves, exactly.
	SetBlend(1.0f);
	Settle();
	const FDreamSkyLighting Curves = SkySubsystem->GetLighting();
	const FDreamSkyLook CurvesLook = SkySubsystem->GetLook();

	// The physical sky: the sun's light through the air, the sky as bright as the style's (its colour the air's), the fog
	// only the weather's (none in this calm).
	SetBlend(0.0f);
	Settle();
	const FDreamSkyLighting Air = SkySubsystem->GetLighting();
	const FDreamSkyLook AirLook = SkySubsystem->GetLook();
	const float SunZ = float(Clock->GetCelestial().SunDirection.GetSafeNormal().Z);
	const float Expected = Luma(Atmosphere->GetSunLight()) * Luma(Atmosphere->GetTables()->LightTransmittance(SunZ));
	TestTrue(FString::Printf(TEXT("the sun is the air's (%.2f, expected %.2f; the style's %.2f)"), Air.SunIlluminance, Expected, Curves.SunIlluminance),
		FMath::IsNearlyEqual(Air.SunIlluminance, Expected, 0.05f * Expected + 1.0e-3f));
	TestTrue(TEXT("about as bright as the style's sun"), FMath::IsNearlyEqual(Air.SunIlluminance, Curves.SunIlluminance, 0.25f * Curves.SunIlluminance));
	const float StyleSky = 0.5f * (Luma(CurvesLook.Zenith) + Luma(CurvesLook.Horizon));
	const float AirSky = 0.5f * (Luma(AirLook.Zenith) + Luma(AirLook.Horizon));
	TestTrue(FString::Printf(TEXT("the sky as bright as the style's (%.4f against %.4f)"), AirSky, StyleSky), FMath::IsNearlyEqual(AirSky, StyleSky, 0.05f * StyleSky));
	TestTrue(TEXT("a zenith bluer than its horizon"), AirLook.Zenith.B / FMath::Max(AirLook.Zenith.R, 1.0e-6f) > AirLook.Horizon.B / FMath::Max(AirLook.Horizon.R, 1.0e-6f));
	TestTrue(TEXT("the ambient light from it"), Luma(Air.AmbientSky) > 0.0f && !Air.AmbientSky.Equals(Curves.AmbientSky, 1.0e-4f));
	if (UDreamAtmosphereSettings::Get()->bAerialPerspective)
	{
		TestTrue(FString::Printf(TEXT("no clear-weather haze in the height fog (%.5f; the style's %.5f)"), Air.FogDensity, Curves.FogDensity), Air.FogDensity < 0.1f * Curves.FogDensity);
	}

	// Back to the gradient: the curves again, to the last digit.
	SetBlend(1.0f);
	Settle();
	const FDreamSkyLighting Again = SkySubsystem->GetLighting();
	TestTrue(TEXT("the gradient again gives the style's lighting exactly"),
		Again.SunIlluminance == Curves.SunIlluminance && Again.SunColor.Equals(Curves.SunColor, 1.0e-6f) && Again.AmbientSky.Equals(Curves.AmbientSky, 1.0e-6f)
		&& Again.FogDensity == Curves.FogDensity && Again.FogColor.Equals(Curves.FogColor, 1.0e-6f));
	TestTrue(TEXT("and the style's dome colours"), SkySubsystem->GetLook().Zenith.Equals(CurvesLook.Zenith, 1.0e-6f) && SkySubsystem->GetLook().Horizon.Equals(CurvesLook.Horizon, 1.0e-6f));
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
