// Copyright DreamMoon. All Rights Reserved.

#include "DreamSky.h"
#include "DreamSkyModule.h"
#include "DreamSkyStyle.h"
#include "DreamSkySubsystem.h"
#include "DreamWorldCelestial.h"
#include "DreamWorldParameters.h"
#include "DreamWorldSubsystem.h"

#include "Components/DirectionalLightComponent.h"
#include "Engine/Level.h"
#include "Engine/Texture2D.h"
#include "Engine/World.h"
#include "Materials/Material.h"
#include "Materials/MaterialInterface.h"
#include "Misc/AutomationTest.h"
#include "UObject/Package.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace DreamSky::Tests
{
	constexpr EAutomationTestFlags Flags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter;

	struct FSkyWorld
	{
		UWorld* World = nullptr;
		UDreamWorldSubsystem* Clock = nullptr;
		UDreamSkySubsystem* SkySubsystem = nullptr;
		ADreamSky* Sky = nullptr;

		FSkyWorld()
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
		}
		~FSkyWorld()
		{
			if (World)
			{
				World->DestroyWorld(false);
			}
		}
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDreamSkyStyleContinuityTest, "DreamSky.Style.DayIsContinuous", DreamSky::Tests::Flags)
bool FDreamSkyStyleContinuityTest::RunTest(const FString& Parameters)
{
	const UDreamSkyStyle* Style = UDreamSkySubsystem::GetDefaultStyle();
	FDreamCelestialSettings Settings;
	const double Start = DreamWorld::Celestial::FromDateTime(FDateTime(2026, 6, 21, 0, 0, 0));

	float MaxMainStep = 0.0f;
	float MaxOverlap = 0.0f;
	float MaxEvStep = 0.0f;
	bool bPhaseOk = true;
	FDreamSkyLighting Prev = Style->EvaluateLighting(DreamWorld::Celestial::Evaluate(Settings, Start));
	for (int32 Minute = 1; Minute <= 1440; ++Minute)
	{
		const FDreamSkyLighting L = Style->EvaluateLighting(DreamWorld::Celestial::Evaluate(Settings, Start + Minute * 60.0));
		MaxMainStep = FMath::Max(MaxMainStep, FMath::Abs(L.MainLightIlluminance - Prev.MainLightIlluminance));
		MaxOverlap = FMath::Max(MaxOverlap, FMath::Min(L.SunIlluminance, L.MoonIlluminance));
		MaxEvStep = FMath::Max(MaxEvStep, FMath::Abs(L.ExposureEV100 - Prev.ExposureEV100));
		bPhaseOk &= FMath::IsNearlyEqual(L.DayPhase.Day + L.DayPhase.Twilight + L.DayPhase.Night, 1.0f, 1e-4f);
		Prev = L;
	}
	// A toon ramp reads the main light: it must never jump within a minute of game time.
	TestTrue(FString::Printf(TEXT("main light changes smoothly (largest step %.3f lux per game minute)"), MaxMainStep), MaxMainStep < 0.5f);
	TestTrue(FString::Printf(TEXT("sun and moon never light at once (overlap %.4f lux)"), MaxOverlap), MaxOverlap < 0.01f);
	TestTrue(FString::Printf(TEXT("exposure changes smoothly (largest step %.3f EV)"), MaxEvStep), MaxEvStep < 0.1f);
	TestTrue(TEXT("day phase weights sum to one"), bPhaseOk);

	// Midnight: the night light points down from above, and the sun is off.
	const FDreamSkyLighting Midnight = Style->EvaluateLighting(DreamWorld::Celestial::Evaluate(Settings, Start));
	TestTrue(TEXT("midnight main light is the moon"), Midnight.bMainLightIsMoon);
	TestTrue(TEXT("midnight main light is above the horizon"), Midnight.MainLightDirection.Z > 0.2);
	TestEqual(TEXT("midnight sun is off"), Midnight.SunIlluminance, 0.0f);
	TestTrue(TEXT("night is darker than day"), Midnight.MainLightIlluminance < Style->EvaluateLighting(DreamWorld::Celestial::Evaluate(Settings, Start + 12 * 3600.0)).MainLightIlluminance);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDreamSkyApplyTest, "DreamSky.Actor.SnapAndEase", DreamSky::Tests::Flags)
