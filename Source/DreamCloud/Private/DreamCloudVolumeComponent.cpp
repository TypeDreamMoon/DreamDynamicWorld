// Copyright DreamMoon. All Rights Reserved.

#include "DreamCloudVolumeComponent.h"
#include "DreamCloudModule.h"

#include "Engine/CollisionProfile.h"
#include "Engine/StaticMesh.h"
#include "Engine/VolumeTexture.h"
#include "HAL/PlatformTime.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "Math/Float16.h"
#include "UObject/ConstructorHelpers.h"

namespace DreamCloud::Params
{
	static const FName Field(TEXT("Field"));
	static const FName FieldBox(TEXT("FieldBox"));
	static const FName Look(TEXT("Look"));
	static const FName Inflate(TEXT("Inflate"));
	static const FName EdgeSoftness(TEXT("EdgeSoftness"));
	static const FName Extinction(TEXT("Extinction"));
	static const FName SunStrength(TEXT("SunStrength"));
	static const FName AmbientStrength(TEXT("AmbientStrength"));
	static const FName LitTint(TEXT("LitTint"));
	static const FName ShadeTint(TEXT("ShadeTint"));
	static const FName LobeScale(TEXT("LobeScale"));
	static const FName ToneSoftness(TEXT("ToneSoftness"));
	static const FName NoiseScale(TEXT("NoiseScale"));
	static const FName NoiseErosion(TEXT("NoiseErosion"));
	static const FName NoiseDrift(TEXT("NoiseDrift"));
	static const FName StepScale(TEXT("StepScale"));
}

UDreamCloudVolumeComponent::UDreamCloudVolumeComponent(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	static ConstructorHelpers::FObjectFinder<UStaticMesh> Cube(TEXT("/Engine/BasicShapes/Cube.Cube"));
	if (Cube.Succeeded())
	{
		SetStaticMesh(Cube.Object);
	}
	SetCollisionEnabled(ECollisionEnabled::NoCollision);
	SetCollisionProfileName(UCollisionProfile::NoCollision_ProfileName);
	SetGenerateOverlapEvents(false);
	SetCanEverAffectNavigation(false);
	CastShadow = false;
	bCastDynamicShadow = false;
	bAffectDistanceFieldLighting = false;
	bAffectDynamicIndirectLighting = false;
	bReceivesDecals = false;
	bUseAsOccluder = false;
	bVisibleInRayTracing = false;
	bSelectable = true;

	FDreamCloudPrimitiveShape Sphere;
	Sphere.Type = EDreamCloudPrimitive::Sphere;
	Sphere.Size = FVector(500.0);
	Primitives.Add(Sphere);
}

uint32 UDreamCloudVolumeComponent::ComputeSourceHash() const
{
	uint32 Hash = HashCombine(GetTypeHash(DreamCloudBake::Version), GetTypeHash(uint8(ShapeSource)));
	Hash = HashCombine(Hash, GetTypeHash(FMath::Clamp(Resolution, 16, 256)));
	Hash = HashCombine(Hash, GetTypeHash(Padding));
	if (ShapeSource == EDreamCloudShapeSource::Mesh)
	{
		Hash = HashCombine(Hash, ShapeMesh ? GetTypeHash(ShapeMesh->GetPathName()) : 0u);
#if WITH_EDITORONLY_DATA
		// A mesh rebuilt from new source gets a new lighting guid.
		Hash = HashCombine(Hash, ShapeMesh ? GetTypeHash(ShapeMesh->GetLightingGuid()) : 0u);
#endif
	}
	else
	{
		Hash = HashCombine(Hash, GetTypeHash(PrimitiveBlend));
		for (const FDreamCloudPrimitiveShape& S : Primitives)
		{
			Hash = HashCombine(Hash, GetTypeHash(uint8(S.Type)));
			Hash = HashCombine(Hash, GetTypeHash(S.Transform.GetLocation()));
			Hash = HashCombine(Hash, GetTypeHash(S.Transform.GetRotation().Euler()));
			Hash = HashCombine(Hash, GetTypeHash(S.Size));
			Hash = HashCombine(Hash, GetTypeHash(S.Rounding));
		}
	}
	return Hash;
}

