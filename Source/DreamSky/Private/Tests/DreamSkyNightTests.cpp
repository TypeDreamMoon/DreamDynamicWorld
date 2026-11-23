// Copyright DreamMoon. All Rights Reserved.

#include "DreamSky.h"
#include "DreamSkyCloudRenderer.h"
#include "DreamSkyFunnels.h"
#include "DreamSkyModifier.h"
#include "DreamSkyEclipse.h"
#include "DreamSkyModule.h"
#include "DreamSkyStyle.h"
#include "DreamSkySubsystem.h"
#include "DreamWorldSettings.h"
#include "DreamWorldSubsystem.h"
#include "DreamWorldQuality.h"
#include "HAL/IConsoleManager.h"

#include "Components/DirectionalLightComponent.h"
#include "Engine/World.h"
#include "Materials/MaterialParameterCollection.h"
#include "Materials/MaterialParameterCollectionInstance.h"
#include "Misc/AutomationTest.h"
#include "UObject/Package.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace DreamSky::NightTests
{
	constexpr EAutomationTestFlags Flags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter;

	/** A world with a sky, its clock at Date and Latitude (realistic), stopped. */
	struct FNightWorld
	{
		UWorld* World = nullptr;
		UDreamWorldSubsystem* Clock = nullptr;
		UDreamSkySubsystem* SkySubsystem = nullptr;
		ADreamSky* Sky = nullptr;

		FNightWorld(const FDateTime& Date, double Latitude, double Longitude, float UtcOffset)
		{
			World = UWorld::CreateWorld(EWorldType::Game, false);
			if (!World)
			{
				return;
			}
			Clock = World->GetSubsystem<UDreamWorldSubsystem>();
			SkySubsystem = World->GetSubsystem<UDreamSkySubsystem>();
			FActorSpawnParameters Params;
			Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
			Sky = World->SpawnActor<ADreamSky>(Params);
			FDreamWorldClockConfig Config;
			Config.StartTime = Date;
			Config.bAnimate = false;
			Config.Celestial.Mode = EDreamCelestialMode::Realistic;
			Config.Celestial.Latitude = Latitude;
			Config.Celestial.Longitude = Longitude;
			Config.Celestial.UtcOffsetHours = UtcOffset;
			Clock->ApplyClockConfig(Config, true);
			Clock->Tick(0.016f);
		}
		~FNightWorld()
		{
			if (World)
			{
				World->DestroyWorld(false);
			}
		}
		bool IsValid() const { return World && Clock && SkySubsystem && Sky; }

		FLinearColor Parameter(FName Name) const
		{
			FLinearColor Value = FLinearColor::Transparent;
			const UMaterialParameterCollection* Collection = Cast<UMaterialParameterCollection>(UDreamWorldSettings::Get()->ParameterCollection.TryLoad());
			const UMaterialParameterCollectionInstance* Instance = Collection ? World->GetParameterCollectionInstance(Collection) : nullptr;
			if (Instance)
			{
				Instance->GetVectorParameterValue(Name, Value);
			}
			return Value;
		}
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDreamSkyAuroraTest, "DreamSky.Night.Aurora", DreamSky::NightTests::Flags)
bool FDreamSkyAuroraTest::RunTest(const FString& Parameters)
{
	using namespace DreamSky::NightTests;
	using namespace DreamSky::Names;

	// Where it shows: by latitude, either hemisphere, in the realistic mode; as told in the artistic one.
	UDreamSkyStyle* Style = NewObject<UDreamSkyStyle>(GetTransientPackage());
	FDreamCelestialSettings Settings;
	Settings.Mode = EDreamCelestialMode::Realistic;
	Settings.Latitude = 70.0;
	TestEqual(TEXT("in full at 70 N"), Style->EvaluateAuroraPresence(Settings), 1.0f);
	Settings.Latitude = -70.0;
	TestEqual(TEXT("in full at 70 S"), Style->EvaluateAuroraPresence(Settings), 1.0f);
	Settings.Latitude = 35.0;
	TestEqual(TEXT("none at 35 N"), Style->EvaluateAuroraPresence(Settings), 0.0f);
	Settings.Latitude = 58.0;
	TestNearlyEqual(TEXT("half way at 58 N"), Style->EvaluateAuroraPresence(Settings), 0.5f, 1e-4f);
	Settings.Mode = EDreamCelestialMode::Artistic;
	TestEqual(TEXT("artistic: none unless told"), Style->EvaluateAuroraPresence(Settings), 0.0f);
	Style->Aurora.ArtisticPresence = 0.6f;
	TestNearlyEqual(TEXT("artistic: as told"), Style->EvaluateAuroraPresence(Settings), 0.6f, 1e-6f);
	TestEqual(TEXT("not by day"), UDreamSkyStyle::AuroraNight(20.0f), 0.0f);
	TestEqual(TEXT("not in the civil twilight"), UDreamSkyStyle::AuroraNight(-5.0f), 0.0f);
	TestEqual(TEXT("in full at night"), UDreamSkyStyle::AuroraNight(-20.0f), 1.0f);

	// Tromso at midwinter midnight: the aurora's parameters say so, toward the north (+X, north unturned).
	{
		FNightWorld W(FDateTime(2026, 12, 21, 0, 0, 0), 69.65, 18.96, 1.0f);
		if (!TestTrue(TEXT("north world"), W.IsValid()))
		{
			return false;
		}
		const FLinearColor A = W.Parameter(MPC_Aurora);
		TestTrue(FString::Printf(TEXT("strong in the arctic night (%.4f)"), A.R), FMath::Abs(A.R) > 0.5f * W.Sky->GetEffectiveStyle()->Aurora.Brightness);
		TestTrue(TEXT("its own soft light under the toon clouds too, by default (positive)"), A.R > 0.0f);
		UDreamSkyStyle* TestStyle = W.Sky->GetEffectiveStyle();
		TestStyle->Aurora.bStylizedTones = true;
		W.Clock->Tick(0.016f);
		TestTrue(TEXT("in the toon clouds' banded tones when the style asks (negative)"), W.Parameter(MPC_Aurora).R < 0.0f);
		W.Sky->CloudLook = EDreamSkyCloudLook::Realistic;
		W.Clock->Tick(0.016f);
		TestTrue(TEXT("under realistic clouds, smooth whatever the style asks (positive)"), W.Parameter(MPC_Aurora).R > 0.0f);
		TestStyle->Aurora.bStylizedTones = false;
		TestTrue(TEXT("toward the north"), A.B > 0.99f && FMath::Abs(A.A) < 0.01f);
		TestNearlyEqual(TEXT("the arc's distance"), W.Parameter(MPC_AuroraLow).A, W.Sky->GetEffectiveStyle()->Aurora.ArcDistance, 1e-3f);
	}
	// The south's points south.
	{
		FNightWorld W(FDateTime(2026, 6, 21, 0, 0, 0), -69.0, 140.0, 9.0f);
		if (!TestTrue(TEXT("south world"), W.IsValid()))
		{
			return false;
		}
		const FLinearColor A = W.Parameter(MPC_Aurora);
		TestTrue(TEXT("strong in the antarctic night"), FMath::Abs(A.R) > 0.0f);
		TestTrue(TEXT("toward the south"), A.B < -0.99f);
	}
	// Tokyo: none, until a modifier brings it; by day not even then.
	{
		FNightWorld W(FDateTime(2026, 12, 21, 0, 0, 0), 35.68, 139.69, 9.0f);
		if (!TestTrue(TEXT("Tokyo world"), W.IsValid()))
		{
			return false;
		}
		TestEqual(TEXT("none at Tokyo"), W.Parameter(MPC_Aurora).R, 0.0f);
		UDreamSkyModifier* Bring = NewObject<UDreamSkyModifier>(GetTransientPackage());
		Bring->Settings.bOverride_Aurora = true;
		Bring->Settings.Aurora = 1.0f;
		W.SkySubsystem->AddModifier(Bring, 0.0f);
		W.Clock->Tick(0.016f);
		TestTrue(TEXT("a modifier brings it at night"), FMath::Abs(W.Parameter(MPC_Aurora).R) > 0.0f);
		W.Clock->SetTimeOfDay(12.0f);
		W.Clock->Tick(0.016f);
		TestEqual(TEXT("but not by day"), W.Parameter(MPC_Aurora).R, 0.0f);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDreamSkyNightGlowTest, "DreamSky.Night.GlowAndLightPollution", DreamSky::NightTests::Flags)
bool FDreamSkyNightGlowTest::RunTest(const FString& Parameters)
{
	using namespace DreamSky::NightTests;
	using namespace DreamSky::Names;
	TestEqual(TEXT("no glow by day"), UDreamSkyStyle::GlowNight(10.0f), 0.0f);
	TestEqual(TEXT("in full by the end of the civil twilight"), UDreamSkyStyle::GlowNight(-6.0f), 1.0f);

	FNightWorld W(FDateTime(2026, 12, 21, 0, 0, 0), 35.68, 139.69, 9.0f);
	if (!TestTrue(TEXT("world"), W.IsValid()))
	{
		return false;
	}
	const UDreamSkyStyle* Style = W.Sky->GetEffectiveStyle();
	FLinearColor Glow = W.Parameter(MPC_NightGlow);
	TestTrue(TEXT("the airglow at night"), Glow.G > 0.0f && FMath::IsNearlyEqual(Glow.G, Style->NightGlow.Airglow.G, 1e-6f));
	TestNearlyEqual(TEXT("how much the bases catch"), Glow.A, Style->NightGlow.CloudUnderLight, 1e-6f);
	TestEqual(TEXT("no city by default (the wilderness)"), W.Parameter(MPC_LightPollution).R, 0.0f);

	// A modifier brings a city's light: at the horizon its colour times the strength.
	UDreamSkyModifier* City = NewObject<UDreamSkyModifier>(GetTransientPackage());
	City->Settings.bOverride_LightPollution = true;
	City->Settings.LightPollution = 0.06f;
	City->Settings.bOverride_Airglow = true;
	City->Settings.Airglow = 2.0f;
	W.SkySubsystem->AddModifier(City, 0.0f);
	W.Clock->Tick(0.016f);
	const FLinearColor Light = W.Parameter(MPC_LightPollution);
	TestNearlyEqual(TEXT("the city's light, in its colour"), Light.R, 0.06f * Style->NightGlow.LightPollutionColor.R, 1e-4f);
	TestNearlyEqual(TEXT("how high it climbs"), Light.A, Style->NightGlow.Climb, 1e-5f);
	TestNearlyEqual(TEXT("the airglow doubled"), W.Parameter(MPC_NightGlow).G, 2.0f * Style->NightGlow.Airglow.G, 1e-6f);

	// By day neither shows.
	W.Clock->SetTimeOfDay(12.0f);
	W.Clock->Tick(0.016f);
	TestEqual(TEXT("no city light by day"), W.Parameter(MPC_LightPollution).R, 0.0f);
	TestEqual(TEXT("no airglow by day"), W.Parameter(MPC_NightGlow).G, 0.0f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDreamSkyEclipseTest, "DreamSky.Night.Eclipses", DreamSky::NightTests::Flags)
bool FDreamSkyEclipseTest::RunTest(const FString& Parameters)
{
	using namespace DreamSky::NightTests;
	using namespace DreamSky::Names;
	using namespace DreamSky::Eclipse;

	// The overlap of two disks.
	TestEqual(TEXT("central: covered"), DiskOverlap(0.0f, 1.03f), 1.0f);
	TestEqual(TEXT("apart: nothing"), DiskOverlap(2.5f, 1.03f), 0.0f);
	TestNearlyEqual(TEXT("two unit disks a radius apart: the lens"), DiskOverlap(1.0f, 1.0f), (2.0f * UE_PI / 3.0f - FMath::Sqrt(3.0f) / 2.0f) / UE_PI, 1e-4f);
	TestNearlyEqual(TEXT("a smaller disk inside: its area"), DiskOverlap(0.1f, 0.5f), 0.25f, 1e-5f);
	TestNearlyEqual(TEXT("the reverse"), DistanceForOverlap(DiskOverlap(0.7f, 1.03f), 1.03f), 0.7f, 1e-3f);

	// The moon half a sun radius off the sun's centre, along the disk's X.
	{
		const FVector Sun = FVector(1.0, 0.0, 1.0).GetSafeNormal();
		FVector X, Y;
		DiskFrame(Sun, X, Y);
		const FVector Moon = (Sun + X * (0.5 * SunRadius)).GetSafeNormal();
		FVector2D On;
		const float Covered = Solar(Sun, Moon, On);
		TestNearlyEqual(TEXT("the moon's place on the sun"), float(On.X), 0.5f, 2e-3f);
		TestNearlyEqual(TEXT("and how much it covers"), Covered, DiskOverlap(0.5f, float(MoonToSun)), 2e-3f);
		FVector2D Away;
		TestEqual(TEXT("behind the view, no eclipse"), Solar(Sun, -Moon, Away), 0.0f);
	}

	// The total lunar eclipse of 2026-03-03, at its middle (11:33 UT) from Tokyo, the moon up: the umbra on the moon;
	// a week later none.
	{
		FNightWorld W(FDateTime(2026, 3, 3, 20, 33, 0), 35.68, 139.69, 9.0f);
		if (!TestTrue(TEXT("Tokyo world"), W.IsValid()))
		{
			return false;
		}
		const FLinearColor Lunar = W.Parameter(MPC_LunarEclipse);
		TestTrue(FString::Printf(TEXT("the moon in the umbra (%.3f)"), Lunar.R), Lunar.R > 0.5f);
		TestNearlyEqual(TEXT("the umbra's radius in moon radii"), Lunar.A, float(UmbraRadius / MoonRadius), 1e-3f);
		W.Clock->SetDateTime(FDateTime(2026, 3, 10, 20, 33, 0));
		W.Clock->Tick(0.016f);
		TestEqual(TEXT("a week later none"), W.Parameter(MPC_LunarEclipse).R, 0.0f);
	}

	// A scripted solar eclipse anywhere: total, the sun's light gone, the corona showing, the moon placed on the sun.
	{
		FNightWorld W(FDateTime(2026, 6, 21, 12, 0, 0), 35.68, 139.69, 9.0f);
		if (!TestTrue(TEXT("noon world"), W.IsValid()))
		{
			return false;
		}
		const float Sun = W.Sky->SunLight->Intensity;
		TestTrue(TEXT("no eclipse at noon on an ordinary day"), W.Parameter(MPC_Eclipse).R == 0.0f && Sun > 0.0f);
		const FLinearColor DayZenith = W.SkySubsystem->GetLook().Zenith;
		const float DayEV = W.SkySubsystem->GetLighting().ExposureEV100;
		UDreamSkyModifier* Eclipse = NewObject<UDreamSkyModifier>(GetTransientPackage());
		Eclipse->Settings.bOverride_SolarEclipse = true;
		Eclipse->Settings.SolarEclipse = 1.0f;
		W.SkySubsystem->SetModifierAlpha(Eclipse, 0.5f);
		W.Clock->Tick(0.016f);
		const FLinearColor Half = W.Parameter(MPC_Eclipse);
		TestNearlyEqual(TEXT("half covered"), Half.R, 0.5f, 1e-4f);
		TestNearlyEqual(TEXT("the moon placed to cover that much"), DiskOverlap(FVector2D(Half.G, Half.B).Size(), float(MoonToSun)), 0.5f, 2e-3f);
		TestNearlyEqual(TEXT("half the sun's light"), W.Sky->SunLight->Intensity, 0.5f * Sun, 0.01f * Sun);
		TestEqual(TEXT("no corona yet"), Half.A, 0.0f);
		TestTrue(TEXT("half covered: the sky as bright as ever (the eye makes up for it)"), W.SkySubsystem->GetLook().Zenith.Equals(DayZenith, 1e-4f));
		TestNearlyEqual(TEXT("and the exposure as it was"), W.SkySubsystem->GetLighting().ExposureEV100, DayEV, 1e-4f);
		W.SkySubsystem->SetModifierAlpha(Eclipse, 1.0f);
		W.Clock->Tick(0.016f);
		const FLinearColor Total = W.Parameter(MPC_Eclipse);
		TestEqual(TEXT("total"), Total.R, 1.0f);
		TestTrue(TEXT("the corona at totality"), Total.A > 0.0f);
		TestTrue(TEXT("the sun's light gone"), W.Sky->SunLight->Intensity <= 1e-4f * FMath::Max(Sun, 1.0f));
		const UDreamSkyStyle& Style = *W.Sky->GetEffectiveStyle();
		const FLinearColor Ring = W.SkySubsystem->GetLook().Horizon;
		TestTrue(TEXT("the horizon all round the orange ring"), Ring.Equals(FLinearColor(Style.Eclipse.TotalityRing.R, Style.Eclipse.TotalityRing.G, Style.Eclipse.TotalityRing.B, 1.0f), 1e-4f));
		TestTrue(TEXT("the zenith a deep blue, darker than by day"), W.SkySubsystem->GetLook().Zenith.B < DayZenith.B && W.SkySubsystem->GetLook().Zenith.B > W.SkySubsystem->GetLook().Zenith.R);
		TestTrue(TEXT("the exposure opened for the twilight"), W.SkySubsystem->GetLighting().ExposureEV100 < DayEV);
		const FLinearColor Stars = W.Parameter(MPC_Stars);
		TestTrue(TEXT("the stars out"), Stars.R > 0.99f);
		TestNearlyEqual(TEXT("only the brightest"), Stars.A, 1.0f - Style.Eclipse.TotalityStars, 1e-4f);
		W.SkySubsystem->SetModifierAlpha(Eclipse, 0.0f);
		W.Clock->Tick(0.016f);
		TestEqual(TEXT("over: every star again (none by day anyway)"), W.Parameter(MPC_Stars).A, 0.0f);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDreamSkyLightShaftsTest, "DreamSky.LightShafts", DreamSky::NightTests::Flags)
bool FDreamSkyLightShaftsTest::RunTest(const FString& Parameters)
{
	using namespace DreamSky::NightTests;

	FNightWorld W(FDateTime(2026, 6, 21, 12, 0, 0), 35.68, 139.69, 9.0f);
	if (!TestTrue(TEXT("noon world"), W.IsValid()))
	{
		return false;
	}
	auto Cover = [&](float Coverage)
	{
		UDreamSkyModifier* M = NewObject<UDreamSkyModifier>(GetTransientPackage());
		M->Settings.bOverride_CloudCoverage = true;
		M->Settings.CloudCoverage = Coverage;
		return M;
	};
	const float Intensity = W.Sky->GetEffectiveStyle()->LightShafts.Intensity;
	UDreamSkyModifier* Clear = Cover(0.0f);
	W.SkySubsystem->SetModifierAlpha(Clear, 1.0f);
	W.Clock->Tick(0.016f);
	TestEqual(TEXT("a clear sky casts none"), W.SkySubsystem->GetLightShaftStrength(), 0.0f);
	W.SkySubsystem->RemoveModifier(Clear);
	UDreamSkyModifier* Broken = Cover(0.5f);
	W.SkySubsystem->SetModifierAlpha(Broken, 1.0f);
	W.Clock->Tick(0.016f);
	TestNearlyEqual(TEXT("broken clouds at noon: the style's intensity"), W.SkySubsystem->GetLightShaftStrength(), Intensity, 1e-4f);
	UDreamSkyModifier* Brighter = NewObject<UDreamSkyModifier>(GetTransientPackage());
	Brighter->Settings.bOverride_LightShafts = true;
	Brighter->Settings.LightShafts = 2.0f;
	W.SkySubsystem->SetModifierAlpha(Brighter, 1.0f);
	W.Clock->Tick(0.016f);
	TestNearlyEqual(TEXT("a modifier scales them"), W.SkySubsystem->GetLightShaftStrength(), 2.0f * Intensity, 1e-3f);
	W.SkySubsystem->SetModifierAlpha(Brighter, 0.5f);
	W.Clock->Tick(0.016f);
	TestNearlyEqual(TEXT("half in: along the logarithm"), W.SkySubsystem->GetLightShaftStrength(), FMath::Sqrt(2.0f) * Intensity, 1e-3f);
	W.Clock->SetDateTime(FDateTime(2026, 6, 21, 23, 0, 0));
	W.Clock->Tick(0.016f);
	// (The moon's light makes them as well, while it is the main light and up.)
	const FDreamSkyLighting& Night = W.SkySubsystem->GetLighting();
	if (!Night.bMainLightIsMoon || Night.MainLightDirection.Z <= 0.0)
	{
		TestEqual(TEXT("none with the sun down"), W.SkySubsystem->GetLightShaftStrength(), 0.0f);
	}
	W.SkySubsystem->RemoveModifier(Broken);
	W.SkySubsystem->RemoveModifier(Brighter);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDreamSkyLensFlareTest, "DreamSky.LensFlare", DreamSky::NightTests::Flags)
bool FDreamSkyLensFlareTest::RunTest(const FString& Parameters)
{
	using namespace DreamSky::NightTests;

	FNightWorld W(FDateTime(2026, 6, 21, 12, 0, 0), 35.68, 139.69, 9.0f);
	if (!TestTrue(TEXT("noon world"), W.IsValid()))
	{
		return false;
	}
	const FDreamSkyLensFlare& Flare = W.Sky->GetEffectiveStyle()->LensFlare;
	TestTrue(TEXT("a lens effect: none unless the project turns it on"), Flare.Preset == EDreamSkyLensFlarePreset::None);
	TestNearlyEqual(TEXT("as bright as the style says"), W.SkySubsystem->GetLook().LensFlare, Flare.Intensity, 1e-5f);
	UDreamSkyModifier* Dim = NewObject<UDreamSkyModifier>(GetTransientPackage());
	Dim->Settings.bOverride_LensFlare = true;
	Dim->Settings.LensFlare = 0.25f;
	W.SkySubsystem->SetModifierAlpha(Dim, 1.0f);
	W.Clock->Tick(0.016f);
	TestNearlyEqual(TEXT("a modifier scales it"), W.SkySubsystem->GetLook().LensFlare, 0.25f * Flare.Intensity, 1e-4f);
	W.SkySubsystem->RemoveModifier(Dim);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDreamSkyScreenWetnessTest, "DreamSky.ScreenWetness", DreamSky::NightTests::Flags)
bool FDreamSkyScreenWetnessTest::RunTest(const FString& Parameters)
{
	using namespace DreamSky::NightTests;

	FNightWorld W(FDateTime(2026, 6, 21, 12, 0, 0), 35.68, 139.69, 9.0f);
	if (!TestTrue(TEXT("noon world"), W.IsValid()))
	{
		return false;
	}
	TestTrue(TEXT("drops on the lens are the project's choice: off by default"), !W.Sky->GetEffectiveStyle()->ScreenDroplets.bEnabled);
	TestEqual(TEXT("dry with no weather"), W.SkySubsystem->GetScreenWetness(), 0.0f);
	auto Weather = [&W](float Rain, float Snow)
	{
		FDreamWorldEnvironment Env;
		Env.bActive = true;
		Env.Precipitation = Rain;
		Env.SnowRatio = Snow;
		W.Clock->SetEnvironment(Env);
	};
	// (The clock is stopped and nothing jumps: the lens follows over its time.)
	Weather(1.0f, 0.0f);
	W.Clock->Tick(0.5f);
	if (!W.Clock->GetEnvironment().bActive || W.Clock->GetEnvironment().Precipitation < 0.99f)
	{
		AddWarning(TEXT("the world's weather was set over the test's: the lens is not checked"));
		return true;
	}
	const float Early = W.SkySubsystem->GetScreenWetness();
	TestTrue(FString::Printf(TEXT("out in the rain it starts to gather (%.3f)"), Early), Early > 0.0f && Early < 0.5f);
	for (int32 Step = 0; Step < 60; ++Step)
	{
		W.Clock->Tick(0.5f);
	}
	const float Wet = W.SkySubsystem->GetScreenWetness();
	TestTrue(FString::Printf(TEXT("and gets wet through (%.3f)"), Wet), Wet > 0.4f);
	Weather(0.0f, 0.0f);
	W.Clock->Tick(0.5f);
	const float Drying = W.SkySubsystem->GetScreenWetness();
	TestTrue(FString::Printf(TEXT("the rain over: it dries off slowly (%.3f)"), Drying), Drying < Wet && Drying > 0.5f * Wet);
	Weather(1.0f, 1.0f);
	W.Clock->SetCinematicModeOverride(true);
	W.Clock->Tick(0.016f);
	TestEqual(TEXT("snow leaves no drops; a cinematic frame is where it is heading at once"), W.SkySubsystem->GetScreenWetness(), 0.0f);
	Weather(1.0f, 0.0f);
	W.Clock->Tick(0.016f);
	TestTrue(TEXT("and at once wet in the rain"), W.SkySubsystem->GetScreenWetness() > 0.4f);
	W.Clock->SetCinematicModeOverride(false);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDreamSkyScreenFrostTest, "DreamSky.ScreenFrost", DreamSky::NightTests::Flags)
bool FDreamSkyScreenFrostTest::RunTest(const FString& Parameters)
{
	using namespace DreamSky::NightTests;

	FNightWorld W(FDateTime(2026, 1, 15, 12, 0, 0), 35.68, 139.69, 9.0f);
	if (!TestTrue(TEXT("noon world"), W.IsValid()))
	{
		return false;
	}
	TestTrue(TEXT("frost on the lens is the project's choice: off by default"), !W.Sky->GetEffectiveStyle()->ScreenFrost.bEnabled);
	TestEqual(TEXT("clear with no weather"), W.SkySubsystem->GetScreenFrost(), 0.0f);
	auto Weather = [&W](float Precipitation, float Snow, float Temperature)
	{
		FDreamWorldEnvironment Env;
		Env.bActive = true;
		Env.Precipitation = Precipitation;
		Env.SnowRatio = Snow;
		Env.Temperature = Temperature;
		W.Clock->SetEnvironment(Env);
	};
	auto Run = [&W](float Seconds)
	{
		for (float T = 0.0f; T < Seconds; T += 0.5f)
		{
			W.Clock->Tick(0.5f);
		}
	};
	Weather(1.0f, 1.0f, 5.0f);
	W.Clock->Tick(0.5f);
	if (!W.Clock->GetEnvironment().bActive || W.Clock->GetEnvironment().Temperature != 5.0f)
	{
		AddWarning(TEXT("the world's weather was set over the test's: the lens is not checked"));
		return true;
	}
	Run(20.0f);
	TestEqual(TEXT("no frost in the warm, snow or not"), W.SkySubsystem->GetScreenFrost(), 0.0f);
	Weather(1.0f, 1.0f, -12.0f);
	W.Clock->Tick(0.5f);
	const float Early = W.SkySubsystem->GetScreenFrost();
	TestTrue(FString::Printf(TEXT("out in the snow in the cold it starts to grow (%.3f)"), Early), Early > 0.0f && Early < 0.1f);
	Run(60.0f);
	const float Frosted = W.SkySubsystem->GetScreenFrost();
	TestTrue(FString::Printf(TEXT("and creeps in from the rim (%.3f)"), Frosted), Frosted > 0.6f);
	TestTrue(FString::Printf(TEXT("frozen, not wet (%.3f)"), W.SkySubsystem->GetScreenWetness()), W.SkySubsystem->GetScreenWetness() < 0.05f);
	Weather(0.0f, 0.0f, -12.0f);
	Run(5.0f);
	const float Lingering = W.SkySubsystem->GetScreenFrost();
	TestTrue(FString::Printf(TEXT("the snow over, out in the cold it stays a long while (%.3f of %.3f)"), Lingering, Frosted), Lingering < Frosted && Lingering > 0.9f * Frosted);
	Weather(0.0f, 0.0f, 10.0f);
	Run(5.0f);
	const float Melting = W.SkySubsystem->GetScreenFrost();
	TestTrue(FString::Printf(TEXT("in the warm it melts (%.3f)"), Melting), Melting < 0.65f * Lingering);
	TestTrue(FString::Printf(TEXT("into water on the lens (%.3f)"), W.SkySubsystem->GetScreenWetness()), W.SkySubsystem->GetScreenWetness() > 0.05f);
	Weather(1.0f, 1.0f, -12.0f);
	W.Clock->SetCinematicModeOverride(true);
	W.Clock->Tick(0.016f);
	TestTrue(TEXT("a cinematic frame is where it is heading at once"), W.SkySubsystem->GetScreenFrost() > 0.6f);
	Weather(0.0f, 0.0f, 10.0f);
	W.Clock->Tick(0.016f);
	TestEqual(TEXT("and at once clear in the warm"), W.SkySubsystem->GetScreenFrost(), 0.0f);
	W.Clock->SetCinematicModeOverride(false);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDreamSkyHeatHazeTest, "DreamSky.HeatHaze", DreamSky::NightTests::Flags)
bool FDreamSkyHeatHazeTest::RunTest(const FString& Parameters)
{
	using namespace DreamSky::NightTests;

	auto Air = [](FNightWorld& W, float Temperature, float Coverage, float Rain, float Wind)
	{
		FDreamWorldEnvironment Env;
		Env.bActive = true;
		Env.Temperature = Temperature;
		Env.CloudCoverage = Coverage;
		Env.Precipitation = Rain;
		Env.Wind = Wind;
		W.Clock->SetEnvironment(Env);
		W.Clock->Tick(0.016f);
		return W.SkySubsystem->GetHeatHaze();
	};
	{
		FNightWorld W(FDateTime(2026, 7, 20, 13, 0, 0), 35.68, 139.69, 9.0f);
		if (!TestTrue(TEXT("summer noon world"), W.IsValid()))
		{
			return false;
		}
		TestTrue(TEXT("the air's own shimmer, drawn unless the project turns it off"), W.Sky->GetEffectiveStyle()->HeatHaze.bEnabled);
		W.Clock->Tick(0.016f);
		const float Hot = Air(W, 38.0f, 0.0f, 0.0f, 0.0f);
		if (W.Clock->GetEnvironment().Temperature != 38.0f)
		{
			AddWarning(TEXT("the world's weather was set over the test's: the shimmer is not checked"));
			return true;
		}
		TestTrue(FString::Printf(TEXT("a hot clear noon shimmers (%.3f)"), Hot), Hot > 0.9f);
		TestEqual(TEXT("a mild day does not"), Air(W, 18.0f, 0.0f, 0.0f, 0.0f), 0.0f);
		const float Warm = Air(W, 30.0f, 0.0f, 0.0f, 0.0f);
		TestTrue(FString::Printf(TEXT("a warm one a little (%.3f)"), Warm), Warm > 0.1f && Warm < Hot);
		TestTrue(TEXT("overcast stops it"), Air(W, 38.0f, 1.0f, 0.0f, 0.0f) < 0.01f);
		TestTrue(TEXT("so does the rain"), Air(W, 38.0f, 0.3f, 1.0f, 0.0f) < 0.01f);
		const float Windy = Air(W, 38.0f, 0.0f, 0.0f, 1.0f);
		TestTrue(FString::Printf(TEXT("the wind stirs it away (%.3f)"), Windy), Windy < 0.6f * Hot);
	}
	{
		FNightWorld W(FDateTime(2026, 7, 20, 23, 0, 0), 35.68, 139.69, 9.0f);
		if (TestTrue(TEXT("summer night world"), W.IsValid()))
		{
			W.Clock->Tick(0.016f);
			TestEqual(TEXT("no sun, no shimmer"), Air(W, 38.0f, 0.0f, 0.0f, 0.0f), 0.0f);
		}
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDreamSkyRainbowTest, "DreamSky.Rainbow", DreamSky::NightTests::Flags)
bool FDreamSkyRainbowTest::RunTest(const FString& Parameters)
{
	using namespace DreamSky::NightTests;

	auto Weather = [](FNightWorld& W, float Rain, float Coverage, float Seconds)
	{
		FDreamWorldEnvironment Env;
		Env.bActive = true;
		Env.Precipitation = Rain;
		Env.CloudCoverage = Coverage;
		W.Clock->SetEnvironment(Env);
		for (float T = 0.0f; T < Seconds; T += 0.5f)
		{
			W.Clock->Tick(0.5f);
		}
		return W.SkySubsystem->GetRainbow();
	};
	{
		// (A summer evening: the sun some 15 degrees up in the west.)
		FNightWorld W(FDateTime(2026, 7, 20, 17, 30, 0), 35.68, 139.69, 9.0f);
		if (!TestTrue(TEXT("summer evening world"), W.IsValid()))
		{
			return false;
		}
		TestTrue(TEXT("the air's own light, drawn unless the project turns it off"), W.Sky->GetEffectiveStyle()->Rainbow.bEnabled);
		const float Elevation = FMath::RadiansToDegrees(FMath::Asin(float(W.SkySubsystem->GetLighting().MainLightDirection.Z)));
		TestTrue(FString::Printf(TEXT("the sun is low (%.1f degrees)"), Elevation), Elevation > 3.0f && Elevation < 30.0f);
		TestEqual(TEXT("no rain, no rainbow"), Weather(W, 0.0f, 0.2f, 1.0f), 0.0f);
		const float Shower = Weather(W, 1.0f, 0.3f, 10.0f);
		if (W.Clock->GetEnvironment().Precipitation < 0.99f)
		{
			AddWarning(TEXT("the world's weather was set over the test's: the rainbow is not checked"));
			return true;
		}
		TestTrue(FString::Printf(TEXT("a shower in the low sun shows one (%.3f)"), Shower), Shower > 0.5f);
		TestTrue(TEXT("not with the sun behind an overcast"), Weather(W, 1.0f, 1.0f, 1.0f) < 0.01f);
		const float After = Weather(W, 0.0f, 0.2f, 5.0f);
		TestTrue(FString::Printf(TEXT("the rain over, it stays a while (%.3f)"), After), After > 0.5f);
		const float Later = Weather(W, 0.0f, 0.2f, 300.0f);
		TestTrue(FString::Printf(TEXT("and fades as the air dries (%.3f)"), Later), Later < 0.1f);
		Weather(W, 1.0f, 0.3f, 10.0f);
		W.Clock->SetCinematicModeOverride(true);
		TestEqual(TEXT("a cinematic frame is where it is heading at once"), Weather(W, 0.0f, 0.2f, 0.5f), 0.0f);
		W.Clock->SetCinematicModeOverride(false);
	}
	{
		FNightWorld W(FDateTime(2026, 7, 20, 12, 0, 0), 35.68, 139.69, 9.0f);
		if (TestTrue(TEXT("summer noon world"), W.IsValid()))
		{
			TestEqual(TEXT("the sun too high: the bow is below the horizon"), Weather(W, 1.0f, 0.3f, 10.0f), 0.0f);
		}
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDreamSkyMilkyWayTest, "DreamSky.MilkyWay", DreamSky::NightTests::Flags)
bool FDreamSkyMilkyWayTest::RunTest(const FString& Parameters)
{
	using namespace DreamSky::NightTests;
	using namespace DreamSky::Names;

	// (A moonless summer night in the mountains: the new moon of 2026-08-12, near midnight.)
	FNightWorld W(FDateTime(2026, 8, 12, 23, 30, 0), 35.68, 139.69, 9.0f);
	if (!TestTrue(TEXT("new moon night world"), W.IsValid()))
	{
		return false;
	}
	TestTrue(TEXT("the air's own light, drawn unless the project turns it off"), W.Sky->GetEffectiveStyle()->MilkyWay.bEnabled);
	const float Dark = W.SkySubsystem->GetMilkyWayVisibility();
	TestTrue(FString::Printf(TEXT("a moonless night shows it (%.3f)"), Dark), Dark > 0.8f);
	TestTrue(TEXT("and the dome is told to draw it"), W.Parameter(MPC_MilkyWay).G > 0.0f);
	UDreamSkyModifier* City = NewObject<UDreamSkyModifier>(GetTransientPackage());
	City->Settings.bOverride_LightPollution = true;
	City->Settings.LightPollution = 0.06f;
	W.SkySubsystem->SetModifierAlpha(City, 1.0f);
	W.Clock->Tick(0.016f);
	const float Lit = W.SkySubsystem->GetMilkyWayVisibility();
	TestTrue(FString::Printf(TEXT("a bright city all but hides it (%.3f)"), Lit), Lit < 0.1f * Dark);
	W.SkySubsystem->RemoveModifier(City);
	W.Clock->SetTimeOfDay(19.3f);
	W.Clock->Tick(0.016f);
	TestTrue(FString::Printf(TEXT("not in the twilight (%.3f)"), W.SkySubsystem->GetMilkyWayVisibility()), W.SkySubsystem->GetMilkyWayVisibility() < 0.2f);
	W.Clock->SetTimeOfDay(12.0f);
	W.Clock->Tick(0.016f);
	TestEqual(TEXT("not by day"), W.SkySubsystem->GetMilkyWayVisibility(), 0.0f);
	TestEqual(TEXT("nor drawn"), W.Parameter(MPC_MilkyWay).G, 0.0f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDreamSkyCatalogueStarsTest, "DreamSky.Stars.Catalogue", DreamSky::NightTests::Flags)
bool FDreamSkyCatalogueStarsTest::RunTest(const FString& Parameters)
{
	using namespace DreamSky::NightTests;
	using namespace DreamSky::Names;

	// (A winter night in Tokyo, Orion high in the south.)
	const FDateTime Local(2026, 1, 15, 21, 0, 0);
	const double Latitude = 35.68, Longitude = 139.69;
	FNightWorld W(Local, Latitude, Longitude, 9.0f);
	if (!TestTrue(TEXT("winter night world"), W.IsValid()))
	{
		return false;
	}
	TestTrue(TEXT("the real sky's stars by default"), W.Sky->GetEffectiveStyle()->StarField == EDreamSkyStarField::Catalogue);
	const FLinearColor P = W.Parameter(MPC_StarPole);
	const FLinearColor Q = W.Parameter(MPC_StarEquinox);
	TestEqual(TEXT("the dome is told to draw them"), P.A, 1.0f);
	const FVector Pole(P.R, P.G, P.B);
	const FVector Equinox(Q.R, Q.G, Q.B);
	TestNearlyEqual(TEXT("the celestial pole as high as the latitude"), FMath::RadiansToDegrees(FMath::Asin(Pole.Z)), Latitude, 0.05);
	TestTrue(TEXT("toward north (+X)"), Pole.X > 0.5 && FMath::Abs(Pole.Y) < 1.0e-3);
	TestNearlyEqual(TEXT("the equinox on the celestial equator"), FVector::DotProduct(Pole, Equinox), 0.0, 1.0e-3);

	// (Sirius where the textbook puts it: its hour angle from Greenwich mean sidereal time. The catalogue's places are
	// of 2000; the sky has precessed some 0.4 degrees since.)
	const double Jd = (Local - FTimespan::FromHours(9.0)).GetJulianDay();
	const double Lst = FMath::DegreesToRadians(FMath::Fmod(280.46061837 + 360.98564736629 * (Jd - 2451545.0) + Longitude, 360.0));
	const double Ra = FMath::DegreesToRadians(15.0 * (6.0 + 45.0 / 60.0 + 8.9 / 3600.0));
	const double Dec = FMath::DegreesToRadians(-(16.0 + 42.0 / 60.0 + 58.0 / 3600.0));
	const double Phi = FMath::DegreesToRadians(Latitude);
	const double Ha = Lst - Ra;
	const double Altitude = FMath::RadiansToDegrees(FMath::Asin(FMath::Sin(Phi) * FMath::Sin(Dec) + FMath::Cos(Phi) * FMath::Cos(Dec) * FMath::Cos(Ha)));
	const double Azimuth = FMath::RadiansToDegrees(FMath::Atan2(-FMath::Cos(Dec) * FMath::Sin(Ha), FMath::Sin(Dec) * FMath::Cos(Phi) - FMath::Cos(Dec) * FMath::Cos(Ha) * FMath::Sin(Phi)));
	const FVector Y = FVector::CrossProduct(Equinox, Pole);
	const FVector Sirius = FMath::Cos(Dec) * (FMath::Cos(Ra) * Equinox + FMath::Sin(Ra) * Y) + FMath::Sin(Dec) * Pole;
	const double Shown = FMath::RadiansToDegrees(FMath::Asin(Sirius.Z));
	const double ShownAzimuth = FMath::RadiansToDegrees(FMath::Atan2(Sirius.Y, Sirius.X));
	TestTrue(FString::Printf(TEXT("Sirius is up (%.1f degrees)"), Altitude), Altitude > 10.0);
	TestNearlyEqual(TEXT("Sirius as high as the textbook says"), Shown, Altitude, 0.6);
	TestNearlyEqual(TEXT("and as far round from north"), FMath::Fmod(ShownAzimuth - Azimuth + 540.0, 360.0) - 180.0, 0.0, 0.8);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDreamSkyToonTonesTest, "DreamSky.ToonTones", DreamSky::NightTests::Flags)
bool FDreamSkyToonTonesTest::RunTest(const FString& Parameters)
{
	using namespace DreamSky::NightTests;

	// The defaults bake the look's own tones: its fixed steps before they could be set.
	const FDreamSkyToonClouds Defaults;
	FDreamSkyToonBaked Baked;
	Defaults.Bake(Baked);
	TestEqual(TEXT("a ramp of 256 samples"), Baked.Ramp.Num(), FDreamSkyToonBaked::RampSize);
	float Worst = 0.0f;
	for (int32 Index = 0; Index < Baked.Ramp.Num(); ++Index)
	{
		const float X = float(Index) / 255.0f;
		const float Tone = 0.5f * FMath::SmoothStep(0.24f, 0.28f, X) + 0.5f * FMath::SmoothStep(0.56f, 0.6f, X);
		const float Under = 0.5f * FMath::SmoothStep(0.05f, 0.35f, X) + 0.5f * FMath::SmoothStep(0.45f, 0.8f, X);
		Worst = FMath::Max(Worst, FMath::Max(FMath::Abs(Baked.Ramp[Index].X - Tone), FMath::Abs(Baked.Ramp[Index].Y - Under)));
	}
	TestTrue(FString::Printf(TEXT("the defaults are the look's own steps (%.2e)"), Worst), Worst < 1.0e-5f);
	TestTrue(TEXT("and its own half tone, underside, key and rim"), Baked.Half == FVector4f(0.96f, 0.97f, 1.03f, 0.55f)
		&& Baked.Deep == FVector4f(0.62f, 0.64f, 0.7f, 0.35f) && Baked.Key == FVector4f(0.7f, 0.45f, 0.5f, 0.55f)
		&& Baked.Key2 == FVector4f(-0.3f, 0.6f, 3.0f, 2.0f) && Baked.RimFocus == 4.0f);
	const FDreamSkyToonBaked Fixed;
	bool bAdvanced = true;
	for (int32 Index = 0; Index < 7; ++Index)
	{
		const FVector4f D = Baked.Advanced[Index] - Fixed.Advanced[Index];
		bAdvanced &= FMath::Max(FMath::Max(FMath::Abs(D.X), FMath::Abs(D.Y)), FMath::Max(FMath::Abs(D.Z), FMath::Abs(D.W))) < 1.0e-3f;
	}
	TestTrue(TEXT("and its own finer settings"), bAdvanced);

	// A curve in place of the bands.
	FDreamSkyToonClouds Curved;
	Curved.bUseToneCurve = true;
	Curved.ToneCurve.GetRichCurve()->Reset();
	Curved.ToneCurve.GetRichCurve()->AddKey(0.0f, 0.0f);
	Curved.ToneCurve.GetRichCurve()->AddKey(1.0f, 1.0f);
	FDreamSkyToonBaked FromCurve;
	Curved.Bake(FromCurve);
	TestNearlyEqual(TEXT("a curve drawn instead of the bands"), FromCurve.Ramp[128].X, 128.0f / 255.0f, 0.02f);

	// A sky modifier blends to its own tones as it fades in.
	FNightWorld W(FDateTime(2026, 6, 21, 12, 0, 0), 35.68, 139.69, 9.0f);
	if (!TestTrue(TEXT("noon world"), W.IsValid()))
	{
		return false;
	}
	TestTrue(TEXT("the sky's look carries the style's tones"), W.SkySubsystem->GetLook().Toon.IsValid()
		&& FMath::IsNearlyEqual(W.SkySubsystem->GetLook().Toon.Ramp[100].X, Baked.Ramp[100].X));
	UDreamSkyModifier* Flat = NewObject<UDreamSkyModifier>(GetTransientPackage());
	Flat->Settings.bOverride_ToonClouds = true;
	Flat->Settings.ToonClouds.Bands = { FDreamSkyToneBand(0.1f, 0.0f, 1.0f) };
	Flat->Settings.ToonClouds.HalfToneMix = 0.95f;
	W.SkySubsystem->SetModifierAlpha(Flat, 0.5f);
	W.Clock->Tick(0.016f);
	const FDreamSkyToonBaked& Half = W.SkySubsystem->GetLook().Toon;
	TestNearlyEqual(TEXT("halfway to its tones at half strength"), Half.Ramp[100].X, 0.5f * (Baked.Ramp[100].X + 1.0f), 1.0e-4f);
	TestNearlyEqual(TEXT("and halfway to its half tone"), Half.Half.W, 0.5f * (0.55f + 0.95f), 1.0e-4f);
	W.SkySubsystem->RemoveModifier(Flat);

	// The painted clouds' settings too.
	const FDreamSkyPaintedClouds& Style = W.Sky->GetEffectiveStyle()->PaintedClouds;
	W.Clock->Tick(0.016f);
	TestNearlyEqual(TEXT("the sky's look carries the style's painted settings"), W.SkySubsystem->GetLook().Painted.HalfToneAt, Style.HalfToneAt, 1.0e-5f);
	UDreamSkyModifier* Paint = NewObject<UDreamSkyModifier>(GetTransientPackage());
	Paint->Settings.bOverride_PaintedClouds = true;
	Paint->Settings.PaintedClouds.HalfToneAt = 0.12f;
	Paint->Settings.PaintedClouds.WarmMax = 3.0f;
	W.SkySubsystem->SetModifierAlpha(Paint, 0.5f);
	W.Clock->Tick(0.016f);
	TestNearlyEqual(TEXT("halfway to the modifier's painted tones"), W.SkySubsystem->GetLook().Painted.HalfToneAt, 0.5f * (Style.HalfToneAt + 0.12f), 1.0e-4f);
	TestNearlyEqual(TEXT("and its finer settings"), W.SkySubsystem->GetLook().Painted.WarmMax, 0.5f * (Style.WarmMax + 3.0f), 1.0e-4f);
	W.SkySubsystem->RemoveModifier(Paint);
	return true;
}


IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDreamSkyHalosTest, "DreamSky.Night.Halos", DreamSky::NightTests::Flags)
bool FDreamSkyHalosTest::RunTest(const FString& Parameters)
{
	using namespace DreamSky::NightTests;
	using namespace DreamSky::Names;

	// The sun dogs: 22 degrees out with the sun on the horizon, further as it climbs, none past some 60 degrees.
	TestNearlyEqual(TEXT("on the horizon: 22 degrees"), UDreamSkyStyle::SunDogAzimuth(0.0f), 21.84f, 0.05f);
	TestNearlyEqual(TEXT("at 30 degrees: further out"), UDreamSkyStyle::SunDogAzimuth(30.0f), 28.7f, 0.3f);
	TestTrue(TEXT("red nearer the sun than blue"), UDreamSkyStyle::SunDogAzimuth(10.0f, 1.307f) < UDreamSkyStyle::SunDogAzimuth(10.0f, 1.317f));
	TestTrue(TEXT("none with the sun at 65"), UDreamSkyStyle::SunDogAzimuth(65.0f) < 0.0f);

	// Where they show: a sheet of high cloud makes the halos, thinner cloud the corona; the default thin cirrus neither.
	UDreamSkyStyle* Style = NewObject<UDreamSkyStyle>(GetTransientPackage());
	TestTrue(TEXT("the default high cloud: no halos, no corona"), Style->EvaluateHaloPresence(Style->CloudHighCover).Equals(FVector2f::ZeroVector, 1e-6f));
	TestTrue(TEXT("a sheet: both in full"), Style->EvaluateHaloPresence(0.9f).Equals(FVector2f(1.0f, 1.0f), 1e-6f));
	const FVector2f Half = Style->EvaluateHaloPresence(0.55f);
	TestTrue(FString::Printf(TEXT("thinner: the corona before the halos (%s)"), *Half.ToString()), Half.Y > Half.X && Half.X > 0.0f);
	Style->Halos.bEnabled = false;
	TestTrue(TEXT("off: none"), Style->EvaluateHaloPresence(0.9f).Equals(FVector2f::ZeroVector, 1e-6f));

	// What the dome reads: the style's brightness times how far they show.
	FNightWorld W(FDateTime(2026, 6, 21, 10, 0, 0), 35.0, 139.7, 9.0f);
	if (!TestTrue(TEXT("world"), W.IsValid()))
	{
		return false;
	}
	UDreamSkyStyle* Live = W.Sky->GetEffectiveStyle();
	TestTrue(TEXT("the default sky: nothing"), W.Parameter(MPC_Halo).Equals(FLinearColor::Transparent, 1e-6f));
	Live->CloudHighCover = 0.9f;
	W.Clock->Tick(0.016f);
	const FLinearColor H = W.Parameter(MPC_Halo);
	TestNearlyEqual(TEXT("a sheet of high cloud: the ring"), H.R, Live->Halos.Intensity, 1e-5f);
	TestNearlyEqual(TEXT("the dogs"), H.G, Live->Halos.SunDogs, 1e-5f);
	TestNearlyEqual(TEXT("the corona"), H.B, Live->Halos.Corona, 1e-5f);
	TestNearlyEqual(TEXT("the ring's radius"), W.Parameter(MPC_Halo2).R, Live->Halos.Radius, 1e-5f);
	Live->Halos.bEnabled = false;
	W.Clock->Tick(0.016f);
	TestTrue(TEXT("turned off: nothing"), W.Parameter(MPC_Halo).Equals(FLinearColor::Transparent, 1e-6f));

	// The earth's shadow over the style's gradient: its tints, its height, its strength (none when off).
	TestNearlyEqual(TEXT("the earth's shadow drawn as strongly as the style says"), W.Parameter(MPC_EarthBelt).A, Live->EarthShadow.Strength, 1e-5f);
	TestNearlyEqual(TEXT("as high"), W.Parameter(MPC_EarthShadow).A, Live->EarthShadow.Height, 1e-5f);
	TestTrue(TEXT("its shadow blue-grey, its belt pink"), W.Parameter(MPC_EarthShadow).B > W.Parameter(MPC_EarthShadow).R && W.Parameter(MPC_EarthBelt).R > W.Parameter(MPC_EarthBelt).G);
	Live->EarthShadow.bEnabled = false;
	W.Clock->Tick(0.016f);
	TestEqual(TEXT("off: not drawn"), W.Parameter(MPC_EarthBelt).A, 0.0f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDreamSkyNoctilucentTest, "DreamSky.Night.Noctilucent", DreamSky::NightTests::Flags)
bool FDreamSkyNoctilucentTest::RunTest(const FString& Parameters)
{
	using namespace DreamSky::NightTests;
	using namespace DreamSky::Names;

	// Where and when: high latitudes, either hemisphere, in the weeks round its midsummer; as told in the artistic mode.
	UDreamSkyStyle* Style = NewObject<UDreamSkyStyle>(GetTransientPackage());
	FDreamCelestialSettings Settings;
	Settings.Mode = EDreamCelestialMode::Realistic;
	Settings.Latitude = 60.0;
	const FDateTime June(2026, 6, 21), July(2026, 7, 20), December(2026, 12, 21);
	TestNearlyEqual(TEXT("60 N at midsummer: in full"), Style->EvaluateNoctilucentPresence(Settings, June), 1.0f, 1e-4f);
	TestTrue(TEXT("a month after: still"), Style->EvaluateNoctilucentPresence(Settings, July) > 0.9f);
	TestEqual(TEXT("in midwinter: none"), Style->EvaluateNoctilucentPresence(Settings, December), 0.0f);
	Settings.Latitude = -60.0;
	TestNearlyEqual(TEXT("60 S in December: in full"), Style->EvaluateNoctilucentPresence(Settings, December), 1.0f, 1e-4f);
	TestEqual(TEXT("60 S in June: none"), Style->EvaluateNoctilucentPresence(Settings, June), 0.0f);
	Settings.Latitude = 35.0;
	TestEqual(TEXT("35 N: none"), Style->EvaluateNoctilucentPresence(Settings, June), 0.0f);
	Settings.Latitude = 85.0;
	TestEqual(TEXT("85 N (the sun never sets in summer): none"), Style->EvaluateNoctilucentPresence(Settings, June), 0.0f);
	Settings.Mode = EDreamCelestialMode::Artistic;
	TestEqual(TEXT("artistic: none unless told"), Style->EvaluateNoctilucentPresence(Settings, June), 0.0f);
	Style->Noctilucent.ArtisticPresence = 0.7f;
	TestNearlyEqual(TEXT("artistic: as told"), Style->EvaluateNoctilucentPresence(Settings, June), 0.7f, 1e-6f);
	// In the deep twilight only.
	TestEqual(TEXT("not with the sun up"), UDreamSkyStyle::NoctilucentTwilight(2.0f), 0.0f);
	TestEqual(TEXT("not in the bright civil twilight"), UDreamSkyStyle::NoctilucentTwilight(-3.0f), 0.0f);
	TestEqual(TEXT("in full 10 degrees under"), UDreamSkyStyle::NoctilucentTwilight(-10.0f), 1.0f);
	TestEqual(TEXT("not at night, the sun too far under to light them"), UDreamSkyStyle::NoctilucentTwilight(-18.0f), 0.0f);

	// Helsinki at a midsummer midnight: the dome is told to draw them.
	{
		FNightWorld W(FDateTime(2026, 6, 21, 0, 30, 0), 60.17, 24.94, 2.0f);
		if (!TestTrue(TEXT("north world"), W.IsValid()))
		{
			return false;
		}
		const FLinearColor N = W.Parameter(MPC_Noctilucent);
		const float Brightness = W.Sky->GetEffectiveStyle()->Noctilucent.Brightness;
		TestTrue(FString::Printf(TEXT("drawn at midsummer midnight (%.4f of %.4f)"), N.R, Brightness), N.R > 0.5f * Brightness);
		TestNearlyEqual(TEXT("as high as the style says"), N.G, W.Sky->GetEffectiveStyle()->Noctilucent.Altitude, 1e-3f);
		W.Sky->GetEffectiveStyle()->Noctilucent.bEnabled = false;
		W.Clock->Tick(0.016f);
		TestEqual(TEXT("turned off: not drawn"), W.Parameter(MPC_Noctilucent).R, 0.0f);
	}
	// The same place at a midwinter midnight: none.
	{
		FNightWorld W(FDateTime(2026, 12, 21, 18, 0, 0), 60.17, 24.94, 2.0f);
		if (TestTrue(TEXT("winter world"), W.IsValid()))
		{
			TestEqual(TEXT("not in midwinter"), W.Parameter(MPC_Noctilucent).R, 0.0f);
		}
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDreamSkyMeteorsTest, "DreamSky.Night.Meteors", DreamSky::NightTests::Flags)
bool FDreamSkyMeteorsTest::RunTest(const FString& Parameters)
{
	using namespace DreamSky::NightTests;
	using namespace DreamSky::Names;

	// The showers by the date: at their peaks, falling off either side, none between.
	UDreamSkyStyle* Style = NewObject<UDreamSkyStyle>(GetTransientPackage());
	const FDreamSkyShowerActivity Perseids = Style->EvaluateMeteorShower(FDateTime(2026, 8, 12, 12, 0, 0));
	TestEqual(TEXT("August 12: the Perseids"), Perseids.Name, FName(TEXT("Perseids")));
	TestTrue(FString::Printf(TEXT("at their peak (%.1f)"), Perseids.ZenithalHourlyRate), Perseids.ZenithalHourlyRate > 90.0f);
	TestNearlyEqual(TEXT("their radiant in Perseus"), Perseids.Declination, 58.0f, 1e-3f);
	TestTrue(TEXT("a week after: much weaker"), Style->EvaluateMeteorShower(FDateTime(2026, 8, 19, 12, 0, 0)).ZenithalHourlyRate < 0.2f * Perseids.ZenithalHourlyRate);
	TestEqual(TEXT("December 14: the Geminids"), Style->EvaluateMeteorShower(FDateTime(2026, 12, 14, 12, 0, 0)).Name, FName(TEXT("Geminids")));
	TestEqual(TEXT("January 4 (round the year's end): the Quadrantids"), Style->EvaluateMeteorShower(FDateTime(2027, 1, 4, 0, 0, 0)).Name, FName(TEXT("Quadrantids")));
	const FDreamSkyShowerActivity March = Style->EvaluateMeteorShower(FDateTime(2026, 3, 1));
	TestTrue(TEXT("early March: none"), March.Name.IsNone() && March.ZenithalHourlyRate == 0.0f);

	// The Perseids' night at 45 N: the radiant up in the north-east, the shower drawn from it, sporadic ones besides.
	FNightWorld W(FDateTime(2026, 8, 12, 23, 30, 0), 45.0, 10.0, 1.0f);
	if (!TestTrue(TEXT("world"), W.IsValid()))
	{
		return false;
	}
	const FLinearColor M = W.Parameter(MPC_Meteors);
	const FLinearColor Radiant = W.Parameter(MPC_MeteorRadiant);
	TestEqual(TEXT("the subsystem says the Perseids"), W.SkySubsystem->GetMeteorShower().Name, FName(TEXT("Perseids")));
	TestTrue(FString::Printf(TEXT("the radiant above the horizon (%.2f)"), Radiant.B), Radiant.B > 0.2f);
	TestTrue(FString::Printf(TEXT("the shower drawn (%.1f an hour) with sporadic ones (%.1f)"), M.G, M.R), M.G > M.R && M.R > 0.0f);
	TestNearlyEqual(TEXT("the rate the subsystem gives is the dome's"), W.SkySubsystem->GetMeteorRate(), M.R + M.G, 1e-3f);
	TestTrue(TEXT("satellites"), M.A > 0.0f);
	// By day: none.
	FDreamWorldClockConfig Config;
	Config.StartTime = FDateTime(2026, 8, 12, 13, 0, 0);
	Config.bAnimate = false;
	Config.Celestial = W.Clock->GetCelestialSettings();
	W.Clock->ApplyClockConfig(Config, true);
	W.Clock->Tick(0.016f);
	TestEqual(TEXT("by day: no meteors"), W.SkySubsystem->GetMeteorRate(), 0.0f);
	W.Sky->GetEffectiveStyle()->Meteors.bEnabled = false;
	Config.StartTime = FDateTime(2026, 8, 12, 23, 30, 0);
	W.Clock->ApplyClockConfig(Config, true);
	W.Clock->Tick(0.016f);
	TestEqual(TEXT("turned off: none at night either"), W.Parameter(MPC_Meteors).R + W.Parameter(MPC_Meteors).G + W.Parameter(MPC_Meteors).A, 0.0f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDreamSkyCloudClearingTest, "DreamSky.Clouds.Clearing", DreamSky::NightTests::Flags)
bool FDreamSkyCloudClearingTest::RunTest(const FString& Parameters)
{
	using namespace DreamSky::NightTests;
	using namespace DreamSky::Names;

	// The clouds clear round a low sun, hardly round a high one; round the moon likewise; none when off.
	UDreamSkyStyle* Style = NewObject<UDreamSkyStyle>(GetTransientPackage());
	auto Look = [Style](float SunElevation, float MoonElevation)
	{
		FDreamCelestialState Celestial;
		Celestial.SunElevation = SunElevation;
		Celestial.MoonElevation = MoonElevation;
		return Style->EvaluateLook(Celestial);
	};
	const FDreamSkyLook Evening = Look(3.0f, -30.0f), Noon = Look(60.0f, -30.0f), Night = Look(-30.0f, 5.0f);
	TestTrue(FString::Printf(TEXT("a low sun clears the clouds round it (%.2f)"), Evening.SunClearing), Evening.SunClearing > 0.4f);
	TestTrue(FString::Printf(TEXT("hardly at noon (%.2f)"), Noon.SunClearing), Noon.SunClearing < 0.05f);
	TestTrue(FString::Printf(TEXT("a low moon too (%.2f)"), Night.MoonClearing), Night.MoonClearing > 0.3f);
	TestEqual(TEXT("the sun under the horizon: none round it"), Night.SunClearing, 0.0f);
	TestNearlyEqual(TEXT("as far round as the style says"), Evening.ClearingRadius, Style->CloudClearing.Radius, 1e-5f);
	Style->CloudClearing.bEnabled = false;
	TestEqual(TEXT("off: none"), Look(3.0f, 5.0f).SunClearing + Look(3.0f, 5.0f).MoonClearing, 0.0f);
	Style->CloudClearing.bEnabled = true;

	// A sky modifier turns it toward its own, halfway at half strength.
	UDreamSkyModifier* Modifier = NewObject<UDreamSkyModifier>(GetTransientPackage());
	Modifier->Settings.bOverride_CloudClearing = true;
	Modifier->Settings.CloudClearing = FVector4f(1.0f, 1.0f, 30.0f, 4.0f);
	FDreamSkyLook Blended = Evening;
	FDreamSkyLighting Lighting;
	Modifier->Settings.Apply(Lighting, Blended, 0.5f);
	TestNearlyEqual(TEXT("a modifier halfway: the strength"), Blended.SunClearing, 0.5f * (Evening.SunClearing + 1.0f), 1e-4f);
	TestNearlyEqual(TEXT("and the radius"), Blended.ClearingRadius, 0.5f * (Evening.ClearingRadius + 30.0f), 1e-3f);

	// What the renderer and the dome read.
	FNightWorld W(FDateTime(2026, 6, 21, 19, 40, 0), 35.0, 139.7, 9.0f);
	if (TestTrue(TEXT("world"), W.IsValid()))
	{
		const FLinearColor C = W.Parameter(MPC_CloudClearing);
		TestNearlyEqual(TEXT("the shared parameters carry the sun's strength"), C.R, W.SkySubsystem->GetLook().SunClearing, 1e-5f);
		TestNearlyEqual(TEXT("and the radius"), C.B, W.SkySubsystem->GetLook().ClearingRadius, 1e-5f);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDreamSkyDiskScaleTest, "DreamSky.Night.DiskScale", DreamSky::NightTests::Flags)
bool FDreamSkyDiskScaleTest::RunTest(const FString& Parameters)
{
	using namespace DreamSky::NightTests;
	using namespace DreamSky::Names;

	// The disks drawn larger near the horizon, as they are high up.
	UDreamSkyStyle* Style = NewObject<UDreamSkyStyle>(GetTransientPackage());
	auto Look = [Style](float SunElevation, float MoonElevation)
	{
		FDreamCelestialState Celestial;
		Celestial.SunElevation = SunElevation;
		Celestial.MoonElevation = MoonElevation;
		return Style->EvaluateLook(Celestial);
	};
	const FDreamSkyLook Low = Look(1.0f, 1.0f), High = Look(60.0f, 60.0f);
	TestTrue(FString::Printf(TEXT("the sun larger on the horizon (%.2f)"), Low.SunDiskScale), Low.SunDiskScale > 1.3f);
	TestTrue(FString::Printf(TEXT("the moon too (%.2f)"), Low.MoonDiskScale), Low.MoonDiskScale > 1.3f);
	TestNearlyEqual(TEXT("high up: as they are"), High.SunDiskScale, 1.0f, 1e-4f);
	TestNearlyEqual(TEXT("the moon high up: as it is"), High.MoonDiskScale, 1.0f, 1e-4f);

	// A modifier turns them toward its own.
	UDreamSkyModifier* Modifier = NewObject<UDreamSkyModifier>(GetTransientPackage());
	Modifier->Settings.bOverride_DiskScale = true;
	Modifier->Settings.DiskScale = FVector2f(3.0f, 2.0f);
	FDreamSkyLook Blended = High;
	FDreamSkyLighting Lighting;
	Modifier->Settings.Apply(Lighting, Blended, 0.5f);
	TestNearlyEqual(TEXT("a modifier halfway: the sun's"), Blended.SunDiskScale, 2.0f, 1e-4f);
	TestNearlyEqual(TEXT("the moon's"), Blended.MoonDiskScale, 1.5f, 1e-4f);

	// What the dome is told: the disk larger, its glow wider; the sun light's source angle untouched.
	FNightWorld W(FDateTime(2026, 6, 21, 19, 0, 0), 35.0, 139.7, 9.0f);
	if (TestTrue(TEXT("world"), W.IsValid()))
	{
		const UDreamSkyStyle* Live = W.Sky->GetEffectiveStyle();
		const float Scale = W.SkySubsystem->GetLook().SunDiskScale;
		TestTrue(FString::Printf(TEXT("the evening sun drawn larger (%.2f)"), Scale), Scale > 1.1f);
		TestNearlyEqual(TEXT("the disk's radius"), W.Parameter(MPC_SunDisk).A, Live->SunDiskSize * Scale, 1e-4f);
		TestNearlyEqual(TEXT("its glow wider"), W.Parameter(MPC_SunHalo).A, FMath::Max(Live->SunHaloExponent / (Scale * Scale), 1.0f), 1e-4f);
		const float SourceAngle = W.Sky->SunLight->LightSourceAngle;
		W.Clock->Tick(0.016f);
		TestNearlyEqual(TEXT("the light's source angle as it was"), W.Sky->SunLight->LightSourceAngle, SourceAngle, 1e-6f);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDreamSkyTwilightCloudColoursTest, "DreamSky.Clouds.TwilightColours", DreamSky::NightTests::Flags)
bool FDreamSkyTwilightCloudColoursTest::RunTest(const FString& Parameters)
{
	// The style's own cloud colours in the deep twilight and the night: a low-saturation grey-blue, not a glowing blue.
	const UDreamSkyStyle* Style = GetDefault<UDreamSkyStyle>();
	for (float Elevation : { -12.0f, -10.0f, -8.0f, -6.0f })
	{
		FDreamCelestialState Celestial;
		Celestial.SunElevation = Elevation;
		const FDreamSkyLook Look = Style->EvaluateLook(Celestial);
		const FLinearColor S = Look.CloudShadow, L = Look.CloudLit;
		TestTrue(FString::Printf(TEXT("sun %.0f: the shade low in saturation (%s)"), Elevation, *S.ToString()), S.B < 1.6f * FMath::Min(S.R, S.G));
		TestTrue(FString::Printf(TEXT("sun %.0f: the lit side too (%s)"), Elevation, *L.ToString()), L.B < 1.45f * FMath::Min(L.R, L.G));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDreamWorldQualityTest, "DreamSky.Quality", DreamSky::NightTests::Flags)
bool FDreamWorldQualityTest::RunTest(const FString& Parameters)
{
	using namespace DreamSky::NightTests;
	using namespace DreamSky::Names;
	IConsoleManager& Console = IConsoleManager::Get();
	auto Int = [&Console](const TCHAR* Name) { const IConsoleVariable* V = Console.FindConsoleVariable(Name); return V ? V->GetInt() : -999; };
	auto Float = [&Console](const TCHAR* Name) { const IConsoleVariable* V = Console.FindConsoleVariable(Name); return V ? V->GetFloat() : -999.0f; };
	const int32 Saved = UDreamWorldQuality::GetQualityOverride();

	// Every entry of the mapping table names a console variable that is there, with five levels.
	for (int32 G = 0; G < 5; ++G)
	{
		for (const FDreamWorldQualitySetting& Setting : UDreamWorldQuality::QualityTable(EDreamWorldQualityGroup(G)))
		{
			TestNotNull(*FString::Printf(TEXT("%s is a console variable"), Setting.Name), Console.FindConsoleVariable(Setting.Name));
		}
	}
	TestNotNull(TEXT("the wind's entry"), Console.FindConsoleVariable(TEXT("r.DreamWind.Quality")));

	// Low: the flat clouds, coarse clouds, no shafts, the sky's extras gone, fewer particles, no volumetric fog.
	UDreamWorldQuality::SetQualityOverride(0);
	UDreamWorldQuality::Apply();
	TestEqual(TEXT("low: every group low"), UDreamWorldQuality::GetLevel(EDreamWorldQualityGroup::Sky) + UDreamWorldQuality::GetLevel(EDreamWorldQualityGroup::Wind), 0);
	TestEqual(TEXT("low: the flat clouds"), Int(TEXT("DreamSky.Clouds.Flat")), 1);
	TestEqual(TEXT("low: the clouds at a quarter"), Int(TEXT("DreamSky.CloudRender.Downsample")), 4);
	TestEqual(TEXT("low: no sky extras"), Int(TEXT("DreamSky.SkyDetail")), 0);
	TestNearlyEqual(TEXT("low: fewer particles"), Float(TEXT("DreamWeather.ParticleDensity")), 0.35f, 1e-4f);
	TestEqual(TEXT("low: no volumetric fog"), Int(TEXT("DreamSky.VolumetricFog")), 0);
	{
		// The dome is told: no halos, no meteors with the sky's extras gone.
		FNightWorld W(FDateTime(2026, 8, 12, 23, 30, 0), 45.0, 10.0, 1.0f);
		if (TestTrue(TEXT("world"), W.IsValid()))
		{
			W.Sky->GetEffectiveStyle()->CloudHighCover = 0.9f;
			W.Clock->Tick(0.016f);
			TestEqual(TEXT("low: no meteors drawn"), W.Parameter(MPC_Meteors).G, 0.0f);
			TestEqual(TEXT("low: no halos"), W.Parameter(MPC_Halo).R, 0.0f);
		}
	}
	// A value set by hand outlives the levels.
	if (IConsoleVariable* Samples = Console.FindConsoleVariable(TEXT("DreamSky.CloudRender.Samples")))
	{
		Samples->Set(200, ECVF_SetByConsole);
		UDreamWorldQuality::SetQualityOverride(1);
		UDreamWorldQuality::Apply();
		TestEqual(TEXT("set by hand: kept over the level's"), Int(TEXT("DreamSky.CloudRender.Samples")), 200);
		TestEqual(TEXT("the rest at medium"), Int(TEXT("DreamSky.CloudRender.Downsample")), 3);
		Samples->Set(160, ECVF_SetByConsole);
	}
	// Cinematic, then back to the engine's levels (epic by default: the tuned values).
	UDreamWorldQuality::SetQualityOverride(4);
	UDreamWorldQuality::Apply();
	TestEqual(TEXT("cinematic: the clouds at full resolution"), Int(TEXT("DreamSky.CloudRender.Downsample")), 1);
	TestEqual(TEXT("cinematic: the sun maps whole"), Int(TEXT("DreamSky.CloudRender.SunMapSlices")), 1);
	UDreamWorldQuality::SetQualityOverride(Saved);
	UDreamWorldQuality::Apply(true);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDreamSkyFunnelsTest, "DreamSky.Funnels", DreamSky::NightTests::Flags)
bool FDreamSkyFunnelsTest::RunTest(const FString& Parameters)
{
	using namespace DreamSky::NightTests;
	using namespace DreamSky::Names;
	auto Vortex = [](double X, double Y, float Strength, float Spin, bool bFunnel)
	{
		FDreamWorldVortex V;
		V.Location = FVector(X, Y, 120.0);
		V.Radius = 10000.0f;
		V.Strength = Strength;
		V.Spin = Spin;
		V.TangentialSpeed = 3000.0f;
		V.Lean = FVector2D(4000.0, -2000.0);
		V.bFunnel = bFunnel;
		return V;
	};

	// How far a funnel has turned: its wall's speed over its radius, in real seconds at the clock's rate, from when it formed.
	FDreamWorldVortex Turning = Vortex(0.0, 0.0, 1.0f, 1.0f, true);
	Turning.BornSeconds = 1000.0;
	TestNearlyEqual(TEXT("100 game seconds at 10x: 10 real seconds at 0.3 rad/s"), DreamSky::Funnels::Turned(Turning, 1100.0, 10.0f), 3.0f, 1e-4f);
	TestNearlyEqual(TEXT("wrapped to a turn"), DreamSky::Funnels::Turned(Turning, 1000.0 + 300.0, 1.0f), float(FMath::Fmod(90.0, UE_DOUBLE_TWO_PI)), 1e-3f);
	TestNearlyEqual(TEXT("nothing before it formed (a turn back, wrapped)"), DreamSky::Funnels::Turned(Turning, 1000.0, 1.0f), 0.0f, 1e-6f);

	// Which are drawn: tornadoes with a strength (not a dust devil, not a spent one), the nearest four, the spin in the
	// strength's sign.
	TMap<int32, FDreamWorldVortex> Vortices;
	Vortices.Add(1, Vortex(50000.0, 0.0, 0.8f, 1.0f, true));
	Vortices.Add(2, Vortex(10000.0, 0.0, 0.5f, -1.0f, true));
	Vortices.Add(3, Vortex(5000.0, 0.0, 1.0f, 1.0f, false));
	Vortices.Add(4, Vortex(6000.0, 0.0, 0.0f, 1.0f, true));
	Vortices.Add(5, Vortex(90000.0, 0.0, 1.0f, 1.0f, true));
	Vortices.Add(6, Vortex(20000.0, 0.0, 1.0f, 1.0f, true));
	Vortices.Add(7, Vortex(70000.0, 0.0, 1.0f, 1.0f, true));
	FLinearColor Funnel[DreamSky::Funnels::MaxFunnels];
	FLinearColor Shape[DreamSky::Funnels::MaxFunnels];
	FLinearColor Decay;
	const int32 Count = DreamSky::Funnels::Pack(Vortices, FVector::ZeroVector, 0.0, 1.0f, Funnel, Shape, Decay);
	TestEqual(TEXT("four of the five tornadoes"), Count, 4);
	TestTrue(TEXT("the nearest first: 100 m, a clockwise one at half strength"), FMath::IsNearlyEqual(Funnel[0].R, 10000.0f) && FMath::IsNearlyEqual(Funnel[0].A, -0.5f));
	TestTrue(TEXT("then 200 m, 500 m, 700 m"), FMath::IsNearlyEqual(Funnel[1].R, 20000.0f) && FMath::IsNearlyEqual(Funnel[2].R, 50000.0f) && FMath::IsNearlyEqual(Funnel[3].R, 70000.0f));
	TestTrue(TEXT("its radius, the ground's height and the lean"), FMath::IsNearlyEqual(Funnel[0].B, 10000.0f) && FMath::IsNearlyEqual(Shape[0].G, 120.0f)
		&& FMath::IsNearlyEqual(Shape[0].B, 4000.0f) && FMath::IsNearlyEqual(Shape[0].A, -2000.0f));
	Vortices.Reset();
	Vortices.Add(1, Vortex(1000.0, 0.0, 1.0f, 1.0f, true));
	TestEqual(TEXT("one tornado"), DreamSky::Funnels::Pack(Vortices, FVector::ZeroVector, 0.0, 1.0f, Funnel, Shape, Decay), 1);
	TestTrue(TEXT("the other slots empty"), Funnel[1] == FLinearColor(0.0f, 0.0f, 0.0f, 0.0f) && Shape[3] == FLinearColor(0.0f, 0.0f, 0.0f, 0.0f));
	TestTrue(TEXT("none roping out"), Decay == FLinearColor(0.0f, 0.0f, 0.0f, 0.0f));

	// Roping out as it dies: drawn all the way down however its strength falls, leaning further, its decay in its slot.
	Vortices.Reset();
	FDreamWorldVortex Dying = Vortex(1000.0, 0.0, 0.3f, -1.0f, true);
	Dying.Decay = 0.7f;
	Vortices.Add(1, Vortex(500.0, 0.0, 1.0f, 1.0f, true));
	Vortices.Add(2, Dying);
	DreamSky::Funnels::Pack(Vortices, FVector::ZeroVector, 0.0, 1.0f, Funnel, Shape, Decay);
	TestTrue(TEXT("its decay in the second slot, none in the first"), FMath::IsNearlyEqual(Decay.G, 0.7f) && Decay.R == 0.0f);
	TestTrue(TEXT("all the way down, still clockwise"), FMath::IsNearlyEqual(Funnel[1].A, -1.0f));
	TestTrue(TEXT("leaning three times as far"), FMath::IsNearlyEqual(Shape[1].B, 12000.0f, 1.0f) && FMath::IsNearlyEqual(Shape[1].A, -6000.0f, 1.0f)
		&& FMath::IsNearlyEqual(Shape[0].B, 4000.0f, 1.0f));
	TestTrue(TEXT("the lean grows smoothly from none"), DreamSky::Funnels::RopeLean(0.0f) == 1.0f && DreamSky::Funnels::RopeLean(0.35f) > 1.5f
		&& DreamSky::Funnels::RopeLean(0.35f) < 2.5f && DreamSky::Funnels::RopeLean(1.0f) == 3.0f);

	// The renderer reads them after its other parameters, as DreamSkyCloudRender.usf's DSKY_FUNNEL_A / _B have them.
	const TArray<FName>& Names = FDreamSkyCloudViewExtension::GetParameterNames();
	TestTrue(TEXT("the renderer's parameters: the funnels at 31..34, their shapes at 35..38, their decay at 39"), Names.Num() == 40
		&& Names[31] == MPC_Funnel[0] && Names[34] == MPC_Funnel[3] && Names[35] == MPC_FunnelShape[0] && Names[38] == MPC_FunnelShape[3]
		&& Names[39] == MPC_FunnelDecay);

	// In a world: registered with the clock, updated and removed by id; written to the shared parameters each frame.
	FNightWorld W(FDateTime(2026, 6, 21, 16, 0, 0), 35.0, 139.7, 9.0f);
	if (TestTrue(TEXT("world"), W.IsValid()))
	{
		FDreamWorldVortex Tornado = Vortex(30000.0, -12000.0, 0.9f, 1.0f, true);
		Tornado.BornSeconds = W.Clock->GetWorldSeconds() - 600.0;
		const int32 Id = W.Clock->SetVortex(0, Tornado);
		TestTrue(TEXT("a new id"), Id > 0 && W.Clock->GetVortices().Contains(Id));
		Tornado.Location.X = 32000.0;
		TestEqual(TEXT("updated under the same id"), W.Clock->SetVortex(Id, Tornado), Id);
		TestEqual(TEXT("one registered"), W.Clock->GetVortices().Num(), 1);
		W.Clock->Tick(0.016f);
		const FLinearColor F0 = W.Parameter(MPC_Funnel[0]);
		const FLinearColor S0 = W.Parameter(MPC_FunnelShape[0]);
		TestTrue(TEXT("the shared parameters carry it"), FMath::IsNearlyEqual(F0.R, 32000.0f, 1.0f) && FMath::IsNearlyEqual(F0.A, 0.9f, 1e-4f));
		Tornado.Decay = 0.4f;
		W.Clock->SetVortex(Id, Tornado);
		W.Clock->Tick(0.016f);
		TestNearlyEqual(TEXT("and its decay"), W.Parameter(MPC_FunnelDecay).R, 0.4f, 1e-4f);
		TestNearlyEqual(TEXT("turned as far as the time says"), S0.R, DreamSky::Funnels::Turned(Tornado, W.Clock->GetWorldSeconds(), W.Clock->GetTimeScale()), 1e-3f);
		W.Clock->RemoveVortex(Id);
		W.Clock->Tick(0.016f);
		TestNearlyEqual(TEXT("removed: no funnel"), W.Parameter(MPC_Funnel[0]).A, 0.0f, 1e-6f);
	}
	return true;
}

#endif