bool FDreamSkyApplyTest::RunTest(const FString& Parameters)
{
	DreamSky::Tests::FSkyWorld W;
	if (!TestNotNull(TEXT("clock"), W.Clock) || !TestNotNull(TEXT("sky subsystem"), W.SkySubsystem) || !TestNotNull(TEXT("sky actor"), W.Sky))
	{
		return false;
	}
	TestTrue(TEXT("the sky actor is in charge"), W.SkySubsystem->GetSky() == W.Sky);

	FDreamWorldClockConfig Config;
	Config.StartTime = FDateTime(2026, 6, 21, 12, 0, 0);
	Config.bAnimate = true;
	W.Clock->ApplyClockConfig(Config, true);
	W.Clock->Tick(0.016f);

	const UDreamSkyStyle* Style = W.Sky->GetEffectiveStyle();
	const FDreamSkyLighting Noon = Style->EvaluateLighting(W.Clock->GetCelestial());
	TestNearlyEqual(TEXT("a jump snaps the sun to its target"), W.Sky->SunLight->Intensity, Noon.SunIlluminance, 1e-3f);
	TestTrue(TEXT("the sun light points down"), W.Sky->SunLight->GetForwardVector().Z < -0.5);
	TestEqual(TEXT("the moon light is off at noon"), W.Sky->MoonLight->Intensity, 0.0f);
	TestTrue(TEXT("the sun light is the atmosphere sun"), W.Sky->SunLight->IsUsedAsAtmosphereSunLight());

	// Jump to just before sunset, then let the clock run: the light eases toward a dimmer target.
	W.Clock->SetTimeOfDay(17.5f);
	W.Clock->Tick(0.016f);
	const float Before = W.Sky->SunLight->Intensity;
	W.Clock->SetDayLengthMinutes(0.5f); // 48 game minutes per real second
	W.Clock->Tick(0.25f);
	const float Target = Style->EvaluateLighting(W.Clock->GetCelestial()).SunIlluminance;
	const float After = W.Sky->SunLight->Intensity;
	TestTrue(FString::Printf(TEXT("eases instead of jumping (%.3f -> %.3f, target %.3f)"), Before, After, Target), After < Before && After > Target);

	// Midnight: the sun is black but still points at the sun (below), the moon takes over from above.
	W.Clock->SetTimeOfDay(0.0f);
	W.Clock->Tick(0.016f);
	TestEqual(TEXT("sun black at midnight"), W.Sky->SunLight->Intensity, 0.0f);
	TestTrue(TEXT("sun light still follows the sun"), W.Sky->SunLight->GetForwardVector().Z > 0.5);
	TestTrue(TEXT("moon lit at midnight"), W.Sky->MoonLight->Intensity > 0.0f);
	TestTrue(TEXT("moon light points down"), W.Sky->MoonLight->GetForwardVector().Z < -0.2);
	TestTrue(TEXT("the director reports the moon as main light"), W.SkySubsystem->GetLighting().bMainLightIsMoon);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDreamSkyPerfTest, "DreamSky.Perf.GameThreadTick", DreamSky::Tests::Flags)
