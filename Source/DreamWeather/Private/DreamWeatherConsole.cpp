// Copyright DreamMoon. All Rights Reserved.

#include "DreamWeatherChannels.h"
#include "DreamWeatherModule.h"
#include "DreamWeatherPreset.h"
#include "DreamWeatherSettings.h"
#include "DreamWeatherSubsystem.h"

#include "DreamWorldSubsystem.h"

#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "HAL/IConsoleManager.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "TimerManager.h"
#include "UObject/Package.h"

namespace DreamWeather::Console
{
	static UDreamWeatherSubsystem* Find(UWorld* World)
	{
		UDreamWeatherSubsystem* Weather = World ? World->GetSubsystem<UDreamWeatherSubsystem>() : nullptr;
		if (!Weather)
		{
			UE_LOG(LogDreamWeather, Warning, TEXT("No Dream Weather in this world."));
		}
		return Weather;
	}

	/** A preset by object path, or by name among the plugin's presets. */
	static UDreamWeatherPreset* FindPreset(const FString& Name)
	{
		const FString Path = Name.StartsWith(TEXT("/")) ? Name : FString::Printf(TEXT("/DreamDynamicWorld/Weather/Presets/%s.%s"), *Name, *Name);
		return LoadObject<UDreamWeatherPreset>(nullptr, *Path);
	}

	static FAutoConsoleCommandWithWorldAndArgs SetCommand(
		TEXT("DreamWeather.Set"),
		TEXT("DreamWeather.Set <preset> [seconds]: force a weather (WP_Rain, or an object path) until DreamWeather.Clear."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
		{
			UDreamWeatherSubsystem* Weather = Find(World);
			UDreamWeatherPreset* Preset = Args.Num() > 0 ? FindPreset(Args[0]) : nullptr;
			if (Weather && Preset)
			{
				Weather->ForceWeather(Preset, Args.Num() > 1 ? FCString::Atof(*Args[1]) : 5.0f);
			}
			else if (Args.Num() > 0 && !Preset)
			{
				UE_LOG(LogDreamWeather, Warning, TEXT("No weather preset '%s'."), *Args[0]);
			}
		}));

	/** One scripted strike this far in front of the view (cm), or above the origin without a view. */
	static void StrikeAhead(UWorld* World, double Distance)
	{
		UDreamWeatherSubsystem* Weather = World ? World->GetSubsystem<UDreamWeatherSubsystem>() : nullptr;
		const UDreamWorldSubsystem* Clock = World ? World->GetSubsystem<UDreamWorldSubsystem>() : nullptr;
		if (!Weather)
		{
			return;
		}
		FVector View = FVector::ZeroVector;
		FVector Ahead = FVector::ForwardVector;
		if (Clock && Clock->HasViewLocation())
		{
			View = Clock->GetViewLocation();
			Ahead = Clock->HasViewRotation() ? FRotator(0.0, Clock->GetViewRotation().Yaw, 0.0).Vector() : Ahead;
		}
		Weather->StrikeLightning(FVector(View.X, View.Y, 0.0) + Ahead * Distance, 1.0f);
	}

	static FAutoConsoleCommandWithWorldAndArgs StrikeCommand(
		TEXT("DreamWeather.Strike"),
		TEXT("DreamWeather.Strike [metres ahead=2000] [count=1] [interval seconds=0.8]: scripted lightning in front of the view, whatever the weather."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
		{
			if (!Find(World))
			{
				return;
			}
			const double Distance = (Args.Num() > 0 ? FCString::Atod(*Args[0]) : 2000.0) * 100.0;
			const int32 Count = FMath::Max(1, Args.Num() > 1 ? FCString::Atoi(*Args[1]) : 1);
			const float Interval = FMath::Max(0.05f, Args.Num() > 2 ? FCString::Atof(*Args[2]) : 0.8f);
			StrikeAhead(World, Distance);
			if (Count > 1)
			{
				// The rest on a timer (game worlds), one every Interval real seconds.
				TSharedRef<int32> Left = MakeShared<int32>(Count - 1);
				TSharedRef<FTimerHandle> Handle = MakeShared<FTimerHandle>();
				TWeakObjectPtr<UWorld> WeakWorld(World);
				World->GetTimerManager().SetTimer(*Handle, FTimerDelegate::CreateLambda([WeakWorld, Distance, Left, Handle]
				{
					UWorld* W = WeakWorld.Get();
					if (!W)
					{
						return;
					}
					StrikeAhead(W, Distance);
					if (--(*Left) <= 0)
					{
						W->GetTimerManager().ClearTimer(*Handle);
					}
				}), Interval, true);
			}
		}));

