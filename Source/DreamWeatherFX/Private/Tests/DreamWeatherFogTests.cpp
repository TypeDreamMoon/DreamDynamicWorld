// Copyright DreamMoon. All Rights Reserved.

#include "DreamWeather.h"
#include "DreamWeatherFog.h"
#include "DreamWeatherFXSubsystem.h"
#include "DreamWeatherModule.h"
#include "DreamWeatherPreset.h"
#include "DreamWeatherSelector.h"
#include "DreamWeatherSubsystem.h"
#include "DreamWindSubsystem.h"
#include "DreamWorldSubsystem.h"

#include "Components/PostProcessComponent.h"
#include "Engine/World.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "UObject/Package.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace DreamWeatherFX::FogTests
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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDreamWeatherFogTest, "DreamWeather.FX.Fog", DreamWeatherFX::FogTests::Flags)
bool FDreamWeatherFogTest::RunTest(const FString& Parameters)
{
	using namespace DreamWeatherFX::FogTests;
	using namespace DreamWeather::Tags;
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
	if (!TestNotNull(TEXT("world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(false); };
	UDreamWorldSubsystem* Clock = World->GetSubsystem<UDreamWorldSubsystem>();
	UDreamWindSubsystem* Wind = UDreamWindSubsystem::Get(World);
	UDreamWeatherFXSubsystem* Effects = UDreamWeatherFXSubsystem::Get(World);
	auto Frame = [Clock, Wind](float Seconds)
	{
		Clock->Tick(Seconds);
		if (Wind)
		{
			Wind->Tick(Seconds);
		}
		Clock->Tick(0.0f);
	};
	UDreamWeatherSelector_Fixed* Fixed = NewObject<UDreamWeatherSelector_Fixed>();
	Fixed->Preset = MakePreset(TEXT("WindyFog"), { { Channel_CloudCoverage, 0.6f }, { Channel_Fog, 1.0f }, { Channel_Wind, 0.6f } });
	FActorSpawnParameters Spawn;
	Spawn.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	ADreamWeather* Weather = World->SpawnActor<ADreamWeather>(Spawn);
	Fixed->Rename(nullptr, Weather);
	Weather->Selector = Fixed;
	FDreamWorldClockConfig Config;
	Config.StartTime = FDateTime(2026, 10, 15, 8, 0, 0);
	Config.bAnimate = false;
	Clock->ApplyClockConfig(Config, true);
	Clock->ReportView(FVector(0, 0, 200), FRotator::ZeroRotator, true);
	Frame(0.016f);

	// The drift: the wind at the view summed, frame by frame.
	if (TestNotNull(TEXT("effects"), Effects) && TestNotNull(TEXT("wind"), Wind))
	{
		const FVector2D Before = Effects->GetFogDrift();
		const FVector Air = Wind->SampleWindVelocity(FVector(0, 0, 200));
		for (int32 i = 0; i < 10; ++i)
		{
			Frame(0.1f);
		}
		const FVector2D Moved = Effects->GetFogDrift() - Before;
		AddInfo(FString::Printf(TEXT("wind (%.0f, %.0f) cm/s; the drift moved (%.0f, %.0f) cm in a second"), Air.X, Air.Y, Moved.X, Moved.Y));
		TestTrue(TEXT("it blows"), Air.Size2D() > 50.0);
		TestTrue(TEXT("the drift follows the wind over a second"), (Moved - FVector2D(Air)).Size() < FVector2D(Air).Size() * 0.25 + 5.0);
	}

	// A fog bank as thick as the weather's fog makes it.
	ADreamFogBank* Bank = World->SpawnActor<ADreamFogBank>(FVector(0, 0, 0), FRotator::ZeroRotator, Spawn);
	Bank->Density = 0.4f;
	Bank->ClearWeatherShare = 0.25f;
	Bank->Refresh();
	TestNearlyEqual(TEXT("in a full fog, its density"), Bank->GetDensityNow(), 0.4f, 1.0e-3f);
	Fixed->Preset = MakePreset(TEXT("Clear"), { { Channel_CloudCoverage, 0.1f }, { Channel_Fog, 0.0f } });
	World->GetSubsystem<UDreamWeatherSubsystem>()->ForceWeather(Fixed->Preset, 0.0f);
	Frame(0.016f);
	Bank->Refresh();
	TestNearlyEqual(TEXT("no fog: the clear share of it"), Bank->GetDensityNow(), 0.1f, 1.0e-3f);
	// Its patches drift with the wind and stay in the box.
	if (TestTrue(TEXT("it has its patches"), Bank->GetPatchCount() == Bank->Patches))
	{
		// Steps in which the first patch moved (it may wrap round the box or start afresh: count steps, not distance).
		int32 Moving = 0;
		for (int32 i = 0; i < 20; ++i)
		{
			const FVector Before = Bank->GetPatchLocation(0);
			Bank->Drift(0.5f);
			Moving += FVector::Dist2D(Before, Bank->GetPatchLocation(0)) > 1.0 ? 1 : 0;
		}
		bool bInside = true;
		for (int32 i = 0; i < Bank->GetPatchCount(); ++i)
		{
			const FVector P = Bank->GetPatchLocation(i);
			bInside &= FMath::Abs(P.X) <= Bank->Extent.X + 1.0 && FMath::Abs(P.Y) <= Bank->Extent.Y + 1.0;
		}
		AddInfo(FString::Printf(TEXT("a patch moved in %d of 20 half-second steps"), Moving));
		TestTrue(TEXT("the patches drift"), Moving >= 15);
		TestTrue(TEXT("and stay in the box"), bInside);
	}

	// The wind fog's pass knows its slab.
	ADreamWindFog* Wisps = World->SpawnActor<ADreamWindFog>(FVector(0, 0, 50), FRotator::ZeroRotator, Spawn);
	if (TestNotNull(TEXT("the wind fog's pass (compile M_DreamWindFog)"), Wisps->GetPass()))
	{
		float Base = 0.0f, Thickness = 0.0f;
		Wisps->GetPass()->GetScalarParameterValue(FHashedMaterialParameterInfo(TEXT("FogBase")), Base);
		Wisps->GetPass()->GetScalarParameterValue(FHashedMaterialParameterInfo(TEXT("FogThickness")), Thickness);
		TestNearlyEqual(TEXT("the slab's floor at the actor"), Base, 50.0f, 1.0e-3f);
		TestNearlyEqual(TEXT("its thickness"), Thickness, Wisps->Thickness, 1.0e-3f);
		TestTrue(TEXT("an unbound post process"), Wisps->PostProcess->bUnbound && Wisps->PostProcess->Settings.WeightedBlendables.Array.Num() == 1);
	}
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