bool UDreamCloudVolumeComponent::NeedsBake() const
{
	return !Field.IsValid() || Field.SourceHash != ComputeSourceHash();
}

bool UDreamCloudVolumeComponent::Rebake()
{
	const double Start = FPlatformTime::Seconds();
	DreamCloudBake::FSettings Settings;
	Settings.Resolution = Resolution;
	Settings.Padding = Padding;
	FDreamCloudField Baked;
	Baked.SourceHash = ComputeSourceHash();
	bool bBaked = false;
	if (ShapeSource == EDreamCloudShapeSource::Mesh)
	{
		TArray<FVector3f> Positions;
		TArray<uint32> Indices;
		bBaked = DreamCloudBake::GatherMesh(ShapeMesh, Positions, Indices) && DreamCloudBake::FromTriangles(Positions, Indices, Settings, Baked);
	}
	else
	{
		bBaked = DreamCloudBake::FromPrimitives(Primitives, PrimitiveBlend, Settings, Baked);
	}
	if (!bBaked)
	{
		UE_LOG(LogDreamCloud, Warning, TEXT("%s: nothing to bake a cloud from."), *GetPathName());
		return false;
	}
	Field = MoveTemp(Baked);
	UE_LOG(LogDreamCloud, Verbose, TEXT("%s: baked %dx%dx%d in %.1f ms."), *GetPathName(), Field.Resolution.X, Field.Resolution.Y, Field.Resolution.Z,
		(FPlatformTime::Seconds() - Start) * 1000.0);
	if (IsRegistered())
	{
		UploadField();
		UpdateCloudMaterial();
		UpdateBounds();
		MarkRenderTransformDirty();
		MarkRenderStateDirty();
	}
	return true;
}

void UDreamCloudVolumeComponent::UploadField()
{
	if (!Field.IsValid())
	{
		FieldTexture = nullptr;
		return;
	}
	const FIntVector N = Field.Resolution;
	UVolumeTexture* Texture = UVolumeTexture::CreateTransient(N.X, N.Y, N.Z, PF_R16F, MakeUniqueObjectName(GetTransientPackage(), UVolumeTexture::StaticClass(), TEXT("DreamCloudField")));
	if (!Texture)
	{
		return;
	}
	Texture->SRGB = false;
	Texture->Filter = TF_Bilinear;
	Texture->AddressMode = TA_Clamp;
	Texture->NeverStream = true;
	FTexture2DMipMap& Mip = Texture->GetPlatformData()->Mips[0];
	void* Data = Mip.BulkData.Lock(LOCK_READ_WRITE);
	FMemory::Memcpy(Data, Field.Distances.GetData(), Field.Distances.Num() * sizeof(uint16));
	Mip.BulkData.Unlock();
	Texture->UpdateResource();
	FieldTexture = Texture;
}

void UDreamCloudVolumeComponent::EnsureMaterial()
{
	if (CloudMaterial)
	{
		return;
	}
	UMaterialInterface* Base = LoadObject<UMaterialInterface>(nullptr, DreamCloud::Names::VolumeMaterialPath);
	if (!Base)
	{
		UE_LOG(LogDreamCloud, Warning, TEXT("%s: the cloud material %s is missing."), *GetPathName(), DreamCloud::Names::VolumeMaterialPath);
		return;
	}
	CloudMaterial = UMaterialInstanceDynamic::Create(Base, this);
	CloudMaterial->SetFlags(RF_Transient);
	SetMaterial(0, CloudMaterial);
}

