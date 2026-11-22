// Copyright DreamMoon. All Rights Reserved.

#include "DreamAtmosphereSubsystem.h"
#include "DreamAtmosphereAerial.h"
#include "DreamAtmosphereHlslShim.h"
#include "DreamAtmosphereModule.h"
#include "DreamAtmosphereSettings.h"
#include "DreamAtmosphereTables.h"
#include "DreamAtmosphereViewExtension.h"
#include "DreamWorldSubsystem.h"

#include "Engine/TextureRenderTargetVolume.h"
#include "Engine/VolumeTexture.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"
#include "Misc/App.h"
#include "RHIGlobals.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(DreamAtmosphereSubsystem)

static TAutoConsoleVariable<int32> CVarDreamAtmosphereForceBakedSky(
	TEXT("r.DreamAtmosphere.ForceBakedSky"),
	0,
	TEXT("Take the sky from the sky style's baked table as where the GPU cannot build the tables (mobile, no Shader Model 5): 1, or not (0)."),
	ECVF_Default);

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

bool UDreamAtmosphereSubsystem::CanBuildTables()
{
	return FApp::CanEverRender() && !GUsingNullRHI && GMaxRHIFeatureLevel >= ERHIFeatureLevel::SM5 && CVarDreamAtmosphereForceBakedSky.GetValueOnGameThread() == 0;
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
	const bool bBuild = CanBuildTables();
	if (bBuild && Tables && !Tables->IsBuiltFor(Parameters))
	{
		Tables->Build(Parameters);
		++BuildCount;
	}
	UDreamWorldSubsystem* Clock = GetWorld() ? GetWorld()->GetSubsystem<UDreamWorldSubsystem>() : nullptr;
	const FDreamCelestialState Celestial = Clock ? Clock->GetCelestial() : FDreamCelestialState();
	if (ViewExtension)
	{
		// (The aerial perspective reads the transmittance and multiple scattering tables: none without the GPU's.)
		ViewExtension->SetFrame_GameThread(DreamAtmosphere::Aerial::MakeFrame(bBuild ? Tables.Get() : nullptr, Celestial.SunDirection, Celestial.MoonDirection,
			Celestial.MoonIllumination));
	}
	if (Clock)
	{
		using namespace DreamAtmosphere::Names;
		const FLinearColor Sun = GetSunLight();
		const FLinearColor Moon = GetMoonSkyLight();
		Clock->SetVectorParameter(MPC_AtmosphereSun, FLinearColor(Sun.R, Sun.G, Sun.B, GetSkyDecode()));
		Clock->SetVectorParameter(MPC_AtmosphereMoon, FLinearColor(Moon.R, Moon.G, Moon.B, 0.0f));
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

void UDreamAtmosphereSubsystem::SetBakedSky(const FDreamAtmosphereBakedSky& InBaked)
{
	Baked = InBaked;
	BakedTexture = Baked.Table.IsNull() ? nullptr : Baked.Table.LoadSynchronous();
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
	FLinearColor C = GetMoonLight() * (FMath::Max(S->MoonSkyIlluminance, 0.0f) / Full);
	C.A = 1.0f;
	return C;
}

bool UDreamAtmosphereSubsystem::IsSkyBaked() const
{
	return !CanBuildTables() && BakedTexture && Baked.IsFor(Parameters);
}

bool UDreamAtmosphereSubsystem::IsSkyReady() const
{
	if (CanBuildTables())
	{
		return Tables && Tables->IsBuiltFor(Parameters) && Tables->HasCpuSky();
	}
	return IsSkyBaked();
}

UTexture* UDreamAtmosphereSubsystem::GetSkyTexture() const
{
	if (!IsSkyReady())
	{
		return nullptr;
	}
	return IsSkyBaked() ? static_cast<UTexture*>(BakedTexture.Get()) : static_cast<UTexture*>(Tables->GetSky());
}

float UDreamAtmosphereSubsystem::GetSkyDecode() const
{
	if (!IsSkyReady())
	{
		return 0.0f;
	}
	return IsSkyBaked() ? 1.0f / FMath::Max(Baked.Scale, 1.0e-6f) : 1.0f;
}

FLinearColor UDreamAtmosphereSubsystem::ReadBaked(const TArray<FLinearColor>& Rows, float LightZ) const
{
	using namespace DreamWind::Hlsl;
	const int32 N = Rows.Num();
	if (N == 0)
	{
		return FLinearColor::Black;
	}
	// The table's depth coordinate for the light, as the GPU reads it (texel k holds DSA_TableParam(k)).
	const float W = DSA_SkyUvw(0.0f, 1.0f, LightZ).z * float(N - 1);
	const int32 K0 = FMath::Clamp(int32(W), 0, N - 1);
	const int32 K1 = FMath::Min(K0 + 1, N - 1);
	FLinearColor C = FMath::Lerp(Rows[K0], Rows[K1], W - float(K0));
	C.A = 1.0f;
	return C;
}

FLinearColor UDreamAtmosphereSubsystem::SkyZenith(const FVector& Light) const
{
	if (IsSkyBaked())
	{
		return ReadBaked(Baked.Zenith, float(Light.GetSafeNormal().Z));
	}
	return Tables && Tables->HasCpuSky() ? Tables->SampleSky(FVector::UpVector, Light) : FLinearColor::Black;
}

FLinearColor UDreamAtmosphereSubsystem::SkyHorizon(const FVector& Light) const
{
	if (IsSkyBaked())
	{
		return ReadBaked(Baked.Horizon, float(Light.GetSafeNormal().Z));
	}
	if (!Tables || !Tables->HasCpuSky())
	{
		return FLinearColor::Black;
	}
	// All round, from the light's side: the same eight directions the bake averages.
	const FVector L = Light.GetSafeNormal();
	const double Base = FMath::Atan2(L.Y, L.X);
	FLinearColor Round = FLinearColor::Black;
	for (int32 i = 0; i < 8; ++i)
	{
		const double Az = Base + i * UE_PI / 4.0;
		Round += Tables->SampleSky(FVector(FMath::Cos(Az) * 0.99875, FMath::Sin(Az) * 0.99875, 0.05), L);
	}
	Round = Round * 0.125f;
	Round.A = 1.0f;
	return Round;
}

FLinearColor UDreamAtmosphereSubsystem::LightTransmittance(float LightZ) const
{
	using namespace DreamWind::Hlsl;
	FVector4f P[5];
	Parameters.Pack(P);
	const FDSA_Atmosphere A = DSA_Unpack(ToHlsl(P[0]), ToHlsl(P[1]), ToHlsl(P[2]), ToHlsl(P[3]), ToHlsl(P[4]));
	const float3 T = DSA_LightTransmittance(A, A.BottomRadius, FMath::Clamp(LightZ, -1.0f, 1.0f));
	return FLinearColor(T.x, T.y, T.z, 1.0f);
}
