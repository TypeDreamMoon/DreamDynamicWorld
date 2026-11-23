// Copyright DreamMoon. All Rights Reserved.

#include "DreamSurfaceDripLine.h"
#include "DreamSurfaceMaps.h"
#include "DreamSurfaceModule.h"
#include "DreamSurfaceSubsystem.h"

#include "DreamWorldSubsystem.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"
#include "ImageCore.h"
#include "ImageUtils.h"
#include "UObject/UObjectIterator.h"
#include "Misc/Paths.h"
#include "RenderingThread.h"
#include "TextureResource.h"

namespace DreamSurface::Console
{
	static UDreamSurfaceSubsystem* Find(UWorld* World)
	{
		UDreamSurfaceSubsystem* Surface = World ? World->GetSubsystem<UDreamSurfaceSubsystem>() : nullptr;
		if (!Surface)
		{
			UE_LOG(LogDreamSurface, Warning, TEXT("No Dream Surface in this world."));
		}
		return Surface;
	}

	/** A map's texels, false-coloured by Colour, saved as a PNG. */
	static bool SaveMap(UTextureRenderTarget2D* Map, const FString& Filename, TFunctionRef<FColor(const FLinearColor&)> Colour)
	{
		FTextureRenderTargetResource* Resource = Map ? Map->GameThread_GetRenderTargetResource() : nullptr;
		if (!Resource)
		{
			return false;
		}
		TArray<FLinearColor> Pixels;
		Resource->ReadLinearColorPixels(Pixels);
		FImage Image(Map->SizeX, Map->SizeY, ERawImageFormat::BGRA8, EGammaSpace::sRGB);
		TArrayView64<FColor> Out = Image.AsBGRA8();
		for (int64 i = 0; i < FMath::Min<int64>(Out.Num(), Pixels.Num()); ++i)
		{
			Out[i] = Colour(Pixels[i]);
		}
		const bool bSaved = FImageUtils::SaveImageByExtension(*Filename, Image);
		UE_LOG(LogDreamSurface, Display, TEXT("%s %s"), bSaved ? TEXT("Saved") : TEXT("Could not save"), *Filename);
		return bSaved;
	}

	static uint8 Byte(float V) { return uint8(FMath::Clamp(FMath::RoundToInt32(V * 255.0f), 0, 255)); }

	static FAutoConsoleCommandWithWorldAndArgs AtCommand(
		TEXT("DreamSurface.At"),
		TEXT("DreamSurface.At x y z: the state of a surface at that point now (SampleSurface: under a roof, under a water body's flood)."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
		{
			UDreamSurfaceSubsystem* Surface = Find(World);
			if (!Surface || Args.Num() < 3)
			{
				return;
			}
			const FVector P(FCString::Atod(*Args[0]), FCString::Atod(*Args[1]), FCString::Atod(*Args[2]));
			const FDreamSurfaceState S = Surface->SampleSurface(P);
			UE_LOG(LogDreamSurface, Display, TEXT("Surface at (%.0f, %.0f, %.0f): wet %.3f water %.3f snow %.3f dust %.3f ice %.3f frost %.3f rime %.3f hail %.3f flooded %.3f silt %.3f"),
				P.X, P.Y, P.Z, S.Wetness, S.Water, S.Snow, S.Dust, S.Ice, S.Frost, S.Rime, S.Hail, S.Flooded, S.Silt);
		}));

	static FAutoConsoleCommandWithWorldAndArgs DumpCommand(
		TEXT("DreamSurface.Dump"),
		TEXT("DreamSurface.Dump: the state in the open at the view and where the surface maps stand."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
		{
			UDreamSurfaceSubsystem* Surface = Find(World);
			if (!Surface)
			{
				return;
			}
			const FDreamSurfaceState& S = Surface->GetViewState();
			const FDreamSurfaceMaps* Maps = Surface->GetMaps();
			UE_LOG(LogDreamSurface, Display, TEXT("Surface: active %d, view wet %.3f water %.3f snow %.3f dust %.3f ice %.3f frost %.3f rime %.3f frozen %.3f hail %.3f flooded %.3f silt %.3f; near map at (%.0f, %.0f) %.0f cm; %u draws, %u integrations"),
				Surface->IsActive() ? 1 : 0, S.Wetness, S.Water, S.Snow, S.Dust, S.Ice, S.Frost, S.Rime, S.FrozenWater, S.Hail, S.Flooded, S.Silt,
				Maps ? Maps->GetNearCenter().X : 0.0, Maps ? Maps->GetNearCenter().Y : 0.0, Maps ? Maps->GetNearExtent() : 0.0f,
				Maps ? Maps->GetDrawCount() : 0u, Maps ? Maps->GetIntegrateCount() : 0u);
			for (TObjectIterator<UDreamSurfaceDripLineComponent> It; It; ++It)
			{
				if (It->GetWorld() == World && It->IsRegistered())
				{
					FVector Start, End;
					It->GetLine(Start, End);
					UE_LOG(LogDreamSurface, Display, TEXT("Drip line %s: strength %.2f, %.1f drops/s, effect %s, %d particles, floors %.0f / %.0f, from (%.0f, %.0f, %.0f) to (%.0f, %.0f, %.0f)"),
						*It->GetOwner()->GetActorNameOrLabel(), It->GetDripStrength(), It->GetDropRate(), It->HasEffect() ? TEXT("on") : TEXT("off"),
						It->GetParticleCount(), It->GetFloors().X, It->GetFloors().Y, Start.X, Start.Y, Start.Z, End.X, End.Y, End.Z);
				}
			}
		}));

