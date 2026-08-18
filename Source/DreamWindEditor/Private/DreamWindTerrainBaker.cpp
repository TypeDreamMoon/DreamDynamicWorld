// Copyright DreamMoon. All Rights Reserved.
//
// Bakes a UDreamWindTerrainMap from the level's collision: a grid of downward traces gives a height
// field; the height relative to a blurred copy of itself is 'exposure' (ridge > 0 > hollow), and the
// blurred slope steers the wind toward valleys.

#include "DreamWindEditorLibrary.h"
#include "DreamWindGlobalActor.h"
#include "DreamWindModule.h"
#include "DreamWindSubsystem.h"
#include "DreamWindTerrainMap.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "Engine/CollisionProfile.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Landscape.h"
#include "Misc/PackageName.h"
#include "Misc/ScopedSlowTask.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"

#define LOCTEXT_NAMESPACE "DreamWindTerrainBaker"

namespace DreamWind::TerrainBake
{
	static FBox LevelStaticBounds(UWorld* World)
	{
		FBox Bounds(ForceInit);
		for (TActorIterator<AActor> It(World); It; ++It)
		{
			AActor* Actor = *It;
			if (!Actor || Actor->IsA<ADreamWindGlobal>())
			{
				continue;
			}
			const bool bStatic = Actor->IsA<ALandscape>() || Actor->IsA<AStaticMeshActor>() || Actor->GetRootComponent() && Actor->GetRootComponent()->Mobility == EComponentMobility::Static;
			if (!bStatic || Actor->IsHidden())
			{
				continue;
			}
			const FBox ActorBox = Actor->GetComponentsBoundingBox(true);
			if (ActorBox.IsValid && ActorBox.GetSize().SizeSquared() > 1.0)
			{
				Bounds += ActorBox;
			}
		}
		return Bounds;
	}

	/** Separable box blur with clamped edges. */
	static void BoxBlur(const TArray<float>& In, TArray<float>& Out, int32 W, int32 H, int32 R)
	{
		TArray<float> Tmp;
		Tmp.SetNumUninitialized(W * H);
		Out.SetNumUninitialized(W * H);
		const float Norm = 1.0f / float(2 * R + 1);
		for (int32 y = 0; y < H; ++y)
		{
			for (int32 x = 0; x < W; ++x)
			{
				float Sum = 0.0f;
				for (int32 k = -R; k <= R; ++k)
				{
					Sum += In[y * W + FMath::Clamp(x + k, 0, W - 1)];
				}
				Tmp[y * W + x] = Sum * Norm;
			}
		}
		for (int32 y = 0; y < H; ++y)
		{
			for (int32 x = 0; x < W; ++x)
			{
				float Sum = 0.0f;
				for (int32 k = -R; k <= R; ++k)
				{
					Sum += Tmp[FMath::Clamp(y + k, 0, H - 1) * W + x];
				}
				Out[y * W + x] = Sum * Norm;
			}
		}
	}
}

