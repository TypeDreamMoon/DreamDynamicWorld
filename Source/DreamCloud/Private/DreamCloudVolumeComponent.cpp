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
#include "DreamCloudLighting.h"
#include "DreamCloudSubsystem.h"
#include "Engine/TextureRenderTargetVolume.h"
#include "Engine/World.h"
#include "Misc/App.h"
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
	static const FName LightMap(TEXT("LightMap"));
	static const FName LightMapValid(TEXT("LightMapValid"));
	static const FName Holes(TEXT("Holes"));
	static const FName HolesValid(TEXT("HolesValid"));
	static const FName HoleEdgeNoise(TEXT("HoleEdgeNoise"));
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
			Hash = HashCombine(Hash, GetTypeHash(S.bSubtract));
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
	ResetCarving();
	bLightStale = true;
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
	CloudMaterial->SetScalarParameterValue(DreamCloud::Params::StepScale, StepScale * DreamCloud::GetQuality().StepScale * CoverageStepScale);
	CloudMaterial->SetScalarParameterValue(DreamCloud::Params::LightMapValid, DreamCloud::DebugView() > 0 ? float(DreamCloud::DebugView()) : LightMap && bLightMapDrawn && DreamCloud::UseLightMap() ? 1.0f : 0.0f);
	if (LightMap)
	{
		CloudMaterial->SetTextureParameterValue(DreamCloud::Params::LightMap, LightMap);
	}
	CloudMaterial->SetScalarParameterValue(DreamCloud::Params::HolesValid, HoleTexture ? 1.0f : 0.0f);
	if (HoleTexture)
	{
		CloudMaterial->SetTextureParameterValue(DreamCloud::Params::Holes, HoleTexture);
	}
	CloudMaterial->SetScalarParameterValue(DreamCloud::Params::HoleEdgeNoise, HoleEdgeNoise);
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
	if (!bBake || !Rebake())
	{
		UploadField();
	}
	ResetCarving();
	bLightStale = true;
	if (UDreamCloudSubsystem* Clouds = GetWorld() ? GetWorld()->GetSubsystem<UDreamCloudSubsystem>() : nullptr)
	{
		Clouds->RegisterVolume(this);
	}
	EnsureMaterial();
	UpdateCloudMaterial();
	UpdateBounds();
	MarkRenderTransformDirty();
}

void UDreamCloudVolumeComponent::OnUnregister()
{
	if (UDreamCloudSubsystem* Clouds = GetWorld() ? GetWorld()->GetSubsystem<UDreamCloudSubsystem>() : nullptr)
	{
		Clouds->UnregisterVolume(this);
	}
	Super::OnUnregister();
}

#if WITH_EDITOR
void UDreamCloudVolumeComponent::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);
	bLightStale = true;
	if (NeedsBake())
	{
		Rebake();
	}
	UpdateCloudMaterial();
}
#endif

float UDreamCloudVolumeComponent::MeanScale() const
{
	const FVector S = GetComponentScale().GetAbs();
	return FMath::Max(float((S.X + S.Y + S.Z) / 3.0), 1.0e-4f);
}

void UDreamCloudVolumeComponent::ResetCarving()
{
	HoleGridSize = Field.IsValid() ? DreamCloudLighting::ResolutionFor(Field.Size, HoleResolution) : FIntVector::ZeroValue;
	Holes.Reset();
	Holes.SetNumZeroed(HoleGridSize.X * HoleGridSize.Y * HoleGridSize.Z);
	bHolesLive = false;
	bHolesChanged = false;
	if (HoleTexture)
	{
		HoleTexture = nullptr;
		UpdateCloudMaterial();
	}
}

void UDreamCloudVolumeComponent::ClearCarving()
{
	for (float& V : Holes)
	{
		V = 0.0f;
	}
	bHolesLive = false;
	bHolesChanged = true;
}

