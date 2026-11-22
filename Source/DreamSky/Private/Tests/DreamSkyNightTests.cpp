// Copyright DreamMoon. All Rights Reserved.

#include "DreamSky.h"
#include "DreamSkyModifier.h"
#include "DreamSkyEclipse.h"
#include "DreamSkyModule.h"
#include "DreamSkyStyle.h"
#include "DreamSkySubsystem.h"
#include "DreamWorldSettings.h"
#include "DreamWorldSubsystem.h"

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

#endif
