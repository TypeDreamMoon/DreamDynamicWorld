// Copyright DreamMoon. All Rights Reserved.

#include "DreamWorldConfig.h"

#include "Engine/DeveloperSettings.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"
#include "Interfaces/IPluginManager.h"
#include "UObject/UObjectIterator.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(DreamWorldConfig)

FOnDreamWorldConfigApplied UDreamWorldConfig::OnApplied;

namespace DreamWorldConfigImpl
{
	static TWeakObjectPtr<UDreamWorldConfig> LastApplied;

	static const TCHAR* const Prefixes[] = {
		TEXT("DreamWorld."), TEXT("DreamSky."), TEXT("DreamWeather."), TEXT("DreamWind."), TEXT("DreamSurface."), TEXT("DreamCloud."),
		TEXT("DreamAtmosphere."), TEXT("r.DreamWorld."), TEXT("r.DreamWind."), TEXT("r.DreamSurface."),
	};

	static UWorld* WorldOf(const UObject* Context)
	{
		return Context && GEngine ? GEngine->GetWorldFromContextObject(Context, EGetWorldErrorMode::ReturnNull) : nullptr;
	}

	/** A property's key in a config: its name, with the index for a fixed array's later elements. */
	static FName KeyOf(const FProperty* Property, int32 Index)
	{
		return Index == 0 ? Property->GetFName() : FName(*FString::Printf(TEXT("%s[%d]"), *Property->GetName(), Index));
	}

	template <typename Fn>
	static void ForEachConfigValue(UObject* Settings, Fn&& Visit)
	{
		for (TFieldIterator<FProperty> It(Settings->GetClass()); It; ++It)
		{
			FProperty* Property = *It;
			if (!Property->HasAnyPropertyFlags(CPF_Config) || Property->HasAnyPropertyFlags(CPF_Transient | CPF_Deprecated))
			{
				continue;
			}
			for (int32 Index = 0; Index < Property->ArrayDim; ++Index)
			{
				Visit(Property, Index);
			}
		}
	}

	static FString Export(UObject* Settings, const FProperty* Property, int32 Index)
	{
		FString Text;
		Property->ExportText_InContainer(Index, Text, Settings, nullptr, Settings, PPF_None);
		return Text;
	}

	/** The plugin's console variables: name, variable. */
	template <typename Fn>
	static void ForEachVariable(Fn&& Visit)
	{
		for (const TCHAR* Prefix : Prefixes)
		{
			IConsoleManager::Get().ForEachConsoleObjectThatStartsWith(FConsoleObjectVisitor::CreateLambda([&](const TCHAR* Name, IConsoleObject* Object)
			{
				IConsoleVariable* Variable = Object ? Object->AsVariable() : nullptr;
				if (Variable && !(Variable->GetFlags() & ECVF_ReadOnly))
				{
					Visit(FString(Name), Variable);
				}
			}), Prefix);
		}
	}

	/** Set above its scalability level and away from its default: the config keeps it. */
	static bool IsSetByHand(IConsoleVariable* Variable)
	{
		const uint32 SetBy = uint32(Variable->GetFlags()) & uint32(ECVF_SetByMask);
		return SetBy > uint32(ECVF_SetByScalability) && Variable->GetString() != Variable->GetDefaultValue();
	}
}

TMap<FName, UDreamWorldConfig::FWorldStateProvider>& UDreamWorldConfig::Providers()
{
	static TMap<FName, FWorldStateProvider> Map;
	return Map;
}

void UDreamWorldConfig::RegisterWorldState(FName Key, FWorldStateProvider Provider)
{
	Providers().Add(Key, MoveTemp(Provider));
}

void UDreamWorldConfig::UnregisterWorldState(FName Key)
{
	Providers().Remove(Key);
}

UDreamWorldConfig* UDreamWorldConfig::GetLastApplied()
{
	return DreamWorldConfigImpl::LastApplied.Get();
}

TArrayView<const TCHAR* const> UDreamWorldConfig::GetConsoleVariablePrefixes()
{
	return MakeArrayView(DreamWorldConfigImpl::Prefixes);
}

TArray<UClass*> UDreamWorldConfig::GetSettingsClasses()
{
	TSet<FName> Packages;
	if (const TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("DreamDynamicWorld")))
	{
		for (const FModuleDescriptor& Module : Plugin->GetDescriptor().Modules)
		{
			Packages.Add(FName(*(TEXT("/Script/") + Module.Name.ToString())));
		}
	}
	TArray<UClass*> Out;
	for (TObjectIterator<UClass> It; It; ++It)
	{
		UClass* Class = *It;
		if (Class->IsChildOf(UDeveloperSettings::StaticClass()) && !Class->HasAnyClassFlags(CLASS_Abstract | CLASS_Deprecated | CLASS_NewerVersionExists)
			&& Packages.Contains(Class->GetOutermost()->GetFName()))
		{
			Out.Add(Class);
		}
	}
	Out.Sort([](const UClass& A, const UClass& B) { return A.GetPathName() < B.GetPathName(); });
	return Out;
}

