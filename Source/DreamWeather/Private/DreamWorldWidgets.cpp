// Copyright DreamMoon. All Rights Reserved.

#include "DreamWorldWidgets.h"
#include "DreamWeatherPreset.h"
#include "DreamWeatherRadar.h"
#include "DreamWeatherSubsystem.h"
#include "DreamWeatherTypes.h"
#include "DreamWorldCalendar.h"
#include "DreamWorldSubsystem.h"

#include "Blueprint/WidgetTree.h"
#include "Brushes/SlateRoundedBoxBrush.h"
#include "Components/Border.h"
#include "Components/SizeBox.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Components/WidgetComponent.h"
#include "Engine/Font.h"
#include "Engine/World.h"
#include "Rendering/DrawElements.h"
#include "UObject/ConstructorHelpers.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SLeafWidget.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "DreamWorldWidgets"

namespace DreamWorldWidgetsImpl
{
	/** A filled disc or pill: a rounded box rounded to half its height. */
	static const FSlateBrush& RoundBrush()
	{
		static const FSlateRoundedBoxBrush Brush(FLinearColor::White);
		return Brush;
	}

	/** Draws in a square's own units, 0..1 across. */
	struct FPainter
	{
		const FGeometry& Geometry;
		FSlateWindowElementList& Elements;
		int32 Layer;
		FVector2f Origin;
		float Side;

		FPainter(const FGeometry& InGeometry, FSlateWindowElementList& InElements, int32 InLayer)
			: Geometry(InGeometry), Elements(InElements), Layer(InLayer)
		{
			const FVector2f Size(InGeometry.GetLocalSize());
			Side = FMath::Min(Size.X, Size.Y);
			Origin = (Size - FVector2f(Side)) * 0.5f;
		}

		FVector2f At(float X, float Y) const { return Origin + FVector2f(X, Y) * Side; }

		void Pill(float X0, float Y0, float X1, float Y1, const FLinearColor& Color) const
		{
			const FVector2f Min = At(X0, Y0);
			const FVector2f Max = At(X1, Y1);
			FSlateDrawElement::MakeBox(Elements, Layer, Geometry.ToPaintGeometry(Max - Min, FSlateLayoutTransform(Min)), &RoundBrush(), ESlateDrawEffect::None, Color);
		}

		void Disc(float X, float Y, float R, const FLinearColor& Color) const
		{
			Pill(X - R, Y - R, X + R, Y + R, Color);
		}

		void Line(std::initializer_list<FVector2f> Points, const FLinearColor& Color, float Thickness) const
		{
			TArray<FVector2f> Out;
			for (const FVector2f& P : Points)
			{
				Out.Add(At(P.X, P.Y));
			}
			FSlateDrawElement::MakeLines(Elements, Layer + 1, Geometry.ToPaintGeometry(), Out, ESlateDrawEffect::None, Color, true, Thickness * Side);
		}

		/** An arc of a circle, angles in degrees clockwise from up. */
		void Arc(float X, float Y, float R, float From, float To, const FLinearColor& Color, float Thickness) const
		{
			TArray<FVector2f> Out;
			const int32 Steps = 24;
			for (int32 i = 0; i <= Steps; ++i)
			{
				const float A = FMath::DegreesToRadians(FMath::Lerp(From, To, float(i) / Steps));
				Out.Add(At(X + R * FMath::Sin(A), Y - R * FMath::Cos(A)));
			}
			FSlateDrawElement::MakeLines(Elements, Layer + 1, Geometry.ToPaintGeometry(), Out, ESlateDrawEffect::None, Color, true, Thickness * Side);
		}

		void Cloud(float X, float Y, float Scale, const FLinearColor& Color) const
		{
			Disc(X - 0.13f * Scale, Y + 0.0f * Scale, 0.12f * Scale, Color);
			Disc(X + 0.01f * Scale, Y - 0.06f * Scale, 0.16f * Scale, Color);
			Disc(X + 0.16f * Scale, Y + 0.02f * Scale, 0.11f * Scale, Color);
			Pill(X - 0.27f * Scale, Y + 0.0f * Scale, X + 0.28f * Scale, Y + 0.13f * Scale, Color);
		}

