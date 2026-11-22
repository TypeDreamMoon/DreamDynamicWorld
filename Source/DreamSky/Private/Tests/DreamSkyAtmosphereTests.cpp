// Copyright DreamMoon. All Rights Reserved.

#include "DreamAtmosphereAerial.h"
#include "DreamAtmosphereSettings.h"
#include "DreamAtmosphereSubsystem.h"
#include "DreamAtmosphereTables.h"
#include "DreamSky.h"
#include "DreamSkyStyle.h"
#include "DreamSkySubsystem.h"
#include "Engine/VolumeTexture.h"
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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDreamSkyAtmosphereBakedTest, "DreamSky.Atmosphere.BakedSky", DreamSkyAtmosphereTests::Flags)
bool FDreamSkyAtmosphereBakedTest::RunTest(const FString& Parameters)
{
	using namespace DreamSkyAtmosphereTests;
	if (!FApp::CanEverRender() || GUsingNullRHI)
	{
		AddInfo(TEXT("No renderer (-nullrhi): nothing to bake from."));
		return true;
	}
	IConsoleVariable* ForceBaked = IConsoleManager::Get().FindConsoleVariable(TEXT("r.DreamAtmosphere.ForceBakedSky"));
	if (!TestNotNull(TEXT("the baked sky's switch"), ForceBaked))
	{
		return false;
	}
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
	if (!TestNotNull(TEXT("world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT
	{
		ForceBaked->Set(0, ECVF_SetByCode);
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
	// A style of the test's own (the default one is an asset), the default's values.
	UDreamSkyStyle* Style = NewObject<UDreamSkyStyle>(GetTransientPackage());
	Sky->Style = Style;
	FDreamWorldClockConfig Config;
	Config.StartTime = FDateTime(2026, 6, 21, 17, 0, 0);
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
	SetBlend(0.0f);
	Settle();
	if (!TestTrue(TEXT("the GPU's sky is ready"), Atmosphere->IsSkyReady() && !Atmosphere->IsSkyBaked()))
	{
		return false;
	}
	const FDreamSkyLighting Gpu = SkySubsystem->GetLighting();
	const FDreamSkyLook GpuLook = SkySubsystem->GetLook();

	// Baked from the same air (as Setup and DreamSky.BakeSky do, into the transient package here).
	FDreamAtmosphereBakedSky Baked;
	UVolumeTexture* Texture = Atmosphere->GetTables()->BakeSky(GetTransientPackage(), TEXT("DreamSkyTestBakedSky"), 1.0e4f, Baked);
	if (!TestNotNull(TEXT("baked"), Texture))
	{
		return false;
	}
	TestTrue(TEXT("a zenith and a horizon for each light elevation"), Baked.Zenith.Num() == Atmosphere->GetTables()->GetCpuSkySize().Z && Baked.IsFor(Style->Atmosphere));
	Style->BakedSky = Baked;

	// As on a GPU that cannot build the tables: the baked sky, lighting the sky as the GPU's did.
	ForceBaked->Set(1, ECVF_SetByCode);
	Settle();
	TestTrue(TEXT("the sky comes from the baked table"), Atmosphere->IsSkyReady() && Atmosphere->IsSkyBaked());
	TestTrue(TEXT("the dome samples the baked texture"), Atmosphere->GetSkyTexture() == Texture);
	TestTrue(TEXT("decoded by its scale"), FMath::IsNearlyEqual(Atmosphere->GetSkyDecode(), 1.0e-4f, 1.0e-9f));
	const FDreamSkyLighting Mobile = SkySubsystem->GetLighting();
	const FDreamSkyLook MobileLook = SkySubsystem->GetLook();
	TestTrue(FString::Printf(TEXT("the same sun (%.3f, the GPU's %.3f)"), Mobile.SunIlluminance, Gpu.SunIlluminance), FMath::IsNearlyEqual(Mobile.SunIlluminance, Gpu.SunIlluminance, 1.0e-3f));
	TestTrue(FString::Printf(TEXT("the same sky: zenith %s against %s"), *MobileLook.Zenith.ToString(), *GpuLook.Zenith.ToString()),
		MobileLook.Zenith.Equals(GpuLook.Zenith, 0.02f * FMath::Max(Luma(GpuLook.Zenith), 1.0e-3f)) && MobileLook.Horizon.Equals(GpuLook.Horizon, 0.02f * FMath::Max(Luma(GpuLook.Horizon), 1.0e-3f)));
	TestTrue(TEXT("the same ambient"), Mobile.AmbientSky.Equals(Gpu.AmbientSky, 0.02f * FMath::Max(Luma(Gpu.AmbientSky), 1.0e-3f)));

	// A bake for other air does not stand for this one: the gradient there.
	Style->BakedSky.BakedFor.MieScattering *= 3.0f;
	Settle();
	TestFalse(TEXT("a bake of other air is not taken"), Atmosphere->IsSkyReady());
	TestEqual(TEXT("and the dome draws the gradient (no table)"), Atmosphere->GetSkyDecode(), 0.0f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDreamSkyAtmosphereWeatherTest, "DreamSky.Atmosphere.AerialWeather", DreamSkyAtmosphereTests::Flags)
bool FDreamSkyAtmosphereWeatherTest::RunTest(const FString& Parameters)
{
	using namespace DreamSkyAtmosphereTests;
	namespace Aerial = DreamAtmosphere::Aerial;

	// The frame: clear weather leaves it exactly as it was; an overcast dims the lights and lends the air a grey.
	Aerial::FFrame Clear;
	Clear.SunLight = FVector3f(11.0f, 10.0f, 9.0f);
	Clear.MoonLight = FVector3f(1.0f, 1.0f, 1.2f);
	const FLinearColor Horizon(0.4f, 0.6f, 1.0f);
	Aerial::FFrame Same = Clear;
	Aerial::ApplyWeather(Same, FDreamAtmosphereWeather(), Horizon);
	TestTrue(TEXT("clear weather: the lights untouched"), Same.SunLight == Clear.SunLight && Same.MoonLight == Clear.MoonLight);
	TestTrue(TEXT("no veil, the identity grade"), Same.VeilLight == FVector3f::ZeroVector && Same.WeatherGrade == FVector4f(1.0f, 1.0f, 1.0f, 1.0f));
	FDreamAtmosphereWeather Deck;
	Deck.DirectLight = 0.25f;
	Deck.DiffuseLight = 0.75f;
	Deck.Saturation = 0.3f;
	Deck.Tint = FLinearColor(0.7f, 0.7f, 0.7f);
	Aerial::FFrame Grey = Clear;
	Aerial::ApplyWeather(Grey, Deck, Horizon);
	TestTrue(TEXT("the sun and the moon dimmed by the direct share"), Grey.SunLight.Equals(Clear.SunLight * 0.25f, 1.0e-5f) && Grey.MoonLight.Equals(Clear.MoonLight * 0.25f, 1.0e-5f));
	TestTrue(TEXT("the veil: the clear horizon's brightness, grey, times the diffuse share"),
		FMath::IsNearlyEqual(Grey.VeilLight.X, Luma(Horizon) * 0.75f, 1.0e-5f) && Grey.VeilLight.X == Grey.VeilLight.Y && Grey.VeilLight.Y == Grey.VeilLight.Z);
	TestTrue(TEXT("the grade carried"), Grey.WeatherGrade.Equals(FVector4f(0.7f, 0.7f, 0.7f, 0.3f), 1.0e-6f));

	// The sky sets it from the weather every frame.
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
	if (!TestNotNull(TEXT("world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(false); };
	UDreamWorldSubsystem* Clock = World->GetSubsystem<UDreamWorldSubsystem>();
	UDreamAtmosphereSubsystem* Atmosphere = UDreamAtmosphereSubsystem::Get(World);
	if (!Atmosphere)
	{
		AddInfo(TEXT("The atmosphere is off in the project settings: nothing to weather."));
		return true;
	}
	FActorSpawnParameters Spawn;
	Spawn.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	ADreamSky* Sky = World->SpawnActor<ADreamSky>(Spawn);
	if (!TestNotNull(TEXT("clock"), Clock) || !TestNotNull(TEXT("sky actor"), Sky) || !TestNotNull(TEXT("style"), Sky->GetEffectiveStyle()))
	{
		return false;
	}
	const UDreamSkyStyle* Style = Sky->GetEffectiveStyle();
	FDreamWorldClockConfig Config;
	Config.StartTime = FDateTime(2026, 6, 21, 12, 0, 0);
	Config.bAnimate = false;
	Clock->ApplyClockConfig(Config, true);
	auto WeatherFor = [&](float Coverage, float Darkness, float Precipitation, float Dust)
	{
		FDreamWorldEnvironment Env;
		Env.bActive = true;
		Env.CloudCoverage = Coverage;
		Env.CloudDarkness = Darkness;
		Env.Precipitation = Precipitation;
		Env.Dust = Dust;
		Clock->SetEnvironment(Env);
		Clock->Tick(0.016f);
		return Atmosphere->GetWeather();
	};

	TestTrue(TEXT("a fair sky: the air as clear as without weather (the noon blue stays)"), WeatherFor(0.3f, 0.0f, 0.0f, 0.0f).IsClear());

	const FDreamAtmosphereWeather Overcast = WeatherFor(1.0f, 0.0f, 0.0f, 0.0f);
	const float Expected = 1.0f - Style->OvercastLightDimming;
	TestTrue(FString::Printf(TEXT("an overcast hides the sun from the air (%.2f of it, expected %.2f)"), Overcast.DirectLight, Expected),
		FMath::IsNearlyEqual(Overcast.DirectLight, Expected, 1.0e-4f));
	TestTrue(FString::Printf(TEXT("and gives the rest back as the deck's grey (%.2f)"), Overcast.DiffuseLight), FMath::IsNearlyEqual(Overcast.DiffuseLight, 1.0f - Expected, 1.0e-4f));
	TestTrue(FString::Printf(TEXT("greyer (saturation %.2f)"), Overcast.Saturation), Overcast.Saturation < 0.9f);
	TestTrue(FString::Printf(TEXT("and dimmer (tint %.2f)"), Luma(Overcast.Tint)), Luma(Overcast.Tint) < 0.99f);

	const FDreamAtmosphereWeather Storm = WeatherFor(1.0f, 1.0f, 1.0f, 0.0f);
	TestTrue(FString::Printf(TEXT("a storm's deck is darker (%.2f against %.2f)"), Storm.DiffuseLight, Overcast.DiffuseLight), Storm.DiffuseLight < Overcast.DiffuseLight);

	const FDreamAtmosphereWeather Dusty = WeatherFor(0.3f, 0.0f, 0.0f, 1.0f);
	TestTrue(TEXT("dust dims the sun in the air and turns it ochre"), Dusty.DirectLight < 1.0f && Dusty.Tint.R > Dusty.Tint.B);

	Clock->SetEnvironment(FDreamWorldEnvironment());
	Clock->Tick(0.016f);
	TestTrue(TEXT("no weather: clear air again"), Atmosphere->GetWeather().IsClear());
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
