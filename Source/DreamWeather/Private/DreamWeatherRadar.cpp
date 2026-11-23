// Copyright DreamMoon. All Rights Reserved.

#include "DreamWeatherRadar.h"
#include "DreamWeatherSubsystem.h"
#include "DreamWorldSubsystem.h"

#include "Camera/PlayerCameraManager.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/KismetRenderingLibrary.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "UObject/ConstructorHelpers.h"
#include "Rendering/DrawElements.h"
#include "Widgets/SLeafWidget.h"

namespace DreamWeather::RadarNames
{
	static const TCHAR* MaterialPath = TEXT("/DreamDynamicWorld/Weather/M_DreamWeatherRadar.M_DreamWeatherRadar");
}

UDreamWeatherRadar* UDreamWeatherRadar::Get(const UObject* WorldContextObject)
{
	const UWorld* World = WorldContextObject ? WorldContextObject->GetWorld() : nullptr;
	return World ? World->GetSubsystem<UDreamWeatherRadar>() : nullptr;
}

bool UDreamWeatherRadar::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE || WorldType == EWorldType::Editor;
}

TStatId UDreamWeatherRadar::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UDreamWeatherRadar, STATGROUP_Tickables);
}

UMaterialInterface* UDreamWeatherRadar::GetRadarMaterial()
{
	return LoadObject<UMaterialInterface>(nullptr, DreamWeather::RadarNames::MaterialPath);
}

UTextureRenderTarget2D* UDreamWeatherRadar::GetRadarTexture()
{
	const int32 Size = FMath::Clamp(Look.Resolution, 64, 2048);
	if (!Texture || Texture->SizeX != Size)
	{
		Texture = NewObject<UTextureRenderTarget2D>(this, TEXT("DreamWeatherRadar"), RF_Transient);
		Texture->RenderTargetFormat = RTF_RGBA8;
		Texture->ClearColor = Look.Background;
		Texture->InitAutoFormat(Size, Size);
		Texture->UpdateResourceImmediate(true);
		SinceDrawn = 1.0e9;
	}
	return Texture;
}

void UDreamWeatherRadar::SetLook(const FDreamWeatherRadarLook& InLook)
{
	Look = InLook;
	SinceDrawn = 1.0e9;
}

void UDreamWeatherRadar::SetCenter(FVector World)
{
	HeldCenter = World;
	SinceDrawn = 1.0e9;
}

void UDreamWeatherRadar::ClearCenter()
{
	HeldCenter.Reset();
	SinceDrawn = 1.0e9;
}

FVector UDreamWeatherRadar::GetCenter() const
{
	if (HeldCenter.IsSet())
	{
		return HeldCenter.GetValue();
	}
	const UWorld* World = GetWorld();
	const APlayerController* PC = World ? World->GetFirstPlayerController() : nullptr;
	if (PC && PC->PlayerCameraManager)
	{
		return PC->PlayerCameraManager->GetCameraLocation();
	}
	// (No view -- a server, a test --: the world's origin.)
	return FVector::ZeroVector;
}

FVector2D UDreamWeatherRadar::NorthDirection() const
{
	const UDreamWorldSubsystem* Clock = GetWorld() ? GetWorld()->GetSubsystem<UDreamWorldSubsystem>() : nullptr;
	const double Yaw = FMath::DegreesToRadians(Clock ? double(Clock->GetCelestialSettings().NorthYaw) : 0.0);
	return FVector2D(FMath::Cos(Yaw), FMath::Sin(Yaw));
}

FVector2D UDreamWeatherRadar::WorldToRadar(FVector World) const
{
	// (North up, east right: the picture's y runs south.)
	const FVector2D North = NorthDirection();
	const FVector2D East(-North.Y, North.X);
	const FVector2D Offset = FVector2D(World - GetCenter()) / (FMath::Max(Look.RangeKm, 1.0f) * 100000.0);
	return FVector2D(0.5 + 0.5 * FVector2D::DotProduct(Offset, East), 0.5 - 0.5 * FVector2D::DotProduct(Offset, North));
}

FVector UDreamWeatherRadar::RadarToWorld(FVector2D UV) const
{
	const FVector2D North = NorthDirection();
	const FVector2D East(-North.Y, North.X);
	const FVector2D P = (UV - FVector2D(0.5, 0.5)) * 2.0;
	const FVector Center = GetCenter();
	const FVector2D Offset = (P.X * East - P.Y * North) * (FMath::Max(Look.RangeKm, 1.0f) * 100000.0);
	return FVector(Center.X + Offset.X, Center.Y + Offset.Y, Center.Z);
}

