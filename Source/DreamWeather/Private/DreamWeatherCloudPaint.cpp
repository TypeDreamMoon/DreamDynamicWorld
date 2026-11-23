// Copyright DreamMoon. All Rights Reserved.

#include "DreamWeatherCloudPaint.h"
#include "DreamWeatherSubsystem.h"

#include "Components/BoxComponent.h"
#include "Engine/Texture2D.h"
#include "Engine/World.h"
#include "TextureResource.h"

namespace DreamCloudPaintImpl
{
	static constexpr int32 MinResolution = 16;
	static constexpr int32 MaxResolution = 2048;

	static uint8 ToByte(float Value)
	{
		return uint8(FMath::Clamp(FMath::RoundToInt(Value * 255.0f), 0, 255));
	}

	/** Bilinear between texel centres, clamped at the edges (the GPU's filter with clamp addressing). */
	static float Bilinear(const TArray<uint8>& Grid, int32 N, double U, double V)
	{
		const double X = U * N - 0.5;
		const double Y = V * N - 0.5;
		const int32 X0 = FMath::FloorToInt(X);
		const int32 Y0 = FMath::FloorToInt(Y);
		const float FX = float(X - X0);
		const float FY = float(Y - Y0);
		auto At = [&](int32 IX, int32 IY)
		{
			// Rows along V (Y), columns along U (X).
			return float(Grid[FMath::Clamp(IY, 0, N - 1) * N + FMath::Clamp(IX, 0, N - 1)]) / 255.0f;
		};
		const float A = FMath::Lerp(At(X0, Y0), At(X0 + 1, Y0), FX);
		const float B = FMath::Lerp(At(X0, Y0 + 1), At(X0 + 1, Y0 + 1), FX);
		return FMath::Lerp(A, B, FY);
	}
}

void UDreamCloudPainting::Initialize(int32 InResolution)
{
	Resolution = FMath::Clamp(InResolution, DreamCloudPaintImpl::MinResolution, DreamCloudPaintImpl::MaxResolution);
	Cover.Init(0, Resolution * Resolution);
	Amount.Init(0, Resolution * Resolution);
	MarkChanged();
}

void UDreamCloudPainting::Resize(int32 InResolution)
{
	const int32 NewResolution = FMath::Clamp(InResolution, DreamCloudPaintImpl::MinResolution, DreamCloudPaintImpl::MaxResolution);
	if (!IsValidPainting())
	{
		Initialize(NewResolution);
		return;
	}
	if (NewResolution == Resolution)
	{
		return;
	}
	TArray<uint8> NewCover, NewAmount;
	NewCover.SetNumUninitialized(NewResolution * NewResolution);
	NewAmount.SetNumUninitialized(NewResolution * NewResolution);
	for (int32 Y = 0; Y < NewResolution; ++Y)
	{
		for (int32 X = 0; X < NewResolution; ++X)
		{
			const double U = (X + 0.5) / NewResolution;
			const double V = (Y + 0.5) / NewResolution;
			NewCover[Y * NewResolution + X] = DreamCloudPaintImpl::ToByte(DreamCloudPaintImpl::Bilinear(Cover, Resolution, U, V));
			NewAmount[Y * NewResolution + X] = DreamCloudPaintImpl::ToByte(DreamCloudPaintImpl::Bilinear(Amount, Resolution, U, V));
		}
	}
	Resolution = NewResolution;
	Cover = MoveTemp(NewCover);
	Amount = MoveTemp(NewAmount);
	MarkChanged();
}

void UDreamCloudPainting::Fill(float InCover, float InAmount)
{
	if (!IsValidPainting())
	{
		Initialize(Resolution);
	}
	FMemory::Memset(Cover.GetData(), DreamCloudPaintImpl::ToByte(InCover), Cover.Num());
	FMemory::Memset(Amount.GetData(), DreamCloudPaintImpl::ToByte(InAmount), Amount.Num());
	MarkChanged();
}

