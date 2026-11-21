// Copyright DreamMoon. All Rights Reserved.

#include "DreamSky.h"
#include "DreamSkyModule.h"
#include "DreamSkyStyle.h"
#include "DreamSkySubsystem.h"
#include "DreamWorldCelestial.h"
#include "DreamWorldParameters.h"
#include "DreamWorldSubsystem.h"

#include "Components/DirectionalLightComponent.h"
#include "Components/ExponentialHeightFogComponent.h"
#include "Components/PostProcessComponent.h"
#include "Components/SkyLightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/VolumetricCloudComponent.h"
#include "Engine/Level.h"
#include "Engine/StaticMesh.h"
#include "Engine/VolumeTexture.h"
#include "HAL/IConsoleManager.h"
#include "MaterialShared.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialParameterCollection.h"
#include "Materials/MaterialParameterCollectionInstance.h"
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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDreamSkyCumulusGrowthTest, "DreamSky.Style.CumulusFollowTheHour", DreamSky::Tests::Flags)
bool FDreamSkyCumulusGrowthTest::RunTest(const FString& Parameters)
{
	using DreamSky::CumulusGrowth;
	TestTrue(TEXT("fewer in the morning than in the afternoon"), CumulusGrowth(8.0f) < CumulusGrowth(15.0f));
	TestNearlyEqual(TEXT("fully grown in the mid-afternoon"), CumulusGrowth(15.0f), 1.0f, 1e-4f);
	TestTrue(TEXT("falling apart toward evening"), CumulusGrowth(20.0f) < CumulusGrowth(17.0f));
	TestTrue(TEXT("fewest in the night"), CumulusGrowth(3.0f) <= CumulusGrowth(8.0f) && CumulusGrowth(3.0f) > 0.0f);
	TestNearlyEqual(TEXT("midnight is the same from either side"), CumulusGrowth(0.0f), CumulusGrowth(24.0f), 1e-4f);
	TestNearlyEqual(TEXT("hours wrap around the day"), CumulusGrowth(-9.0f), CumulusGrowth(15.0f), 1e-4f);
	float Last = CumulusGrowth(0.0f);
	bool bSmooth = true;
	for (float H = 0.25f; H <= 24.0f; H += 0.25f)
	{
		bSmooth &= FMath::Abs(CumulusGrowth(H) - Last) < 0.1f;
		Last = CumulusGrowth(H);
	}
	TestTrue(TEXT("no jumps through the day"), bSmooth);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDreamSkyCloudFormationTest, "DreamSky.Clouds.FormationFollowsTheClock", DreamSky::Tests::Flags)