EDreamWeatherRadarLevel UDreamWeatherRadar::LevelFor(const FDreamWeatherRadarLook& InLook, float Precipitation)
{
	if (Precipitation >= InLook.ExtremeFrom) { return EDreamWeatherRadarLevel::Extreme; }
	if (Precipitation >= InLook.HeavyFrom) { return EDreamWeatherRadarLevel::Heavy; }
	if (Precipitation >= InLook.ModerateFrom) { return EDreamWeatherRadarLevel::Moderate; }
	if (Precipitation >= InLook.LightFrom) { return EDreamWeatherRadarLevel::Light; }
	return EDreamWeatherRadarLevel::None;
}

FDreamWeatherRadarSample UDreamWeatherRadar::SampleRadar(FVector World) const
{
	FDreamWeatherRadarSample Out;
	if (const UDreamWeatherSubsystem* Weather = UDreamWeatherSubsystem::Get(this))
	{
		const FDreamWeatherSample S = Weather->SampleWeather(World);
		Out.Precipitation = S.Precipitation;
		Out.CloudCover = S.CloudCoverage;
		Out.CloudDarkness = S.CloudDarkness;
		Out.SnowShare = S.SnowRatio;
		Out.Level = LevelFor(Look, S.Precipitation);
	}
	return Out;
}

bool UDreamWeatherRadar::FindNearestPrecipitation(FVector From, float RadiusKm, float MinPrecipitation, FVector& OutLocation, float& OutDistanceKm) const
{
	const UDreamWeatherSubsystem* Weather = UDreamWeatherSubsystem::Get(this);
	if (!Weather)
	{
		return false;
	}
	// Ring by ring outward on a 1 km grid: the first ring with any rain holds the nearest (the nearest of it).
	const int32 Reach = FMath::Clamp(FMath::CeilToInt(RadiusKm), 0, 200);
	for (int32 Ring = 0; Ring <= Reach; ++Ring)
	{
		bool bFound = false;
		double Best = TNumericLimits<double>::Max();
		for (int32 Y = -Ring; Y <= Ring; ++Y)
		{
			for (int32 X = -Ring; X <= Ring; ++X)
			{
				if (FMath::Max(FMath::Abs(X), FMath::Abs(Y)) != Ring)
				{
					continue;
				}
				const double Km = FMath::Sqrt(double(X * X + Y * Y));
				if (Km > RadiusKm || Km >= Best)
				{
					continue;
				}
				const FVector At = From + FVector(X * 100000.0, Y * 100000.0, 0.0);
				if (Weather->SampleWeather(At).Precipitation >= MinPrecipitation)
				{
					bFound = true;
					Best = Km;
					OutLocation = At;
				}
			}
		}
		if (bFound)
		{
			OutDistanceKm = float(Best);
			return true;
		}
	}
	return false;
}

void UDreamWeatherRadar::ApplyTo(UMaterialInstanceDynamic* Material) const
{
	if (!Material)
	{
		return;
	}
	const FVector Center = GetCenter();
	const FVector2D North = NorthDirection();
	const FDreamWeatherRadarLook& L = Look;
	auto Step = [](const FLinearColor& C, float From) { return FLinearColor(C.R, C.G, C.B, FMath::Clamp(From, 0.0f, 1.0f)); };
	Material->SetVectorParameterValue(TEXT("RadarView"), FLinearColor(float(Center.X), float(Center.Y), FMath::Max(L.RangeKm, 1.0f) * 100000.0f, 0.0f));
	Material->SetVectorParameterValue(TEXT("RadarFrame"), FLinearColor(float(North.X), float(North.Y), L.RingKm > 0.0f ? L.RingKm * 100000.0f : 1.0e12f,
		FMath::Clamp(L.MarksOpacity, 0.0f, 1.0f)));
	Material->SetVectorParameterValue(TEXT("RadarLight"), Step(L.LightColor, L.LightFrom));
	Material->SetVectorParameterValue(TEXT("RadarModerate"), Step(L.ModerateColor, L.ModerateFrom));
	Material->SetVectorParameterValue(TEXT("RadarHeavy"), Step(L.HeavyColor, L.HeavyFrom));
	Material->SetVectorParameterValue(TEXT("RadarExtreme"), Step(L.ExtremeColor, L.ExtremeFrom));
	Material->SetVectorParameterValue(TEXT("RadarCloud"), FLinearColor(L.CloudColor.R, L.CloudColor.G, L.CloudColor.B, FMath::Clamp(L.CloudOpacity, 0.0f, 1.0f)));
	Material->SetVectorParameterValue(TEXT("RadarBackground"), FLinearColor(L.Background.R, L.Background.G, L.Background.B, FMath::Clamp(L.Outside, 0.0f, 1.0f)));
	// (Snow as the view's precipitation falls: the weather map holds no snow of its own.)
	const UDreamWeatherSubsystem* Weather = UDreamWeatherSubsystem::Get(this);
	const float Snow = Weather ? FMath::Clamp(Weather->GetViewSample().SnowRatio, 0.0f, 1.0f) : 0.0f;
	Material->SetVectorParameterValue(TEXT("RadarSnow"), FLinearColor(L.SnowColor.R, L.SnowColor.G, L.SnowColor.B, Snow));
	Material->SetVectorParameterValue(TEXT("RadarMarks"), L.MarksColor);
}

