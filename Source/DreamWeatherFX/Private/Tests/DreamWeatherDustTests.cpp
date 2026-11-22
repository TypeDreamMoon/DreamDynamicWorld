// Copyright DreamMoon. All Rights Reserved.

#include "DreamWeather.h"
#include "DreamWeatherFXSettings.h"
#include "DreamWeatherFXSubsystem.h"
#include "DreamWeatherModule.h"
#include "DreamWeatherPreset.h"
#include "DreamWeatherSelector.h"
#include "DreamWeatherSubsystem.h"
#include "DreamWindSubsystem.h"
#include "DreamWorldSubsystem.h"

#include "Engine/World.h"
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "UObject/Package.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace DreamWeatherFX::DustTests
{
	constexpr EAutomationTestFlags Flags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter;

	UDreamWeatherPreset* MakePreset(const TCHAR* Name, std::initializer_list<TPair<FGameplayTag, float>> Channels)
	{
		UDreamWeatherPreset* Preset = NewObject<UDreamWeatherPreset>(GetTransientPackage(), MakeUniqueObjectName(GetTransientPackage(), UDreamWeatherPreset::StaticClass(), Name));
		for (const TPair<FGameplayTag, float>& Channel : Channels)
		{
			Preset->Channels.Add(Channel.Key, Channel.Value);
		}
		return Preset;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDreamWeatherDustTest, "DreamWeather.FX.DustAndDebris", DreamWeatherFX::DustTests::Flags)
bool FDreamWeatherDustTest::RunTest(const FString& Parameters)
{
	using namespace DreamWeatherFX::DustTests;
	using namespace DreamWeather::Tags;
	using FX = UDreamWeatherFXSubsystem;

	// The season's debris: autumn has the most, winter the least, and the colour turns.
	FLinearColor Spring, Autumn, Winter;
	const float InSpring = FX::SeasonDebris(FDateTime(2026, 4, 15), false, Spring);
	const float InAutumn = FX::SeasonDebris(FDateTime(2026, 10, 15), false, Autumn);
	const float InWinter = FX::SeasonDebris(FDateTime(2027, 1, 15), false, Winter);
	TestTrue(FString::Printf(TEXT("autumn has the most debris (%.2f / %.2f / %.2f)"), InSpring, InAutumn, InWinter), InAutumn > InSpring && InSpring > InWinter);
	TestTrue(TEXT("autumn leaves are redder than spring's"), Autumn.R > Spring.R && Autumn.G < Spring.G + 0.2f);
	FLinearColor South;
	TestNearlyEqual(TEXT("the south's April is an autumn"), FX::SeasonDebris(FDateTime(2026, 4, 15), true, South), InAutumn, 1.0e-3f);

	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
	if (!TestNotNull(TEXT("world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(false); };
	UDreamWorldSubsystem* Clock = World->GetSubsystem<UDreamWorldSubsystem>();
	UDreamWindSubsystem* WindSubsystem = UDreamWindSubsystem::Get(World);
	FX* Effects = FX::Get(World);
	// The wind builds its frame in its own tick, which a bare test world never runs: a frame here is the clock's
	// (the weather sets the wind) then the wind's, then the clock's again (the effects read the wind).
	auto Frame = [Clock, WindSubsystem]()
	{
		Clock->Tick(0.016f);
		if (WindSubsystem)
		{
			WindSubsystem->Tick(0.016f);
		}
		Clock->Tick(0.016f);
	};
	UDreamWeatherSelector_Fixed* Fixed = NewObject<UDreamWeatherSelector_Fixed>();
	Fixed->Preset = MakePreset(TEXT("Calm"), { { Channel_CloudCoverage, 0.1f }, { Channel_Wind, 0.0f } });
	FActorSpawnParameters Spawn;
	Spawn.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	ADreamWeather* Weather = World->SpawnActor<ADreamWeather>(Spawn);
	Fixed->Rename(nullptr, Weather);
	Weather->Selector = Fixed;
	FDreamWorldClockConfig Config;
	Config.StartTime = FDateTime(2026, 10, 15, 12, 0, 0);
	Clock->ApplyClockConfig(Config, true);
	Clock->ReportView(FVector(0, 0, 170), FRotator::ZeroRotator, true);
	Frame();
	TestEqual(TEXT("calm and clean: no dust paths traced"), FX::DustLayer < FX::NumLayers ? Effects->GetPaths(FX::DustLayer).GetTracesCast() : 0LL, 0LL);
	TestEqual(TEXT("and no debris"), Effects->GetDebrisRate(), 0.0f);

	// A dust storm: the dust layer runs, nearly flat downwind.
	Fixed->Preset = MakePreset(TEXT("Dust"), { { Channel_CloudCoverage, 0.3f }, { Channel_Dust, 1.0f }, { Channel_Wind, 0.8f } });
	Clock->SetTimeOfDay(12.0f);
	Frame();
	TestTrue(FString::Printf(TEXT("dust streams (%.0f motes/s)"), Effects->GetDropRate(FX::DustLayer)), Effects->GetDropRate(FX::DustLayer) > 500.0f);
	const FVector Fall = Effects->GetFallVelocity(FX::DustLayer);
	TestTrue(FString::Printf(TEXT("along the ground, downwind (%s)"), *Fall.ToString()), Fall.Size2D() > 3.0 * FMath::Abs(Fall.Z));
	TestEqual(TEXT("a full buffer of dust paths"), Effects->GetPaths(FX::DustLayer).GetEnds().Num(), UDreamWeatherFXSettings::Get()->PathCount);
	TestTrue(FString::Printf(TEXT("an autumn gale carries debris (%.0f/s)"), Effects->GetDebrisRate()), Effects->GetDebrisRate() > 0.0f);
	for (int32 i = 0; i < 60; ++i)
	{
		Frame();
	}
	TestNearlyEqual(TEXT("the storm's dust is all in the air"), Effects->GetPresence(FX::DustLayer), 1.0f, 1.0e-3f);

	// A gale with no dust: the paths run for the debris, but no motes.
	Fixed->Preset = MakePreset(TEXT("Gale"), { { Channel_CloudCoverage, 0.3f }, { Channel_Dust, 0.0f }, { Channel_Wind, 1.0f } });
	Clock->SetTimeOfDay(12.0f);
	Frame();
	TestEqual(TEXT("a clean gale: no motes"), Effects->GetDropRate(FX::DustLayer), 0.0f);
	TestTrue(TEXT("but debris"), Effects->GetDebrisRate() > 0.0f);
	// The motes and puffs still flying (they live 20 s) fade with the dust, not cut off and not lingering.
	const float Fading = Effects->GetPresence(FX::DustLayer);
	TestTrue(FString::Printf(TEXT("the dust in the air fades (%.2f)"), Fading), Fading > 0.0f && Fading < 1.0f);
	for (int32 i = 0; i < 60; ++i)
	{
		Frame();
	}
	TestEqual(TEXT("and is gone within a second or two, long before its particles would die"), Effects->GetPresence(FX::DustLayer), 0.0f);
	return true;
}

#endif