bool FDreamSkyCloudFormationTest::RunTest(const FString& Parameters)
{
	using DreamSky::CloudFormation;
	// Some game days in, where float seconds would have lost the minutes.
	const double Day = 86400.0;
	const double Start = 40.0 * Day + 16.0 * 3600.0;
	TestNearlyEqual(TEXT("the same moment gives the same clouds"), CloudFormation(Start, 1.0f), CloudFormation(Start, 1.0f), 0.0f);
	TestNearlyEqual(TEXT("speed 0 holds the shapes"), CloudFormation(Start, 0.0f), CloudFormation(Start + 7200.0, 0.0f), 0.0f);
	// Twenty game minutes take a cloud through about an eighth of the phase.
	auto Gap = [](float A, float B) { const float D = FMath::Abs(A - B); return FMath::Min(D, 1.0f - D); };
	TestNearlyEqual(TEXT("an eighth in twenty minutes"), Gap(CloudFormation(Start, 1.0f), CloudFormation(Start + 1200.0, 1.0f)), 0.125f, 1e-3f);
	TestNearlyEqual(TEXT("twice as far at twice the speed"), Gap(CloudFormation(Start, 2.0f), CloudFormation(Start + 600.0, 2.0f)), 0.125f, 1e-3f);
	// Smooth: a frame moves it a hair, and where it wraps it wraps to the same place in the noise (0 = 1).
	bool bInRange = true;
	bool bSmooth = true;
	float Last = CloudFormation(Start, 1.0f);
	for (double T = Start; T < Start + 6.0 * 3600.0; T += 30.0)
	{
		const float Now = CloudFormation(T, 1.0f);
		bInRange &= Now >= 0.0f && Now < 1.0f;
		bSmooth &= Gap(Now, Last) < 0.005f;
		Last = Now;
	}
	TestTrue(TEXT("0..1"), bInRange);
	TestTrue(TEXT("no jumps over six hours, the wrap included"), bSmooth);
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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDreamSkyCloudNoiseTest, "DreamSky.Assets.CloudNoise", DreamSky::Tests::Flags)
bool FDreamSkyCloudNoiseTest::RunTest(const FString& Parameters)
{
	const UVolumeTexture* Noise = LoadObject<UVolumeTexture>(nullptr, TEXT("/DreamDynamicWorld/Sky/T_DreamCloudNoise.T_DreamCloudNoise"));
	if (!TestNotNull(TEXT("the volumetric clouds' 3D noise exists"), Noise))
	{
		return false;
	}
	TestFalse(TEXT("stored as data, not colour"), Noise->SRGB);
	TestEqual(TEXT("a cube of voxels"), int32(Noise->Source.GetSizeX()), int32(Noise->Source.GetNumSlices()));
	TestEqual(TEXT("128 voxels across"), int32(Noise->Source.GetSizeX()), 128);
	TestTrue(TEXT("tiles"), Noise->AddressMode == TA_Wrap);
	const UMaterialInterface* Material = LoadObject<UMaterialInterface>(nullptr, TEXT("/DreamDynamicWorld/Sky/M_DreamVolumetricCloud.M_DreamVolumetricCloud"));
	TestNotNull(TEXT("the volumetric cloud material exists"), Material);
	TestTrue(TEXT("it is a Volume domain material"), Material && Material->GetMaterial() && Material->GetMaterial()->MaterialDomain == MD_Volume);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDreamSkyCloudBackendTest, "DreamSky.Clouds.Backend", DreamSky::Tests::Flags)