		void Sun(float X, float Y, float R, const FLinearColor& Color) const
		{
			Disc(X, Y, R, Color);
			for (int32 i = 0; i < 8; ++i)
			{
				const float A = i * UE_PI / 4.0f;
				const FVector2f D(FMath::Sin(A), -FMath::Cos(A));
				Line({ FVector2f(X, Y) + D * (R * 1.35f), FVector2f(X, Y) + D * (R * 1.75f) }, Color, 0.035f);
			}
		}

		void Moon(float X, float Y, float R, const FLinearColor& Color) const
		{
			// A crescent as a thick arc, so it needs nothing behind it to cut it out.
			Arc(X + R * 0.12f, Y, R * 0.78f, 190.0f, 350.0f, Color, R * 0.55f);
		}
	};

	class SDreamWeatherIcon : public SLeafWidget
	{
	public:
		SLATE_BEGIN_ARGS(SDreamWeatherIcon) {}
			SLATE_ARGUMENT(TWeakObjectPtr<UDreamWeatherWidget>, Owner)
		SLATE_END_ARGS()

		void Construct(const FArguments& InArgs)
		{
			Owner = InArgs._Owner;
		}

		virtual FVector2D ComputeDesiredSize(float) const override
		{
			const UDreamWeatherWidget* W = Owner.Get();
			const float Side = W ? W->IconSize : 64.0f;
			return FVector2D(Side, Side);
		}

		virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& Geometry, const FSlateRect& Culling, FSlateWindowElementList& Elements, int32 LayerId,
			const FWidgetStyle& Style, bool bParentEnabled) const override
		{
			const UDreamWeatherWidget* W = Owner.Get();
			if (!W)
			{
				return LayerId;
			}
			EDreamWeatherIcon Icon;
			bool bDay;
			FText Name;
			float Celsius;
			W->GetShown(Icon, bDay, Name, Celsius);
			const FDreamWorldWidgetColors& C = W->Colors;
			const FPainter P(Geometry, Elements, LayerId);
			const FLinearColor Dark = C.Cloud * FLinearColor(0.62f, 0.62f, 0.64f, 1.0f);
			const FLinearColor Drop = C.Precipitation;
			const FLinearColor Ice(0.9f, 0.92f, 0.94f, 1.0f);
			auto Lights = [&](float X, float Y, float R)
			{
				if (bDay)
				{
					P.Sun(X, Y, R, C.Sun);
				}
				else
				{
					P.Moon(X, Y, R, C.Moon);
				}
			};
			auto Streaks = [&](const FLinearColor& Color, int32 Count)
			{
				for (int32 i = 0; i < Count; ++i)
				{
					const float X = 0.3f + 0.2f * i;
					P.Line({ FVector2f(X + 0.04f, 0.66f), FVector2f(X - 0.03f, 0.84f) }, Color, 0.045f);
				}
			};
			auto Dots = [&](const FLinearColor& Color, float R, float Shift)
			{
				for (int32 i = 0; i < 3; ++i)
				{
					P.Disc(0.3f + 0.2f * i + Shift, 0.72f + 0.08f * (i % 2), R, Color);
				}
			};
			switch (Icon)
			{
			case EDreamWeatherIcon::Clear:
				Lights(0.5f, 0.5f, 0.2f);
				break;
			case EDreamWeatherIcon::PartlyCloudy:
				Lights(0.62f, 0.36f, 0.15f);
				P.Cloud(0.44f, 0.6f, 1.0f, C.Cloud);
				break;
			case EDreamWeatherIcon::Cloudy:
				P.Cloud(0.6f, 0.4f, 0.8f, Dark);
				P.Cloud(0.44f, 0.58f, 1.0f, C.Cloud);
				break;
			case EDreamWeatherIcon::Fog:
				P.Cloud(0.5f, 0.36f, 0.8f, C.Cloud);
				for (int32 i = 0; i < 3; ++i)
				{
					P.Line({ FVector2f(0.2f + 0.05f * (i % 2), 0.62f + 0.1f * i), FVector2f(0.8f - 0.05f * (i % 2), 0.62f + 0.1f * i) }, C.Cloud, 0.04f);
				}
				break;
			case EDreamWeatherIcon::Rain:
				P.Cloud(0.5f, 0.42f, 1.0f, C.Cloud);
				Streaks(Drop, 3);
				break;
			case EDreamWeatherIcon::Snow:
				P.Cloud(0.5f, 0.42f, 1.0f, C.Cloud);
				Dots(Ice, 0.035f, 0.0f);
				break;
			case EDreamWeatherIcon::Sleet:
				P.Cloud(0.5f, 0.42f, 1.0f, C.Cloud);
				P.Line({ FVector2f(0.36f, 0.66f), FVector2f(0.31f, 0.8f) }, Drop, 0.045f);
				P.Line({ FVector2f(0.66f, 0.66f), FVector2f(0.61f, 0.8f) }, Drop, 0.045f);
				P.Disc(0.5f, 0.76f, 0.035f, Ice);
				break;
			case EDreamWeatherIcon::Hail:
				P.Cloud(0.5f, 0.42f, 1.0f, Dark);
				Dots(Ice, 0.05f, 0.0f);
				break;
			case EDreamWeatherIcon::Storm:
				P.Cloud(0.5f, 0.4f, 1.0f, Dark);
				P.Line({ FVector2f(0.54f, 0.56f), FVector2f(0.44f, 0.72f), FVector2f(0.56f, 0.72f), FVector2f(0.46f, 0.9f) }, C.Sun, 0.05f);
				P.Line({ FVector2f(0.3f, 0.64f), FVector2f(0.26f, 0.76f) }, Drop, 0.04f);
				P.Line({ FVector2f(0.72f, 0.64f), FVector2f(0.68f, 0.76f) }, Drop, 0.04f);
				break;
			case EDreamWeatherIcon::Dust:
				for (int32 i = 0; i < 3; ++i)
				{
					const float Y = 0.36f + 0.14f * i;
					P.Line({ FVector2f(0.18f, Y), FVector2f(0.34f, Y - 0.05f), FVector2f(0.5f, Y), FVector2f(0.66f, Y - 0.05f), FVector2f(0.82f, Y) },
						C.Sun * FLinearColor(0.8f, 0.7f, 0.6f, 1.0f), 0.045f);
				}
				break;
			}
			return LayerId + 2;
		}

	private:
		TWeakObjectPtr<UDreamWeatherWidget> Owner;
	};
}

