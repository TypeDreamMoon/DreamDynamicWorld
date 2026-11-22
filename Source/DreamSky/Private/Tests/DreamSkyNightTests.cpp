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
		TestTrue(TEXT("in the toon clouds' banded tones (negative)"), A.R < 0.0f);
		W.Sky->CloudLook = EDreamSkyCloudLook::Realistic;
		W.Clock->Tick(0.016f);
		TestTrue(TEXT("under realistic clouds, smooth (positive)"), W.Parameter(MPC_Aurora).R > 0.0f);
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
		W.SkySubsystem->SetModifierAlpha(Eclipse, 1.0f);
		W.Clock->Tick(0.016f);
		const FLinearColor Total = W.Parameter(MPC_Eclipse);
		TestEqual(TEXT("total"), Total.R, 1.0f);
		TestTrue(TEXT("the corona at totality"), Total.A > 0.0f);
		TestTrue(TEXT("the sun's light gone"), W.Sky->SunLight->Intensity <= 1e-4f * FMath::Max(Sun, 1.0f));
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

#endif
