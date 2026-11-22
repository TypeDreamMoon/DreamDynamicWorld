// Copyright DreamMoon. All Rights Reserved.

#include "DreamWeather.h"
#include "DreamWeatherModule.h"
#include "DreamWeatherPreset.h"
#include "DreamWeatherSelector.h"
#include "DreamWeatherStatusComponent.h"
#include "DreamWeatherSubsystem.h"
#include "DreamWorldSubsystem.h"

#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/World.h"
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "UObject/Package.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace DreamWeatherFX::StatusTests
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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDreamWeatherStatusTest, "DreamWeather.FX.Status", DreamWeatherFX::StatusTests::Flags)
bool FDreamWeatherStatusTest::RunTest(const FString& Parameters)
{
	using namespace DreamWeatherFX::StatusTests;
	using namespace DreamWeather::Tags;
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
	if (!TestNotNull(TEXT("world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(false); };
	UDreamWorldSubsystem* Clock = World->GetSubsystem<UDreamWorldSubsystem>();
	UDreamWeatherSelector_Fixed* Fixed = NewObject<UDreamWeatherSelector_Fixed>();
	Fixed->Preset = MakePreset(TEXT("Rain"), { { Channel_CloudCoverage, 1.0f }, { Channel_Precipitation, 1.0f } });
	FActorSpawnParameters Spawn;
	Spawn.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	ADreamWeather* WeatherActor = World->SpawnActor<ADreamWeather>(Spawn);
	Fixed->Rename(nullptr, WeatherActor);
	WeatherActor->Selector = Fixed;
	FDreamWorldClockConfig Config;
	Config.StartTime = FDateTime(2026, 6, 21, 12, 0, 0);
	Clock->ApplyClockConfig(Config, true);
	Clock->ReportView(FVector(0, 0, 170), FRotator::ZeroRotator, true);
	Clock->Tick(0.016f);

	// One out in the rain, one under a roof.
	AActor* Walker = World->SpawnActor<AActor>(AActor::StaticClass(), FTransform(FVector(0, 0, 0)), Spawn);
	USceneComponent* Root = NewObject<USceneComponent>(Walker);
	Walker->SetRootComponent(Root);
	Root->RegisterComponent();
	UDreamWeatherStatusComponent* Status = NewObject<UDreamWeatherStatusComponent>(Walker);
	Status->RegisterComponent();

	AActor* Dry = World->SpawnActor<AActor>(AActor::StaticClass(), FTransform(FVector(5000, 0, 0)), Spawn);
	USceneComponent* DryRoot = NewObject<USceneComponent>(Dry);
	Dry->SetRootComponent(DryRoot);
	DryRoot->RegisterComponent();
	Dry->SetActorLocation(FVector(5000, 0, 0));
	UDreamWeatherStatusComponent* DryStatus = NewObject<UDreamWeatherStatusComponent>(Dry);
	DryStatus->RegisterComponent();
	if (UStaticMesh* Cube = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube")))
	{
		AStaticMeshActor* Roof = World->SpawnActor<AStaticMeshActor>(FVector(5000, 0, 400), FRotator::ZeroRotator, Spawn);
		Roof->GetStaticMeshComponent()->SetMobility(EComponentMobility::Movable);
		Roof->GetStaticMeshComponent()->SetStaticMesh(Cube);
		Roof->SetActorScale3D(FVector(10.0, 10.0, 0.4));
	}

	int32 RainStarts = 0, SoakedStarts = 0;
	// (Polling the state rather than binding the dynamic event, which needs a UFUNCTION.)
	for (int32 i = 0; i < 30; ++i)
	{
		const bool bWasRain = Status->IsExposed(EDreamWeatherExposure::Rain);
		const bool bWasSoaked = Status->IsExposed(EDreamWeatherExposure::Soaked);
		Status->UpdateStatus(1.0f);
		DryStatus->UpdateStatus(1.0f);
		RainStarts += (!bWasRain && Status->IsExposed(EDreamWeatherExposure::Rain)) ? 1 : 0;
		SoakedStarts += (!bWasSoaked && Status->IsExposed(EDreamWeatherExposure::Soaked)) ? 1 : 0;
	}
	TestTrue(TEXT("out in the rain: exposed"), Status->GetStatus().RainExposure > 0.9f);
	TestEqual(TEXT("the rain started once"), RainStarts, 1);
	TestNearlyEqual(TEXT("soaked after half a minute"), Status->GetStatus().Wetness, 1.0f, 1.0e-3f);
	TestEqual(TEXT("and told so once"), SoakedStarts, 1);
	TestTrue(TEXT("under the roof: sheltered"), DryStatus->GetStatus().bSheltered);
	TestEqual(TEXT("and dry"), DryStatus->GetStatus().Wetness, 0.0f);

	// The rain stops: it dries, slowly.
	Fixed->Preset = MakePreset(TEXT("Clear"), { { Channel_CloudCoverage, 0.1f } });
	Clock->SetTimeOfDay(12.0f);
	Clock->Tick(0.016f);
	Status->UpdateStatus(1.0f);
	TestFalse(TEXT("the rain stopped"), Status->IsExposed(EDreamWeatherExposure::Rain));
	for (int32 i = 0; i < 60; ++i)
	{
		Status->UpdateStatus(1.0f);
	}
	const float AfterAMinute = Status->GetStatus().Wetness;
	TestTrue(FString::Printf(TEXT("a minute later: drying, not dry (%.2f)"), AfterAMinute), AfterAMinute < 0.95f && AfterAMinute > 0.1f);
	Status->ResetStatus();
	TestEqual(TEXT("a change of clothes: dry"), Status->GetStatus().Wetness, 0.0f);
	return true;
}

#endif
