// Copyright DreamMoon. All Rights Reserved.
//
// Console commands for trying cloud volumes out in a running game (and from DreamWorld.Script files); the quality
// level is DreamCloud.Quality (DreamCloudSubsystem.cpp):
//   DreamCloud.Spawn <look> <shape> <x> <y> <z> [size cm]
//   DreamCloud.Set <property> <value> [name filter]
//   DreamCloud.Clear, DreamCloud.Dump, DreamCloud.CarveView <radius>

#include "DreamCloudCarverComponent.h"
#include "DreamCloudModule.h"
#include "DreamCloudSubsystem.h"
#include "Engine/TextureRenderTargetVolume.h"
#include "DreamCloudVolume.h"
#include "DreamCloudVolumeComponent.h"

#include "Camera/PlayerCameraManager.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"
#include "Materials/MaterialInstanceDynamic.h"

namespace DreamCloud::Console
{
	static EDreamCloudLook ParseLook(const FString& Word)
	{
		if (Word.Equals(TEXT("toon"), ESearchCase::IgnoreCase))
		{
			return EDreamCloudLook::Toon;
		}
		if (Word.Equals(TEXT("painted"), ESearchCase::IgnoreCase))
		{
			return EDreamCloudLook::Painted;
		}
		return EDreamCloudLook::Realistic;
	}

	static FAutoConsoleCommandWithWorldAndArgs SpawnCommand(
		TEXT("DreamCloud.Spawn"),
		TEXT("DreamCloud.Spawn <realistic|toon|painted> <sphere|box|blob|cube|spheremesh|/Path/To.Mesh> <x> <y> <z> [size cm = 1000]: spawns a cloud volume")
		TEXT(" (@<forward> <right> <up>: that far from the view, along its heading).")
		TEXT(" sphere, box, blob: primitives that size; cube, spheremesh and a mesh path: the mesh, scaled to that size (meshes of 100 cm)."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
		{
			if (!World || Args.Num() < 5)
			{
				UE_LOG(LogDreamCloud, Warning, TEXT("DreamCloud.Spawn <look> <shape> <x> <y> <z> [size]"));
				return;
			}
			// @forward right up: relative to the view (its heading only), so a script can place a cloud in front of it.
			const bool bAhead = Args[2].StartsWith(TEXT("@"));
			FVector At(FCString::Atod(bAhead ? *Args[2] + 1 : *Args[2]), FCString::Atod(*Args[3]), FCString::Atod(*Args[4]));
			if (bAhead)
			{
				const APlayerController* PC = World->GetFirstPlayerController();
				if (PC && PC->PlayerCameraManager)
				{
					const FRotator Heading(0.0, PC->PlayerCameraManager->GetCameraRotation().Yaw, 0.0);
					At = PC->PlayerCameraManager->GetCameraLocation() + Heading.RotateVector(At);
				}
			}
			const double Size = Args.IsValidIndex(5) ? FCString::Atod(*Args[5]) : 1000.0;
			FActorSpawnParameters Params;
			Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
			Params.bDeferConstruction = true;
			ADreamCloudVolume* Volume = World->SpawnActor<ADreamCloudVolume>(At, FRotator::ZeroRotator, Params);
			if (!Volume)
			{
				return;
			}
			UDreamCloudVolumeComponent* Cloud = Volume->GetCloud();
			Cloud->Look = ParseLook(Args[0]);
			const FString& Shape = Args[1];
			const float Half = float(0.5 * Size);
			FVector Scale = FVector::OneVector;
			Cloud->Primitives.Reset();
			if (Shape.Equals(TEXT("sphere"), ESearchCase::IgnoreCase))
			{
				FDreamCloudPrimitiveShape S;
				S.Size = FVector(Half);
				Cloud->Primitives.Add(S);
			}
			else if (Shape.Equals(TEXT("box"), ESearchCase::IgnoreCase))
			{
				FDreamCloudPrimitiveShape S;
				S.Type = EDreamCloudPrimitive::Box;
				S.Size = FVector(Half);
				S.Rounding = 0.1f * Half;
				Cloud->Primitives.Add(S);
			}
			else if (Shape.Equals(TEXT("blob"), ESearchCase::IgnoreCase))
			{
				// A cumulus-like heap: a wide base and two crowns.
				const FVector Parts[] = { FVector(0.0, 0.0, -0.1), FVector(-0.35, 0.1, 0.2), FVector(0.3, -0.15, 0.15), FVector(0.05, 0.25, -0.05) };
				const float Radii[] = { 0.55f, 0.4f, 0.45f, 0.4f };
				for (int32 i = 0; i < UE_ARRAY_COUNT(Parts); ++i)
				{
					FDreamCloudPrimitiveShape S;
					S.Transform.SetLocation(Parts[i] * Size);
					S.Size = FVector(Radii[i] * Half);
					Cloud->Primitives.Add(S);
				}
				Cloud->PrimitiveBlend = 0.25f * Half;
			}
			else
			{
				const TCHAR* Path = Shape.Equals(TEXT("cube"), ESearchCase::IgnoreCase) ? TEXT("/Engine/BasicShapes/Cube.Cube")
					: Shape.Equals(TEXT("spheremesh"), ESearchCase::IgnoreCase) ? TEXT("/Engine/BasicShapes/Sphere.Sphere") : *Shape;
				Cloud->ShapeSource = EDreamCloudShapeSource::Mesh;
				Cloud->ShapeMesh = LoadObject<UStaticMesh>(nullptr, Path);
				Scale = FVector(Size / 100.0);
			}
			Volume->FinishSpawning(FTransform(FRotator::ZeroRotator, At, Scale));
			// The component registered (and baked its default shape) while the actor was spawned: bake what was set since.
			if (Cloud->NeedsBake())
			{
				Cloud->Rebake();
			}
			Cloud->UpdateCloudMaterial();
			UE_LOG(LogDreamCloud, Display, TEXT("DreamCloud.Spawn: %s %s at %s, field %s"), *Args[0], *Shape, *At.ToString(), *Cloud->Field.Resolution.ToString());
		}));