	static FAutoConsoleCommandWithWorldAndArgs ClearCommand(
		TEXT("DreamWeather.Clear"),
		TEXT("DreamWeather.Clear [seconds]: drop the forced weather."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
		{
			if (UDreamWeatherSubsystem* Weather = Find(World))
			{
				Weather->ClearForcedWeather(Args.Num() > 0 ? FCString::Atof(*Args[0]) : 5.0f);
			}
		}));

	static FAutoConsoleCommandWithWorldAndArgs ForecastCommand(
		TEXT("DreamWeather.Forecast"),
		TEXT("DreamWeather.Forecast [hours] [step]: print the scheduled weather ahead."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
		{
			if (UDreamWeatherSubsystem* Weather = Find(World))
			{
				const float Hours = Args.Num() > 0 ? FCString::Atof(*Args[0]) : 24.0f;
				const float Step = Args.Num() > 1 ? FCString::Atof(*Args[1]) : 3.0f;
				for (const FDreamWeatherForecastEntry& E : Weather->GetForecast(Hours, Step))
				{
					UE_LOG(LogDreamWeather, Display, TEXT("  %s  %s"), *E.Time.ToString(TEXT("%Y-%m-%d %H:%M")), E.Preset ? *E.Preset->GetDisplayNameOrName().ToString() : TEXT("-"));
				}
			}
		}));

	static FAutoConsoleCommandWithWorldAndArgs DumpCommand(
		TEXT("DreamWeather.Dump"),
		TEXT("DreamWeather.Dump: print the weather at the view, every channel."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
		{
			if (UDreamWeatherSubsystem* Weather = Find(World))
			{
				const FDreamWeatherSample& S = Weather->GetViewSample();
				const TArray<FDreamWeatherChannelDef>& Defs = FDreamWeatherChannels::Get();
				UE_LOG(LogDreamWeather, Display, TEXT("Weather %s (%s), %.1f C, %d modifiers, tags %s"),
					Weather->IsActive() ? TEXT("on") : TEXT("off"),
					Weather->GetCurrentWeather() ? *Weather->GetCurrentWeather()->GetName() : TEXT("-"), S.Temperature, Weather->GetNumModifiers(), *S.Tags.ToStringSimple());
				for (int32 i = 0; i < Defs.Num(); ++i)
				{
					UE_LOG(LogDreamWeather, Display, TEXT("  %-40s %.3f"), *Defs[i].Channel.ToString(), S.Values.Get(i));
				}
			}
		}));

	static const FName EdgeMarkerTag(TEXT("DreamWeatherEdgeMarker"));

	/** A channel by the last part of its tag (SnowRatio, Precipitation...), any case. */
	static int32 FindChannel(const FString& Name)
	{
		const TArray<FDreamWeatherChannelDef>& Defs = FDreamWeatherChannels::Get();
		for (int32 i = 0; i < Defs.Num(); ++i)
		{
			const FString Tag = Defs[i].Channel.ToString();
			if (Tag.Equals(Name, ESearchCase::IgnoreCase) || Tag.EndsWith(TEXT(".") + Name, ESearchCase::IgnoreCase))
			{
				return i;
			}
		}
		return INDEX_NONE;
	}