class SDreamClockFace : public SLeafWidget
{
public:
	SLATE_BEGIN_ARGS(SDreamClockFace) {}
		SLATE_ARGUMENT(TWeakObjectPtr<UDreamClockWidget>, Owner)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs)
	{
		Owner = InArgs._Owner;
	}

	virtual FVector2D ComputeDesiredSize(float) const override
	{
		const UDreamClockWidget* W = Owner.Get();
		const float Side = W ? W->DialSize : 96.0f;
		return FVector2D(Side, Side);
	}

	virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& Geometry, const FSlateRect& Culling, FSlateWindowElementList& Elements, int32 LayerId,
		const FWidgetStyle& Style, bool bParentEnabled) const override
	{
		const UDreamClockWidget* W = Owner.Get();
		if (!W)
		{
			return LayerId;
		}
		using DreamWorldWidgetsImpl::FPainter;
		const FPainter P(Geometry, Elements, LayerId);
		const FDreamWorldWidgetColors& C = W->Colors;
		bool bDay = true;
		const float Hours = W->GetHours(bDay);
		P.Disc(0.5f, 0.5f, 0.5f, C.Face);
		// The hours' ticks, the quarters longer.
		for (int32 i = 0; i < 12; ++i)
		{
			const float A = i * UE_PI / 6.0f;
			const FVector2f D(FMath::Sin(A), -FMath::Cos(A));
			const float Inner = i % 3 == 0 ? 0.36f : 0.41f;
			P.Line({ FVector2f(0.5f) + D * Inner, FVector2f(0.5f) + D * 0.46f }, C.Hands * FLinearColor(1.0f, 1.0f, 1.0f, 0.7f), i % 3 == 0 ? 0.03f : 0.015f);
		}
		// Day or night, below the middle.
		if (bDay)
		{
			P.Disc(0.5f, 0.72f, 0.06f, C.Sun);
		}
		else
		{
			P.Moon(0.5f, 0.72f, 0.07f, C.Moon);
		}
		const float Minutes = FMath::Fmod(Hours * 60.0f, 60.0f);
		const float HourAngle = FMath::Fmod(Hours, 12.0f) / 12.0f * 2.0f * UE_PI;
		const float MinuteAngle = Minutes / 60.0f * 2.0f * UE_PI;
		const FVector2f HourDir(FMath::Sin(HourAngle), -FMath::Cos(HourAngle));
		const FVector2f MinuteDir(FMath::Sin(MinuteAngle), -FMath::Cos(MinuteAngle));
		P.Line({ FVector2f(0.5f) - HourDir * 0.05f, FVector2f(0.5f) + HourDir * 0.25f }, C.Hands, 0.05f);
		P.Line({ FVector2f(0.5f) - MinuteDir * 0.06f, FVector2f(0.5f) + MinuteDir * 0.38f }, C.Hands, 0.03f);
		// (The hands' pivot above them.)
		FPainter Top(Geometry, Elements, LayerId + 2);
		Top.Disc(0.5f, 0.5f, 0.035f, C.Hands);
		return LayerId + 3;
	}

private:
	TWeakObjectPtr<UDreamClockWidget> Owner;
};

namespace DreamWorldWidgetsImpl
{
	static FSlateFontInfo DefaultFont(int32 Size)
	{
		if (IsRunningDedicatedServer())
		{
			return FSlateFontInfo();
		}
		static ConstructorHelpers::FObjectFinder<UFont> Roboto(*UWidget::GetDefaultFontName());
		return FSlateFontInfo(Roboto.Object, Size, FName("Regular"));
	}
}

// ---- Clock ------------------------------------------------------------------------------------------------------------

UDreamClockWidget::UDreamClockWidget()
{
	Font = DreamWorldWidgetsImpl::DefaultFont(18);
}

FText UDreamClockWidget::FormatTime(float Hours, bool bIn24Hour)
{
	const int32 Total = FMath::FloorToInt(FMath::Fmod(FMath::Fmod(Hours, 24.0f) + 24.0f, 24.0f) * 60.0f + 1.0e-3f);
	const int32 H = (Total / 60) % 24;
	const int32 M = Total % 60;
	if (bIn24Hour)
	{
		return FText::FromString(FString::Printf(TEXT("%02d:%02d"), H, M));
	}
	const int32 H12 = H % 12 == 0 ? 12 : H % 12;
	return FText::FromString(FString::Printf(TEXT("%d:%02d %s"), H12, M, H < 12 ? TEXT("AM") : TEXT("PM")));
}

FText UDreamClockWidget::FormatDate(const FDreamCalendarDate& Date)
{
	const FText Month = Date.MonthName.IsEmpty() ? FText::AsNumber(Date.Month) : Date.MonthName;
	return FText::Format(LOCTEXT("DateFormat", "{0} {1} {2} {3}"), Date.WeekDayName, FText::AsNumber(Date.Day), Month,
		FText::AsNumber(Date.Year, &FNumberFormattingOptions::DefaultNoGrouping()));
}

