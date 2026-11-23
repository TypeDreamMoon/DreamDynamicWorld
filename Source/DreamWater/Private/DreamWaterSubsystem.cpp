// Copyright DreamMoon. All Rights Reserved.

#include "DreamWaterSubsystem.h"

#include "DreamWeatherSubsystem.h"
#include "DreamWorldSubsystem.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "HAL/IConsoleManager.h"
#include "JsonObjectConverter.h"

namespace DreamWater::Subsystem
{
	const FName SnapshotKey(TEXT("DreamWater"));
	constexpr float StepHours = 0.25f;
	constexpr float MaxSettleHours = 48.0f;
}

UDreamWaterSubsystem* UDreamWaterSubsystem::Get(const UObject* WorldContext)
{
	const UWorld* World = WorldContext ? WorldContext->GetWorld() : nullptr;
	return World ? World->GetSubsystem<UDreamWaterSubsystem>() : nullptr;
}

bool UDreamWaterSubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
	if (!Super::ShouldCreateSubsystem(Outer))
	{
		return false;
	}
	const UWorld* World = Cast<UWorld>(Outer);
	return World && (World->WorldType == EWorldType::Game || World->WorldType == EWorldType::PIE || World->WorldType == EWorldType::Editor);
}

void UDreamWaterSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	if (UDreamWorldSubsystem* Clock = Collection.InitializeDependency<UDreamWorldSubsystem>())
	{
		ClockSubsystem = Clock;
		CaptureHandle = Clock->OnCaptureSnapshot.AddWeakLambda(this, [this](FDreamWorldSnapshot& Snapshot) { CaptureSnapshot(Snapshot); });
		ApplyHandle = Clock->OnApplySnapshot.AddWeakLambda(this, [this](const FDreamWorldSnapshot& Snapshot) { ApplySnapshot(Snapshot); });
	}
	if (UDreamWeatherSubsystem* Weather = Collection.InitializeDependency<UDreamWeatherSubsystem>())
	{
		WeatherSubsystem = Weather;
		FrameHandle = Weather->OnWeatherFrame.AddUObject(this, &UDreamWaterSubsystem::HandleWeatherFrame);
	}
}

void UDreamWaterSubsystem::Deinitialize()
{
	if (UDreamWorldSubsystem* Clock = ClockSubsystem.Get())
	{
		Clock->OnCaptureSnapshot.Remove(CaptureHandle);
		Clock->OnApplySnapshot.Remove(ApplyHandle);
	}
	if (UDreamWeatherSubsystem* Weather = WeatherSubsystem.Get())
	{
		Weather->OnWeatherFrame.Remove(FrameHandle);
	}
	Bodies.Reset();
	Super::Deinitialize();
}

void UDreamWaterSubsystem::RegisterBody(UDreamWaterBodyComponent* Body)
{
	Bodies.RemoveAll([](const TWeakObjectPtr<UDreamWaterBodyComponent>& B) { return !B.IsValid(); });
	if (Body)
	{
		Bodies.AddUnique(Body);
	}
}

void UDreamWaterSubsystem::UnregisterBody(UDreamWaterBodyComponent* Body)
{
	Bodies.RemoveAll([Body](const TWeakObjectPtr<UDreamWaterBodyComponent>& B) { return !B.IsValid() || B.Get() == Body; });
}

TArray<UDreamWaterBodyComponent*> UDreamWaterSubsystem::GetBodies() const
{
	TArray<UDreamWaterBodyComponent*> Out;
	for (const TWeakObjectPtr<UDreamWaterBodyComponent>& B : Bodies)
	{
		if (UDreamWaterBodyComponent* Body = B.Get())
		{
			Out.Add(Body);
		}
	}
	return Out;
}

UDreamWaterBodyComponent* UDreamWaterSubsystem::FindWaterAt(FVector Position) const
{
	UDreamWaterBodyComponent* Best = nullptr;
	double BestDistance = TNumericLimits<double>::Max();
	for (UDreamWaterBodyComponent* Body : GetBodies())
	{
		if (Body->Covers(Position))
		{
			const double Distance = FMath::Abs(Position.Z - Body->GetSurfaceHeight());
			if (Distance < BestDistance)
			{
				BestDistance = Distance;
				Best = Body;
			}
		}
	}
	return Best;
}

FDreamWaterWeather UDreamWaterSubsystem::WeatherAt(const UDreamWaterBodyComponent& Body) const
{
	FDreamWaterWeather Out;
	const UDreamWeatherSubsystem* Weather = WeatherSubsystem.Get();
	if (Weather && Weather->IsActive())
	{
		const FDreamWeatherSample Sample = Weather->SampleWeather(Body.GetComponentLocation());
		Out.Rain = Sample.Rain;
		Out.Temperature = Sample.Temperature;
		Out.Cloud = Sample.CloudCoverage;
	}
	return Out;
}

void UDreamWaterSubsystem::AdvanceAll(float Hours)
{
	for (UDreamWaterBodyComponent* Body : GetBodies())
	{
		const FDreamWaterWeather Weather = WeatherAt(*Body);
		for (float Left = Hours; Left > 1.0e-6f; Left -= DreamWater::Subsystem::StepHours)
		{
			Body->Advance(Weather, FMath::Min(Left, DreamWater::Subsystem::StepHours));
		}
	}
}