bool UDreamCloudPainting::PaintAt(FVector2D UV, float Radius, float TargetCover, float Strength, bool bErase)
{
	if (!IsValidPainting())
	{
		Initialize(Resolution);
	}
	const int32 N = Resolution;
	const double R = FMath::Max(double(Radius), 0.5 / N);
	const float Target = FMath::Clamp(TargetCover, 0.0f, 1.0f);
	const float Peak = FMath::Clamp(Strength, 0.0f, 1.0f);
	const int32 X0 = FMath::Max(0, FMath::FloorToInt((UV.X - R) * N));
	const int32 X1 = FMath::Min(N - 1, FMath::CeilToInt((UV.X + R) * N));
	const int32 Y0 = FMath::Max(0, FMath::FloorToInt((UV.Y - R) * N));
	const int32 Y1 = FMath::Min(N - 1, FMath::CeilToInt((UV.Y + R) * N));
	bool bChanged = false;
	for (int32 Y = Y0; Y <= Y1; ++Y)
	{
		for (int32 X = X0; X <= X1; ++X)
		{
			const double D = FVector2D::Distance(FVector2D((X + 0.5) / N, (Y + 0.5) / N), UV) / R;
			if (D >= 1.0)
			{
				continue;
			}
			// Smooth to nothing at the rim.
			const float W = Peak * float(FMath::Square(1.0 - D * D));
			const int32 I = Y * N + X;
			const float A = Amount[I] / 255.0f;
			const float C = Cover[I] / 255.0f;
			float NewA, NewC = C;
			if (bErase)
			{
				NewA = A * (1.0f - W);
			}
			else
			{
				// Laid over what is there as paint over paint ("over", premultiplied): a bare texel takes the target outright,
				// a fully painted one mixes W of it in.
				NewA = W + A * (1.0f - W);
				NewC = NewA > 1.0e-4f ? (Target * W + C * A * (1.0f - W)) / NewA : C;
			}
			const uint8 BA = DreamCloudPaintImpl::ToByte(NewA);
			const uint8 BC = DreamCloudPaintImpl::ToByte(NewC);
			if (BA != Amount[I] || BC != Cover[I])
			{
				Amount[I] = BA;
				Cover[I] = BC;
				bChanged = true;
			}
		}
	}
	if (bChanged)
	{
		MarkChanged();
	}
	return bChanged;
}

FVector2f UDreamCloudPainting::Sample(FVector2D UV) const
{
	if (!IsValidPainting() || UV.X < 0.0 || UV.Y < 0.0 || UV.X > 1.0 || UV.Y > 1.0)
	{
		return FVector2f::ZeroVector;
	}
	return FVector2f(DreamCloudPaintImpl::Bilinear(Cover, Resolution, UV.X, UV.Y), DreamCloudPaintImpl::Bilinear(Amount, Resolution, UV.X, UV.Y));
}

bool UDreamCloudPainting::IsValidPainting() const
{
	return Resolution >= DreamCloudPaintImpl::MinResolution && Cover.Num() == Resolution * Resolution && Amount.Num() == Cover.Num();
}

UTexture2D* UDreamCloudPainting::GetTexture()
{
	if (!IsValidPainting() || !FApp::CanEverRender())
	{
		return nullptr;
	}
	const int32 N = Resolution;
	TArray<uint8> Texels;
	Texels.SetNumUninitialized(N * N * 2);
	for (int32 I = 0; I < N * N; ++I)
	{
		Texels[I * 2 + 0] = Cover[I];
		Texels[I * 2 + 1] = Amount[I];
	}
	if (!Texture || Texture->GetSizeX() != N)
	{
		Texture = UTexture2D::CreateTransient(N, N, PF_R8G8, MakeUniqueObjectName(GetTransientPackage(), UTexture2D::StaticClass(), TEXT("DreamCloudPaint")));
		if (!Texture)
		{
			return nullptr;
		}
		Texture->SRGB = false;
		Texture->Filter = TF_Bilinear;
		Texture->AddressX = TA_Clamp;
		Texture->AddressY = TA_Clamp;
		FTexture2DMipMap& Mip = Texture->GetPlatformData()->Mips[0];
		FMemory::Memcpy(Mip.BulkData.Lock(LOCK_READ_WRITE), Texels.GetData(), Texels.Num());
		Mip.BulkData.Unlock();
		Texture->UpdateResource();
		TextureRevision = Revision;
		return Texture;
	}
	if (TextureRevision != Revision)
	{
		// The whole texture again (a few hundred KB at most), the copy freed once the render thread has it.
		uint8* Copy = static_cast<uint8*>(FMemory::Malloc(Texels.Num()));
		FMemory::Memcpy(Copy, Texels.GetData(), Texels.Num());
		FUpdateTextureRegion2D* Region = new FUpdateTextureRegion2D(0, 0, 0, 0, N, N);
		Texture->UpdateTextureRegions(0, 1, Region, uint32(N * 2), 2, Copy, [](uint8* Data, const FUpdateTextureRegion2D* Regions)
		{
			FMemory::Free(Data);
			delete Regions;
		});
		TextureRevision = Revision;
	}
	return Texture;
}