bool FDreamSkyPerfTest::RunTest(const FString& Parameters)
{
	DreamSky::Tests::FSkyWorld W;
	if (!TestNotNull(TEXT("clock"), W.Clock) || !TestNotNull(TEXT("sky actor"), W.Sky))
	{
		return false;
	}
	FDreamWorldClockConfig Config;
	Config.StartTime = FDateTime(2026, 6, 21, 4, 0, 0);
	Config.DayLengthMinutes = 1.0f; // a whole day in a real minute: every light, colour and the moon change all the time
	Config.bAnimate = true;
	W.Clock->ApplyClockConfig(Config, true);
	// A view to probe (its rays find nothing in this empty world, but they are cast).
	W.Clock->ReportViewLocation(FVector(0, 0, 170), true);
	for (int32 i = 0; i < 30; ++i)
	{
		W.Clock->Tick(1.0f / 60.0f);
	}

	// The clock and everything it drives (view probe, sky director, lights, fog, exposure, shared parameters),
	// 60 fps for 20 seconds = a third of a game day, through sunrise.
	constexpr int32 Frames = 1200;
	double Worst = 0.0;
	const double Start = FPlatformTime::Seconds();
	for (int32 i = 0; i < Frames; ++i)
	{
		const double T0 = FPlatformTime::Seconds();
		W.Clock->Tick(1.0f / 60.0f);
		Worst = FMath::Max(Worst, FPlatformTime::Seconds() - T0);
	}
	const double AverageMs = (FPlatformTime::Seconds() - Start) * 1000.0 / Frames;
	const FString Summary = FString::Printf(TEXT("clock + sky game-thread tick: %.4f ms average, %.4f ms worst over %d frames"), AverageMs, Worst * 1000.0, Frames);
	AddInfo(Summary);
	UE_LOG(LogDreamSky, Display, TEXT("%s"), *Summary);
	TestTrue(Summary, AverageMs < 0.25);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDreamSkyTestLevelTest, "DreamSky.Assets.TestLevel", DreamSky::Tests::Flags)
bool FDreamSkyTestLevelTest::RunTest(const FString& Parameters)
{
	const TCHAR* MapPath = TEXT("/DreamDynamicWorld/Sky/Maps/Lvl_DreamSkyTest");
	UPackage* Package = LoadPackage(nullptr, MapPath, LOAD_None);
	UWorld* World = Package ? UWorld::FindWorldInPackage(Package) : nullptr;
	if (!TestNotNull(TEXT("the sky test level loads"), World))
	{
		return false;
	}
	ADreamSky* Sky = nullptr;
	int32 Cameras = 0;
	for (AActor* Actor : World->PersistentLevel->Actors)
	{
		Sky = Sky ? Sky : Cast<ADreamSky>(Actor);
		Cameras += Actor && Actor->GetClass()->GetName() == TEXT("CameraActor") ? 1 : 0;
	}
	if (!TestNotNull(TEXT("the level has a Dream Sky"), Sky))
	{
		return false;
	}
	TestNotNull(TEXT("with a style asset"), Sky->Style.Get());
	TestEqual(TEXT("one camera for the shots"), Cameras, 1);
	const UMaterialInterface* DomeMaterial = Sky->SkyMaterial.LoadSynchronous();
	TestNotNull(TEXT("the sky material exists"), DomeMaterial);
	TestTrue(TEXT("the sky material is a sky material"), DomeMaterial && DomeMaterial->GetMaterial() && DomeMaterial->GetMaterial()->bIsSky);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDreamSkyCloudSpritesTest, "DreamSky.Assets.CloudSprites", DreamSky::Tests::Flags)
bool FDreamSkyCloudSpritesTest::RunTest(const FString& Parameters)
{
	const UTexture2D* Sprites = LoadObject<UTexture2D>(nullptr, TEXT("/DreamDynamicWorld/Sky/T_DreamCloudSprites.T_DreamCloudSprites"));
	if (!TestNotNull(TEXT("the horizon cloud sprites exist"), Sprites))
	{
		return false;
	}
	// The channels are light, rim, core and coverage: data, read as it is stored.
	TestFalse(TEXT("stored as data, not colour"), Sprites->SRGB);
	TestTrue(TEXT("compressed with a full alpha channel"), Sprites->CompressionSettings == TC_BC7);
	TestEqual(TEXT("the packer's atlas size"), int32(Sprites->Source.GetSizeX()), 2048);
	TestTrue(TEXT("not streamed (the sky always shows it whole)"), Sprites->NeverStream);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDreamSkyInteriorAndFlashTest, "DreamSky.Style.InteriorAndFlash", DreamSky::Tests::Flags)
