// Copyright DreamMoon. All Rights Reserved.

#include "DreamWeatherFXSubsystem.h"
#include "DreamWeatherFXSettings.h"
#include "DreamWeatherSubsystem.h"
#include "DreamWeatherTypes.h"
#include "DreamWindSubsystem.h"
#include "DreamWorldSubsystem.h"

#include "CollisionQueryParams.h"
#include "Engine/HitResult.h"
#include "Engine/World.h"
#include "Camera/PlayerCameraManager.h"
#include "DreamWindCameraShake.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/WorldSettings.h"
#include "NiagaraComponent.h"
#include "NiagaraDataInterfaceArrayFunctionLibrary.h"
#include "NiagaraSystem.h"

#include "HAL/IConsoleManager.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(DreamWeatherFXSubsystem)

DECLARE_CYCLE_STAT(TEXT("DreamWeather Precipitation"), STAT_DreamWeatherPrecipitation, STATGROUP_Game);
DEFINE_LOG_CATEGORY_STATIC(LogDreamWeatherFX, Log, All);

namespace DreamWeatherFX
{
	static const FName Paths(TEXT("User.Paths"));
	static const FName Starts(TEXT("User.Starts"));
	static const FName Normals(TEXT("User.Normals"));
	static const FName DropRate(TEXT("User.DropRate"));
	static TAutoConsoleVariable<float> CVarParticleDensity(
		TEXT("DreamWeather.ParticleDensity"), 1.0f,
		TEXT("How many of the weather's particles (rain, snow, dust, their splashes) are spawned, of the style's (the quality levels set it)."),
		ECVF_Scalability);
	static const FName SplashRate(TEXT("User.SplashRate"));
	static const FName Anywhere(TEXT("User.Anywhere"));
	static const FName Presence(TEXT("User.Presence"));

	/** Seconds an effect stays alive after its precipitation stopped (its last drops finish falling). */
	static constexpr float IdleSecondsBeforeDeactivate = 8.0f;

	/** How fast the dust already in the air follows the weather (Presence per second): its particles live 20 s, the
	 *  haze they make must end with the storm. */
	static constexpr float PresenceRate = 2.0f;
}

UDreamWeatherFXSettings::UDreamWeatherFXSettings()
{
	Rain.System = FSoftObjectPath(TEXT("/DreamDynamicWorld/Weather/Effects/NS_DreamRain.NS_DreamRain"));
	Snow.System = FSoftObjectPath(TEXT("/DreamDynamicWorld/Weather/Effects/NS_DreamSnow.NS_DreamSnow"));
	Snow.DropsPerSecond = 3000.0f;
	Snow.SplashesPerSecond = 0.0f;
	Snow.FallSpeed = 130.0f;
	Snow.WindDrift = 0.8f;
	Snow.MaxSlant = 2.0f;
	LightningBolt = FSoftObjectPath(TEXT("/DreamDynamicWorld/Weather/Effects/NS_DreamLightningBolt.NS_DreamLightningBolt"));
	Dust.System = FSoftObjectPath(TEXT("/DreamDynamicWorld/Weather/Effects/NS_DreamDust.NS_DreamDust"));
	Dust.DropsPerSecond = 2500.0f;
	Dust.SplashesPerSecond = 0.0f;
	Dust.FallSpeed = 120.0f;
	Dust.WindDrift = 1.0f;
	Dust.MaxSlant = 12.0f;
	WindDebris = FSoftObjectPath(TEXT("/DreamDynamicWorld/Wind/Effects/NS_DreamWindLeaves.NS_DreamWindLeaves"));
}

UDreamWeatherFXSubsystem* UDreamWeatherFXSubsystem::Get(const UObject* WorldContextObject)
{
	const UWorld* World = WorldContextObject ? WorldContextObject->GetWorld() : nullptr;
	return World ? World->GetSubsystem<UDreamWeatherFXSubsystem>() : nullptr;
}

bool UDreamWeatherFXSubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
	if (!Super::ShouldCreateSubsystem(Outer))
	{
		return false;
	}
	const UWorld* World = Cast<UWorld>(Outer);
	return World && (World->WorldType == EWorldType::Game || World->WorldType == EWorldType::PIE || World->WorldType == EWorldType::Editor);
}

void UDreamWeatherFXSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	Effects.SetNum(NumLayers);
	if (UDreamWeatherSubsystem* Weather = Collection.InitializeDependency<UDreamWeatherSubsystem>())
	{
		WeatherSubsystem = Weather;
		FrameHandle = Weather->OnWeatherFrame.AddUObject(this, &UDreamWeatherFXSubsystem::HandleWeatherFrame);
		StoppedHandle = Weather->OnWeatherStopped.AddUObject(this, &UDreamWeatherFXSubsystem::StopPrecipitation);
		Weather->OnLightningStrike.AddUniqueDynamic(this, &UDreamWeatherFXSubsystem::HandleLightningStrike);
	}
}

void UDreamWeatherFXSubsystem::Deinitialize()
{
	if (UDreamWeatherSubsystem* Weather = WeatherSubsystem.Get())
	{
		Weather->OnWeatherFrame.Remove(FrameHandle);
		Weather->OnWeatherStopped.Remove(StoppedHandle);
		Weather->OnLightningStrike.RemoveDynamic(this, &UDreamWeatherFXSubsystem::HandleLightningStrike);
	}
	for (FBolt& Bolt : Bolts)
	{
		if (UNiagaraComponent* Effect = Bolt.Effect.Get())
		{
			Effect->DestroyComponent();
		}
	}
	Bolts.Reset();
	if (DebrisEffect)
	{
		DebrisEffect->DestroyComponent();
		DebrisEffect = nullptr;
	}
	for (TObjectPtr<UNiagaraComponent>& Effect : Effects)
	{
		if (Effect)
		{
			Effect->DestroyComponent();
			Effect = nullptr;
		}
	}
	Super::Deinitialize();
}

TArray<UNiagaraComponent*> UDreamWeatherFXSubsystem::GetBolts() const
{
	TArray<UNiagaraComponent*> Alive;
	for (const FBolt& Bolt : Bolts)
	{
		if (UNiagaraComponent* Effect = Bolt.Effect.Get())
		{
			Alive.Add(Effect);
		}
	}
	return Alive;
}

void UDreamWeatherFXSubsystem::HandleLightningStrike(const FDreamLightningStrike& Strike)
{
	const UDreamWeatherFXSettings* S = UDreamWeatherFXSettings::Get();
	UWorld* World = GetWorld();
	if (!World || !S->bLightningBolts || Strike.Distance > S->BoltMaxDistance || (!World->IsGameWorld() && !S->bPrecipitationInEditor))
	{
		return;
	}
	UNiagaraSystem* System = Cast<UNiagaraSystem>(S->LightningBolt.TryLoad());
	if (!System)
	{
		return;
	}

	// The strike is placed at the view's height: find the ground under it (or keep it where it is).
	FVector End = Strike.Location;
	FHitResult Hit;
	const FCollisionQueryParams Query(SCENE_QUERY_STAT(DreamLightningBolt), false);
	if (World->LineTraceSingleByChannel(Hit, End + FVector(0.0, 0.0, S->BoltHeight), End - FVector(0.0, 0.0, S->BoltHeight), ECC_Visibility, Query))
	{
		End = Hit.ImpactPoint;
	}
	// A bolt leaves the cloud a little off to one side of where it lands; its shape comes from the same hash.
	const uint32 Shape = HashCombine(GetTypeHash(++BoltsDrawn), GetTypeHash(Strike.Location));
	const double LeanAngle = double(Shape & 0xffffu) / 65535.0 * UE_DOUBLE_TWO_PI;
	const double Lean = double(S->BoltHeight) * 0.2 * double((Shape >> 16) & 0xffu) / 255.0;
	const FVector Start = End + FVector(FMath::Cos(LeanAngle) * Lean, FMath::Sin(LeanAngle) * Lean, S->BoltHeight);

	// Owned like the precipitation effects (not through the spawn helpers, which refuse to spawn where nothing
	// renders); the subsystem lets it go once its flash is over.
	UObject* Outer = World->GetWorldSettings() ? static_cast<UObject*>(World->GetWorldSettings()) : static_cast<UObject*>(World);
	UNiagaraComponent* Effect = NewObject<UNiagaraComponent>(Outer, NAME_None, RF_Transient | RF_TextExportTransient | RF_DuplicateTransient);
	Effect->SetAsset(System);
	Effect->SetAutoActivate(false);
	Effect->SetAutoDestroy(false);   // UpdateBolts lets it go (a system that cannot run would otherwise vanish at once)
	Effect->SetUsingAbsoluteLocation(true);
	Effect->SetUsingAbsoluteRotation(true);
	Effect->SetWorldLocation(End);
	Effect->RegisterComponentWithWorld(World);
	Effect->SetVariableVec3(TEXT("User.Start"), Start);
	Effect->SetVariableVec3(TEXT("User.End"), End);
	Effect->SetVariableFloat(TEXT("User.Seed"), float(Shape % 997u));
	Effect->SetVariableFloat(TEXT("User.Glow"), 1.0f);
	// A few pixels wide however far away (about 5 milliradians), never thinner than a real channel's glow.
	Effect->SetVariableFloat(TEXT("User.Width"), FMath::Clamp(Strike.Distance * 0.005f, 60.0f, 5000.0f));
	Effect->Activate(true);
	FBolt& Bolt = Bolts.AddDefaulted_GetRef();
	Bolt.Effect = Effect;
}

