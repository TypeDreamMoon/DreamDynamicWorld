// Copyright DreamMoon. All Rights Reserved.

#include "DreamSky.h"
#include "DreamSkyModifier.h"
#include "DreamSkyStyle.h"
#include "DreamSkySubsystem.h"
#include "DreamWorldSubsystem.h"

#include "Components/DirectionalLightComponent.h"
#include "Components/PostProcessComponent.h"
#include "Engine/World.h"
#include "JsonObjectConverter.h"
#include "Misc/AutomationTest.h"
#include "UObject/Package.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace DreamSky::ModifierTests
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
			// Noon, the clock standing still: the lighting snaps to the style's every tick, so only the modifiers change it.
			FDreamWorldClockConfig Config;
			Config.StartTime = FDateTime(2026, 6, 21, 12, 0, 0);
			Config.bAnimate = false;
			Clock->ApplyClockConfig(Config, true);
			Clock->Tick(0.016f);
		}
		~FSkyWorld()
		{
			if (World)
			{
				World->DestroyWorld(false);
			}
		}
		bool IsValid() const { return World && Clock && SkySubsystem && Sky; }
	};

	UDreamSkyModifier* MakeModifier(float FadeSeconds = 0.0f)
	{
		UDreamSkyModifier* M = NewObject<UDreamSkyModifier>(GetTransientPackage());
		M->FadeInSeconds = FadeSeconds;
		M->FadeOutSeconds = FadeSeconds;
		M->Easing = EEasingFunc::Linear;
		return M;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDreamSkyModifierApplyTest, "DreamSky.Modifiers.Apply", DreamSky::ModifierTests::Flags)
