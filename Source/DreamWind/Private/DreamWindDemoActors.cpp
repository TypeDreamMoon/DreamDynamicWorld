// Copyright DreamMoon. All Rights Reserved.

#include "DreamWindDemoActors.h"
#include "DreamWindSourceComponent.h"

#include "Components/ArrowComponent.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/SphereComponent.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInterface.h"
#include "UObject/ConstructorHelpers.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(DreamWindDemoActors)

// -------------------------------------------------------------------------------------------------

ADreamWindSourceActor::ADreamWindSourceActor()
{
	PrimaryActorTick.bCanEverTick = false;

	Source = CreateDefaultSubobject<UDreamWindSourceComponent>(TEXT("Source"));
	RootComponent = Source;

	RadiusVisual = CreateDefaultSubobject<USphereComponent>(TEXT("RadiusVisual"));
	RadiusVisual->SetupAttachment(Source);
	RadiusVisual->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	RadiusVisual->ShapeColor = FColor(255, 220, 60);
	RadiusVisual->bDrawOnlyIfSelected = false;
	RadiusVisual->InitSphereRadius(400.0f);

#if WITH_EDITORONLY_DATA
	Arrow = CreateEditorOnlyDefaultSubobject<UArrowComponent>(TEXT("Arrow"));
	if (Arrow)
	{
		Arrow->SetupAttachment(Source);
		Arrow->ArrowColor = FColor(255, 220, 60);
		Arrow->ArrowSize = 2.5f;
		Arrow->ArrowLength = 200.0f;
	}
#endif
}

void ADreamWindSourceActor::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);
	if (RadiusVisual && Source)
	{
		RadiusVisual->SetSphereRadius(FMath::Max(Source->Radius, 10.0f), false);
	}
}

// -------------------------------------------------------------------------------------------------

ADreamWindGrassField::ADreamWindGrassField()
{
	PrimaryActorTick.bCanEverTick = false;

	USceneComponent* Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	Root->SetMobility(EComponentMobility::Static);
	RootComponent = Root;

	Grass = CreateDefaultSubobject<UInstancedStaticMeshComponent>(TEXT("Grass"));
	Grass->SetupAttachment(Root);
	Grass->SetMobility(EComponentMobility::Static);
	Grass->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Grass->SetCastShadow(false);
	Grass->NumCustomDataFloats = 0;

	static ConstructorHelpers::FObjectFinder<UStaticMesh> ConeFinder(TEXT("/Engine/BasicShapes/Cone.Cone"));
	if (ConeFinder.Succeeded())
	{
		Mesh = ConeFinder.Object;
	}
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> GrassFinder(TEXT("/DreamWind/Materials/M_DreamWindGrass.M_DreamWindGrass"));
	if (GrassFinder.Succeeded())
	{
		Material = GrassFinder.Object;
	}
}

void ADreamWindGrassField::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);
	Rebuild();
}

void ADreamWindGrassField::Rebuild()
{
	if (!Grass)
	{
		return;
	}
	Grass->ClearInstances();
	Grass->SetStaticMesh(Mesh);
	if (Material)
	{
		Grass->SetMaterial(0, Material);
	}
	Grass->SetCastShadow(bCastShadow);
	if (!Mesh || Count <= 0)
	{
		return;
	}

	FRandomStream Rand(Seed);
	const FTransform& ActorXf = GetActorTransform();
	TArray<FTransform> Instances;
	Instances.Reserve(Count);
	int32 Attempts = 0;
	while (Instances.Num() < Count && Attempts < Count * 4)
	{
		++Attempts;
		const FVector Local(Rand.FRandRange(-Extent, Extent), Rand.FRandRange(-Extent, Extent), 0.0);
		const FVector World = ActorXf.TransformPosition(Local);
		bool bExcluded = false;
		for (const FBox& Box : Exclusions)
		{
			if (Box.IsValid && Box.IsInsideXY(World))
			{
				bExcluded = true;
				break;
			}
		}
		if (bExcluded)
		{
			continue;
		}
		const float W = float(Rand.FRandRange(WidthScale.X, WidthScale.Y));
		const float H = float(Rand.FRandRange(HeightScale.X, HeightScale.Y));
		Instances.Emplace(FRotator(0.0, Rand.FRandRange(0.0, 360.0), 0.0), Local, FVector(W, W, H));
	}
	Grass->AddInstances(Instances, false, false);
}
