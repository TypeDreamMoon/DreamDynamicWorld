// Copyright DreamMoon. All Rights Reserved.

#include "DreamAtmosphereSubsystem.h"
#include "DreamAtmosphereAerial.h"
#include "DreamAtmosphereModule.h"
#include "DreamAtmosphereSettings.h"
#include "DreamAtmosphereTables.h"
#include "DreamAtmosphereViewExtension.h"
#include "DreamWorldSubsystem.h"

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
		ViewExtension = FSceneViewExtensions::NewExtension<FDreamAtmosphereViewExtension>(GetWorld());
	}
}

void UDreamAtmosphereSubsystem::Deinitialize()
{
	if (ViewExtension)
	{
		// Before the tables go: no frame may read them after this.
		ViewExtension->SetFrame_GameThread(DreamAtmosphere::Aerial::FFrame());
		ViewExtension.Reset();
	}
	Super::Deinitialize();
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
	UDreamWorldSubsystem* Clock = GetWorld() ? GetWorld()->GetSubsystem<UDreamWorldSubsystem>() : nullptr;
	const FDreamCelestialState Celestial = Clock ? Clock->GetCelestial() : FDreamCelestialState();
	if (ViewExtension)
	{
		ViewExtension->SetFrame_GameThread(DreamAtmosphere::Aerial::MakeFrame(Tables, Celestial.SunDirection, Celestial.MoonDirection, Celestial.MoonIllumination));
	}
	if (Clock)
	{
		using namespace DreamAtmosphere::Names;
		const FLinearColor Sun = GetSunLight();
		const FLinearColor Moon = GetMoonSkyLight();
		Clock->SetVectorParameter(MPC_AtmosphereSun, FLinearColor(Sun.R, Sun.G, Sun.B, IsSkyReady() ? 1.0f : 0.0f));
		Clock->SetVectorParameter(MPC_AtmosphereMoon, FLinearColor(Moon.R, Moon.G, Moon.B, 0.0f));
	}
}

FLinearColor UDreamAtmosphereSubsystem::GetSunLight() const
{
	const UDreamAtmosphereSettings* S = UDreamAtmosphereSettings::Get();
	FLinearColor C = S->SunColor * FMath::Max(S->SunIlluminance, 0.0f);
	C.A = 1.0f;
	return C;
}

FLinearColor UDreamAtmosphereSubsystem::GetMoonLight() const
{
	const UDreamAtmosphereSettings* S = UDreamAtmosphereSettings::Get();
	const UDreamWorldSubsystem* Clock = GetWorld() ? GetWorld()->GetSubsystem<UDreamWorldSubsystem>() : nullptr;
	const float Phase = Clock ? FMath::Clamp(Clock->GetCelestial().MoonIllumination, 0.0f, 1.0f) : 1.0f;
	FLinearColor C = S->MoonColor * (FMath::Max(S->MoonIlluminance, 0.0f) * Phase);
	C.A = 1.0f;
	return C;
}

FLinearColor UDreamAtmosphereSubsystem::GetMoonSkyLight() const
{
	const UDreamAtmosphereSettings* S = UDreamAtmosphereSettings::Get();
	const float Full = FMath::Max(S->MoonIlluminance, 1.0e-6f);
	return GetMoonLight() * (FMath::Max(S->MoonSkyIlluminance, 0.0f) / Full);
}

bool UDreamAtmosphereSubsystem::IsSkyReady() const
{
	return Tables && Tables->IsBuiltFor(Parameters) && Tables->HasCpuSky();
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
