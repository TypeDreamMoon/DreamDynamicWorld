// Copyright DreamMoon. All Rights Reserved.

// Console commands for the sky modifiers: add, remove, list and clear them, and three ready-made ones (a storm's gloom,
// a red sky, a clear bright day) to try the stack without an asset of one's own.

#include "DreamSky.h"
#include "DreamSkyModifier.h"
#include "DreamSkyModule.h"
#include "DreamSkySubsystem.h"

#include "Engine/World.h"
#include "HAL/IConsoleManager.h"
#include "UObject/Package.h"

namespace DreamSky::Console
{
	static UDreamSkySubsystem* Find(UWorld* World)
	{
		UDreamSkySubsystem* Subsystem = World ? World->GetSubsystem<UDreamSkySubsystem>() : nullptr;
		if (!Subsystem)
		{
			UE_LOG(LogDreamSky, Warning, TEXT("No Dream Sky in this world."));
		}
		return Subsystem;
	}

	static float SecondsArg(const TArray<FString>& Args, int32 Index)
	{
		return Args.IsValidIndex(Index) ? FCString::Atof(*Args[Index]) : -1.0f;
	}

	/** One of the ready-made modifiers (made once, kept in the transient package by the stack that holds it). */
	static UDreamSkyModifier* Demo(const FString& Name)
	{
		const FString ObjectName = FString::Printf(TEXT("DreamSkyDemoModifier_%s"), *Name);
		if (UDreamSkyModifier* Existing = FindObject<UDreamSkyModifier>(GetTransientPackage(), *ObjectName))
		{
			return Existing;
		}
		UDreamSkyModifier* M = NewObject<UDreamSkyModifier>(GetTransientPackage(), *ObjectName, RF_Transient);
		FDreamSkyModifierSettings& S = M->Settings;
		if (Name.Equals(TEXT("Gloom"), ESearchCase::IgnoreCase))
		{
			S.bOverride_SunIntensity = true;
			S.SunIntensity = 0.3f;
			S.bOverride_SkyLightIntensity = true;
			S.SkyLightIntensity = 0.6f;
			S.bOverride_SkySaturation = true;
			S.SkySaturation = 0.35f;
			S.bOverride_SkyTint = true;
			S.SkyTint = FLinearColor(0.55f, 0.6f, 0.7f);
			S.bOverride_CloudCoverage = true;
			S.CloudCoverage = 0.92f;
			S.bOverride_CloudDarkness = true;
			S.CloudDarkness = 0.6f;
			S.bOverride_FogDensity = true;
			S.FogDensity = 3.0f;
			S.bOverride_ExposureCompensation = true;
			S.ExposureCompensation = -0.5f;
		}
		else if (Name.Equals(TEXT("Crimson"), ESearchCase::IgnoreCase))
		{
			S.bOverride_SkyTint = true;
			S.SkyTint = FLinearColor(1.6f, 0.45f, 0.35f);
			S.bOverride_SunTint = true;
			S.SunTint = FLinearColor(1.0f, 0.35f, 0.25f);
			S.bOverride_CloudLitTint = true;
			S.CloudLitTint = FLinearColor(1.0f, 0.55f, 0.45f);
			S.bOverride_CloudShadowTint = true;
			S.CloudShadowTint = FLinearColor(0.8f, 0.35f, 0.4f);
			S.bOverride_FogColor = true;
			S.FogColor = FLinearColor(0.9f, 0.25f, 0.2f);
			S.bOverride_AmbientTint = true;
			S.AmbientTint = FLinearColor(1.0f, 0.55f, 0.5f);
		}
		else if (Name.Equals(TEXT("Clear"), ESearchCase::IgnoreCase))
		{
			S.bOverride_CloudCoverage = true;
			S.CloudCoverage = 0.05f;
			S.bOverride_SkySaturation = true;
			S.SkySaturation = 1.3f;
			S.bOverride_SunIntensity = true;
			S.SunIntensity = 1.25f;
			S.bOverride_Haze = true;
			S.Haze = 0.0f;
		}
		else
		{
			UE_LOG(LogDreamSky, Warning, TEXT("No ready-made sky modifier called %s (Gloom, Crimson, Clear)."), *Name);
			return nullptr;
		}
		return M;
	}

	static UDreamSkyModifier* Resolve(const FString& NameOrPath)
	{
		if (UDreamSkyModifier* Ready = NameOrPath.Contains(TEXT("/")) ? nullptr : Demo(NameOrPath))
		{
			return Ready;
		}
		UDreamSkyModifier* Loaded = LoadObject<UDreamSkyModifier>(nullptr, *NameOrPath);
		if (!Loaded)
		{
			UE_LOG(LogDreamSky, Warning, TEXT("No sky modifier at %s."), *NameOrPath);
		}
		return Loaded;
	}

	static FAutoConsoleCommandWithWorldAndArgs AddCommand(
		TEXT("DreamSky.Modifier.Add"),
		TEXT("DreamSky.Modifier.Add <asset path | Gloom | Crimson | Clear> [fade seconds]: adds a sky modifier, fading in (no time: its own)."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
		{
			UDreamSkySubsystem* Sky = Find(World);
			UDreamSkyModifier* Modifier = Sky && Args.Num() > 0 ? Resolve(Args[0]) : nullptr;
			if (Modifier)
			{
				Sky->AddModifier(Modifier, SecondsArg(Args, 1));
			}
		}));