void UDreamWeatherFXSubsystem::UpdateBolts(const UDreamWeatherSubsystem& Weather, float DeltaTime)
{
	// A bolt outlives its flash by a moment at most; one that is still around after that is let go.
	constexpr float MaxAge = 4.0f;
	const float Shape = Weather.GetLightningFlashShape();
	for (int32 i = Bolts.Num() - 1; i >= 0; --i)
	{
		FBolt& Bolt = Bolts[i];
		UNiagaraComponent* Effect = Bolt.Effect.Get();
		Bolt.Age += FMath::Max(DeltaTime, 0.0f);
		if (!Effect || Bolt.Age > MaxAge)
		{
			if (Effect)
			{
				Effect->DestroyComponent();
			}
			Bolts.RemoveAt(i);
			continue;
		}
		// The newest bolt is the one flashing; an older one dies away fast.
		Bolt.Glow = i == Bolts.Num() - 1 ? Shape : Bolt.Glow * FMath::Exp(-12.0f * FMath::Max(DeltaTime, 0.0f));
		Effect->SetVariableFloat(TEXT("User.Glow"), Bolt.Glow);
	}
}

UNiagaraComponent* UDreamWeatherFXSubsystem::GetEffect(int32 Layer) const
{
	return Effects.IsValidIndex(Layer) ? Effects[Layer].Get() : nullptr;
}

void UDreamWeatherFXSubsystem::StopPrecipitation()
{
	DebrisRate = 0.0f;
	if (DebrisEffect)
	{
		DebrisEffect->SetVariableFloat(TEXT("User.DebrisRate"), 0.0f);
	}
	for (int32 i = 0; i < NumLayers; ++i)
	{
		States[i].DropRate = 0.0f;
		States[i].SplashRate = 0.0f;
		if (UNiagaraComponent* Effect = GetEffect(i))
		{
			Effect->SetVariableFloat(DreamWeatherFX::DropRate, 0.0f);
			Effect->SetVariableFloat(DreamWeatherFX::SplashRate, 0.0f);
		}
	}
}