void UDreamCloudVolumeComponent::Carve(FVector WorldStart, FVector WorldEnd, float Radius, float Strength)
{
	if (!bCarvable || !Field.IsValid() || Holes.Num() == 0 || Radius <= 0.0f || Strength <= 0.0f)
	{
		return;
	}
	// In the shape's space, relative to the box's centre, as the grid and the material are.
	const FTransform& ToWorld = GetComponentTransform();
	const FVector A = ToWorld.InverseTransformPosition(WorldStart) - Field.Center;
	const FVector B = ToWorld.InverseTransformPosition(WorldEnd) - Field.Center;
	const float R = Radius / MeanScale();
	const float Feather = 0.35f * R;
	// Stamped above 1 by the hold: the material sees a clean passage until the value has decayed back to 1.
	const float Peak = FMath::Clamp(Strength, 0.0f, 1.0f) * (1.0f + HoldTime / FMath::Max(RefillTime, 0.1f));
	const FIntVector N = HoleGridSize;
	const FVector Cell = Field.Size / FVector(N);
	FIntVector Lo;
	FIntVector Hi;
	for (int32 Axis = 0; Axis < 3; ++Axis)
	{
		const double Min = (FMath::Min(A[Axis], B[Axis]) - R + 0.5 * Field.Size[Axis]) / Cell[Axis] - 0.5;
		const double Max = (FMath::Max(A[Axis], B[Axis]) + R + 0.5 * Field.Size[Axis]) / Cell[Axis] - 0.5;
		Lo[Axis] = FMath::Clamp(FMath::FloorToInt(Min), 0, N[Axis] - 1);
		Hi[Axis] = FMath::Clamp(FMath::CeilToInt(Max), 0, N[Axis] - 1);
	}
	bool bAny = false;
	for (int32 Z = Lo.Z; Z <= Hi.Z; ++Z)
	{
		for (int32 Y = Lo.Y; Y <= Hi.Y; ++Y)
		{
			for (int32 X = Lo.X; X <= Hi.X; ++X)
			{
				const FVector P = FVector(X + 0.5, Y + 0.5, Z + 0.5) * Cell - 0.5 * Field.Size;
				const float D = float(FMath::PointDistToSegment(P, A, B));
				const float W = FMath::Clamp((R - D) / Feather, 0.0f, 1.0f);
				if (W > 0.0f)
				{
					float& V = Holes[X + N.X * (Y + N.Y * Z)];
					if (Peak * W > V)
					{
						V = Peak * W;
						bAny = true;
					}
				}
			}
		}
	}
	if (bAny)
	{
		bHolesLive = true;
		bHolesChanged = true;
	}
}

float UDreamCloudVolumeComponent::GetCarving(FVector WorldPoint) const
{
	if (!Field.IsValid() || Holes.Num() == 0)
	{
		return 0.0f;
	}
	const FVector P = GetComponentTransform().InverseTransformPosition(WorldPoint) - Field.Center;
	return FMath::Clamp(DreamCloudLighting::SampleGrid(Holes, HoleGridSize, Field.Size, P), 0.0f, 1.0f);
}

void UDreamCloudVolumeComponent::UploadHoles()
{
	if (!FApp::CanEverRender() || Holes.Num() == 0)
	{
		return;
	}
	const FIntVector N = HoleGridSize;
	if (!HoleTexture || HoleTexture->SizeX != N.X || HoleTexture->SizeY != N.Y || HoleTexture->SizeZ != N.Z)
	{
		HoleTexture = NewObject<UTextureRenderTargetVolume>(this, NAME_None, RF_Transient);
		HoleTexture->ClearColor = FLinearColor::Black;
		HoleTexture->Init(N.X, N.Y, N.Z, PF_G8);
		HoleTexture->UpdateResourceImmediate(true);
		UpdateCloudMaterial();
	}
	TArray<uint8> Texels;
	Texels.SetNumUninitialized(Holes.Num());
	for (int32 i = 0; i < Holes.Num(); ++i)
	{
		Texels[i] = uint8(FMath::Clamp(Holes[i], 0.0f, 1.0f) * 255.0f + 0.5f);
	}
	DreamCloudLighting::EnqueueHoleUpload(HoleTexture->GameThread_GetRenderTargetResource(), N, MoveTemp(Texels));
}

