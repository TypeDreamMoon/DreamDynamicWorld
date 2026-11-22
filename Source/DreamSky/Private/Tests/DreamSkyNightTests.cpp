// Copyright DreamMoon. All Rights Reserved.

#include "DreamSky.h"
#include "DreamSkyModifier.h"
#include "DreamSkyModule.h"
#include "DreamSkyStyle.h"
#include "DreamSkySubsystem.h"
#include "DreamWorldSettings.h"
#include "DreamWorldSubsystem.h"

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
		TestTrue(FString::Printf(TEXT("strong in the arctic night (%.4f)"), A.R), A.R > 0.5f * W.Sky->GetEffectiveStyle()->Aurora.Brightness);
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
		TestTrue(TEXT("strong in the antarctic night"), A.R > 0.0f);
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
		TestTrue(TEXT("a modifier brings it at night"), W.Parameter(MPC_Aurora).R > 0.0f);
		W.Clock->SetTimeOfDay(12.0f);
		W.Clock->Tick(0.016f);
		TestEqual(TEXT("but not by day"), W.Parameter(MPC_Aurora).R, 0.0f);
	}
	return true;
}

#endif