void UDreamWeatherFXSubsystem::HandleWeatherFrame(UDreamWorldSubsystem& Clock, UDreamWeatherSubsystem& Weather, const FDreamWeatherSample& View, float DeltaTime)
{
	SCOPE_CYCLE_COUNTER(STAT_DreamWeatherPrecipitation);
	UpdateBolts(Weather, DeltaTime);
	const UDreamWeatherFXSettings* S = UDreamWeatherFXSettings::Get();
	UWorld* World = GetWorld();
	if (World && World->IsGameWorld() && S->bWindCameraShake)
	{
		// Every local player's camera buffets in a strong wind.
		for (FConstPlayerControllerIterator It = World->GetPlayerControllerIterator(); It; ++It)
		{
			APlayerController* Player = It->Get();
			if (Player && Player->IsLocalController() && Player->PlayerCameraManager
				&& !Player->PlayerCameraManager->FindCameraModifierByClass(UDreamWindCameraModifier::StaticClass()))
			{
				UDreamWindCameraModifier::Enable(Player);
			}
		}
	}
	if (!World || !S->bPrecipitation || (!World->IsGameWorld() && !S->bPrecipitationInEditor))
	{
		StopPrecipitation();
		return;
	}
	if (Effects.Num() != NumLayers)
	{
		Effects.SetNum(NumLayers);
	}

	const FVector ViewLocation = Clock.HasViewLocation() ? Clock.GetViewLocation() : FVector::ZeroVector;
	const FVector Forward = Clock.HasViewRotation() ? Clock.GetViewRotation().Vector() : FVector::ForwardVector;
	FVector Wind = FVector::ZeroVector;
	if (const UDreamWindSubsystem* WindSubsystem = World->GetSubsystem<UDreamWindSubsystem>())
	{
		Wind = WindSubsystem->SampleWindVelocity(ViewLocation);
		Wind.Z = 0.0;
	}
	UpdateLayer(RainLayer, S->Rain, FMath::Clamp(View.Rain, 0.0f, 1.0f), Weather, ViewLocation, Forward, Wind, DeltaTime);
	UpdateLayer(SnowLayer, S->Snow, FMath::Clamp(View.Snow, 0.0f, 1.0f), Weather, ViewLocation, Forward, Wind, DeltaTime);

	// Blown dust, and the debris of a strong wind: both on the dust layer's nearly flat paths, which run while either blows.
	FLinearColor DebrisColor;
	const bool bSouth = Clock.GetCelestialSettings().Latitude < 0.0;
	const float Debris = FMath::SmoothStep(float(S->DebrisWindSpeed.X), float(FMath::Max(S->DebrisWindSpeed.Y, S->DebrisWindSpeed.X + 1.0)), float(Wind.Size2D()))
		* SeasonDebris(Clock.GetDateTime(), bSouth, DebrisColor);
	const float Dust = FMath::Clamp(View.Dust, 0.0f, 1.0f);
	const float Blown = FMath::Max(Dust, FMath::Min(Debris, 1.0f));
	UpdateLayer(DustLayer, S->Dust, Blown, Weather, ViewLocation, Forward, Wind, DeltaTime, Blown > 0.0f ? Dust / Blown : 0.0f);
	UpdateDebris(Clock, Debris, ViewLocation);
}

UNiagaraComponent* UDreamWeatherFXSubsystem::EnsureEffect(int32 Index, const FDreamPrecipitationLayer& Layer, const FVector& ViewLocation)
{
	if (UNiagaraComponent* Existing = GetEffect(Index))
	{
		return Existing;
	}
	UNiagaraSystem* System = Cast<UNiagaraSystem>(Layer.System.TryLoad());
	UWorld* World = GetWorld();
	if (!System || !World)
	{
		return nullptr;
	}
	// Owned by the world settings actor, like any effect spawned at a location; never saved with the level.
	UObject* Outer = World->GetWorldSettings() ? static_cast<UObject*>(World->GetWorldSettings()) : static_cast<UObject*>(World);
	UNiagaraComponent* Effect = NewObject<UNiagaraComponent>(Outer, NAME_None, RF_Transient | RF_TextExportTransient | RF_DuplicateTransient);
	Effect->SetAsset(System);
	Effect->SetAutoActivate(false);
	Effect->SetAutoDestroy(false);
	Effect->SetUsingAbsoluteLocation(true);
	Effect->SetUsingAbsoluteRotation(true);
	Effect->SetWorldLocation(ViewLocation);
	Effect->RegisterComponentWithWorld(World);
	Effects[Index] = Effect;
	States[Index].bActivated = false;
	return Effect;
}

