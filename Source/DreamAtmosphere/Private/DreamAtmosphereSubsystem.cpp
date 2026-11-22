// Copyright DreamMoon. All Rights Reserved.

#include "DreamAtmosphereSubsystem.h"
#include "DreamAtmosphereSettings.h"
#include "DreamAtmosphereTables.h"

#include "Engine/World.h"
#include "Misc/App.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(DreamAtmosphereSubsystem)

UDreamAtmosphereSubsystem* UDreamAtmosphereSubsystem::Get(const UWorld* World)
{
	return World ? World->GetSubsystem<UDreamAtmosphereSubsystem>() : nullptr;
}

bool UDreamAtmosphereSubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
	if (!Super::ShouldCreateSubsystem(Outer) || !UDreamAtmosphereSettings::Get()->bEnabled)
	{
		return false;
	}
	const UWorld* World = Cast<UWorld>(Outer);
	return World && (World->WorldType == EWorldType::Game || World->WorldType == EWorldType::PIE || World->WorldType == EWorldType::Editor);
}

void UDreamAtmosphereSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	Parameters = UDreamAtmosphereSettings::Get()->Parameters;
	if (FApp::CanEverRender())
	{
		Tables = NewObject<UDreamAtmosphereTables>(this);
	}
}

void UDreamAtmosphereSubsystem::Tick(float DeltaTime)
{
	if (!bOverridden)
	{
		Parameters = UDreamAtmosphereSettings::Get()->Parameters;
	}
	if (Tables && !Tables->IsBuiltFor(Parameters))
	{
		Tables->Build(Parameters);
		++BuildCount;
	}
}

TStatId UDreamAtmosphereSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UDreamAtmosphereSubsystem, STATGROUP_Tickables);
}

void UDreamAtmosphereSubsystem::SetParameters(const FDreamAtmosphereParameters& InParameters)
{
	Parameters = InParameters;
	bOverridden = true;
}

void UDreamAtmosphereSubsystem::ResetParameters()
{
	bOverridden = false;
	Parameters = UDreamAtmosphereSettings::Get()->Parameters;
}