	/** Whether an actor's name (or, in the editor, its label) contains the filter. */
	static bool NameMatches(const AActor& Actor, const FString& Filter)
	{
#if WITH_EDITOR
		if (Actor.GetActorLabel().Contains(Filter))
		{
			return true;
		}
#endif
		return Actor.GetName().Contains(Filter);
	}

	static FAutoConsoleCommandWithWorldAndArgs SetCommand(
		TEXT("DreamCloud.Set"),
		TEXT("DreamCloud.Set <property> <value> [name filter]: sets a property of every cloud volume (or those whose actor name or label contains the filter),")
		TEXT(" as the editor would (text form: 0.1, (R=1,G=0.5,B=0.5,A=1), Toon), and bakes again when the shape changed."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
		{
			if (!World || Args.Num() < 2)
			{
				UE_LOG(LogDreamCloud, Warning, TEXT("DreamCloud.Set <property> <value> [filter]"));
				return;
			}
			FProperty* Property = UDreamCloudVolumeComponent::StaticClass()->FindPropertyByName(FName(*Args[0]));
			if (!Property)
			{
				UE_LOG(LogDreamCloud, Warning, TEXT("DreamCloud.Set: no property %s"), *Args[0]);
				return;
			}
			int32 Count = 0;
			for (TActorIterator<ADreamCloudVolume> It(World); It; ++It)
			{
				if (Args.IsValidIndex(2) && !NameMatches(**It, Args[2]))
				{
					continue;
				}
				UDreamCloudVolumeComponent* Cloud = It->GetCloud();
				if (!Property->ImportText_InContainer(*Args[1], Cloud, Cloud, PPF_None))
				{
					UE_LOG(LogDreamCloud, Warning, TEXT("DreamCloud.Set: cannot read %s as %s"), *Args[1], *Args[0]);
					return;
				}
				if (Cloud->NeedsBake())
				{
					Cloud->Rebake();
				}
				Cloud->MarkRenderStateDirty();
				Cloud->UpdateCloudMaterial();
				++Count;
			}
			UE_LOG(LogDreamCloud, Display, TEXT("DreamCloud.Set %s = %s on %d volumes"), *Args[0], *Args[1], Count);
		}));

	static FAutoConsoleCommandWithWorldAndArgs ClearCommand(
		TEXT("DreamCloud.Clear"),
		TEXT("DreamCloud.Clear: removes every cloud volume in the world."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
		{
			if (!World)
			{
				return;
			}
			for (TActorIterator<ADreamCloudVolume> It(World); It; ++It)
			{
				It->Destroy();
			}
		}));