void UDreamWeatherRadar::Tick(float DeltaTime)
{
	if (!Texture)
	{
		return;
	}
	SinceDrawn += FApp::GetDeltaTime();
	if (SinceDrawn < FMath::Max(Look.RefreshSeconds, 0.0f))
	{
		return;
	}
	SinceDrawn = 0.0;
	if (!DrawMaterial)
	{
		if (UMaterialInterface* Material = GetRadarMaterial())
		{
			DrawMaterial = UMaterialInstanceDynamic::Create(Material, this);
		}
	}
	if (DrawMaterial && FApp::CanEverRender())
	{
		ApplyTo(DrawMaterial);
		UKismetRenderingLibrary::DrawMaterialToRenderTarget(this, Texture, DrawMaterial);
	}
}

// ---- UMG ----------------------------------------------------------------------------------------------------------------

void UDreamWeatherRadarImage::SynchronizeProperties()
{
	Super::SynchronizeProperties();
	const UWorld* World = GetWorld();
	if (!World || !World->IsGameWorld())
	{
		return;
	}
	if (UDreamWeatherRadar* Radar = UDreamWeatherRadar::Get(this))
	{
		if (RangeKm > 0.0f)
		{
			FDreamWeatherRadarLook L = Radar->GetLook();
			L.RangeKm = RangeKm;
			Radar->SetLook(L);
		}
		SetBrushResourceObject(Radar->GetRadarTexture());
	}
}

namespace DreamWeatherRadarImpl
{
	/** The radar image's brush drawn opaque. */
	class SRadarImage : public SLeafWidget
	{
	public:
		SLATE_BEGIN_ARGS(SRadarImage) {}
			SLATE_ARGUMENT(TWeakObjectPtr<UDreamWeatherRadarImage>, Owner)
		SLATE_END_ARGS()

		void Construct(const FArguments& InArgs) { Owner = InArgs._Owner; }

		virtual FVector2D ComputeDesiredSize(float) const override
		{
			const UDreamWeatherRadarImage* Image = Owner.Get();
			return Image ? FVector2D(Image->GetBrush().GetImageSize()) : FVector2D(256.0, 256.0);
		}

		virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& Geometry, const FSlateRect& Culling, FSlateWindowElementList& Elements, int32 LayerId,
			const FWidgetStyle& Style, bool bParentEnabled) const override
		{
			const UDreamWeatherRadarImage* Image = Owner.Get();
			if (Image && Image->GetBrush().GetResourceObject())
			{
				FSlateDrawElement::MakeBox(Elements, LayerId, Geometry.ToPaintGeometry(), &Image->GetBrush(), ESlateDrawEffect::IgnoreTextureAlpha,
					Image->GetColorAndOpacity() * Style.GetColorAndOpacityTint());
			}
			return LayerId;
		}

	private:
		TWeakObjectPtr<UDreamWeatherRadarImage> Owner;
	};
}

TSharedRef<SWidget> UDreamWeatherRadarImage::RebuildWidget()
{
	return SNew(DreamWeatherRadarImpl::SRadarImage).Owner(this);
}

#if WITH_EDITOR
const FText UDreamWeatherRadarImage::GetPaletteCategory()
{
	return NSLOCTEXT("DreamWeather", "RadarPalette", "Dream Weather");
}
#endif

// ---- The display ------------------------------------------------------------------------------------------------------

ADreamWeatherRadarDisplay::ADreamWeatherRadarDisplay()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.TickInterval = 0.25f;
	Screen = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Screen"));
	RootComponent = Screen;
	static ConstructorHelpers::FObjectFinder<UStaticMesh> Plane(TEXT("/Engine/BasicShapes/Plane.Plane"));
	if (Plane.Succeeded())
	{
		Screen->SetStaticMesh(Plane.Object);
	}
	Screen->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Screen->SetCastShadow(false);
}

void ADreamWeatherRadarDisplay::BeginPlay()
{
	Super::BeginPlay();
	if (UMaterialInterface* Radar = UDreamWeatherRadar::GetRadarMaterial())
	{
		Material = UMaterialInstanceDynamic::Create(Radar, this);
		Screen->SetMaterial(0, Material);
	}
}

void ADreamWeatherRadarDisplay::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (const UDreamWeatherRadar* Radar = UDreamWeatherRadar::Get(this))
	{
		Radar->ApplyTo(Material);
	}
}