	static FAutoConsoleCommandWithWorldAndArgs RemoveCommand(
		TEXT("DreamSky.Modifier.Remove"),
		TEXT("DreamSky.Modifier.Remove <asset path | Gloom | Crimson | Clear> [fade seconds]: fades a sky modifier out and drops it."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
		{
			UDreamSkySubsystem* Sky = Find(World);
			UDreamSkyModifier* Modifier = Sky && Args.Num() > 0 ? Resolve(Args[0]) : nullptr;
			if (Modifier)
			{
				Sky->RemoveModifier(Modifier, SecondsArg(Args, 1));
			}
		}));

	static FAutoConsoleCommandWithWorldAndArgs AlphaCommand(
		TEXT("DreamSky.Modifier.Alpha"),
		TEXT("DreamSky.Modifier.Alpha <asset path | Gloom | Crimson | Clear> <alpha> [seconds]: holds a sky modifier at a strength."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
		{
			UDreamSkySubsystem* Sky = Find(World);
			UDreamSkyModifier* Modifier = Sky && Args.Num() > 1 ? Resolve(Args[0]) : nullptr;
			if (Modifier)
			{
				Sky->SetModifierAlpha(Modifier, FCString::Atof(*Args[1]), FMath::Max(SecondsArg(Args, 2), 0.0f));
			}
		}));

	static FAutoConsoleCommandWithWorldAndArgs ClearCommand(
		TEXT("DreamSky.Modifier.Clear"),
		TEXT("DreamSky.Modifier.Clear [fade seconds]: fades every scripted sky modifier out."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
		{
			if (UDreamSkySubsystem* Sky = Find(World))
			{
				Sky->ClearModifiers(SecondsArg(Args, 0));
			}
		}));

	static FAutoConsoleCommandWithWorldAndArgs ConditionsDemoCommand(
		TEXT("DreamSky.Conditions.Demo"),
		TEXT("DreamSky.Conditions.Demo [Off]: gives the level's sky two conditional entries to try them: a grey, cool grade while it")
		TEXT(" rains outdoors, and a blue night (20:00 to 05:00, the stars brighter). Off takes them away again."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
		{
			UDreamSkySubsystem* Subsystem = Find(World);
			ADreamSky* Sky = Subsystem ? Subsystem->GetSky() : nullptr;
			if (!Sky)
			{
				return;
			}
			static const FName Name(TEXT("DemoRainGrade"));
			Sky->PostProcesses.RemoveAll([](const FDreamSkyConditionalPostProcess& E) { return E.Name == Name; });
			UDreamSkyModifier* NightBlue = nullptr;
			{
				const TCHAR* ObjectName = TEXT("DreamSkyDemoModifier_NightBlue");
				NightBlue = FindObject<UDreamSkyModifier>(GetTransientPackage(), ObjectName);
				if (!NightBlue)
				{
					NightBlue = NewObject<UDreamSkyModifier>(GetTransientPackage(), ObjectName, RF_Transient);
					NightBlue->Settings.bOverride_SkyTint = true;
					NightBlue->Settings.SkyTint = FLinearColor(0.55f, 0.75f, 1.35f);
					NightBlue->Settings.bOverride_StarIntensity = true;
					NightBlue->Settings.StarIntensity = 2.0f;
					NightBlue->Settings.bOverride_MoonTint = true;
					NightBlue->Settings.MoonTint = FLinearColor(0.7f, 0.85f, 1.2f);
				}
			}
			Sky->ConditionalModifiers.RemoveAll([NightBlue](const FDreamSkyConditionalModifier& E) { return E.Modifier == NightBlue; });
			if (Args.Num() > 0 && Args[0].Equals(TEXT("Off"), ESearchCase::IgnoreCase))
			{
				return;
			}
			FDreamSkyConditionalPostProcess& Grade = Sky->PostProcesses.AddDefaulted_GetRef();
			Grade.Name = Name;
			FDreamSkyWeatherRange& Rain = Grade.Condition.Weather.AddDefaulted_GetRef();
			Rain.Value = EDreamSkyWeatherValue::Rain;
			Rain.Min = 0.2f;
			Rain.Soft = 0.3f;
			Grade.Condition.Place = EDreamSkyPlace::Outdoors;
			Grade.BlendSeconds = 2.0f;
			Grade.Settings.bOverride_ColorSaturation = true;
			Grade.Settings.ColorSaturation = FVector4(0.55f, 0.6f, 0.7f, 1.0f);
			Grade.Settings.bOverride_ColorGain = true;
			Grade.Settings.ColorGain = FVector4(0.9f, 0.95f, 1.08f, 1.0f);
			Grade.Settings.bOverride_ColorContrast = true;
			Grade.Settings.ColorContrast = FVector4(1.1f, 1.1f, 1.1f, 1.0f);
			FDreamSkyConditionalModifier& Night = Sky->ConditionalModifiers.AddDefaulted_GetRef();
			Night.Modifier = NightBlue;
			Night.Condition.bHours = true;
			Night.Condition.StartHour = 20.0f;
			Night.Condition.EndHour = 5.0f;
			Night.Condition.FadeHours = 1.0f;
		}));

	static FAutoConsoleCommandWithWorldAndArgs ListCommand(
		TEXT("DreamSky.Modifier.List"),
		TEXT("DreamSky.Modifier.List: the sky modifiers in the stack, in the order they go on, with their strength."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
		{
			if (UDreamSkySubsystem* Sky = Find(World))
			{
				const TArray<UDreamSkyModifier*> Modifiers = Sky->GetModifiers();
				UE_LOG(LogDreamSky, Display, TEXT("%d sky modifier(s):"), Modifiers.Num());
				for (const UDreamSkyModifier* M : Modifiers)
				{
					UE_LOG(LogDreamSky, Display, TEXT("  %s  priority %d  alpha %.3f"), *GetNameSafe(M), M ? M->Priority : 0, Sky->GetModifierAlpha(M));
				}
			}
		}));
}