bool FDreamSkyCloudBackendTest::RunTest(const FString& Parameters)
{
	DreamSky::Tests::FSkyWorld W;
	IConsoleVariable* CVar = IConsoleManager::Get().FindConsoleVariable(TEXT("DreamSky.Clouds"));
	if (!TestNotNull(TEXT("clock"), W.Clock) || !TestNotNull(TEXT("sky actor"), W.Sky) || !TestNotNull(TEXT("DreamSky.Clouds"), CVar)
		|| !TestNotNull(TEXT("the volumetric cloud component"), W.Sky->VolumetricClouds.Get()))
	{
		return false;
	}
	const int32 SavedOverride = CVar->GetInt();
	CVar->Set(-1, ECVF_SetByCode);
	const UMaterialParameterCollection* Collection = LoadObject<UMaterialParameterCollection>(nullptr, TEXT("/DreamDynamicWorld/MPC_DreamDynamicWorld.MPC_DreamDynamicWorld"));
	auto DomeLeavesCloudsOut = [&W, Collection]()
	{
		FLinearColor Value = FLinearColor::Black;
		UMaterialParameterCollectionInstance* Instance = Collection ? W.World->GetParameterCollectionInstance(Collection) : nullptr;
		return Instance && Instance->GetVectorParameterValue(DreamSky::Names::MPC_CloudLayer, Value) && Value.A > 0.5f;
	};
	auto DomeRadius = [&W]()
	{
		const UStaticMesh* Mesh = W.Sky->SkyDome->GetStaticMesh();
		return Mesh ? W.Sky->SkyDome->GetComponentScale().X * Mesh->GetBounds().SphereRadius : 0.0;
	};

	FDreamWorldClockConfig Config;
	Config.StartTime = FDateTime(2026, 6, 21, 12, 0, 0);
	W.Clock->ApplyClockConfig(Config, true);
	W.Clock->Tick(0.016f);
	TestTrue(TEXT("toon clouds by default"), W.Sky->GetEffectiveClouds() == EDreamSkyClouds::Toon);
	TestFalse(TEXT("no volumetric layer with toon clouds"), W.Sky->VolumetricClouds->GetVisibleFlag());
	TestFalse(TEXT("the dome draws its own clouds"), DomeLeavesCloudsOut());
	const double ToonDome = DomeRadius();
	// (It is in the depth buffer: a terrain reaching further than the dome stood out behind it as a black band under the
	// horizon. Its sky goes by the view direction alone, so it stands far beyond any terrain.)
	TestTrue(FString::Printf(TEXT("the toon dome stands beyond far terrain (%.0f km)"), ToonDome / 100000.0), ToonDome >= 50000000.0);
	// (So the clouds under the horizon are drawn over the scene instead, or the ground would hide them.)
	const UMaterialInterface* Below = W.Sky->CloudsBelow->GetMaterial(0);
	TestTrue(TEXT("the clouds under the horizon are drawn over the scene"), W.Sky->CloudsBelow->GetVisibleFlag());
	TestTrue(TEXT("with their own material"), Below && Below->GetPathName() == TEXT("/DreamDynamicWorld/Sky/M_DreamSkyBelow.M_DreamSkyBelow"));
	TestTrue(TEXT("a translucent one"), Below && IsTranslucentBlendMode(*Below));
	TestTrue(TEXT("round the view, well inside the dome"), W.Sky->CloudsBelow->Bounds.SphereRadius < ToonDome * 0.01);

	W.Sky->Clouds = EDreamSkyClouds::Volumetric;
	W.Clock->Tick(0.016f);
	const UDreamSkyStyle* Style = W.Sky->GetEffectiveStyle();
	TestTrue(TEXT("the volumetric layer shows"), W.Sky->VolumetricClouds->GetVisibleFlag());
	const UMaterialInterface* Material = W.Sky->VolumetricClouds->GetMaterial();
	TestTrue(TEXT("with the plugin's cloud material (not the engine's default)"), Material && Material->GetPathName() == W.Sky->VolumetricCloudMaterial.ToSoftObjectPath().ToString());
	TestNearlyEqual(TEXT("its base at the style's cloud height"), W.Sky->VolumetricClouds->LayerBottomAltitude, Style->CloudLayerHeight / 100000.0f, 1e-3f);
	TestNearlyEqual(TEXT("as tall as asked"), W.Sky->VolumetricClouds->LayerHeight, W.Sky->VolumetricLayerThickness, 1e-3f);
	TestNearlyEqual(TEXT("sampled as finely as asked"), W.Sky->VolumetricClouds->ViewSampleCountScale, W.Sky->VolumetricSampleCountScale, 1e-3f);
	TestTrue(TEXT("the dome leaves its own clouds out"), DomeLeavesCloudsOut());
	TestFalse(TEXT("and so does the sphere of the clouds under the horizon"), W.Sky->CloudsBelow->GetVisibleFlag());
	TestTrue(TEXT("the clouds shade the sky light under them"), bool(W.Sky->SkyLight->bCloudAmbientOcclusion));
	TestTrue(FString::Printf(TEXT("the dome stands beyond the traced clouds (%.0f km)"), DomeRadius() / 100000.0), DomeRadius() >= W.Sky->VolumetricTracingDistance * 100000.0 && DomeRadius() >= ToonDome);
	TestTrue(TEXT("the moon is the second atmosphere light (it lights the clouds at night)"),
		W.Sky->MoonLight->IsUsedAsAtmosphereSunLight() && W.Sky->MoonLight->GetAtmosphereSunLightIndex() == 1);

	// The painted material over the same clouds, chosen from the console; empty gives the actor's back.
	IConsoleVariable* MaterialVar = IConsoleManager::Get().FindConsoleVariable(TEXT("DreamSky.CloudMaterial"));
	if (TestNotNull(TEXT("DreamSky.CloudMaterial"), MaterialVar))
	{
		const TCHAR* PaintedPath = TEXT("/DreamDynamicWorld/Sky/M_DreamVolumetricCloud_Painted.M_DreamVolumetricCloud_Painted");
		MaterialVar->Set(PaintedPath, ECVF_SetByCode);
		W.Clock->Tick(0.016f);
		const UMaterialInterface* Painted = W.Sky->VolumetricClouds->GetMaterial();
		TestTrue(TEXT("the console's material is drawn"), Painted && Painted->GetPathName() == PaintedPath);
		TestTrue(TEXT("it is a Volume domain material"), Painted && Painted->GetMaterial() && Painted->GetMaterial()->MaterialDomain == MD_Volume);
		MaterialVar->Set(TEXT(""), ECVF_SetByCode);
		W.Clock->Tick(0.016f);
		const UMaterialInterface* Back = W.Sky->VolumetricClouds->GetMaterial();
		TestTrue(TEXT("and empty gives the actor's own back"), Back && Back->GetPathName() == W.Sky->VolumetricCloudMaterial.ToSoftObjectPath().ToString());
	}

	// The console variable wins over the actor's choice.
	CVar->Set(0, ECVF_SetByCode);
	W.Clock->Tick(0.016f);
	TestFalse(TEXT("DreamSky.Clouds 0 turns the volumetric layer off"), W.Sky->VolumetricClouds->GetVisibleFlag());
	TestFalse(TEXT("and gives the clouds back to the dome"), DomeLeavesCloudsOut());
	TestFalse(TEXT("painted clouds cast no sky light shade"), bool(W.Sky->SkyLight->bCloudAmbientOcclusion));
	TestNearlyEqual(TEXT("the dome comes back in"), DomeRadius(), ToonDome, 1.0);
	TestTrue(TEXT("and the clouds under the horizon with it"), W.Sky->CloudsBelow->GetVisibleFlag());
	CVar->Set(SavedOverride, ECVF_SetByCode);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDreamSkyCloudShadowSunTest, "DreamSky.Clouds.ShadowsDoNotDimTheSunTwice", DreamSky::Tests::Flags)