void UDreamCloudVolumeComponent::UpdateCloudMaterial()
{
	if (!CloudMaterial)
	{
		return;
	}
	using namespace DreamCloud::Params;
	if (FieldTexture)
	{
		CloudMaterial->SetTextureParameterValue(DreamCloud::Params::Field, FieldTexture);
	}
	const FVector Size = Field.IsValid() ? Field.Size : FVector(100.0);
	CloudMaterial->SetVectorParameterValue(FieldBox, FLinearColor(float(Size.X), float(Size.Y), float(Size.Z), Field.IsValid() ? Field.TexelSize() : 10.0f));
	CloudMaterial->SetScalarParameterValue(DreamCloud::Params::Look, float(uint8(Look)));
	CloudMaterial->SetScalarParameterValue(DreamCloud::Params::Inflate, Inflate);
	CloudMaterial->SetScalarParameterValue(DreamCloud::Params::EdgeSoftness, EdgeSoftness);
	CloudMaterial->SetScalarParameterValue(DreamCloud::Params::Extinction, Extinction);
	CloudMaterial->SetScalarParameterValue(DreamCloud::Params::SunStrength, SunStrength);
	CloudMaterial->SetScalarParameterValue(DreamCloud::Params::AmbientStrength, AmbientStrength);
	CloudMaterial->SetVectorParameterValue(DreamCloud::Params::LitTint, LitTint);
	CloudMaterial->SetVectorParameterValue(DreamCloud::Params::ShadeTint, ShadeTint);
	CloudMaterial->SetScalarParameterValue(DreamCloud::Params::LobeScale, LobeScale);
	CloudMaterial->SetScalarParameterValue(DreamCloud::Params::ToneSoftness, ToneSoftness);
	CloudMaterial->SetScalarParameterValue(DreamCloud::Params::NoiseScale, NoiseScale);
	CloudMaterial->SetScalarParameterValue(DreamCloud::Params::NoiseErosion, NoiseErosion);
	CloudMaterial->SetVectorParameterValue(DreamCloud::Params::NoiseDrift, FLinearColor(float(NoiseDrift.X), float(NoiseDrift.Y), float(NoiseDrift.Z), 0.0f));
	CloudMaterial->SetScalarParameterValue(DreamCloud::Params::StepScale, StepScale);
}

FBox UDreamCloudVolumeComponent::GetFieldBox() const
{
	if (!Field.IsValid())
	{
		return FBox(FVector(-50.0), FVector(50.0));
	}
	return FBox(Field.Center - 0.5 * Field.Size, Field.Center + 0.5 * Field.Size);
}

FMatrix UDreamCloudVolumeComponent::GetRenderMatrix() const
{
	// The engine's cube (100 cm, centred) stretched over the field's box: the material's local space is the box's.
	const FBox Box = GetFieldBox();
	const FTransform BoxInComponent(FQuat::Identity, Box.GetCenter(), Box.GetSize() / 100.0);
	return (BoxInComponent * GetComponentTransform()).ToMatrixWithScale();
}

FBoxSphereBounds UDreamCloudVolumeComponent::CalcBounds(const FTransform& LocalToWorld) const
{
	return FBoxSphereBounds(GetFieldBox().TransformBy(LocalToWorld));
}

void UDreamCloudVolumeComponent::OnRegister()
{
	Super::OnRegister();
#if WITH_EDITOR
	// In the editor the shape may have changed since the field was saved (a mesh reimported): bake again then.
	const bool bBake = NeedsBake();
#else
	// In a game the saved field stands (its hash took editor-only data into account); bake only when there is none.
	const bool bBake = !Field.IsValid();
#endif
	if (bBake)
	{
		Rebake();
	}
	UploadField();
	EnsureMaterial();
	UpdateCloudMaterial();
	UpdateBounds();
	MarkRenderTransformDirty();
}

#if WITH_EDITOR
void UDreamCloudVolumeComponent::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);
	if (NeedsBake())
	{
		Rebake();
	}
	UpdateCloudMaterial();
}
#endif
