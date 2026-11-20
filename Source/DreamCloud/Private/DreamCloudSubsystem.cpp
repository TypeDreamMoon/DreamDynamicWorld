// Copyright DreamMoon. All Rights Reserved.

#include "DreamCloudSubsystem.h"
#include "DreamCloudCarverComponent.h"
#include "DreamCloudModule.h"
#include "DreamCloudVolumeComponent.h"

#include "Engine/World.h"
#include "HAL/IConsoleManager.h"
#include "Materials/MaterialParameterCollection.h"
#include "Materials/MaterialParameterCollectionInstance.h"

namespace DreamCloud
{
	static TAutoConsoleVariable<int32> CVarUseLightMap(
		TEXT("DreamCloud.UseLightMap"),
		1,
		TEXT("1: cloud volumes read their light map (drawn by a compute shader); 0: they march a few taps toward the light from every sample instead (to compare); 2, 3, 4: debug views (the march's segment; the toon surface's hit in the box; the toon march's steps used, whether it reached the segment's end, whether it hit)."),
		ECVF_Default);

	bool UseLightMap()
	{
		return CVarUseLightMap.GetValueOnGameThread() == 1;
	}

	int32 DebugView()
	{
		const int32 Value = CVarUseLightMap.GetValueOnGameThread();
		return Value >= 2 ? Value : 0;
	}

	static TAutoConsoleVariable<int32> CVarQuality(
		TEXT("DreamCloud.Quality"),
		2,
		TEXT("Cloud volume quality: 0 low (steps twice as long, a coarse light map), 1 medium, 2 high (default), 3 epic (finer steps, a finer light map)."),
		ECVF_Scalability);

	int32 GetQualityLevel()
	{
		return FMath::Clamp(CVarQuality.GetValueOnGameThread(), 0, 3);
	}

	FQuality GetQuality()
	{
		static const FQuality Levels[] = {
			{ 2.0f, 16, 24 },
			{ 1.4f, 24, 32 },
			{ 1.0f, 32, 48 },
			{ 0.7f, 48, 64 },
		};
		return Levels[GetQualityLevel()];
	}

	static const FName MainLightName(TEXT("DDW_MainLight"));
	static const TCHAR* CollectionPath = TEXT("/DreamDynamicWorld/MPC_DreamDynamicWorld.MPC_DreamDynamicWorld");
}

void UDreamCloudSubsystem::RegisterVolume(UDreamCloudVolumeComponent* Volume)
{
	Volumes.AddUnique(Volume);
}

void UDreamCloudSubsystem::UnregisterVolume(UDreamCloudVolumeComponent* Volume)
{
	Volumes.Remove(Volume);
}

void UDreamCloudSubsystem::RegisterCarver(UDreamCloudCarverComponent* Carver)
{
	Carvers.AddUnique(Carver);
}

void UDreamCloudSubsystem::UnregisterCarver(UDreamCloudCarverComponent* Carver)
{
	Carvers.Remove(Carver);
}

FVector UDreamCloudSubsystem::GetLightDirection() const
{
	if (!LightOverride.IsNearlyZero())
	{
		return LightOverride;
	}
	if (const UWorld* World = GetWorld())
	{
		if (Collection)
		{
			if (const UMaterialParameterCollectionInstance* Instance = World->GetParameterCollectionInstance(Collection))
			{
				FLinearColor Light;
				if (Instance->GetVectorParameterValue(DreamCloud::MainLightName, Light))
				{
					const FVector Direction(Light.R, Light.G, Light.B);
					if (!Direction.IsNearlyZero())
					{
						return Direction.GetSafeNormal();
					}
				}
			}
		}
	}
	return FVector::UpVector;
}

void UDreamCloudSubsystem::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}
	if (!Collection)
	{
		Collection = LoadObject<UMaterialParameterCollection>(nullptr, DreamCloud::CollectionPath);
	}
	Volumes.RemoveAll([](const TWeakObjectPtr<UDreamCloudVolumeComponent>& V) { return !V.IsValid(); });
	Carvers.RemoveAll([](const TWeakObjectPtr<UDreamCloudCarverComponent>& C) { return !C.IsValid(); });

	// Carvers cut the capsule they swept since the last frame out of the volumes it touches.
	for (const TWeakObjectPtr<UDreamCloudCarverComponent>& Weak : Carvers)
	{
		UDreamCloudCarverComponent* Carver = Weak.Get();
		const FVector Now = Carver->GetComponentLocation();
		const FVector From = Carver->LastCarved.Get(Now);
		Carver->LastCarved = Now;
		if (!Carver->bCarving || Carver->Strength <= 0.0f)
		{
			continue;
		}
		FBox Swept(ForceInit);
		Swept += From;
		Swept += Now;
		Swept = Swept.ExpandBy(Carver->Radius);
		for (const TWeakObjectPtr<UDreamCloudVolumeComponent>& V : Volumes)
		{
			UDreamCloudVolumeComponent* Volume = V.Get();
			if (Volume->bCarvable && Volume->Bounds.GetBox().Intersect(Swept))
			{
				Volume->Carve(From, Now, Carver->Radius, Carver->Strength);
			}
		}
	}

	const FVector Light = GetLightDirection();
	const FVector* View = World->ViewLocationsRenderedLastFrame.Num() > 0 ? &World->ViewLocationsRenderedLastFrame[0] : nullptr;
	for (const TWeakObjectPtr<UDreamCloudVolumeComponent>& V : Volumes)
	{
		V->TickCloud(DeltaTime, Light, View);
	}
}

TStatId UDreamCloudSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UDreamCloudSubsystem, STATGROUP_Tickables);
}