void UDreamWeatherFXSubsystem::UpdateLayer(int32 Index, const FDreamPrecipitationLayer& Layer, float Strength, UDreamWeatherSubsystem& Weather,
	const FVector& ViewLocation, const FVector& Forward, const FVector& Wind, float DeltaTime, float DropShare)
{
	using namespace DreamWeatherFX;
	const UDreamWeatherFXSettings* S = UDreamWeatherFXSettings::Get();
	FLayerState& State = States[Index];
	UNiagaraComponent* Effect = GetEffect(Index);
	const bool bFalling = Strength > 0.001f && (Layer.DropsPerSecond > 0.0f || Layer.SplashesPerSecond > 0.0f);

	// The dust still in the air: the dust's own share of the blowing (the debris on its paths is not dust), its square
	// root (fewer motes spawn as well, so the haze thins about as the weather does).
	const float PresenceTarget = bFalling ? FMath::Sqrt(FMath::Clamp(Strength * DropShare, 0.0f, 1.0f)) : 0.0f;
	const float WasPresent = State.Presence;
	State.Presence = FMath::FInterpConstantTo(State.Presence, PresenceTarget, FMath::Max(DeltaTime, 0.0f), PresenceRate);
	if (Effect && Index == DustLayer)
	{
		Effect->SetVariableFloat(Presence, State.Presence);
		// Faded out: what is still flying goes at once (it would hang in the air as haze for the rest of its 20 s).
		if (State.Presence <= 0.0f && WasPresent > 0.0f && State.bActivated)
		{
			Effect->DeactivateImmediate();
			State.bActivated = false;
		}
	}

	if (!bFalling)
	{
		State.DropRate = 0.0f;
		State.SplashRate = 0.0f;
		if (Effect)
		{
			Effect->SetVariableFloat(DropRate, 0.0f);
			Effect->SetVariableFloat(SplashRate, 0.0f);
			State.Idle += FMath::Max(DeltaTime, 0.0f);
			if (State.Idle > IdleSecondsBeforeDeactivate && State.bActivated)
			{
				Effect->Deactivate();
				State.bActivated = false;
			}
		}
		return;
	}
	State.Idle = 0.0f;

	// Downwind, but never flatter than MaxSlant: a gale would otherwise send snow sideways for hundreds of metres.
	FVector Drift = Wind * double(Layer.WindDrift);
	const double MaxSideways = double(Layer.MaxSlant) * double(Layer.FallSpeed);
	if (Drift.SizeSquared2D() > FMath::Square(MaxSideways))
	{
		Drift = Drift.GetSafeNormal2D() * MaxSideways;
	}
	State.FallVelocity = FVector(Drift.X, Drift.Y, -double(Layer.FallSpeed));
	FDreamWeatherFallPaths::FParams Params;
	Params.Capacity = S->PathCount;
	Params.RefreshSeconds = S->PathRefreshSeconds;
	Params.Radius = S->PathRadius;
	Params.ForwardBias = S->PathForwardBias;
	Params.StartHeight = S->PathStartHeight;
	Params.MaxLength = S->PathMaxLength;
	Params.Channel = S->PathChannel;
	if (Index == DustLayer)
	{
		// Blown along the ground: short, low paths (a dust path starts some 70 m upwind, a few metres up).
		Params.StartHeight = FMath::Min(Params.StartHeight, 600.0f);
		Params.MaxLength = FMath::Min(Params.MaxLength, 1200.0f);
	}
	auto Covered = [&Weather](const FVector& P) { return 1.0f - Weather.GetExposureAt(P); };

	// A view that jumped far (a teleport, a cut) gets a whole new buffer at once; otherwise the buffer turns over
	// every PathRefreshSeconds and follows a walking view.
	UWorld* World = GetWorld();
	if (State.Paths.GetEnds().Num() != Params.Capacity || FVector::DistSquared2D(ViewLocation, State.PathOrigin) > FMath::Square(double(Params.Radius)))
	{
		State.Paths.Fill(World, ViewLocation, Forward, State.FallVelocity, Params, Covered);
		State.PathOrigin = ViewLocation;
		State.SinceUpload = S->UploadInterval;
		State.bFillPending = true;
	}
	else
	{
		State.Paths.Update(World, ViewLocation, Forward, State.FallVelocity, DeltaTime, Params, Covered);
		State.PathOrigin = FMath::Lerp(State.PathOrigin, ViewLocation, double(FMath::Clamp(DeltaTime / FMath::Max(Params.RefreshSeconds, 0.05f), 0.0f, 1.0f)));
	}

	const int32 Count = State.Paths.GetEnds().Num();
	const float Landed = Count > 0 ? float(State.Paths.CountLanded()) / float(Count) : 0.0f;
	// (As many as the quality's particle density allows.)
	const float Density = FMath::Max(DreamWeatherFX::CVarParticleDensity.GetValueOnGameThread(), 0.0f);
	State.DropRate = Layer.DropsPerSecond * Strength * FMath::Clamp(DropShare, 0.0f, 1.0f) * Density;
	State.SplashRate = Layer.SplashesPerSecond * Strength * Landed * Density;

	Effect = EnsureEffect(Index, Layer, ViewLocation);
	if (!Effect)
	{
		return;
	}
	// The effect rides along with the view (its bounds are around it); the particles live in world space.
	if (!Effect->GetComponentLocation().Equals(ViewLocation, 1.0))
	{
		Effect->SetWorldLocation(ViewLocation);
	}
	State.SinceUpload += FMath::Max(DeltaTime, 0.0f);
	if (State.Paths.GetGeneration() != State.UploadedGeneration && State.SinceUpload >= S->UploadInterval)
	{
		UNiagaraDataInterfaceArrayFunctionLibrary::SetNiagaraArrayVector4(Effect, Paths, State.Paths.GetEnds());
		UNiagaraDataInterfaceArrayFunctionLibrary::SetNiagaraArrayVector(Effect, Starts, State.Paths.GetStarts());
		UNiagaraDataInterfaceArrayFunctionLibrary::SetNiagaraArrayVector(Effect, Normals, State.Paths.GetNormals());
		State.UploadedGeneration = State.Paths.GetGeneration();
		State.SinceUpload = 0.0f;
	}
	// Once: an effect whose system is still compiling reports itself inactive until it can run, and asking
	// again every frame would restart it every frame.
	if (!State.bActivated)
	{
		State.bActivated = true;
		State.bFillPending = true;
		Effect->Activate(true);
		UE_LOG(LogDreamWeatherFX, Display, TEXT("%s starts: %d paths (%.0f%% landed), %.0f drops/s, %.0f splashes/s, falling (%.0f, %.0f, %.0f) cm/s"),
			Index == RainLayer ? TEXT("Rain") : Index == SnowLayer ? TEXT("Snow") : TEXT("Dust"), Count, Landed * 100.0f, State.DropRate, State.SplashRate,
			State.FallVelocity.X, State.FallVelocity.Y, State.FallVelocity.Z);
	}

	// Fill the air at once when the effect starts or the view jumps: one frame spawns the drops steady rain keeps
	// in the air, each somewhere along its path. Without it, snow (some twenty seconds to come down) would take
	// that long to reach the view. After that frame, drops start at the top of their paths.
	float SpawnRate = State.DropRate;
	float SpawnAnywhere = 0.0f;
	if (State.bFillPending && Effect->IsActive() && DeltaTime > 0.0f && Count > 0)
	{
		State.bFillPending = false;
		double FallSeconds = 0.0;
		for (const FVector4& End : State.Paths.GetEnds())
		{
			FallSeconds += End.W;
		}
		const float InTheAir = State.DropRate * float(FallSeconds / double(Count));
		SpawnRate += InTheAir / DeltaTime;
		SpawnAnywhere = InTheAir / FMath::Max(InTheAir + State.DropRate * DeltaTime, 1.0f);
		State.FillCount = InTheAir;
		UE_LOG(LogDreamWeatherFX, Display, TEXT("%s fills the air: %.0f drops (%.1f s average fall)"),
			Index == RainLayer ? TEXT("Rain") : Index == SnowLayer ? TEXT("Snow") : TEXT("Dust"), InTheAir, float(FallSeconds / double(Count)));
	}
	Effect->SetVariableFloat(DropRate, SpawnRate);
	Effect->SetVariableFloat(SplashRate, State.SplashRate);
	Effect->SetVariableFloat(Anywhere, SpawnAnywhere);
	if (Index == DustLayer)
	{
		Effect->SetVariableFloat(Presence, State.Presence);
	}
}

