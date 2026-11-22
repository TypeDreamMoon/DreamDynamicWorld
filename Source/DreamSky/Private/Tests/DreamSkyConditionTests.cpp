// Copyright DreamMoon. All Rights Reserved.

#include "DreamSky.h"
#include "DreamSkyConditions.h"
#include "DreamSkyModifier.h"
#include "DreamSkySubsystem.h"
#include "DreamWorldSubsystem.h"

#include "Components/PostProcessComponent.h"
#include "Engine/World.h"
#include "Misc/AutomationTest.h"
#include "UObject/Package.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace DreamSky::ConditionTests
{
	constexpr EAutomationTestFlags Flags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDreamSkyConditionTest, "DreamSky.Conditions.Evaluate", DreamSky::ConditionTests::Flags)
bool FDreamSkyConditionTest::RunTest(const FString& Parameters)
{
	FDreamWorldEnvironment Env;
	FDreamViewOcclusion View;

	// An hour window over midnight, faded at each end inside it.
	FDreamSkyCondition Night;
	Night.bHours = true;
	Night.StartHour = 20.0f;
	Night.EndHour = 4.0f;
	Night.FadeHours = 1.0f;
	TestEqual(TEXT("noon: outside"), Night.Evaluate(12.0f, Env, View), 0.0f);
	TestEqual(TEXT("19:59: not yet"), Night.Evaluate(19.99f, Env, View), 0.0f);
	TestNearlyEqual(TEXT("20:30: half faded in"), Night.Evaluate(20.5f, Env, View), 0.5f, 1e-4f);
	TestEqual(TEXT("midnight: in full"), Night.Evaluate(0.0f, Env, View), 1.0f);
	TestEqual(TEXT("23:00: in full"), Night.Evaluate(23.0f, Env, View), 1.0f);
	TestNearlyEqual(TEXT("03:30: half faded out"), Night.Evaluate(3.5f, Env, View), 0.5f, 1e-4f);
	TestEqual(TEXT("04:00: gone"), Night.Evaluate(4.0f, Env, View), 0.0f);

	// Weather ranges, all of them, softly.
	FDreamSkyCondition SnowyCold;
	FDreamSkyWeatherRange& Snow = SnowyCold.Weather.AddDefaulted_GetRef();
	Snow.Value = EDreamSkyWeatherValue::Snow;
	Snow.Min = 0.2f;
	Snow.Soft = 0.2f;
	FDreamSkyWeatherRange& Cold = SnowyCold.Weather.AddDefaulted_GetRef();
	Cold.Value = EDreamSkyWeatherValue::Temperature;
	Cold.Min = -100.0f;
	Cold.Max = 0.0f;
	Cold.Soft = 2.0f;
	Env.Precipitation = 1.0f;
	Env.SnowRatio = 1.0f;
	Env.Temperature = -5.0f;
	TestEqual(TEXT("snowing and cold: holds"), SnowyCold.Evaluate(12.0f, Env, View), 1.0f);
	Env.Temperature = 1.0f;
	TestNearlyEqual(TEXT("1 C: half out over the 2 C soft edge"), SnowyCold.Evaluate(12.0f, Env, View), 0.5f, 1e-4f);
	Env.Temperature = -5.0f;
	Env.SnowRatio = 0.0f;
	TestEqual(TEXT("raining instead: the snow part is 0"), SnowyCold.Evaluate(12.0f, Env, View), 0.0f);
	Env.SnowRatio = 0.3f;
	TestNearlyEqual(TEXT("snow 0.3: half in over its soft edge"), SnowyCold.Evaluate(12.0f, Env, View), 0.5f, 1e-4f);

	// Indoors and outdoors follow the probe; with no probe, outdoors.
	FDreamSkyCondition Inside;
	Inside.Place = EDreamSkyPlace::Indoors;
	FDreamSkyCondition Outside;
	Outside.Place = EDreamSkyPlace::Outdoors;
	TestEqual(TEXT("no probe: not indoors"), Inside.Evaluate(12.0f, Env, View), 0.0f);
	TestEqual(TEXT("no probe: outdoors"), Outside.Evaluate(12.0f, Env, View), 1.0f);
	View.bValid = true;
	View.Interior = 0.75f;
	TestNearlyEqual(TEXT("mostly indoors"), Inside.Evaluate(12.0f, Env, View), 0.75f, 1e-5f);
	TestNearlyEqual(TEXT("a little outdoors"), Outside.Evaluate(12.0f, Env, View), 0.25f, 1e-5f);

	// Following a condition: at once on a snap, exponentially otherwise.
	TestEqual(TEXT("snap"), DreamSky::Conditions::Follow(0.0f, 1.0f, 2.0f, 0.1f, true), 1.0f);
	TestNearlyEqual(TEXT("one time constant"), DreamSky::Conditions::Follow(0.0f, 1.0f, 2.0f, 2.0f, false), 1.0f - FMath::Exp(-1.0f), 1e-5f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDreamSkyConditionalPostProcessTest, "DreamSky.Conditions.PostProcessAndModifiers", DreamSky::ConditionTests::Flags)
bool FDreamSkyConditionalPostProcessTest::RunTest(const FString& Parameters)
{
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
	if (!TestNotNull(TEXT("world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(false); };
	UDreamSkySubsystem* Subsystem = World->GetSubsystem<UDreamSkySubsystem>();
	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	ADreamSky* Sky = World->SpawnActor<ADreamSky>(Params);
	if (!TestNotNull(TEXT("sky subsystem"), Subsystem) || !TestNotNull(TEXT("sky"), Sky))
	{
		return false;
	}

	// A grade for rain outdoors, at most 0.8, following over 1 s.
	FDreamSkyConditionalPostProcess& Rain = Sky->PostProcesses.AddDefaulted_GetRef();
	Rain.Name = TEXT("Rain");
	FDreamSkyWeatherRange& Wet = Rain.Condition.Weather.AddDefaulted_GetRef();
	Wet.Value = EDreamSkyWeatherValue::Rain;
	Wet.Min = 0.2f;
	Wet.Soft = 0.0f;
	Rain.Condition.Place = EDreamSkyPlace::Outdoors;
	Rain.MaxWeight = 0.8f;
	Rain.BlendSeconds = 1.0f;
	Rain.Priority = 3.0f;
	Rain.Settings.bOverride_ColorSaturation = true;
	Rain.Settings.ColorSaturation = FVector4(0.6f, 0.6f, 0.6f, 1.0f);
	// A night modifier.
	UDreamSkyModifier* NightBlue = NewObject<UDreamSkyModifier>(GetTransientPackage());
	NightBlue->Settings.bOverride_SkyTint = true;
	NightBlue->Settings.SkyTint = FLinearColor(0.6f, 0.7f, 1.0f);
	FDreamSkyConditionalModifier& Night = Sky->ConditionalModifiers.AddDefaulted_GetRef();
	Night.Modifier = NightBlue;
	Night.Condition.bHours = true;
	Night.Condition.StartHour = 20.0f;
	Night.Condition.EndHour = 4.0f;
	Night.Condition.FadeHours = 0.0f;
	Night.BlendSeconds = 0.0f;

	FDreamWorldEnvironment Env;
	Env.bActive = true;
	FDreamViewOcclusion View;
	Sky->UpdateConditions(*Subsystem, 12.0f, Env, View, 0.016f, true);
	TestEqual(TEXT("dry: no grade"), Sky->GetPostProcessWeight(0), 0.0f);
	TestEqual(TEXT("noon: no night modifier"), Subsystem->GetModifierAlpha(NightBlue), 0.0f);

	Env.Precipitation = 1.0f;
	Sky->UpdateConditions(*Subsystem, 12.0f, Env, View, 1.0f, false);
	TestNearlyEqual(TEXT("raining: one time constant toward 0.8"), Sky->GetPostProcessWeight(0), 0.8f * (1.0f - FMath::Exp(-1.0f)), 1e-4f);
	TArray<UPostProcessComponent*> PostProcesses;
	Sky->GetComponents<UPostProcessComponent>(PostProcesses);
	const UPostProcessComponent* Grade = nullptr;
	for (const UPostProcessComponent* P : PostProcesses)
	{
		Grade = P != Sky->PostProcess ? P : Grade;
	}
	if (TestNotNull(TEXT("the grade's post process on the sky"), Grade))
	{
		TestNearlyEqual(TEXT("weighted as the condition holds"), Grade->BlendWeight, Sky->GetPostProcessWeight(0), 1e-5f);
		TestEqual(TEXT("its priority"), Grade->Priority, 3.0f);
		TestTrue(TEXT("its settings"), Grade->Settings.bOverride_ColorSaturation);
	}
	View.bValid = true;
	View.Interior = 1.0f;
	Sky->UpdateConditions(*Subsystem, 12.0f, Env, View, 0.016f, true);
	TestEqual(TEXT("indoors: no rain grade"), Sky->GetPostProcessWeight(0), 0.0f);

	Sky->UpdateConditions(*Subsystem, 22.0f, Env, View, 0.016f, true);
	TestEqual(TEXT("at night the modifier holds in full"), Subsystem->GetModifierAlpha(NightBlue), 1.0f);
	const FDreamWorldSnapshot Snapshot = World->GetSubsystem<UDreamWorldSubsystem>()->CaptureSnapshot();
	const FString* Kept = Snapshot.Extensions.Find(TEXT("DreamSky"));
	TestTrue(TEXT("a conditional modifier is not kept in snapshots"), !Kept || !Kept->Contains(NightBlue->GetName()));

	// Taken off the list, it leaves the stack on the sky's next update.
	Sky->ConditionalModifiers.Reset();
	World->GetSubsystem<UDreamWorldSubsystem>()->Tick(0.016f);
	TestEqual(TEXT("off the list, out of the stack"), Subsystem->GetModifiers().Num(), 0);
	return true;
}

#endif