UDreamCloudVolumeComponent::FLightState UDreamCloudVolumeComponent::GetLightState(const FVector& LightDirection) const
{
	FLightState State;
	const FVector Local = GetComponentTransform().InverseTransformVector(LightDirection.GetSafeNormal());
	const double Length = FMath::Max(Local.Size(), 1.0e-6);
	State.Direction = FVector3f(Local / Length);
	// A cm of the shape's space toward the light is 1 / Length cm in the world.
	State.SigmaPerUnit = Extinction * 0.01f / float(Length);
	State.Inflate = Inflate / MeanScale();
	State.Edge = FMath::Max(EdgeSoftness / MeanScale(), 1.0e-3f);
	// The noise as the material erodes with it (DreamCloudVolume.ush): in the shape's space, drifting with the world's time.
	State.ErodeDepth = NoiseErosion / MeanScale();
	State.InvNoise = MeanScale() / FMath::Max(NoiseScale, 1.0f);
	const UWorld* World = GetWorld();
	State.Drift = FVector3f(NoiseDrift * (World ? World->GetTimeSeconds() : 0.0) / MeanScale());
	return State;
}

void UDreamCloudVolumeComponent::TickCloud(float DeltaTime, const FVector& LightDirection, const FVector* ViewLocation)
{
	if (!Field.IsValid())
	{
		return;
	}
	// Passages grow back.
	if (bHolesLive)
	{
		const float Fill = DeltaTime / FMath::Max(RefillTime, 0.1f);
		bool bAny = false;
		for (float& V : Holes)
		{
			if (V > 0.0f)
			{
				V = FMath::Max(V - Fill, 0.0f);
				bAny |= V > 0.0f;
			}
		}
		bHolesLive = bAny;
		bHolesChanged = true;
	}
	HoleUploadWait -= DeltaTime;
	LightHoleWait -= DeltaTime;
	bool bHolesUploaded = false;
	if (bHolesChanged && HoleUploadWait <= 0.0f)
	{
		UploadHoles();
		bHolesChanged = false;
		HoleUploadWait = 1.0f / 30.0f;
		bHolesUploaded = true;
	}

	// The light map follows the light (and the carving, ten times a second at most).
	const DreamCloud::FQuality Quality = DreamCloud::GetQuality();
	const FLightState Light = GetLightState(LightDirection);
	const int32 LightResolution = Quality.LightMapResolution;
	const float Turned = FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(FVector3f::DotProduct(Light.Direction, LastLight.Direction), -1.0f, 1.0f)));
	const bool bSigma = FMath::Abs(Light.SigmaPerUnit - LastLight.SigmaPerUnit) > 0.01f * FMath::Max(LastLight.SigmaPerUnit, 1.0e-9f);
	const bool bShape = !FMath::IsNearlyEqual(Light.Inflate, LastLight.Inflate, 0.01f) || !FMath::IsNearlyEqual(Light.Edge, LastLight.Edge, 0.01f);
	// The noise drifts through the cloud: draw the map again once it moved half a texel of the map.
	const float MapTexel = float(Field.Size.GetMax()) / FMath::Max(LightResolution, 1);
	const bool bNoise = !FMath::IsNearlyEqual(Light.ErodeDepth, LastLight.ErodeDepth, 0.01f) || !FMath::IsNearlyEqual(Light.InvNoise, LastLight.InvNoise, 1.0e-7f)
		|| FVector3f::Dist(Light.Drift, LastLight.Drift) > 0.5f * MapTexel;
	const bool bHoles = bHolesUploaded && LightHoleWait <= 0.0f;
	if (bLightStale || Turned > LightUpdateAngle || bSigma || bShape || bNoise || bHoles || LightResolution != LastLightResolution)
	{
		LastLight = Light;
		LastLightResolution = LightResolution;
		bLightStale = false;
		++LightMapUpdates;
		if (bHoles)
		{
			LightHoleWait = 0.1f;
		}
		if (FApp::CanEverRender() && FieldTexture && FieldTexture->GetResource())
		{
			DreamCloudLighting::FLightMapParams Params;
			Params.LightDir = Light.Direction;
			Params.SigmaPerUnit = Light.SigmaPerUnit;
			Params.Inflate = Light.Inflate;
			Params.Edge = Light.Edge;
			Params.ErodeDepth = Light.ErodeDepth;
			Params.InvNoise = Light.InvNoise;
			Params.Drift = Light.Drift;
			Params.Resolution = DreamCloudLighting::ResolutionFor(Field.Size, LightResolution);
			Params.Steps = Quality.LightMapSteps;
			if (!LightMap || LightMap->SizeX != Params.Resolution.X || LightMap->SizeY != Params.Resolution.Y || LightMap->SizeZ != Params.Resolution.Z)
			{
				LightMap = NewObject<UTextureRenderTargetVolume>(this, NAME_None, RF_Transient);
				LightMap->ClearColor = FLinearColor::White;
				LightMap->bCanCreateUAV = true;
				LightMap->Init(Params.Resolution.X, Params.Resolution.Y, Params.Resolution.Z, PF_R16F);
				LightMap->UpdateResourceImmediate(true);
				bLightMapDrawn = false;
			}
			if (!NoiseTexture)
			{
				NoiseTexture = LoadObject<UVolumeTexture>(nullptr, DreamCloud::Names::NoisePath);
			}
			DreamCloudLighting::EnqueueLightMap(FieldTexture->GetResource(), NoiseTexture ? NoiseTexture->GetResource() : nullptr, HoleTexture ? HoleTexture->GameThread_GetRenderTargetResource() : nullptr,
				LightMap->GameThread_GetRenderTargetResource(), Field.Size, Params);
			if (!bLightMapDrawn)
			{
				bLightMapDrawn = true;
				UpdateCloudMaterial();
			}
		}
	}

	// Longer steps while the cloud fills much of the view (it costs per pixel), times the quality level's.
	float Coverage = 0.0f;
	if (ViewLocation)
	{
		const double Distance = FVector::Dist(*ViewLocation, Bounds.Origin);
		const double Radius = Bounds.SphereRadius;
		Coverage = Distance <= Radius ? 1.0f : FMath::Clamp(float(FMath::Asin(Radius / Distance) / (0.25 * UE_PI)), 0.0f, 1.0f);
	}
	const float WantedCoverage = FMath::Lerp(1.0f, 1.75f, FMath::SmoothStep(0.35f, 1.0f, Coverage));
	if (FMath::Abs(WantedCoverage - CoverageStepScale) > 0.05f || Quality.StepScale != LastQualityStep || DreamCloud::UseLightMap() != bUsedLightMap || DreamCloud::DebugView() != UsedDebugView)
	{
		CoverageStepScale = WantedCoverage;
		LastQualityStep = Quality.StepScale;
		bUsedLightMap = DreamCloud::UseLightMap();
		UsedDebugView = DreamCloud::DebugView();
		UpdateCloudMaterial();
	}
}

void UDreamCloudVolumeComponent::Serialize(FArchive& Ar)
{
	if (Ar.IsSaving())
	{
		// Only a mesh's field is saved, packed (a game may not have the mesh's triangles); primitives bake again where
		// they are loaded, in a few milliseconds.
		if (ShapeSource == EDreamCloudShapeSource::Mesh)
		{
			Field.Pack();
		}
		else
		{
			Field.Packed.Reset();
		}
	}
	Super::Serialize(Ar);
	if (Ar.IsLoading() && Field.Packed.Num() > 0)
	{
		Field.Unpack();
	}
}