	static FAutoConsoleCommandWithWorldAndArgs RemapCommand(
		TEXT("DreamSurface.Remap"),
		TEXT("DreamSurface.Remap: integrate the surface maps afresh on the next frame."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
		{
			UDreamSurfaceSubsystem* Surface = Find(World);
			if (FDreamSurfaceMaps* Maps = Surface ? Surface->GetMaps() : nullptr)
			{
				Maps->RequestIntegrate();
			}
		}));

	static FAutoConsoleCommandWithWorldAndArgs StampCommand(
		TEXT("DreamSurface.Stamp"),
		TEXT("DreamSurface.Stamp <x> <y> [radius] [strength]: press a print into the snow and the dust and stir the puddles there (cm)."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
		{
			UDreamSurfaceSubsystem* Surface = Find(World);
			if (Surface && Args.Num() >= 2)
			{
				Surface->Stamp(FVector(FCString::Atod(*Args[0]), FCString::Atod(*Args[1]), 0.0),
					Args.Num() > 2 ? FCString::Atof(*Args[2]) : 50.0f, Args.Num() > 3 ? FCString::Atof(*Args[3]) : 1.0f);
			}
		}));

	static FAutoConsoleCommandWithWorldAndArgs WalkCommand(
		TEXT("DreamSurface.Walk"),
		TEXT("DreamSurface.Walk <x0> <y0> <x1> <y1> [stride]: footprints from one point to the other, left and right of the line (cm)."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
		{
			UDreamSurfaceSubsystem* Surface = Find(World);
			if (!Surface || Args.Num() < 4)
			{
				return;
			}
			const FVector2D From(FCString::Atod(*Args[0]), FCString::Atod(*Args[1]));
			const FVector2D To(FCString::Atod(*Args[2]), FCString::Atod(*Args[3]));
			const double Stride = Args.Num() > 4 ? FMath::Max(10.0, FCString::Atod(*Args[4])) : 45.0;
			const FVector2D Along = (To - From).GetSafeNormal();
			const FVector2D Side(-Along.Y, Along.X);
			const int32 Steps = FMath::Min(200, FMath::FloorToInt32((To - From).Size() / Stride));
			for (int32 i = 0; i <= Steps; ++i)
			{
				const FVector2D P = From + Along * (Stride * i) + Side * ((i % 2) ? 14.0 : -14.0);
				Surface->Stamp(FVector(P, 0.0), 11.0f, 0.9f);
			}
		}));

	static FAutoConsoleCommandWithWorldAndArgs SaveMapsCommand(
		TEXT("DreamSurface.SaveMaps"),
		TEXT("DreamSurface.SaveMaps [name]: save the near map (wet red, water green, snow blue) and its dust and cover (dust red, cover green) the cold (ice red, frost green, rime blue), the hail and what a flood left (flooded red, silt green) as PNGs under Saved/DreamSurface."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
		{
			UDreamSurfaceSubsystem* Surface = Find(World);
			const FDreamSurfaceMaps* Maps = Surface ? Surface->GetMaps() : nullptr;
			if (!Maps)
			{
				return;
			}
			FlushRenderingCommands();
			const FString Name = Args.Num() > 0 ? Args[0] : TEXT("Surface");
			const FString Dir = FPaths::ProjectSavedDir() / TEXT("DreamSurface");
			SaveMap(Maps->GetNearMap(), Dir / (Name + TEXT("_State.png")), [](const FLinearColor& C) { return FColor(Byte(C.R), Byte(C.G), Byte(C.B), 255); });
			// The cover map's own colour: how fully covered (green); the state's dust beside it would need the other map.
			SaveMap(Maps->GetCoverMap(), Dir / (Name + TEXT("_Cover.png")), [](const FLinearColor& C) { return FColor(0, Byte(C.G), C.R > -1.0e8f ? 64 : 0, 255); });
			SaveMap(Maps->GetNearMap(), Dir / (Name + TEXT("_Dust.png")), [](const FLinearColor& C) { return FColor(Byte(C.A), Byte(C.A), Byte(C.A), 255); });
			// The cold beside it: ice red, frost green, rime blue.
			SaveMap(Maps->GetNearIceMap(), Dir / (Name + TEXT("_Ice.png")), [](const FLinearColor& C) { return FColor(Byte(C.R), Byte(C.G), Byte(C.B), 255); });
			// The hail lying, grey.
			SaveMap(Maps->GetNearHailMap(), Dir / (Name + TEXT("_Hail.png")), [](const FLinearColor& C) { return FColor(Byte(C.R), Byte(C.R), Byte(C.R), 255); });
			// What a flood left: flooded red, silt green.
			SaveMap(Maps->GetNearHailMap(), Dir / (Name + TEXT("_Flood.png")), [](const FLinearColor& C) { return FColor(Byte(C.G), Byte(C.B), 0, 255); });
		}));
}