void UDreamWorldConfig::Capture(const UObject* WorldContext)
{
	using namespace DreamWorldConfigImpl;
	Modify();
	Settings.Reset();
	for (UClass* Class : GetSettingsClasses())
	{
		UObject* Defaults = Class->GetDefaultObject();
		FDreamWorldConfigSettings& Entry = Settings.AddDefaulted_GetRef();
		Entry.Class = FSoftClassPath(Class);
		ForEachConfigValue(Defaults, [&](const FProperty* Property, int32 Index)
		{
			Entry.Values.Add(KeyOf(Property, Index), Export(Defaults, Property, Index));
		});
	}
	ConsoleVariables.Reset();
	ForEachVariable([&](const FString& Name, IConsoleVariable* Variable)
	{
		if (IsSetByHand(Variable))
		{
			ConsoleVariables.Add(Name, Variable->GetString());
		}
	});
	ConsoleVariables.KeySort(TLess<FString>());
	WorldState.Reset();
	if (UWorld* World = WorldOf(WorldContext))
	{
		for (const TPair<FName, FWorldStateProvider>& Provider : Providers())
		{
			const FString Value = Provider.Value.Capture ? Provider.Value.Capture(*World) : FString();
			if (!Value.IsEmpty())
			{
				WorldState.Add(Provider.Key, Value);
			}
		}
	}
}

int32 UDreamWorldConfig::Apply(const UObject* WorldContext, bool bWriteProjectSettings)
{
	using namespace DreamWorldConfigImpl;
	int32 Changed = 0;
	for (const FDreamWorldConfigSettings& Entry : Settings)
	{
		UClass* Class = Entry.Class.TryLoadClass<UDeveloperSettings>();
		if (!Class)
		{
			continue;
		}
		UDeveloperSettings* Defaults = Class->GetDefaultObject<UDeveloperSettings>();
		bool bClassChanged = false;
		ForEachConfigValue(Defaults, [&](FProperty* Property, int32 Index)
		{
			const FString* Value = Entry.Values.Find(KeyOf(Property, Index));
			if (!Value || Export(Defaults, Property, Index) == *Value)
			{
				return;
			}
			Property->ImportText_Direct(**Value, Property->ContainerPtrToValuePtr<void>(Defaults, Index), Defaults, PPF_None);
			++Changed;
			bClassChanged = true;
#if WITH_EDITOR
			// As if edited in Project Settings: whatever listens for the settings' changes hears it.
			FPropertyChangedEvent Event(Property, EPropertyChangeType::ValueSet);
			Defaults->PostEditChangeProperty(Event);
#endif
		});
#if WITH_EDITOR
		if (bClassChanged && bWriteProjectSettings)
		{
			Defaults->TryUpdateDefaultConfigFile();
		}
#endif
	}
	if (bApplyConsoleVariables)
	{
		ForEachVariable([&](const FString& Name, IConsoleVariable* Variable)
		{
			if (const FString* Value = ConsoleVariables.Find(Name))
			{
				if (Variable->GetString() != *Value)
				{
					Variable->Set(**Value, ECVF_SetByConsole);
					++Changed;
				}
			}
			else if ((uint32(Variable->GetFlags()) & uint32(ECVF_SetByMask)) == uint32(ECVF_SetByConsole))
			{
				// Set from the console but not in this config: back to what it was under that (or its default where no
				// history is kept).
				const FString Before = Variable->GetString();
				Variable->Unset(ECVF_SetByConsole);
				if ((uint32(Variable->GetFlags()) & uint32(ECVF_SetByMask)) == uint32(ECVF_SetByConsole))
				{
					Variable->Set(*Variable->GetDefaultValue(), ECVF_SetByConsole);
				}
				Changed += Variable->GetString() != Before ? 1 : 0;
			}
		});
	}
	if (bApplyWorldState)
	{
		if (UWorld* World = WorldOf(WorldContext))
		{
			for (const TPair<FName, FString>& State : WorldState)
			{
				const FWorldStateProvider* Provider = Providers().Find(State.Key);
				if (Provider && Provider->Apply && (!Provider->Capture || Provider->Capture(*World) != State.Value))
				{
					Provider->Apply(*World, State.Value);
					++Changed;
				}
			}
		}
	}
	LastApplied = this;
	OnApplied.Broadcast(this);
	return Changed;
}

TArray<FString> UDreamWorldConfig::DescribeDifferences(const UObject* WorldContext) const
{
	using namespace DreamWorldConfigImpl;
	TArray<FString> Out;
	for (const FDreamWorldConfigSettings& Entry : Settings)
	{
		UClass* Class = Entry.Class.TryLoadClass<UDeveloperSettings>();
		if (!Class)
		{
			Out.Add(FString::Printf(TEXT("%s: no such settings class"), *Entry.Class.ToString()));
			continue;
		}
		UObject* Defaults = Class->GetDefaultObject();
		ForEachConfigValue(Defaults, [&](const FProperty* Property, int32 Index)
		{
			const FString* Value = Entry.Values.Find(KeyOf(Property, Index));
			const FString Now = Export(Defaults, Property, Index);
			if (Value && Now != *Value)
			{
				Out.Add(FString::Printf(TEXT("%s.%s: %s -> %s"), *Class->GetName(), *KeyOf(Property, Index).ToString(), *Now, **Value));
			}
		});
	}
	ForEachVariable([&](const FString& Name, IConsoleVariable* Variable)
	{
		if (const FString* Value = ConsoleVariables.Find(Name))
		{
			if (Variable->GetString() != *Value)
			{
				Out.Add(FString::Printf(TEXT("%s: %s -> %s"), *Name, *Variable->GetString(), **Value));
			}
		}
	});
	if (UWorld* World = WorldOf(WorldContext))
	{
		for (const TPair<FName, FString>& State : WorldState)
		{
			const FWorldStateProvider* Provider = Providers().Find(State.Key);
			const FString Now = Provider && Provider->Capture ? Provider->Capture(*World) : FString();
			if (Now != State.Value)
			{
				Out.Add(FString::Printf(TEXT("%s: %s -> %s"), *State.Key.ToString(), *Now, *State.Value));
			}
		}
	}
	return Out;
}