bool FDreamSkyModifierApplyTest::RunTest(const FString& Parameters)
{
	FDreamSkyLighting Base;
	Base.SunIlluminance = 100000.0f;
	Base.MoonIlluminance = 0.2f;
	Base.SunColor = FLinearColor(1.0f, 0.9f, 0.8f);
	Base.MainLightColor = Base.SunColor;
	Base.MainLightIlluminance = Base.SunIlluminance;
	Base.ExposureEV100 = 14.0f;
	Base.FogDensity = 0.02f;
	Base.FogColor = FLinearColor(0.2f, 0.4f, 0.8f);
	FDreamSkyLook Look;
	Look.CloudCoverage = 0.3f;
	Look.Zenith = FLinearColor(0.1f, 0.3f, 0.9f);

	FDreamSkyModifierSettings S;
	S.bOverride_SunIntensity = true;
	S.SunIntensity = 0.25f;
	S.bOverride_ExposureCompensation = true;
	S.ExposureCompensation = 2.0f;
	S.bOverride_CloudCoverage = true;
	S.CloudCoverage = 0.9f;
	S.bOverride_FogColor = true;
	S.FogColor = FLinearColor(1.0f, 0.0f, 0.0f);
	S.bOverride_SkySaturation = true;
	S.SkySaturation = 0.0f;

	{
		FDreamSkyLighting L = Base;
		FDreamSkyLook K = Look;
		S.Apply(L, K, 1.0f);
		TestNearlyEqual(TEXT("full: the sun scaled"), L.SunIlluminance, 25000.0f, 1.0f);
		TestNearlyEqual(TEXT("full: the main light follows the sun"), L.MainLightIlluminance, 25000.0f, 1.0f);
		TestNearlyEqual(TEXT("full: +2 stops is 2 EV lower"), L.ExposureEV100, 12.0f, 1e-4f);
		TestNearlyEqual(TEXT("full: the cover reached"), K.CloudCoverage, 0.9f, 1e-5f);
		TestNearlyEqual(TEXT("full: the map's cover replaced"), K.CloudCoverageKept, 0.0f, 1e-5f);
		TestNearlyEqual(TEXT("full: by the target"), K.CloudCoverageAdded, 0.9f, 1e-5f);
		TestTrue(TEXT("full: the fog turned red, its brightness kept"), L.FogColor.R > 0.9f * (L.FogColor.R + L.FogColor.G + L.FogColor.B)
			&& FMath::IsNearlyEqual(0.2126f * L.FogColor.R + 0.7152f * L.FogColor.G + 0.0722f * L.FogColor.B,
				0.2126f * 0.2f + 0.7152f * 0.4f + 0.0722f * 0.8f, 1e-3f));
		TestTrue(TEXT("full: the dome grey"), FMath::IsNearlyEqual(K.Zenith.R, K.Zenith.B, 1e-4f));
	}
	{
		FDreamSkyLighting L = Base;
		FDreamSkyLook K = Look;
		S.Apply(L, K, 0.5f);
		TestNearlyEqual(TEXT("half: a quarter goes to a half (along the logarithm)"), L.SunIlluminance, 50000.0f, 1.0f);
		TestNearlyEqual(TEXT("half: one stop"), L.ExposureEV100, 13.0f, 1e-4f);
		TestNearlyEqual(TEXT("half: the cover half way"), K.CloudCoverage, 0.6f, 1e-5f);
	}
	{
		FDreamSkyLighting L = Base;
		FDreamSkyLook K = Look;
		S.Apply(L, K, 0.0f);
		TestNearlyEqual(TEXT("none: the sun as it was"), L.SunIlluminance, Base.SunIlluminance, 1e-3f);
		TestNearlyEqual(TEXT("none: the cover as it was"), K.CloudCoverage, Look.CloudCoverage, 1e-6f);
	}

	// Stacked, the change to the map's cover is the same as the change to the view's: whatever the map holds there, the
	// two modifiers in turn give what kept and added give.
	{
		FDreamSkyModifierSettings A;
		A.bOverride_CloudCoverage = true;
		A.CloudCoverage = 1.0f;
		FDreamSkyModifierSettings B;
		B.bOverride_CloudCoverage = true;
		B.CloudCoverage = 0.0f;
		FDreamSkyLighting L = Base;
		for (const float Map : { 0.0f, 0.37f, 1.0f })
		{
			FDreamSkyLook K;
			K.CloudCoverage = Map;
			A.Apply(L, K, 0.6f);
			B.Apply(L, K, 0.3f);
			TestNearlyEqual(*FString::Printf(TEXT("stacked: the map's cover %.2f changes as the view's"), Map), Map * K.CloudCoverageKept + K.CloudCoverageAdded, K.CloudCoverage, 1e-5f);
		}
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDreamSkyModifierStackTest, "DreamSky.Modifiers.Stack", DreamSky::ModifierTests::Flags)
bool FDreamSkyModifierStackTest::RunTest(const FString& Parameters)
{
	using namespace DreamSky::ModifierTests;
	FSkyWorld W;
	if (!TestTrue(TEXT("test world"), W.IsValid()))
	{
		return false;
	}
	const float Sun = W.Sky->SunLight->Intensity;
	const float Noon = W.Sky->GetEffectiveStyle()->EvaluateLighting(W.Clock->GetCelestial()).SunIlluminance;
	TestTrue(FString::Printf(TEXT("the sun is up, at the style's noon (%.3f lux)"), Sun), Sun > 0.0f && FMath::IsNearlyEqual(Sun, Noon, Noon * 1e-3f));

	// Fades in over its time, linearly here.
	UDreamSkyModifier* Dim = MakeModifier(1.0f);
	Dim->Settings.bOverride_SunIntensity = true;
	Dim->Settings.SunIntensity = 0.5f;
	W.SkySubsystem->AddModifier(Dim);
	W.Clock->Tick(0.5f);
	TestNearlyEqual(TEXT("half way into its fade"), W.SkySubsystem->GetModifierAlpha(Dim), 0.5f, 1e-3f);
	TestNearlyEqual(TEXT("the sun half way down (along the logarithm)"), W.Sky->SunLight->Intensity, Sun * FMath::Sqrt(0.5f), Sun * 1e-3f);
	W.Clock->Tick(0.6f);
	TestNearlyEqual(TEXT("in full"), W.SkySubsystem->GetModifierAlpha(Dim), 1.0f, 1e-4f);
	TestNearlyEqual(TEXT("the sun halved"), W.Sky->SunLight->Intensity, Sun * 0.5f, Sun * 1e-3f);

	// The higher priority goes on later: two modifiers turning the cover toward different values, the later wins.
	UDreamSkyModifier* Cloudy = MakeModifier();
	Cloudy->Settings.bOverride_CloudCoverage = true;
	Cloudy->Settings.CloudCoverage = 1.0f;
	Cloudy->Priority = 5;
	UDreamSkyModifier* Clear = MakeModifier();
	Clear->Settings.bOverride_CloudCoverage = true;
	Clear->Settings.CloudCoverage = 0.0f;
	Clear->Priority = 1;
	W.SkySubsystem->AddModifier(Cloudy);
	W.SkySubsystem->AddModifier(Clear);
	W.Clock->Tick(0.016f);
	TestNearlyEqual(TEXT("the higher priority goes on last"), W.SkySubsystem->GetLook().CloudCoverage, 1.0f, 1e-4f);
	const TArray<UDreamSkyModifier*> Order = W.SkySubsystem->GetModifiers();
	TestTrue(TEXT("listed in the order they go on"), Order.Num() == 3 && Order.Last() == Cloudy);

	// Removed: faded out, then dropped.
	W.SkySubsystem->RemoveModifier(Dim, 1.0f);
	W.Clock->Tick(0.5f);
	TestNearlyEqual(TEXT("half way out"), W.SkySubsystem->GetModifierAlpha(Dim), 0.5f, 1e-3f);
	W.Clock->Tick(0.6f);
	TestEqual(TEXT("dropped once faded out"), W.SkySubsystem->GetModifiers().Num(), 2);
	TestNearlyEqual(TEXT("the sun back"), W.Sky->SunLight->Intensity, Sun, Sun * 1e-3f);

	// Driven by hand: held where it is set, without the fade's shape.
	UDreamSkyModifier* Held = MakeModifier(5.0f);
	Held->MaxAlpha = 0.8f;
	Held->Easing = EEasingFunc::EaseInOut;
	W.SkySubsystem->SetModifierAlpha(Held, 0.5f);
	W.Clock->Tick(0.016f);
	TestNearlyEqual(TEXT("held at its alpha times MaxAlpha"), W.SkySubsystem->GetModifierAlpha(Held), 0.4f, 1e-4f);
	W.SkySubsystem->SetModifierAlpha(Held, 0.0f, 1.0f);
	W.Clock->Tick(0.25f);
	TestNearlyEqual(TEXT("moves there over the time given (0.5 to 0 in 1 s: 0.375 after a quarter of it)"), W.SkySubsystem->GetModifierAlpha(Held), 0.375f * 0.8f, 1e-3f);
	W.Clock->Tick(1.0f);
	TestEqual(TEXT("at 0 it stays in the stack"), W.SkySubsystem->GetModifiers().Num(), 3);

	W.SkySubsystem->ClearModifiers(0.0f);
	W.Clock->Tick(0.016f);
	TestEqual(TEXT("cleared"), W.SkySubsystem->GetModifiers().Num(), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDreamSkyModifierWeatherTest, "DreamSky.Modifiers.FollowTheWeather", DreamSky::ModifierTests::Flags)
bool FDreamSkyModifierWeatherTest::RunTest(const FString& Parameters)
{
	using namespace DreamSky::ModifierTests;
	FSkyWorld W;
	if (!TestTrue(TEXT("test world"), W.IsValid()))
	{
		return false;
	}
	const float Sun = W.Sky->SunLight->Intensity;
	UDreamSkyModifier* Gloom = MakeModifier(5.0f);
	Gloom->Settings.bOverride_SunIntensity = true;
	Gloom->Settings.SunIntensity = 0.25f;

	// The weather says its storm is in effect at 0.5 where the view is: the sky takes its modifier at that, at once.
	FDreamWorldEnvironment Env;
	Env.bActive = true;
	FDreamWorldWeightedObject& Entry = Env.SkyModifiers.AddDefaulted_GetRef();
	Entry.Object = Gloom;
	Entry.Weight = 0.5f;
	W.Clock->SetEnvironment(Env);
	W.Clock->Tick(0.016f);
	TestNearlyEqual(TEXT("at the weather's weight"), W.SkySubsystem->GetModifierAlpha(Gloom), 0.5f, 1e-4f);
	TestNearlyEqual(TEXT("the sun dimmed half way (a quarter to a half)"), W.Sky->SunLight->Intensity, Sun * 0.5f, Sun * 1e-3f);

	// A script adding the same modifier adds another entry: the two go on in turn.
	W.SkySubsystem->AddModifier(Gloom, 0.0f);
	W.Clock->Tick(0.016f);
	TestEqual(TEXT("two entries"), W.SkySubsystem->GetModifiers().Num(), 2);
	TestNearlyEqual(TEXT("both on the sun"), W.Sky->SunLight->Intensity, Sun * 0.5f * 0.25f, Sun * 1e-3f);

	// The weather's is not kept in a snapshot (it comes back with the weather); the script's is.
	const FDreamWorldSnapshot Snapshot = W.Clock->CaptureSnapshot();
	const FString* Kept = Snapshot.Extensions.Find(TEXT("DreamSky"));
	if (TestNotNull(TEXT("the sky's part of the snapshot"), Kept))
	{
		FDreamSkySnapshotData Data;
		TestTrue(TEXT("it reads"), FJsonObjectConverter::JsonObjectStringToUStruct(*Kept, &Data));
		TestEqual(TEXT("only the script's"), Data.Modifiers.Num(), 1);
	}

	// The weather moves on: its modifier leaves with the next update; the script's stays.
	W.Clock->SetEnvironment(FDreamWorldEnvironment());
	W.Clock->Tick(0.016f);
	TestEqual(TEXT("the weather's gone, the script's left"), W.SkySubsystem->GetModifiers().Num(), 1);
	TestNearlyEqual(TEXT("the script's in full"), W.SkySubsystem->GetModifierAlpha(Gloom), 1.0f, 1e-4f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDreamSkyModifierSnapshotTest, "DreamSky.Modifiers.SnapshotAndPostProcess", DreamSky::ModifierTests::Flags)
bool FDreamSkyModifierSnapshotTest::RunTest(const FString& Parameters)
{
	using namespace DreamSky::ModifierTests;
	FSkyWorld W;
	if (!TestTrue(TEXT("test world"), W.IsValid()))
	{
		return false;
	}
	UDreamSkyModifier* Fading = MakeModifier(2.0f);
	Fading->Settings.bOverride_SkyLightIntensity = true;
	Fading->Settings.SkyLightIntensity = 3.0f;
	Fading->bUsePostProcess = true;
	Fading->PostProcess.bOverride_ColorSaturation = true;
	Fading->PostProcess.ColorSaturation = FVector4(0.2f, 0.2f, 0.2f, 1.0f);
	W.SkySubsystem->AddModifier(Fading);
	W.Clock->Tick(0.5f);

	// Its post process: on the sky, weighted by its alpha.
	TArray<UPostProcessComponent*> PostProcesses;
	W.Sky->GetComponents<UPostProcessComponent>(PostProcesses);
	const UPostProcessComponent* Own = nullptr;
	for (const UPostProcessComponent* P : PostProcesses)
	{
		Own = P != W.Sky->PostProcess ? P : Own;
	}
	if (TestNotNull(TEXT("its post process on the sky"), Own))
	{
		TestNearlyEqual(TEXT("weighted by its alpha"), Own->BlendWeight, W.SkySubsystem->GetModifierAlpha(Fading), 1e-4f);
		TestTrue(TEXT("unbound"), Own->bUnbound);
	}

	// A snapshot keeps the stack: restored, the modifier carries on from where it stood.
	const float Before = W.SkySubsystem->GetModifierAlpha(Fading);
	const FDreamWorldSnapshot Snapshot = W.Clock->CaptureSnapshot();
	W.SkySubsystem->ClearModifiers(0.0f);
	W.Clock->Tick(0.016f);
	TestEqual(TEXT("cleared before the restore"), W.SkySubsystem->GetModifiers().Num(), 0);
	W.Clock->ApplySnapshot(Snapshot);
	TestNearlyEqual(TEXT("restored where it stood"), W.SkySubsystem->GetModifierAlpha(Fading), Before, 1e-4f);
	W.Clock->Tick(0.5f);
	TestTrue(TEXT("and fading on"), W.SkySubsystem->GetModifierAlpha(Fading) > Before);
	return true;
}

#endif