bool UDreamWindEditorLibrary::BakeTerrainMap(UWorld* World, const FBox& Bounds, const FDreamWindTerrainBakeSettings& Settings, UDreamWindTerrainMap* Target)
{
	using namespace DreamWind::TerrainBake;
	if (!World || !Target)
	{
		UE_LOG(LogDreamWind, Error, TEXT("BakeTerrainMap: world or target map missing."));
		return false;
	}
	FBox Box = Bounds;
	if (!Box.IsValid || Box.GetSize().X < 100.0 || Box.GetSize().Y < 100.0)
	{
		Box = LevelStaticBounds(World);
	}
	if (!Box.IsValid || Box.GetSize().X < 100.0 || Box.GetSize().Y < 100.0)
	{
		UE_LOG(LogDreamWind, Error, TEXT("BakeTerrainMap: no usable bounds (level has no static geometry?)."));
		return false;
	}

	const int32 W = FMath::Clamp(Settings.Resolution, 16, 1024);
	const int32 H = W;
	const FVector2D Min(Box.Min.X, Box.Min.Y);
	const FVector2D Size(Box.GetSize().X, Box.GetSize().Y);
	const FVector2D Cell(Size.X / W, Size.Y / H);
	const double TopZ = Box.Max.Z + Settings.TraceMargin;
	const double BottomZ = Box.Min.Z - Settings.TraceMargin;

	FScopedSlowTask SlowTask(float(H), LOCTEXT("Baking", "DreamWind: baking terrain map..."));
	SlowTask.MakeDialog(true);

	// 1. heights
	TArray<float> Height;
	Height.SetNumUninitialized(W * H);
	float MinH = TNumericLimits<float>::Max();
	float MaxH = TNumericLimits<float>::Lowest();
	FCollisionQueryParams Params(SCENE_QUERY_STAT(DreamWindTerrainBake), true);
	int32 Hits = 0;
	for (int32 y = 0; y < H; ++y)
	{
		SlowTask.EnterProgressFrame(1.0f);
		if (SlowTask.ShouldCancel())
		{
			return false;
		}
		for (int32 x = 0; x < W; ++x)
		{
			const FVector Start(Min.X + (x + 0.5) * Cell.X, Min.Y + (y + 0.5) * Cell.Y, TopZ);
			const FVector End(Start.X, Start.Y, BottomZ);
			FHitResult Hit;
			if (World->LineTraceSingleByChannel(Hit, Start, End, Settings.TraceChannel, Params))
			{
				const float h = float(Hit.ImpactPoint.Z);
				Height[y * W + x] = h;
				MinH = FMath::Min(MinH, h);
				MaxH = FMath::Max(MaxH, h);
				++Hits;
			}
			else
			{
				Height[y * W + x] = TNumericLimits<float>::Lowest();   // filled below
			}
		}
	}
	if (Hits == 0)
	{
		UE_LOG(LogDreamWind, Warning, TEXT("BakeTerrainMap: no trace hit anything on channel %d; the map will be flat."), int32(Settings.TraceChannel));
		MinH = MaxH = 0.0f;
	}
	// A miss (nothing under that texel) counts as the lowest ground seen, not as a bottomless pit:
	// otherwise the edge of the level's floor becomes a cliff that flips the wind around.
	for (float& h : Height)
	{
		if (h == TNumericLimits<float>::Lowest())
		{
			h = MinH;
		}
	}

	// 2. blurred height and its slope
	const int32 R = FMath::Clamp(FMath::RoundToInt32(Settings.BlurRadius / float(FMath::Max(Cell.X, 1.0))), 1, W / 2);
	TArray<float> Blurred;
	BoxBlur(Height, Blurred, W, H, R);

	Target->Texels.SetNumUninitialized(W * H);
	const float ExposureScale = FMath::Max(Settings.ExposureScale, 1.0f);
	const float Range = FMath::Max(MaxH - MinH, 1.0f);
	for (int32 y = 0; y < H; ++y)
	{
		for (int32 x = 0; x < W; ++x)
		{
			const float h = Height[y * W + x];
			const float hb = Blurred[y * W + x];
			const float Exposure = FMath::Clamp((h - hb) / ExposureScale, -1.0f, 1.0f);
			const float Mult = FMath::Clamp(1.0f + Settings.ExposureGain * Exposure, 0.1f, 2.5f);

			// central differences of the blurred field, rise over run (dimensionless)
			const float GX = (Blurred[y * W + FMath::Min(x + 1, W - 1)] - Blurred[y * W + FMath::Max(x - 1, 0)]) / float(2.0 * Cell.X);
			const float GY = (Blurred[FMath::Min(y + 1, H - 1) * W + x] - Blurred[FMath::Max(y - 1, 0) * W + x]) / float(2.0 * Cell.Y);
			FVector2f Steer = FVector2f(-GX, -GY) * Settings.SteerGain;   // toward lower ground
			const float SteerLen = Steer.Size();
			if (SteerLen > Settings.MaxSteer)                                // bend, never flip
			{
				Steer *= Settings.MaxSteer / SteerLen;
			}

			Target->Texels[y * W + x] = FVector4f(Mult, Steer.X, Steer.Y, (h - MinH) / Range);
		}
	}

	Target->Bounds = FBox2D(Min, Min + Size);
	Target->Width = W;
	Target->Height = H;
	Target->HeightRange = FVector2D(MinH, MaxH);
	Target->BakeSettings = Settings;
	Target->MarkPackageDirty();

	UE_LOG(LogDreamWind, Display, TEXT("BakeTerrainMap: %dx%d over (%.0f,%.0f)-(%.0f,%.0f), heights %.0f..%.0f, %d/%d hits, blur %d texels."),
		W, H, Min.X, Min.Y, Min.X + Size.X, Min.Y + Size.Y, MinH, MaxH, Hits, W * H, R);
	return true;
}

bool UDreamWindEditorLibrary::BakeTerrainMapForActor(ADreamWindGlobal* Actor)
{
	if (!Actor || !Actor->GetWorld())
	{
		return false;
	}
	UWorld* World = Actor->GetWorld();

	UDreamWindTerrainMap* Map = Actor->TerrainMap;
	bool bCreated = false;
	if (!Map)
	{
		// next to the level: /Game/Maps/Foo -> /Game/Maps/Foo_WindTerrain
		const FString LevelPackage = World->GetOutermost()->GetName();
		const FString PackageName = LevelPackage + TEXT("_WindTerrain");
		const FString AssetName = FPackageName::GetLongPackageAssetName(PackageName);
		UPackage* Package = CreatePackage(*PackageName);
		Package->FullyLoad();
		Map = FindObject<UDreamWindTerrainMap>(Package, *AssetName);
		if (!Map)
		{
			Map = NewObject<UDreamWindTerrainMap>(Package, *AssetName, RF_Public | RF_Standalone);
			FAssetRegistryModule::AssetCreated(Map);
			bCreated = true;
		}
	}

	if (!BakeTerrainMap(World, Actor->TerrainBakeBounds, Actor->TerrainBakeSettings, Map))
	{
		return false;
	}

	Actor->Modify();
	Actor->TerrainMap = Map;
	Actor->ApplyToWorld();
	if (UDreamWindSubsystem* Wind = World->GetSubsystem<UDreamWindSubsystem>())
	{
		Wind->SetTerrainMap(nullptr);   // force a re-upload even when the same asset was re-baked
		Wind->SetTerrainMap(Map);
	}

	// save the map asset
	UPackage* Package = Map->GetOutermost();
	const FString Filename = FPackageName::LongPackageNameToFilename(Package->GetName(), FPackageName::GetAssetPackageExtension());
	FSavePackageArgs Args;
	Args.TopLevelFlags = RF_Public | RF_Standalone;
	Args.SaveFlags = SAVE_NoError;
	if (!UPackage::SavePackage(Package, Map, *Filename, Args))
	{
		UE_LOG(LogDreamWind, Warning, TEXT("BakeTerrainMap: baked but could not save '%s' (save it from the Content Browser)."), *Filename);
	}
	UE_LOG(LogDreamWind, Display, TEXT("BakeTerrainMap: %s %s and assigned to %s."), bCreated ? TEXT("created") : TEXT("re-baked"), *Map->GetPathName(), *Actor->GetActorLabel());
	return true;
}

#undef LOCTEXT_NAMESPACE