void UDreamCloudPainting::MarkChanged()
{
	++Revision;
}

void UDreamCloudPainting::PostLoad()
{
	Super::PostLoad();
	if (!IsValidPainting())
	{
		Initialize(Resolution);
	}
	MarkChanged();
}

#if WITH_EDITOR
void UDreamCloudPainting::PostEditUndo()
{
	Super::PostEditUndo();
	MarkChanged();
}
#endif

// ---- Component --------------------------------------------------------------------------------------------------------

UDreamCloudPaintComponent::UDreamCloudPaintComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
}

FVector2D UDreamCloudPaintComponent::WorldToUV(const FVector& World) const
{
	const FVector Center = GetComponentLocation();
	const double S = FMath::Max(Size, 1.0);
	return FVector2D((World.X - Center.X) / S + 0.5, (World.Y - Center.Y) / S + 0.5);
}

FVector UDreamCloudPaintComponent::UVToWorld(const FVector2D& UV) const
{
	const FVector Center = GetComponentLocation();
	return FVector(Center.X + (UV.X - 0.5) * Size, Center.Y + (UV.Y - 0.5) * Size, Center.Z);
}

FVector4f UDreamCloudPaintComponent::GetFootprintRecord() const
{
	const FVector Center = GetComponentLocation();
	const double S = FMath::Max(Size, 1.0);
	return FVector4f(float(Center.X - 0.5 * S), float(Center.Y - 0.5 * S), float(1.0 / S), float(1.0 / S));
}

bool UDreamCloudPaintComponent::IsUsable() const
{
	return bEnabled && Strength > 0.0f && Paint && Paint->IsValidPainting();
}

void UDreamCloudPaintComponent::ApplyTo(const FVector& Position, float& InOutCover) const
{
	if (!IsUsable())
	{
		return;
	}
	const FVector2f CA = Paint->Sample(WorldToUV(Position));
	InOutCover = FMath::Lerp(InOutCover, CA.X, FMath::Clamp(CA.Y * Strength, 0.0f, 1.0f));
}

void UDreamCloudPaintComponent::OnRegister()
{
	Super::OnRegister();
	if (UDreamWeatherSubsystem* Weather = UDreamWeatherSubsystem::Get(this))
	{
		Weather->RegisterCloudPaint(this);
	}
}

void UDreamCloudPaintComponent::OnUnregister()
{
	if (UWorld* World = GetWorld())
	{
		if (UDreamWeatherSubsystem* Weather = World->GetSubsystem<UDreamWeatherSubsystem>())
		{
			Weather->UnregisterCloudPaint(this);
		}
	}
	Super::OnUnregister();
}

// ---- Actor ------------------------------------------------------------------------------------------------------------

ADreamCloudPaint::ADreamCloudPaint()
{
	CloudPaint = CreateDefaultSubobject<UDreamCloudPaintComponent>(TEXT("CloudPaint"));
	RootComponent = CloudPaint;
#if WITH_EDITORONLY_DATA
	Footprint = CreateEditorOnlyDefaultSubobject<UBoxComponent>(TEXT("Footprint"));
	if (Footprint)
	{
		Footprint->SetupAttachment(CloudPaint);
		Footprint->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Footprint->SetCanEverAffectNavigation(false);
		Footprint->SetHiddenInGame(true);
		Footprint->bIsEditorOnly = true;
		Footprint->SetAbsolute(false, true, true);
		Footprint->ShapeColor = FColor(150, 170, 190);
		Footprint->SetBoxExtent(FVector(0.5 * CloudPaint->Size, 0.5 * CloudPaint->Size, 5000.0), false);
	}
#endif
}

void ADreamCloudPaint::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);
#if WITH_EDITORONLY_DATA
	if (Footprint && CloudPaint)
	{
		// The footprint ignores rotation and scale: the box shows it as the weather uses it.
		Footprint->SetWorldRotation(FRotator::ZeroRotator);
		Footprint->SetWorldScale3D(FVector::OneVector);
		Footprint->SetBoxExtent(FVector(0.5 * CloudPaint->Size, 0.5 * CloudPaint->Size, 5000.0));
	}
#endif
}