bool FDreamSkyCloudShadowSunTest::RunTest(const FString& Parameters)
{
	// An overcast sky dims the sun for clouds that cast no shadow (the dome's), not for clouds that hide it themselves.
	const UDreamSkyStyle* Style = UDreamSkySubsystem::GetDefaultStyle();
	FDreamWorldEnvironment Env;
	Env.bActive = true;
	Env.CloudCoverage = 1.0f;
	auto Overcast = [Style, &Env](bool bCloudsShadowTheSun)
	{
		FDreamSkyLighting L;
		L.SunIlluminance = 10.0f;
		L.MoonIlluminance = 0.5f;
		L.MainLightIlluminance = 10.0f;
		L.bMainLightIsMoon = false;
		FDreamSkyLook Look;
		Style->ApplyWeather(Env, L, Look, bCloudsShadowTheSun);
		return L;
	};
	const FDreamSkyLighting Dome = Overcast(false);
	const FDreamSkyLighting Shadowing = Overcast(true);
	TestNearlyEqual(TEXT("the dome's overcast dims the sun"), Dome.SunIlluminance, 10.0f * (1.0f - Style->OvercastLightDimming), 1e-4f);
	TestNearlyEqual(TEXT("shadowing clouds leave the sun as it is"), Shadowing.SunIlluminance, 10.0f, 1e-4f);
	TestNearlyEqual(TEXT("and the main light with it"), Shadowing.MainLightIlluminance, 10.0f, 1e-4f);
	TestNearlyEqual(TEXT("the moon casts no cloud shadow: still dimmed"), Shadowing.MoonIlluminance, Dome.MoonIlluminance, 1e-5f);
	TestNearlyEqual(TEXT("exposure is the same either way"), Shadowing.ExposureEV100, Dome.ExposureEV100, 1e-5f);

	// Which clouds shadow the sun: the volumetric ones, while the sun casts cloud shadows at some strength.
	DreamSky::Tests::FSkyWorld W;
	IConsoleVariable* CVar = IConsoleManager::Get().FindConsoleVariable(TEXT("DreamSky.Clouds"));
	if (!TestNotNull(TEXT("sky actor"), W.Sky) || !TestNotNull(TEXT("DreamSky.Clouds"), CVar) || !TestNotNull(TEXT("the sun"), W.Sky->SunLight.Get()))
	{
		return false;
	}
	const int32 SavedOverride = CVar->GetInt();
	CVar->Set(-1, ECVF_SetByCode);
	W.Sky->Clouds = EDreamSkyClouds::Toon;
	TestFalse(TEXT("toon clouds cast no shadow"), W.Sky->CloudsShadowTheSun());
	W.Sky->Clouds = EDreamSkyClouds::Volumetric;
	TestTrue(TEXT("volumetric clouds shadow the sun"), W.Sky->CloudsShadowTheSun());
	const float SavedStrength = W.Sky->SunLight->CloudShadowStrength;
	W.Sky->SunLight->CloudShadowStrength = 0.0f;
	TestFalse(TEXT("unless their shadow is turned off"), W.Sky->CloudsShadowTheSun());
	W.Sky->SunLight->CloudShadowStrength = SavedStrength;
	CVar->Set(SavedOverride, ECVF_SetByCode);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDreamSkyOverridesTest, "DreamSky.Actor.Overrides", DreamSky::Tests::Flags)