float UDreamClockWidget::GetHours(bool& bOutDaytime) const
{
	const UWorld* World = GetWorld();
	const UDreamWorldSubsystem* Clock = World ? World->GetSubsystem<UDreamWorldSubsystem>() : nullptr;
	if (!Clock)
	{
		bOutDaytime = true;
		return 10.17f;
	}
	bOutDaytime = Clock->IsDaytime();
	return Clock->GetTimeOfDay();
}

TSharedRef<SWidget> UDreamClockWidget::RebuildWidget()
{
	TWeakObjectPtr<UDreamClockWidget> Self(this);
	TSharedRef<SVerticalBox> Box = SNew(SVerticalBox);
	if (Style != EDreamClockStyle::Digital)
	{
		Box->AddSlot().AutoHeight().HAlign(HAlign_Center).Padding(0.0f, 0.0f, 0.0f, 4.0f)
		[
			SNew(SDreamClockFace).Owner(Self)
		];
	}
	if (Style != EDreamClockStyle::Analog)
	{
		Box->AddSlot().AutoHeight().HAlign(HAlign_Center)
		[
			SNew(STextBlock)
			.Font(Font)
			.ColorAndOpacity(Colors.Text)
			.Text_Lambda([Self]
			{
				const UDreamClockWidget* W = Self.Get();
				bool bDay;
				return W ? FormatTime(W->GetHours(bDay), W->b24Hour) : FText::GetEmpty();
			})
		];
	}
	if (bShowDate)
	{
		FSlateFontInfo Small = Font;
		Small.Size = FMath::Max(8.0f, Font.Size * 0.6f);
		Box->AddSlot().AutoHeight().HAlign(HAlign_Center)
		[
			SNew(STextBlock)
			.Font(Small)
			.ColorAndOpacity(Colors.Text * FLinearColor(1.0f, 1.0f, 1.0f, 0.75f))
			.Text_Lambda([Self]
			{
				const UDreamClockWidget* W = Self.Get();
				const UWorld* World = W ? W->GetWorld() : nullptr;
				const UDreamWorldSubsystem* Clock = World ? World->GetSubsystem<UDreamWorldSubsystem>() : nullptr;
				return Clock ? FormatDate(Clock->GetCalendarDate()) : LOCTEXT("DesignDate", "Monday 3 January 2026");
			})
		];
	}
	Root = Box;
	return Box;
}

void UDreamClockWidget::SynchronizeProperties()
{
	Super::SynchronizeProperties();
}

void UDreamClockWidget::ReleaseSlateResources(bool bReleaseChildren)
{
	Super::ReleaseSlateResources(bReleaseChildren);
	Root.Reset();
}

#if WITH_EDITOR
const FText UDreamClockWidget::GetPaletteCategory()
{
	return NSLOCTEXT("DreamWeather", "RadarPalette", "Dream Weather");
}
#endif

// ---- Weather ----------------------------------------------------------------------------------------------------------

UDreamWeatherWidget::UDreamWeatherWidget()
{
	Font = DreamWorldWidgetsImpl::DefaultFont(16);
}

EDreamWeatherIcon UDreamWeatherWidget::IconFor(const FDreamWeatherSample& S)
{
	const float Falling = 0.05f;
	if (S.Lightning > 0.3f && S.Precipitation > 0.2f)
	{
		return EDreamWeatherIcon::Storm;
	}
	if (S.Hail > Falling)
	{
		return EDreamWeatherIcon::Hail;
	}
	if (S.Sleet > Falling)
	{
		return EDreamWeatherIcon::Sleet;
	}
	if (S.Snow > Falling)
	{
		return EDreamWeatherIcon::Snow;
	}
	if (S.Rain > Falling)
	{
		return EDreamWeatherIcon::Rain;
	}
	if (S.Fog > 0.5f)
	{
		return EDreamWeatherIcon::Fog;
	}
	if (S.Dust > 0.4f)
	{
		return EDreamWeatherIcon::Dust;
	}
	return S.CloudCoverage < 0.25f ? EDreamWeatherIcon::Clear : S.CloudCoverage < 0.65f ? EDreamWeatherIcon::PartlyCloudy : EDreamWeatherIcon::Cloudy;
}