bool FDreamSkyInteriorAndFlashTest::RunTest(const FString& Parameters)
{
	const UDreamSkyStyle* Style = UDreamSkySubsystem::GetDefaultStyle();
	FDreamCelestialSettings Settings;
	const double Noon = DreamWorld::Celestial::FromDateTime(FDateTime(2026, 6, 21, 12, 0, 0));
	const FDreamSkyLighting Day = Style->EvaluateLighting(DreamWorld::Celestial::Evaluate(Settings, Noon));

	// Indoors lifts the exposure by InteriorExposureLift; half indoors by half; an unknown probe does nothing.
	FDreamViewOcclusion Room;
	Room.Interior = 1.0f;
	Room.bValid = true;
	FDreamSkyLighting In = Day;
	Style->ApplyInterior(Room, In);
	TestNearlyEqual(TEXT("indoors lifts the exposure"), In.ExposureEV100, Day.ExposureEV100 - Style->InteriorExposureLift, 1e-4f);
	Room.Interior = 0.5f;
	FDreamSkyLighting Half = Day;
	Style->ApplyInterior(Room, Half);
	TestNearlyEqual(TEXT("half indoors, half the lift"), Half.ExposureEV100, Day.ExposureEV100 - 0.5f * Style->InteriorExposureLift, 1e-4f);
	Room.bValid = false;
	FDreamSkyLighting Unknown = Day;
	Style->ApplyInterior(Room, Unknown);
	TestEqual(TEXT("no probe, no change"), Unknown.ExposureEV100, Day.ExposureEV100);

	// A flash at noon is brightened with the exposure, so it still reads in daylight.
	FDreamWorldEnvironment Env;
	Env.bActive = true;
	Env.LightningFlash = 1.0f;
	Env.LightningDirection = FVector(1, 0, 1).GetSafeNormal();
	FDreamSkyLighting Flash = Day;
	Style->ApplyFlash(Env, Flash);
	const float Gain = FMath::Pow(2.0f, FMath::Max(0.0f, Day.ExposureEV100 - Style->LightningReferenceEV) * Style->LightningExposureCompensation);
	TestNearlyEqual(TEXT("flash gain follows the exposure"), Flash.FlashGain, Gain, 1e-4f);
	TestTrue(TEXT("daylight brightens the flash"), Flash.FlashGain > 1.0f);
	TestNearlyEqual(TEXT("the flash light"), Flash.LightningIlluminance, Gain * Style->LightningLightIlluminance, 1e-3f);
	TestTrue(TEXT("the flash brightens the ambient light"), Flash.AmbientSky.R > Day.AmbientSky.R);
	Env.LightningFlash = 0.0f;
	FDreamSkyLighting Calm = Day;
	Style->ApplyFlash(Env, Calm);
	TestEqual(TEXT("no flash, no flash light"), Calm.LightningIlluminance, 0.0f);

	// The actor's flash light follows: lit toward the strike during a flash, black between flashes.
	DreamSky::Tests::FSkyWorld W;
	if (!TestNotNull(TEXT("sky actor"), W.Sky))
	{
		return false;
	}
	W.Sky->ApplyLighting(Flash, *Style, true);
	TestNearlyEqual(TEXT("flash light on"), W.Sky->LightningLight->Intensity, Flash.LightningIlluminance, 1e-3f);
	TestTrue(TEXT("shining away from the strike"), FVector::DotProduct(W.Sky->LightningLight->GetForwardVector(), Env.LightningDirection) < -0.99);
	TestFalse(TEXT("no flash shadows by default"), W.Sky->LightningLight->CastShadows);
	W.Sky->ApplyLighting(Calm, *Style, false);
	TestEqual(TEXT("flash light off between flashes"), W.Sky->LightningLight->Intensity, 0.0f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDreamSkyParametersTest, "DreamSky.Parameters", DreamSky::Tests::Flags)
bool FDreamSkyParametersTest::RunTest(const FString& Parameters)
{
	using namespace DreamSky::Names;
	for (const FName& Name : { MPC_Zenith, MPC_Horizon, MPC_SunDisk, MPC_Cloud0, MPC_MainLight, MPC_AmbientSky, MPC_DayPhase, MPC_SkyExtra, MPC_SkyHaze, MPC_Cloud2, MPC_Cloud3 })
	{
		TestTrue(*FString::Printf(TEXT("%s registered"), *Name.ToString()), FDreamWorldParameters::IsRegistered(Name));
	}
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
