// Copyright DreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "Components/Image.h"
#include "GameFramework/Actor.h"
#include "DreamWeatherRadar.generated.h"

class UMaterialInstanceDynamic;
class UMaterialInterface;
class UStaticMeshComponent;
class UTextureRenderTarget2D;

/** How the weather radar looks and how far it reaches (M_DreamWeatherRadar's parameters). */
USTRUCT(BlueprintType)
struct DREAMWEATHER_API FDreamWeatherRadarLook
{
	GENERATED_BODY()

	/** How far from its middle it reaches, km (the weather map round the view reaches some 50 km). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Radar", meta = (ClampMin = "1", UIMax = "60"))
	float RangeKm = 25.0f;

	/** The range rings' spacing, km (0: none). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Radar", meta = (ClampMin = "0", UIMax = "50"))
	float RingKm = 10.0f;

	/** The precipitation's steps: each colour from the precipitation (0..1) it begins at (by default muted: a grey green,
	 *  a grey yellow, a terracotta, a dull rose -- told apart by their hue and brightness, not glaring). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Radar", meta = (HideAlphaChannel))
	FLinearColor LightColor = FLinearColor(0.2f, 0.29f, 0.23f);
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Radar", meta = (ClampMin = "0", ClampMax = "1"))
	float LightFrom = 0.03f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Radar", meta = (HideAlphaChannel))
	FLinearColor ModerateColor = FLinearColor(0.36f, 0.34f, 0.22f);
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Radar", meta = (ClampMin = "0", ClampMax = "1"))
	float ModerateFrom = 0.3f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Radar", meta = (HideAlphaChannel))
	FLinearColor HeavyColor = FLinearColor(0.45f, 0.27f, 0.19f);
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Radar", meta = (ClampMin = "0", ClampMax = "1"))
	float HeavyFrom = 0.6f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Radar", meta = (HideAlphaChannel))
	FLinearColor ExtremeColor = FLinearColor(0.42f, 0.2f, 0.3f);
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Radar", meta = (ClampMin = "0", ClampMax = "1"))
	float ExtremeFrom = 0.85f;

	/** Snow's colour (the steps turn to it as far as the view's precipitation falls as snow). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Radar", meta = (HideAlphaChannel))
	FLinearColor SnowColor = FLinearColor(0.55f, 0.64f, 0.74f);

	/** The cloud cover's veil under the precipitation, and how opaque at a full cover. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Radar", meta = (HideAlphaChannel))
	FLinearColor CloudColor = FLinearColor(0.42f, 0.45f, 0.5f);
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Radar", meta = (ClampMin = "0", ClampMax = "1"))
	float CloudOpacity = 0.3f;

	/** The background inside the range, and how bright outside it (of the background). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Radar", meta = (HideAlphaChannel))
	FLinearColor Background = FLinearColor(0.04f, 0.06f, 0.08f);
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Radar", meta = (ClampMin = "0", ClampMax = "1"))
	float Outside = 0.5f;

	/** The marks (rings, the middle's cross, the range's edge): their colour and opacity. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Radar", meta = (HideAlphaChannel))
	FLinearColor MarksColor = FLinearColor(0.62f, 0.68f, 0.66f);
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Radar", meta = (ClampMin = "0", ClampMax = "1"))
	float MarksOpacity = 0.5f;

	/** How often the texture is drawn again, s (real time), and its size, texels. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Radar", meta = (ClampMin = "0", UIMax = "5"))
	float RefreshSeconds = 0.5f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Radar", meta = (ClampMin = "64", ClampMax = "2048"))
	int32 Resolution = 512;
};

/** The radar's precipitation steps. */
UENUM(BlueprintType)
enum class EDreamWeatherRadarLevel : uint8
{
	None,
	Light,
	Moderate,
	Heavy,
	Extreme,
};

/** What the radar shows at a point. */
USTRUCT(BlueprintType)
struct DREAMWEATHER_API FDreamWeatherRadarSample
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Radar") float Precipitation = 0.0f;
	UPROPERTY(BlueprintReadOnly, Category = "Radar") float CloudCover = 0.0f;
	UPROPERTY(BlueprintReadOnly, Category = "Radar") float CloudDarkness = 0.0f;
	/** The share of the precipitation that falls as snow. */
	UPROPERTY(BlueprintReadOnly, Category = "Radar") float SnowShare = 0.0f;
	/** The radar's step for it. */
	UPROPERTY(BlueprintReadOnly, Category = "Radar") EDreamWeatherRadarLevel Level = EDreamWeatherRadarLevel::None;
};

