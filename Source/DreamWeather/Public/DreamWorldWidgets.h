// Copyright DreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Components/Widget.h"
#include "Fonts/SlateFontInfo.h"
#include "GameFramework/Actor.h"
#include "DreamWorldWidgets.generated.h"

class UWidgetComponent;
struct FDreamWeatherSample;

/** How a clock widget shows the time. */
UENUM(BlueprintType)
enum class EDreamClockStyle : uint8
{
	Analog,
	Digital,
	Both
};

/** The kinds of weather a weather widget draws an icon for. */
UENUM(BlueprintType)
enum class EDreamWeatherIcon : uint8
{
	Clear,
	PartlyCloudy,
	Cloudy,
	Fog,
	Rain,
	Snow,
	Sleet,
	Hail,
	Storm,
	Dust
};

/** The colours of the clock and weather widgets (muted, to sit over a game). */
USTRUCT(BlueprintType)
struct DREAMWEATHER_API FDreamWorldWidgetColors
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Colors")
	FLinearColor Text = FLinearColor(0.86f, 0.87f, 0.85f, 1.0f);

	/** The clock's face and the icons' cloud. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Colors")
	FLinearColor Face = FLinearColor(0.16f, 0.17f, 0.18f, 0.85f);

	/** The clock's hands and ticks. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Colors")
	FLinearColor Hands = FLinearColor(0.84f, 0.84f, 0.8f, 1.0f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Colors")
	FLinearColor Sun = FLinearColor(0.86f, 0.7f, 0.38f, 1.0f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Colors")
	FLinearColor Moon = FLinearColor(0.72f, 0.76f, 0.82f, 1.0f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Colors")
	FLinearColor Cloud = FLinearColor(0.62f, 0.65f, 0.68f, 1.0f);

	/** Rain, sleet and the storm's bolt use it lightened or warmed. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Colors")
	FLinearColor Precipitation = FLinearColor(0.5f, 0.62f, 0.72f, 1.0f);
};

/**
 * A clock for UMG showing the world clock's time (UDreamWorldSubsystem): an analog dial, digital text or both, and the
 * calendar's date. Drawn by Slate, no textures; an example of reading the world clock in a HUD.
 */
UCLASS(meta = (DisplayName = "Dream World Clock"))
class DREAMWEATHER_API UDreamClockWidget : public UWidget
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Clock")
	EDreamClockStyle Style = EDreamClockStyle::Both;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Clock")
	bool b24Hour = true;

	/** The calendar's date under the time (weekday, day, month, year). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Clock")
	bool bShowDate = true;

	/** The dial's diameter, slate units. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Clock", meta = (ClampMin = "16", UIMax = "512"))
	float DialSize = 96.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Appearance")
	FSlateFontInfo Font;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Appearance")
	FDreamWorldWidgetColors Colors;

	/** The time as the clock shows it: "07:05", or "7:05 AM" on a 12 hour clock. */
	static FText FormatTime(float Hours, bool bIn24Hour);

	/** The date as the clock shows it: "Monday 3 January 2026". */
	static FText FormatDate(const struct FDreamCalendarDate& Date);

	/** The world clock's hours now (0 in the designer), and whether the sun is up. */
	float GetHours(bool& bOutDaytime) const;

	UDreamClockWidget();
	virtual void SynchronizeProperties() override;
	virtual void ReleaseSlateResources(bool bReleaseChildren) override;
#if WITH_EDITOR
	virtual const FText GetPaletteCategory() override;
#endif

protected:
	virtual TSharedRef<SWidget> RebuildWidget() override;

private:
	TSharedPtr<SWidget> Root;
};

/**
 * The weather where the view is (UDreamWeatherSubsystem::GetViewSample) for UMG: an icon drawn by Slate, the weather's
 * name and the temperature. An example of reading the weather in a HUD.
 */
UCLASS(meta = (DisplayName = "Dream Weather Badge"))
class DREAMWEATHER_API UDreamWeatherWidget : public UWidget
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weather")
	bool bShowName = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weather")
	bool bShowTemperature = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weather")
	bool bFahrenheit = false;

	/** The icon's side, slate units. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weather", meta = (ClampMin = "12", UIMax = "256"))
	float IconSize = 64.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Appearance")
	FSlateFontInfo Font;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Appearance")
	FDreamWorldWidgetColors Colors;

	/** The icon for a sample of the weather (the storm before the hail, sleet, snow and rain; then fog, dust and cloud). */
	static EDreamWeatherIcon IconFor(const FDreamWeatherSample& Sample);

	/** "18°C" or "64°F". */
	static FText FormatTemperature(float Celsius, bool bInFahrenheit);

	/** What it shows now (a fair day in the designer): the icon, whether the sun is up, the name, the temperature. */
	void GetShown(EDreamWeatherIcon& OutIcon, bool& bOutDaytime, FText& OutName, float& OutCelsius) const;

	UDreamWeatherWidget();
	virtual void SynchronizeProperties() override;
	virtual void ReleaseSlateResources(bool bReleaseChildren) override;
#if WITH_EDITOR
	virtual const FText GetPaletteCategory() override;
#endif

protected:
	virtual TSharedRef<SWidget> RebuildWidget() override;

private:
	TSharedPtr<SWidget> Root;
};

/**
 * An example HUD panel built from the widgets above and the radar: the clock, the weather and the radar on a muted
 * card. Usable as is (Create Widget with this class) or as a guide to laying them out in a widget blueprint.
 */
UCLASS(meta = (DisplayName = "Dream World Widgets Example"))
class DREAMWEATHER_API UDreamWorldWidgetsExample : public UUserWidget
{
	GENERATED_BODY()

public:
	/** Show the radar under the clock and the weather. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Example")
	bool bShowRadar = true;

	virtual bool Initialize() override;
};

/** A screen in the world showing UDreamWorldWidgetsExample (a widget component), as in the showcase. */
UCLASS(meta = (DisplayName = "Dream World Widgets Display"))
class DREAMWEATHER_API ADreamWorldWidgetsDisplay : public AActor
{
	GENERATED_BODY()

public:
	ADreamWorldWidgetsDisplay();

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Widgets")
	TObjectPtr<UWidgetComponent> Screen;
};