void UDreamWaterSubsystem::HandleWeatherFrame(UDreamWorldSubsystem& Clock, UDreamWeatherSubsystem& Weather, const FDreamWeatherSample& View, float DeltaTime)
{
	const double Now = Clock.GetWorldSeconds();
	if (LastWorldSeconds < 0.0)
	{
		LastWorldSeconds = Now;
		LastJump = Clock.GetTimeJumpSerial();
	}
	// A jump of the clock forward settles the levels over the hours jumped (two days at most, in the weather as it is
	// now); back, they stay.
	const bool bJumped = Clock.GetTimeJumpSerial() != LastJump;
	const double Hours = (Now - LastWorldSeconds) / 3600.0;
	LastJump = Clock.GetTimeJumpSerial();
	LastWorldSeconds = Now;
	if (Hours > 0.0)
	{
		AdvanceAll(float(FMath::Min(Hours, bJumped ? double(DreamWater::Subsystem::MaxSettleHours) : 1.0)));
	}
	UpdateView(Clock);
}

void UDreamWaterSubsystem::UpdateView(UDreamWorldSubsystem& Clock)
{
	FDreamViewWater View;
	if (Clock.HasViewLocation())
	{
		const FVector Eye = Clock.GetViewLocation();
		if (UDreamWaterBodyComponent* Body = FindWaterAt(Eye))
		{
			View.Body = Body;
			View.SurfaceHeight = Body->GetSurfaceHeight();
			View.Turbidity = Body->GetTurbidity();
			View.Depth = Body->GetDepthBelowSurface(Eye);
			View.LevelOffset = Body->GetLevelOffset();
		}
	}
	ViewWater = View;
	Clock.SetVectorParameter(DreamWater::Names::MPC_Water, FLinearColor(View.SurfaceHeight, View.Turbidity, View.Depth, View.LevelOffset));
}

FString UDreamWaterSubsystem::KeyOf(const UDreamWaterBodyComponent& Body)
{
	// The actor and the component: the same body in the same level after a load.
	const AActor* Owner = Body.GetOwner();
	return FString::Printf(TEXT("%s.%s"), Owner ? *Owner->GetFName().ToString() : TEXT("-"), *Body.GetFName().ToString());
}

void UDreamWaterSubsystem::CaptureSnapshot(FDreamWorldSnapshot& Snapshot) const
{
	FDreamWaterSnapshotData Data;
	for (const UDreamWaterBodyComponent* Body : GetBodies())
	{
		Data.Bodies.Add(KeyOf(*Body), Body->GetState());
	}
	FString Json;
	if (FJsonObjectConverter::UStructToJsonObjectString(Data, Json))
	{
		Snapshot.Extensions.Add(DreamWater::Subsystem::SnapshotKey, Json);
	}
}

void UDreamWaterSubsystem::ApplySnapshot(const FDreamWorldSnapshot& Snapshot)
{
	const FString* Json = Snapshot.Extensions.Find(DreamWater::Subsystem::SnapshotKey);
	FDreamWaterSnapshotData Data;
	if (!Json || !FJsonObjectConverter::JsonObjectStringToUStruct(*Json, &Data))
	{
		return;
	}
	for (UDreamWaterBodyComponent* Body : GetBodies())
	{
		if (const FDreamWaterState* State = Data.Bodies.Find(KeyOf(*Body)))
		{
			Body->SetState(*State);
		}
	}
	// The snapshot's time is the clock's now: no settling over the jump it made.
	if (const UDreamWorldSubsystem* Clock = ClockSubsystem.Get())
	{
		LastWorldSeconds = Clock->GetWorldSeconds();
		LastJump = Clock->GetTimeJumpSerial();
	}
}

namespace DreamWater::Console
{
	static FAutoConsoleCommandWithWorldAndArgs Command(
		TEXT("DreamWater.Bodies"),
		TEXT("DreamWater.Bodies [offset cm [turbidity] | hold | release]: list the water bodies (their level against the usual ")
		TEXT("one, band, turbidity, the rain over them); set every one's level and turbidity, or hold / release them."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
		{
			UDreamWaterSubsystem* Water = UDreamWaterSubsystem::Get(World);
			if (!Water)
			{
				return;
			}
			for (UDreamWaterBodyComponent* Body : Water->GetBodies())
			{
				if (Args.Num() >= 1 && Args[0].Equals(TEXT("hold"), ESearchCase::IgnoreCase))
				{
					Body->bHold = true;
				}
				else if (Args.Num() >= 1 && Args[0].Equals(TEXT("release"), ESearchCase::IgnoreCase))
				{
					Body->bHold = false;
				}
				else if (Args.Num() >= 1)
				{
					FDreamWaterState State = Body->GetState();
					State.LevelOffset = FCString::Atof(*Args[0]);
					State.Turbidity = Args.Num() >= 2 ? FCString::Atof(*Args[1]) : State.Turbidity;
					Body->SetState(State);
				}
				static const TCHAR* BandNames[] = { TEXT("low"), TEXT("normal"), TEXT("high"), TEXT("overflow") };
				const FDreamWaterWeather Weather = Water->WeatherAt(*Body);
				UE_LOG(LogTemp, Display, TEXT("%s: level %+.1f cm (%s), surface %.0f, turbidity %.2f; rain %.2f, %.1f C%s"),
					*Body->GetOwner()->GetActorNameOrLabel(), Body->GetLevelOffset(), BandNames[uint8(Body->GetBand())], Body->GetSurfaceHeight(),
					Body->GetTurbidity(), Weather.Rain, Weather.Temperature, Body->bHold ? TEXT(" (held)") : TEXT(""));
			}
		}));
}