/**
 * The weather radar: the weather map drawn as a radar picture round the view (or a given middle), north up, into a
 * texture any UI can show (GetRadarTexture; UDreamWeatherRadarImage shows it in UMG), or onto a surface
 * (ADreamWeatherRadarDisplay); and the queries a game's UI or gameplay asks of it -- what it shows at a point, where
 * the nearest rain is, where a point falls on it.
 */
UCLASS()
class DREAMWEATHER_API UDreamWeatherRadar : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintPure, Category = "Dream Weather|Radar", meta = (WorldContext = "WorldContextObject", DisplayName = "Get Dream Weather Radar"))
	static UDreamWeatherRadar* Get(const UObject* WorldContextObject);

	/** The radar picture, kept up to date from now on (made on the first call). */
	UFUNCTION(BlueprintCallable, Category = "Dream Weather|Radar")
	UTextureRenderTarget2D* GetRadarTexture();

	UFUNCTION(BlueprintCallable, Category = "Dream Weather|Radar")
	void SetLook(const FDreamWeatherRadarLook& InLook);

	UFUNCTION(BlueprintPure, Category = "Dream Weather|Radar")
	const FDreamWeatherRadarLook& GetLook() const { return Look; }

	/** Hold the radar's middle at a point (its x, y) instead of following the view. */
	UFUNCTION(BlueprintCallable, Category = "Dream Weather|Radar")
	void SetCenter(FVector World);

	/** Follow the view again. */
	UFUNCTION(BlueprintCallable, Category = "Dream Weather|Radar")
	void ClearCenter();

	/** The radar's middle now (world). */
	UFUNCTION(BlueprintPure, Category = "Dream Weather|Radar")
	FVector GetCenter() const;

	/** Where a point falls on the radar picture (0..1 across and down, north up; outside 0..1 beyond its range). */
	UFUNCTION(BlueprintPure, Category = "Dream Weather|Radar")
	FVector2D WorldToRadar(FVector World) const;

	/** The world point (at the middle's height) a place on the picture stands for. */
	UFUNCTION(BlueprintPure, Category = "Dream Weather|Radar")
	FVector RadarToWorld(FVector2D UV) const;

	/** What the radar shows at a point (the weather there, its step). */
	UFUNCTION(BlueprintPure, Category = "Dream Weather|Radar")
	FDreamWeatherRadarSample SampleRadar(FVector World) const;

	/** The nearest point within RadiusKm of From with at least MinPrecipitation (searched on a 1 km grid): false if none. */
	UFUNCTION(BlueprintCallable, Category = "Dream Weather|Radar")
	bool FindNearestPrecipitation(FVector From, float RadiusKm, float MinPrecipitation, FVector& OutLocation, float& OutDistanceKm) const;

	/** Sets the radar's look and where it looks on a dynamic instance of M_DreamWeatherRadar (a surface showing it). */
	UFUNCTION(BlueprintCallable, Category = "Dream Weather|Radar")
	void ApplyTo(UMaterialInstanceDynamic* Material) const;

	/** The radar material (M_DreamWeatherRadar), loaded. */
	static UMaterialInterface* GetRadarMaterial();

	/** The step a precipitation is in under a look. */
	static EDreamWeatherRadarLevel LevelFor(const FDreamWeatherRadarLook& InLook, float Precipitation);

	// UTickableWorldSubsystem
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;
	virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;

private:
	FVector2D NorthDirection() const;

	FDreamWeatherRadarLook Look;
	TOptional<FVector> HeldCenter;
	UPROPERTY(Transient)
	TObjectPtr<UTextureRenderTarget2D> Texture;
	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> DrawMaterial;
	double SinceDrawn = 1.0e9;
};

/** A UMG image showing the weather radar's picture (UDreamWeatherRadar::GetRadarTexture), round the view. */
UCLASS(meta = (DisplayName = "Dream Weather Radar"))
class DREAMWEATHER_API UDreamWeatherRadarImage : public UImage
{
	GENERATED_BODY()

public:
	/** The radar's reach shown, km (0: the radar's own look's). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Radar", meta = (ClampMin = "0", UIMax = "60"))
	float RangeKm = 0.0f;

	virtual void SynchronizeProperties() override;
#if WITH_EDITOR
	virtual const FText GetPaletteCategory() override;
#endif
};

/** A screen in the world showing the weather radar (M_DreamWeatherRadar on a plane, round the view), as in the showcase. */
UCLASS(meta = (DisplayName = "Dream Weather Radar Display"))
class DREAMWEATHER_API ADreamWeatherRadarDisplay : public AActor
{
	GENERATED_BODY()

public:
	ADreamWeatherRadarDisplay();

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Radar")
	TObjectPtr<UStaticMeshComponent> Screen;

	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;

private:
	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> Material;
};