float UDreamWeatherFXSubsystem::SeasonDebris(const FDateTime& Date, bool bSouthernHemisphere, FLinearColor& OutColor)
{
	const UDreamWeatherFXSettings* S = UDreamWeatherFXSettings::Get();
	// Quarters of the year from 1 March, each season whole in its middle month, blending over the month at a turn.
	const int32 Year = Date.GetMonth() >= 3 ? Date.GetYear() : Date.GetYear() - 1;
	const FDateTime March(Year, 3, 1);
	const double Quarter = (Date - March).GetTotalDays() / (FDateTime(Year + 1, 3, 1) - March).GetTotalDays() * 4.0 + (bSouthernHemisphere ? 2.0 : 0.0);
	const float Amount[4] = { S->DebrisSeasonAmount.X, S->DebrisSeasonAmount.Y, S->DebrisSeasonAmount.Z, S->DebrisSeasonAmount.W };
	const FLinearColor Colors[4] = { S->DebrisSpring, S->DebrisSummer, S->DebrisAutumn, S->DebrisWinter };
	float Weights[4];
	float Sum = 0.0f;
	for (int32 i = 0; i < 4; ++i)
	{
		double Q = FMath::Fmod(Quarter - double(i) + 8.0, 4.0);
		Q = Q > 2.0 ? Q - 4.0 : Q;
		Weights[i] = float(FMath::Clamp((FMath::Min(Q, 1.0 - Q) + 1.0 / 6.0) * 3.0, 0.0, 1.0));
		Sum += Weights[i];
	}
	float Out = 0.0f;
	OutColor = FLinearColor::Black;
	for (int32 i = 0; i < 4; ++i)
	{
		const float W = Sum > 0.0f ? Weights[i] / Sum : 0.25f;
		Out += Amount[i] * W;
		OutColor += Colors[i] * W;
	}
	return FMath::Max(Out, 0.0f);
}