	static FAutoConsoleCommandWithWorldAndArgs CarveViewCommand(
		TEXT("DreamCloud.CarveView"),
		TEXT("DreamCloud.CarveView <radius cm, 0 = stop>: the view (the player's pawn, or the camera it looks through) carves the clouds it passes through."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
		{
			APlayerController* PC = World ? World->GetFirstPlayerController() : nullptr;
			// The actor the view moves with: what the player controller looks through (a camera actor, its pawn).
			AActor* Carrier = PC ? (PC->GetViewTarget() ? PC->GetViewTarget() : static_cast<AActor*>(PC->GetPawn())) : nullptr;
			if (!Carrier)
			{
				UE_LOG(LogDreamCloud, Warning, TEXT("DreamCloud.CarveView: no view"));
				return;
			}
			const float Radius = Args.IsValidIndex(0) ? FCString::Atof(*Args[0]) : 200.0f;
			UDreamCloudCarverComponent* Carver = Carrier->FindComponentByClass<UDreamCloudCarverComponent>();
			if (Radius <= 0.0f)
			{
				if (Carver)
				{
					Carver->DestroyComponent();
				}
				return;
			}
			if (!Carver)
			{
				Carver = NewObject<UDreamCloudCarverComponent>(Carrier, TEXT("ViewCarver"), RF_Transient);
				Carver->SetupAttachment(Carrier->GetRootComponent());
				Carver->RegisterComponent();
			}
			Carver->Radius = Radius;
			UE_LOG(LogDreamCloud, Display, TEXT("DreamCloud.CarveView: %s carves %.0f cm"), *Carrier->GetName(), Radius);
		}));
}

namespace DreamCloud::Console
{
	static FAutoConsoleCommandWithWorld DumpCommand(
		TEXT("DreamCloud.Dump"),
		TEXT("DreamCloud.Dump: logs every cloud volume's field, drawn box and material parameters."),
		FConsoleCommandWithWorldDelegate::CreateLambda([](UWorld* World)
		{
			if (!World)
			{
				return;
			}
			for (TActorIterator<ADreamCloudVolume> It(World); It; ++It)
			{
				const UDreamCloudVolumeComponent* Cloud = It->GetCloud();
				const FMatrix Render = Cloud->GetRenderMatrix();
				FLinearColor Box = FLinearColor::Black;
				if (UMaterialInstanceDynamic* Material = Cloud->GetCloudMaterial())
				{
					Material->GetVectorParameterValue(FHashedMaterialParameterInfo(TEXT("FieldBox")), Box);
				}
				UE_LOG(LogDreamCloud, Display, TEXT("%s: scale %s, field %s centre %s size %s, drawn at %s scale %s, bounds %s, material box %s, material %s"),
					*It->GetName(), *Cloud->GetComponentScale().ToString(), *Cloud->Field.Resolution.ToString(), *Cloud->Field.Center.ToString(),
					*Cloud->Field.Size.ToString(), *Render.GetOrigin().ToString(), *Render.GetScaleVector().ToString(), *Cloud->Bounds.BoxExtent.ToString(),
					*Box.ToString(), Cloud->GetMaterial(0) ? *Cloud->GetMaterial(0)->GetName() : TEXT("none"));
				const UDreamCloudSubsystem* Clouds = World->GetSubsystem<UDreamCloudSubsystem>();
				const FVector LightW = Clouds ? Clouds->GetLightDirection() : FVector::UpVector;
				const UDreamCloudVolumeComponent::FLightState Light = Cloud->GetLightState(LightW);
				float Valid = -1.0f;
				if (UMaterialInstanceDynamic* Material = Cloud->GetCloudMaterial())
				{
					Material->GetScalarParameterValue(FHashedMaterialParameterInfo(TEXT("LightMapValid")), Valid);
				}
				const UTextureRenderTargetVolume* Map = Cloud->GetLightMap();
				UE_LOG(LogDreamCloud, Display, TEXT("%s: light %s (in the shape %s, sigma %.6f/cm), light map %s %dx%dx%d after %d updates, valid %.0f; carving %.3f at its middle"),
					*It->GetName(), *LightW.ToString(), *Light.Direction.ToString(), Light.SigmaPerUnit, Map ? *Map->GetName() : TEXT("none"),
					Map ? Map->SizeX : 0, Map ? Map->SizeY : 0, Map ? Map->SizeZ : 0, Cloud->GetLightMapUpdates(), Valid,
					Cloud->GetCarving(Cloud->Bounds.Origin));
			}
		}));
}