	static FAutoConsoleCommandWithWorldAndArgs MarkEdgeCommand(
		TEXT("DreamWeather.MarkEdge"),
		TEXT("DreamWeather.MarkEdge [channel=SnowRatio] [radius m=60] [step m=1] [level=0.5] [post height m=1.5] | off: posts on the ground where")
		TEXT(" the point queries (SampleWeather) cross a level of a channel around the view, to see the zones' edges as gameplay reads them beside")
		TEXT(" what the sky and the ground show. The channel Air is the air's temperature at the base height (degrees), not the Temperature")
		TEXT(" channel's offset: where the ground's snow settles or melts."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
		{
			UDreamWeatherSubsystem* Weather = Find(World);
			if (!Weather)
			{
				return;
			}
			for (TActorIterator<AStaticMeshActor> It(World); It; ++It)
			{
				if (It->Tags.Contains(EdgeMarkerTag))
				{
					It->Destroy();
				}
			}
			if (Args.Num() > 0 && Args[0].Equals(TEXT("off"), ESearchCase::IgnoreCase))
			{
				return;
			}
			const bool bAir = Args.Num() > 0 && Args[0].Equals(TEXT("Air"), ESearchCase::IgnoreCase);
			const int32 Channel = bAir ? INDEX_NONE : FindChannel(Args.Num() > 0 ? Args[0] : TEXT("SnowRatio"));
			if (!bAir && Channel == INDEX_NONE)
			{
				UE_LOG(LogDreamWeather, Warning, TEXT("No weather channel '%s'."), *Args[0]);
				return;
			}
			const double Radius = FMath::Clamp(Args.Num() > 1 ? FCString::Atod(*Args[1]) : 60.0, 1.0, 5000.0) * 100.0;
			const double Step = FMath::Max(Args.Num() > 2 ? FCString::Atod(*Args[2]) : 1.0, 0.1) * 100.0;
			const float Level = Args.Num() > 3 ? FCString::Atof(*Args[3]) : 0.5f;
			const double Height = FMath::Max(Args.Num() > 4 ? FCString::Atod(*Args[4]) : 1.5, 0.1) * 100.0;
			const UDreamWorldSubsystem* Clock = World->GetSubsystem<UDreamWorldSubsystem>();
			const FVector View = Clock && Clock->HasViewLocation() ? Clock->GetViewLocation() : FVector::ZeroVector;
			const int32 N = FMath::Min(FMath::CeilToInt32(Radius / Step), 400);
			const int32 Side = 2 * N + 1;
			TArray<float> Values;
			Values.SetNumUninitialized(Side * Side);
			for (int32 y = 0; y < Side; ++y)
			{
				for (int32 x = 0; x < Side; ++x)
				{
					const FVector P(View.X + (x - N) * Step, View.Y + (y - N) * Step, bAir ? UDreamWeatherSettings::Get()->BaseHeight : View.Z);
					const FDreamWeatherSample Sample = Weather->SampleWeather(P);
					Values[y * Side + x] = bAir ? Sample.Temperature : Sample.Values.Get(Channel);
				}
			}
			UStaticMesh* Post = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
			UMaterialInterface* Base = LoadObject<UMaterialInterface>(nullptr, TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));
			UMaterialInstanceDynamic* Red = Base ? UMaterialInstanceDynamic::Create(Base, GetTransientPackage()) : nullptr;
			if (Red)
			{
				Red->SetVectorParameterValue(TEXT("Color"), FLinearColor(0.9f, 0.05f, 0.03f));
			}
			int32 Count = 0;
			auto Mark = [&](const FVector2D& At)
			{
				// On the ground under the crossing (at the view's height less a person's where there is none).
				FHitResult Hit;
				const FVector Top(At.X, At.Y, View.Z + 100000.0);
				const bool bGround = World->LineTraceSingleByChannel(Hit, Top, Top - FVector(0.0, 0.0, 300000.0), ECC_Visibility);
				const double Z = bGround ? Hit.ImpactPoint.Z : View.Z - 170.0;
				FActorSpawnParameters Params;
				Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
				Params.ObjectFlags |= RF_Transient;
				AStaticMeshActor* Actor = World->SpawnActor<AStaticMeshActor>(FVector(At.X, At.Y, Z + Height * 0.5), FRotator::ZeroRotator, Params);
				if (!Actor)
				{
					return;
				}
				Actor->Tags.Add(EdgeMarkerTag);
				Actor->SetMobility(EComponentMobility::Movable);
				UStaticMeshComponent* Mesh = Actor->GetStaticMeshComponent();
				Mesh->SetStaticMesh(Post);
				Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
				Mesh->SetCastShadow(false);
				// A tenth of its height across (15 cm at least), to be seen from as far as it is tall.
				const double Width = FMath::Max(0.15, Height / 1000.0);
				Mesh->SetWorldScale3D(FVector(Width, Width, Height / 100.0));
				if (Red)
				{
					Mesh->SetMaterial(0, Red);
				}
				++Count;
			};
			// A post where the value crosses the level between two neighbouring samples, placed where it crosses.
			for (int32 y = 0; y < Side; ++y)
			{
				for (int32 x = 0; x < Side; ++x)
				{
					const float A = Values[y * Side + x];
					const FVector2D PA(View.X + (x - N) * Step, View.Y + (y - N) * Step);
					if (x + 1 < Side && (A - Level) * (Values[y * Side + x + 1] - Level) < 0.0f)
					{
						const float B = Values[y * Side + x + 1];
						Mark(PA + FVector2D(Step * (Level - A) / (B - A), 0.0));
					}
					if (y + 1 < Side && (A - Level) * (Values[(y + 1) * Side + x] - Level) < 0.0f)
					{
						const float B = Values[(y + 1) * Side + x];
						Mark(PA + FVector2D(0.0, Step * (Level - A) / (B - A)));
					}
				}
			}
			UE_LOG(LogDreamWeather, Display, TEXT("MarkEdge: %d posts where %s crosses %.2f within %.0f m of the view."), Count,
				bAir ? TEXT("the air temperature") : *FDreamWeatherChannels::Get()[Channel].Channel.ToString(), Level, Radius / 100.0);
		}));
}