void UDreamWeatherFXSubsystem::UpdateDebris(UDreamWorldSubsystem& Clock, float Strength, const FVector& ViewLocation)
{
	using namespace DreamWeatherFX;
	const UDreamWeatherFXSettings* S = UDreamWeatherFXSettings::Get();
	const FLayerState& Blown = States[DustLayer];
	DebrisRate = S->DebrisPerSecond * FMath::Max(Strength, 0.0f) * FMath::Max(DreamWeatherFX::CVarParticleDensity.GetValueOnGameThread(), 0.0f);
	if (DebrisRate <= 0.0f && !DebrisEffect)
	{
		return;
	}
	UWorld* World = GetWorld();
	if (!DebrisEffect && World)
	{
		UNiagaraSystem* System = Cast<UNiagaraSystem>(S->WindDebris.TryLoad());
		if (!System)
		{
			return;
		}
		UObject* Outer = World->GetWorldSettings() ? static_cast<UObject*>(World->GetWorldSettings()) : static_cast<UObject*>(World);
		DebrisEffect = NewObject<UNiagaraComponent>(Outer, NAME_None, RF_Transient | RF_TextExportTransient | RF_DuplicateTransient);
		DebrisEffect->SetAsset(System);
		DebrisEffect->SetAutoActivate(false);
		DebrisEffect->SetAutoDestroy(false);
		DebrisEffect->SetUsingAbsoluteLocation(true);
		DebrisEffect->SetUsingAbsoluteRotation(true);
		DebrisEffect->SetWorldLocation(ViewLocation);
		DebrisEffect->RegisterComponentWithWorld(World);
		// Around the view only: the demo spawn box is off.
		DebrisEffect->SetVariableFloat(TEXT("User.SpawnRate"), 0.0f);
		DebrisUploaded = 0;
	}
	if (!DebrisEffect)
	{
		return;
	}
	if (!DebrisEffect->GetComponentLocation().Equals(ViewLocation, 1.0))
	{
		DebrisEffect->SetWorldLocation(ViewLocation);
	}
	if (Blown.Paths.GetGeneration() != DebrisUploaded && Blown.Paths.GetEnds().Num() > 0)
	{
		UNiagaraDataInterfaceArrayFunctionLibrary::SetNiagaraArrayVector4(DebrisEffect, Paths, Blown.Paths.GetEnds());
		UNiagaraDataInterfaceArrayFunctionLibrary::SetNiagaraArrayVector(DebrisEffect, Starts, Blown.Paths.GetStarts());
		UNiagaraDataInterfaceArrayFunctionLibrary::SetNiagaraArrayVector(DebrisEffect, Normals, Blown.Paths.GetNormals());
		DebrisUploaded = Blown.Paths.GetGeneration();
	}
	FLinearColor Color;
	SeasonDebris(Clock.GetDateTime(), Clock.GetCelestialSettings().Latitude < 0.0, Color);
	DebrisEffect->SetVariableFloat(TEXT("User.DebrisRate"), DebrisRate);
	DebrisEffect->SetVariableLinearColor(TEXT("User.DebrisColor"), Color);
	if (DebrisRate > 0.0f && !DebrisEffect->IsActive())
	{
		DebrisEffect->Activate(true);
	}
}
