// Copyright DreamMoon. All Rights Reserved.
//
// Console commands for trying cloud volumes out in a running game (and from DreamWorld.Script files):
//   DreamCloud.Spawn <look> <shape> <x> <y> <z> [size cm]
//   DreamCloud.Set <property> <value> [name filter]
//   DreamCloud.Clear

#include "DreamCloudModule.h"
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

	static FAutoConsoleCommandWithWorldAndArgs SetCommand(
		TEXT("DreamCloud.Set"),
		TEXT("DreamCloud.Set <property> <value> [name filter]: sets a property of every cloud volume (or those whose actor name contains the filter),")
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
				if (Args.IsValidIndex(2) && !It->GetName().Contains(Args[2]))
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
			}
		}));
}
