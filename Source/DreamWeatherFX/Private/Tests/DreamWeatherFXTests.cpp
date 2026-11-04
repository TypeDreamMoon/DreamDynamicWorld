// Copyright DreamMoon. All Rights Reserved.

#include "DreamWeather.h"
#include "DreamWeatherFXSettings.h"
#include "DreamWeatherFXSubsystem.h"
#include "DreamWeatherModule.h"
#include "DreamWeatherPreset.h"
#include "DreamWeatherSelector.h"
#include "DreamWeatherSubsystem.h"
#include "DreamWorldSubsystem.h"

#include "Engine/World.h"
#include "Materials/MaterialInterface.h"
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "NiagaraComponent.h"
#include "NiagaraDataInterfaceArrayFunctionLibrary.h"
#include "NiagaraSystem.h"
#include "UObject/Package.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace DreamWeatherFX::Tests
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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDreamWeatherFXPrecipitationTest, "DreamWeather.FX.Precipitation", DreamWeatherFX::Tests::Flags)
bool FDreamWeatherFXPrecipitationTest::RunTest(const FString& Parameters)
{
	using namespace DreamWeatherFX::Tests;
	using namespace DreamWeather::Tags;
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
	if (!TestNotNull(TEXT("world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	UDreamWorldSubsystem* Clock = World->GetSubsystem<UDreamWorldSubsystem>();
	UDreamWeatherFXSubsystem* FX = UDreamWeatherFXSubsystem::Get(World);
	if (!TestNotNull(TEXT("clock"), Clock) || !TestNotNull(TEXT("weather effects"), FX))
	{
		return false;
	}
	UDreamWeatherSelector_Fixed* Fixed = NewObject<UDreamWeatherSelector_Fixed>();
	Fixed->Preset = MakePreset(TEXT("Rain"), { { Channel_CloudCoverage, 1.0f }, { Channel_Precipitation, 1.0f } });
	FActorSpawnParameters Spawn;
	Spawn.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	ADreamWeather* Weather = World->SpawnActor<ADreamWeather>(Spawn);
	Fixed->Rename(nullptr, Weather);
	Weather->Selector = Fixed;
	FDreamWorldClockConfig Config;
	Config.StartTime = FDateTime(2026, 6, 21, 12, 0, 0);
	Clock->ApplyClockConfig(Config, true);
	Clock->ReportView(FVector(0, 0, 170), FRotator(0, 90, 0), true);

	Clock->Tick(0.016f);
	const UDreamWeatherFXSettings* S = UDreamWeatherFXSettings::Get();
	TestTrue(FString::Printf(TEXT("it rains: drops spawn (%.0f/s)"), FX->GetDropRate(UDreamWeatherFXSubsystem::RainLayer)),
		FX->GetDropRate(UDreamWeatherFXSubsystem::RainLayer) > 0.5f * S->Rain.DropsPerSecond);
	TestEqual(TEXT("a full buffer of rain paths at once"), FX->GetPaths(UDreamWeatherFXSubsystem::RainLayer).GetEnds().Num(), S->PathCount);
	TestEqual(TEXT("no snow on a warm noon"), FX->GetDropRate(UDreamWeatherFXSubsystem::SnowLayer), 0.0f);
	TestEqual(TEXT("an empty world: nothing to splash on"), FX->GetSplashRate(UDreamWeatherFXSubsystem::RainLayer), 0.0f);
	TestTrue(TEXT("rain falls down"), FX->GetFallVelocity(UDreamWeatherFXSubsystem::RainLayer).Z < -100.0);

	// The paths turn over while it rains: about one buffer per refresh.
	const int64 Before = FX->GetPaths(UDreamWeatherFXSubsystem::RainLayer).GetTracesCast();
	for (int32 i = 0; i < 10; ++i)
	{
		Clock->Tick(S->PathRefreshSeconds / 10.0f);
	}
	const int64 Traced = FX->GetPaths(UDreamWeatherFXSubsystem::RainLayer).GetTracesCast() - Before;
	TestTrue(FString::Printf(TEXT("a buffer per refresh (%lld of %d)"), Traced, S->PathCount), FMath::Abs(Traced - S->PathCount) <= 2);

	if (UNiagaraComponent* Effect = FX->GetEffect(UDreamWeatherFXSubsystem::RainLayer))
	{
		// (Whether it is active says nothing here: a GPU system cannot run without a renderer, as in -nullrhi.)
		TestTrue(TEXT("the rain effect is the configured one"), Effect->GetAsset() == Cast<UNiagaraSystem>(S->Rain.System.TryLoad()));
		TestTrue(TEXT("and follows the view"), Effect->GetComponentLocation().Equals(FVector(0, 0, 170), 1.0));
		const TArray<FVector4> Uploaded = UNiagaraDataInterfaceArrayFunctionLibrary::GetNiagaraArrayVector4(Effect, TEXT("User.Paths"));
		TestEqual(TEXT("the effect was handed every path"), Uploaded.Num(), S->PathCount);
		// (The effect stores them in single precision.)
		TestTrue(TEXT("the latest ones"), Uploaded.Num() > 0 && Uploaded.Last().Equals(FX->GetPaths(UDreamWeatherFXSubsystem::RainLayer).GetEnds().Last(), 0.05));
	}
	else
	{
		AddWarning(TEXT("No rain effect: NS_DreamRain is not built in this project (see DreamWeather.FX.Assets)."));
	}

	// The weather clears: nothing more spawns, and the traces stop.
	Fixed->Preset = MakePreset(TEXT("Clear"), { { Channel_CloudCoverage, 0.1f } });
	Clock->SetTimeOfDay(12.0f);   // a jump: the weather snaps to the new preset
	Clock->Tick(0.016f);
	Clock->Tick(0.016f);
	TestEqual(TEXT("no drops when it is dry"), FX->GetDropRate(UDreamWeatherFXSubsystem::RainLayer), 0.0f);
	const int64 Dry = FX->GetPaths(UDreamWeatherFXSubsystem::RainLayer).GetTracesCast();
	Clock->Tick(0.5f);
	TestEqual(TEXT("and no traces"), FX->GetPaths(UDreamWeatherFXSubsystem::RainLayer).GetTracesCast(), Dry);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDreamWeatherFXAssetsTest, "DreamWeather.FX.Assets", DreamWeatherFX::Tests::Flags)
bool FDreamWeatherFXAssetsTest::RunTest(const FString& Parameters)
{
	const UDreamWeatherFXSettings* S = UDreamWeatherFXSettings::Get();
	UNiagaraSystem* Rain = Cast<UNiagaraSystem>(S->Rain.System.TryLoad());
	TestNotNull(TEXT("the rain effect is built (DFX/Effects/NS_DreamRain.dfs)"), Rain);
	TestNotNull(TEXT("the drop material is built (DShader/Weather/M_DreamRainDrop.dss)"),
		LoadObject<UMaterialInterface>(nullptr, TEXT("/DreamDynamicWorld/Weather/M_DreamRainDrop.M_DreamRainDrop")));
	TestNotNull(TEXT("the splash material is built (DShader/Weather/M_DreamRainSplash.dss)"),
		LoadObject<UMaterialInterface>(nullptr, TEXT("/DreamDynamicWorld/Weather/M_DreamRainSplash.M_DreamRainSplash")));
	TestNotNull(TEXT("the snow effect is built (DFX/Effects/NS_DreamSnow.dfs)"), Cast<UNiagaraSystem>(S->Snow.System.TryLoad()));
	TestNotNull(TEXT("the flake material is built (DShader/Weather/M_DreamSnowFlake.dss)"),
		LoadObject<UMaterialInterface>(nullptr, TEXT("/DreamDynamicWorld/Weather/M_DreamSnowFlake.M_DreamSnowFlake")));
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