bool FDreamSkyOverridesTest::RunTest(const FString& Parameters)
{
	DreamSky::Tests::FSkyWorld W;
	if (!TestNotNull(TEXT("clock"), W.Clock) || !TestNotNull(TEXT("sky actor"), W.Sky))
	{
		return false;
	}
	ADreamSky& Sky = *W.Sky;
	FDreamWorldClockConfig Config;
	Config.StartTime = FDateTime(2026, 6, 21, 12, 0, 0);
	// Every check jumps the clock to noon again: a jump applies the lighting at once (no easing).
	auto Noon = [&W, &Config]()
	{
		W.Clock->ApplyClockConfig(Config, true);
		W.Clock->Tick(0.016f);
	};
	Noon();
	const UDreamSkyStyle* Preset = Sky.GetEffectiveStyle();
	const float PresetCoverage = Preset->CloudCoverage;
	const float PlainSun = Sky.SunLight->Intensity;
	const float PlainBias = Sky.PostProcess->Settings.AutoExposureBias;
	const int32 DefaultCascades = GetDefault<ADreamSky>()->SunLight->DynamicShadowCascades;

	// A style setting: a private copy carries it, the preset stays as it was.
	Sky.CloudOverrides.bOverride_Coverage = true;
	Sky.CloudOverrides.Coverage = PresetCoverage > 0.5f ? 0.2f : 0.8f;
	Sky.RefreshOverrides();
	TestTrue(TEXT("the effective style is a copy"), Sky.GetEffectiveStyle() != Preset);
	TestNearlyEqual(TEXT("with the level's coverage"), Sky.GetEffectiveStyle()->CloudCoverage, Sky.CloudOverrides.Coverage, 1e-5f);
	TestNearlyEqual(TEXT("the preset is untouched"), Preset->CloudCoverage, PresetCoverage, 1e-5f);
	Sky.CloudOverrides.bOverride_FormationSpeed = true;
	Sky.CloudOverrides.FormationSpeed = Preset->CloudFormationSpeed + 1.5f;
	Sky.RefreshOverrides();
	TestNearlyEqual(TEXT("with the level's formation speed"), Sky.GetEffectiveStyle()->CloudFormationSpeed, Sky.CloudOverrides.FormationSpeed, 1e-5f);
	Sky.CloudOverrides.bOverride_FormationSpeed = false;
	Sky.RefreshOverrides();

	// A scale over the style's lighting.
	Sky.LightOverrides.bOverride_SunIntensity = true;
	Sky.LightOverrides.SunIntensity = 0.5f;
	Sky.RefreshOverrides();
	Noon();
	TestNearlyEqual(TEXT("the sun at half its illuminance"), Sky.SunLight->Intensity, PlainSun * 0.5f, FMath::Max(1e-3f, PlainSun * 1e-3f));

	// Exposure compensation: a brighter picture (manual mode, the bias is minus the exposure value).
	Sky.ExposureOverrides.bOverride_Compensation = true;
	Sky.ExposureOverrides.Compensation = 1.0f;
	Sky.RefreshOverrides();
	Noon();
	TestNearlyEqual(TEXT("one stop brighter"), Sky.PostProcess->Settings.AutoExposureBias, PlainBias + 1.0f, 1e-3f);

	// Auto exposure: eye adaptation held around the curve.
	Sky.ExposureOverrides.bOverride_Mode = true;
	Sky.ExposureOverrides.Mode = EDreamSkyExposureMode::Auto;
	Sky.RefreshOverrides();
	Noon();
	const FPostProcessSettings& Exposure = Sky.PostProcess->Settings;
	TestTrue(TEXT("auto exposure adapts (histogram)"), Exposure.bOverride_AutoExposureMethod && Exposure.AutoExposureMethod == AEM_Histogram);
	TestTrue(TEXT("within a range"), Exposure.bOverride_AutoExposureMinBrightness && Exposure.AutoExposureMinBrightness < Exposure.AutoExposureMaxBrightness);

	// A light's own setting, and back to the sky's default when unticked.
	Sky.LightOverrides.bOverride_ShadowCascades = true;
	Sky.LightOverrides.ShadowCascades = DefaultCascades + 2;
	Sky.RefreshOverrides();
	TestEqual(TEXT("the sun's cascades as set"), Sky.SunLight->DynamicShadowCascades, DefaultCascades + 2);
	Sky.LightOverrides.bOverride_ShadowCascades = false;
	Sky.RefreshOverrides();
	TestEqual(TEXT("unticked: the sky's default again"), Sky.SunLight->DynamicShadowCascades, DefaultCascades);

	// A material setting: the volumetric clouds draw an instance of their material carrying it.
	Sky.Clouds = EDreamSkyClouds::Volumetric;
	Sky.CloudOverrides.bOverride_Extinction = true;
	Sky.CloudOverrides.Extinction = 0.2f;
	Sky.RefreshOverrides();
	Noon();
	const UMaterialInstanceDynamic* Instance = Cast<UMaterialInstanceDynamic>(Sky.VolumetricClouds->GetMaterial());
	if (TestNotNull(TEXT("the clouds draw an instance"), Instance))
	{
		TestTrue(TEXT("of the actor's material"), Instance->Parent && Instance->Parent->GetPathName() == Sky.VolumetricCloudMaterial.ToSoftObjectPath().ToString());
		float Extinction = 0.0f;
		TestTrue(TEXT("carrying the extinction"), Instance->GetScalarParameterValue(TEXT("CloudExtinction"), Extinction) && FMath::IsNearlyEqual(Extinction, 0.2f, 1e-5f));
	}
	Sky.CloudOverrides.bOverride_Extinction = false;
	Sky.RefreshOverrides();
	Noon();
	TestFalse(TEXT("unticked: the plain material again"), Sky.VolumetricClouds->GetMaterial() && Sky.VolumetricClouds->GetMaterial()->IsA<UMaterialInstanceDynamic>());

	// Nothing ticked: the preset itself.
	Sky.CloudOverrides = FDreamSkyCloudOverrides();
	Sky.LightOverrides = FDreamSkyLightOverrides();
	Sky.ExposureOverrides = FDreamSkyExposureOverrides();
	Sky.RefreshOverrides();
	TestTrue(TEXT("no overrides: the preset in effect"), Sky.GetEffectiveStyle() == Preset);
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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDreamSkyCloudLookParametersTest, "DreamSky.Clouds.LookParameters", DreamSky::Tests::Flags)
bool FDreamSkyCloudLookParametersTest::RunTest(const FString& Parameters)
{
	// The rendered clouds' realistic and painted looks go by the volumetric cloud materials' own parameters, the level's
	// overrides over them: a level set up for the volumetric clouds looks the same drawn by the renderer.
	DreamSky::Tests::FSkyWorld W;
	if (!TestNotNull(TEXT("sky actor"), W.Sky))
	{
		return false;
	}
	ADreamSky& Sky = *W.Sky;
	const UMaterialInterface* Realistic = Sky.VolumetricCloudMaterial.LoadSynchronous();
	const UMaterialInterface* Painted = LoadObject<UMaterialInterface>(nullptr, TEXT("/DreamDynamicWorld/Sky/M_DreamVolumetricCloud_Painted.M_DreamVolumetricCloud_Painted"));
	if (!TestNotNull(TEXT("the realistic material"), Realistic) || !TestNotNull(TEXT("the painted material"), Painted))
	{
		return false;
	}
	auto Scalar = [](const UMaterialInterface* Material, const TCHAR* Name)
	{
		float Value = -1.0f;
		Material->GetScalarParameterValue(FHashedMaterialParameterInfo(Name), Value);
		return Value;
	};

	Sky.CloudLook = EDreamSkyCloudLook::Realistic;
	TestTrue(TEXT("the actor's look (the console's left alone)"), Sky.GetEffectiveCloudLook() == EDreamSkyCloudLook::Realistic);
	FDreamSkyCloudLookParameters P;
	Sky.GetCloudLookParameters(P);
	TestNearlyEqual(TEXT("realistic: the material's extinction"), P.Extinction, Scalar(Realistic, TEXT("CloudExtinction")), 1e-5f);
	TestNearlyEqual(TEXT("its glow"), P.Glow, Scalar(Realistic, TEXT("CloudGlow")), 1e-5f);
	TestNearlyEqual(TEXT("its multiple scattering"), P.MultiScatter, Scalar(Realistic, TEXT("MultiScatter")), 1e-5f);
	TestNearlyEqual(TEXT("painted: that material's extinction"), P.PaintExtinction, Scalar(Painted, TEXT("CloudExtinction")), 1e-5f);
	TestNearlyEqual(TEXT("its edge softness"), P.EdgeSoftness, Scalar(Painted, TEXT("EdgeSoftness")), 1e-5f);
	TestNearlyEqual(TEXT("its silver lining"), P.SilverLining, Scalar(Painted, TEXT("SilverLining")), 1e-5f);
	TestNearlyEqual(TEXT("the sun's scattering as the volumetric layer has it"), P.SunScattering, Sky.VolumetricSunScattering, 1e-5f);
	TestNearlyEqual(TEXT("the shading's reach (ShadowReach)"), P.ShadowReach, Scalar(Realistic, TEXT("ShadowReach")), 1.0f);
	// The light as the engine's clouds take it: by day the sun light component, its own colour times its illuminance.
	if (W.Clock)
	{
		FDreamWorldClockConfig Config;
		Config.StartTime = FDateTime(2026, 6, 21, 12, 0, 0);
		W.Clock->ApplyClockConfig(Config, true);
		W.Clock->Tick(0.016f);
		Sky.GetCloudLookParameters(P);
		const FLinearColor Sun = Sky.SunLight->GetLightColor() * Sky.SunLight->Intensity;
		TestTrue(FString::Printf(TEXT("noon: the sun light's colour x lux (%s vs %s)"), *P.LightIlluminance.ToString(), *Sun.ToString()),
			Sun.R > 0.0f && P.LightIlluminance.Equals(Sun, 1e-3f * FMath::Max(1.0f, Sun.R)));
		// The height fog the engine lays over its clouds, as the sky's fog component has it.
		TestNearlyEqual(TEXT("the fog's density"), P.FogDensity, Sky.HeightFog->FogDensity, 1e-6f);
		TestNearlyEqual(TEXT("its falloff"), P.FogHeightFalloff, Sky.HeightFog->FogHeightFalloff, 1e-6f);
		TestNearlyEqual(TEXT("its max opacity"), P.FogMaxOpacity, Sky.HeightFog->FogMaxOpacity, 1e-6f);
		TestTrue(TEXT("its colour"), P.FogColor.Equals(Sky.HeightFog->FogInscatteringLuminance, 1e-5f));
	}
	const float RealisticExtinction = P.Extinction;
	const float PaintedExtinction = P.PaintExtinction;

	Sky.CloudOverrides.bOverride_Extinction = true;
	Sky.CloudOverrides.Extinction = 0.2f;
	Sky.CloudOverrides.bOverride_Glow = true;
	Sky.CloudOverrides.Glow = 1.7f;
	Sky.CloudOverrides.bOverride_SilverLining = true;
	Sky.CloudOverrides.SilverLining = 3.0f;
	Sky.GetCloudLookParameters(P);
	TestNearlyEqual(TEXT("overridden extinction: the look in use takes it"), P.Extinction, 0.2f, 1e-5f);
	TestNearlyEqual(TEXT("the other keeps its own"), P.PaintExtinction, PaintedExtinction, 1e-5f);
	TestNearlyEqual(TEXT("overridden glow"), P.Glow, 1.7f, 1e-5f);
	TestNearlyEqual(TEXT("overridden silver lining"), P.SilverLining, 3.0f, 1e-5f);
	Sky.CloudLook = EDreamSkyCloudLook::Painted;
	Sky.GetCloudLookParameters(P);
	TestNearlyEqual(TEXT("painted in use: it takes the extinction"), P.PaintExtinction, 0.2f, 1e-5f);
	TestNearlyEqual(TEXT("and the realistic keeps its own"), P.Extinction, RealisticExtinction, 1e-5f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDreamSkyStormExposureTest, "DreamSky.Clouds.StormStaysDarkerThanOvercast", DreamSky::Tests::Flags)
bool FDreamSkyStormExposureTest::RunTest(const FString& Parameters)
{
	// Under shadowing clouds the eye may brighten by what it sees; a storm is meant to look dark, so it gets little of that.
	const UDreamSkyStyle* Style = UDreamSkySubsystem::GetDefaultStyle();
	const float Full = Style->CloudShadowExposureLift;
	if (!TestTrue(TEXT("the default style lifts at all"), Full > 0.0f))
	{
		return false;
	}
	// Cover and darkness of the stock presets (WP_Clear, WP_Cloudy, WP_Overcast, WP_Storm).
	const float Clear = Style->CloudShadowExposureLiftFor(0.0f, 0.0f);
	const float Cloudy = Style->CloudShadowExposureLiftFor(0.55f, 0.05f);
	const float Overcast = Style->CloudShadowExposureLiftFor(0.9f, 0.25f);
	const float Storm = Style->CloudShadowExposureLiftFor(1.0f, 0.85f);
	TestNearlyEqual(TEXT("a clear sky keeps the curve"), Clear, 0.0f, 1e-5f);
	TestNearlyEqual(TEXT("broken cloud: by the cover alone"), Cloudy, Full * FMath::SmoothStep(0.2f, 0.85f, 0.55f), 1e-4f);
	TestNearlyEqual(TEXT("a grey overcast deck: all of it"), Overcast, Full, 1e-4f);
	TestTrue(FString::Printf(TEXT("a storm gets well under half of the overcast lift (%.2f vs %.2f EV)"), Storm, Overcast), Storm < 0.5f * Overcast);
	TestTrue(TEXT("never negative: never darker than the curve"), Style->CloudShadowExposureLiftFor(1.0f, 1.0f) >= 0.0f);
	for (float Dark = 0.0f; Dark < 1.0f; Dark += 0.05f)
	{
		if (Style->CloudShadowExposureLiftFor(1.0f, Dark + 0.05f) > Style->CloudShadowExposureLiftFor(1.0f, Dark) + 1e-5f)
		{
			AddError(FString::Printf(TEXT("darker clouds lift more at darkness %.2f"), Dark));
			break;
		}
	}
	return true;
}