FText UDreamWeatherWidget::FormatTemperature(float Celsius, bool bInFahrenheit)
{
	const int32 Degrees = FMath::RoundToInt(bInFahrenheit ? Celsius * 1.8f + 32.0f : Celsius);
	return FText::FromString(FString::Printf(TEXT("%d°%s"), Degrees, bInFahrenheit ? TEXT("F") : TEXT("C")));
}

void UDreamWeatherWidget::GetShown(EDreamWeatherIcon& OutIcon, bool& bOutDaytime, FText& OutName, float& OutCelsius) const
{
	const UWorld* World = GetWorld();
	const UDreamWeatherSubsystem* Weather = World ? World->GetSubsystem<UDreamWeatherSubsystem>() : nullptr;
	const UDreamWorldSubsystem* Clock = World ? World->GetSubsystem<UDreamWorldSubsystem>() : nullptr;
	if (!Weather || !Weather->IsActive())
	{
		OutIcon = EDreamWeatherIcon::PartlyCloudy;
		bOutDaytime = true;
		OutName = LOCTEXT("DesignWeather", "Fair");
		OutCelsius = 18.0f;
		return;
	}
	const FDreamWeatherSample& View = Weather->GetViewSample();
	OutIcon = IconFor(View);
	bOutDaytime = !Clock || Clock->IsDaytime();
	// The weather showing (a forced one too), by its name for a player.
	const UDreamWeatherPreset* Shown = Weather->GetWeatherShown();
	OutName = !Shown ? FText::GetEmpty() : Shown->DisplayName.IsEmpty() ? FText::FromName(Shown->GetFName()) : Shown->DisplayName;
	OutCelsius = View.Temperature;
}

TSharedRef<SWidget> UDreamWeatherWidget::RebuildWidget()
{
	TWeakObjectPtr<UDreamWeatherWidget> Self(this);
	FSlateFontInfo Small = Font;
	Small.Size = FMath::Max(8.0f, Font.Size * 0.75f);
	TSharedRef<SVerticalBox> Lines = SNew(SVerticalBox);
	if (bShowName)
	{
		Lines->AddSlot().AutoHeight()
		[
			SNew(STextBlock)
			.Font(Font)
			.ColorAndOpacity(Colors.Text)
			.Text_Lambda([Self]
			{
				const UDreamWeatherWidget* W = Self.Get();
				if (!W)
				{
					return FText::GetEmpty();
				}
				EDreamWeatherIcon Icon;
				bool bDay;
				FText Name;
				float Celsius;
				W->GetShown(Icon, bDay, Name, Celsius);
				return Name;
			})
		];
	}
	if (bShowTemperature)
	{
		Lines->AddSlot().AutoHeight()
		[
			SNew(STextBlock)
			.Font(Small)
			.ColorAndOpacity(Colors.Text * FLinearColor(1.0f, 1.0f, 1.0f, 0.8f))
			.Text_Lambda([Self]
			{
				const UDreamWeatherWidget* W = Self.Get();
				if (!W)
				{
					return FText::GetEmpty();
				}
				EDreamWeatherIcon Icon;
				bool bDay;
				FText Name;
				float Celsius;
				W->GetShown(Icon, bDay, Name, Celsius);
				return FormatTemperature(Celsius, W->bFahrenheit);
			})
		];
	}
	TSharedRef<SHorizontalBox> Box = SNew(SHorizontalBox)
		+ SHorizontalBox::Slot()
		.AutoWidth()
		.VAlign(VAlign_Center)
		.Padding(0.0f, 0.0f, 8.0f, 0.0f)
		[
			SNew(DreamWorldWidgetsImpl::SDreamWeatherIcon).Owner(Self)
		]
		+ SHorizontalBox::Slot()
		.AutoWidth()
		.VAlign(VAlign_Center)
		[
			Lines
		];
	Root = Box;
	return Box;
}

void UDreamWeatherWidget::SynchronizeProperties()
{
	Super::SynchronizeProperties();
}

void UDreamWeatherWidget::ReleaseSlateResources(bool bReleaseChildren)
{
	Super::ReleaseSlateResources(bReleaseChildren);
	Root.Reset();
}

#if WITH_EDITOR
const FText UDreamWeatherWidget::GetPaletteCategory()
{
	return NSLOCTEXT("DreamWeather", "RadarPalette", "Dream Weather");
}
#endif

// ---- The example panel and its screen ---------------------------------------------------------------------------------

bool UDreamWorldWidgetsExample::Initialize()
{
	const bool bFirst = Super::Initialize();
	if (bFirst && WidgetTree && !WidgetTree->RootWidget)
	{
		UBorder* Card = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("Card"));
		Card->SetBrushColor(FLinearColor(0.07f, 0.075f, 0.08f, 1.0f));
		Card->SetPadding(FMargin(18.0f));
		WidgetTree->RootWidget = Card;
		UVerticalBox* Column = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("Column"));
		Card->SetContent(Column);
		UDreamClockWidget* Clock = WidgetTree->ConstructWidget<UDreamClockWidget>(UDreamClockWidget::StaticClass(), TEXT("Clock"));
		Clock->DialSize = 150.0f;
		Clock->Font.Size = 28;
		if (UVerticalBoxSlot* BoxSlot = Column->AddChildToVerticalBox(Clock))
		{
			BoxSlot->SetHorizontalAlignment(HAlign_Center);
			BoxSlot->SetPadding(FMargin(0.0f, 0.0f, 0.0f, 14.0f));
		}
		UDreamWeatherWidget* Weather = WidgetTree->ConstructWidget<UDreamWeatherWidget>(UDreamWeatherWidget::StaticClass(), TEXT("Weather"));
		Weather->IconSize = 84.0f;
		Weather->Font.Size = 24;
		if (UVerticalBoxSlot* BoxSlot = Column->AddChildToVerticalBox(Weather))
		{
			BoxSlot->SetHorizontalAlignment(HAlign_Center);
			BoxSlot->SetPadding(FMargin(0.0f, 0.0f, 0.0f, 14.0f));
		}
		if (bShowRadar)
		{
			USizeBox* RadarBox = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(), TEXT("RadarBox"));
			RadarBox->SetWidthOverride(300.0f);
			RadarBox->SetHeightOverride(300.0f);
			RadarBox->SetContent(WidgetTree->ConstructWidget<UDreamWeatherRadarImage>(UDreamWeatherRadarImage::StaticClass(), TEXT("Radar")));
			if (UVerticalBoxSlot* BoxSlot = Column->AddChildToVerticalBox(RadarBox))
			{
				BoxSlot->SetHorizontalAlignment(HAlign_Center);
			}
		}
	}
	return bFirst;
}

ADreamWorldWidgetsDisplay::ADreamWorldWidgetsDisplay()
{
	Screen = CreateDefaultSubobject<UWidgetComponent>(TEXT("Screen"));
	RootComponent = Screen;
	Screen->SetWidgetSpace(EWidgetSpace::World);
	Screen->SetWidgetClass(UDreamWorldWidgetsExample::StaticClass());
	Screen->SetDrawSize(FVector2D(420.0, 700.0));
	Screen->SetTwoSided(true);
	Screen->SetBlendMode(EWidgetBlendMode::Opaque);
	Screen->SetCollisionEnabled(ECollisionEnabled::NoCollision);
}

#undef LOCTEXT_NAMESPACE
