// Copyright DreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "DreamSurfaceTypes.h"
#include "DreamSurfaceSettings.generated.h"

/** Project Settings > DreamPlugin > Dream Surface. */
UCLASS(Config = Game, DefaultConfig, meta = (DisplayName = "Dream Surface"))
class DREAMSURFACE_API UDreamSurfaceSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	static const UDreamSurfaceSettings* Get() { return GetDefault<UDreamSurfaceSettings>(); }

	//~ UDeveloperSettings
	virtual FName GetCategoryName() const override { return TEXT("DreamPlugin"); }

	/** Run the surfaces wherever the weather runs. Off: every surface stays dry and clean. */
	UPROPERTY(Config, EditAnywhere, Category = "General")
	bool bEnabled = true;

	/** How fast the weather wets, snows over and dusts a surface, and how fast it recovers. */
	UPROPERTY(Config, EditAnywhere, Category = "Rates", meta = (ShowOnlyInnerProperties))
	FDreamSurfaceRates Rates;

	/**
	 * Game hours of the weather's past a surface remembers: its state is the weather integrated over this long. Longer
	 * keeps deep snow through a long thaw; shorter is cheaper to ask.
	 */
	UPROPERTY(Config, EditAnywhere, Category = "Timeline", meta = (ClampMin = "1", ClampMax = "720", Units = "Hours"))
	float MemoryHours = 48.0f;

	/** Game minutes per step of that integral (the weather is taken as holding still over a step). */
	UPROPERTY(Config, EditAnywhere, Category = "Timeline", meta = (ClampMin = "1", ClampMax = "120", Units = "Minutes"))
	float StepMinutes = 15.0f;

	/**
	 * How much grip each thing lying on a surface leaves (SampleContact, GetGripAt): a multiplier on the surface's own,
	 * at full cover.
	 */
	UPROPERTY(Config, EditAnywhere, Category = "Grip", meta = (ShowOnlyInnerProperties))
	FDreamSurfaceGrip Grip;

	/** The depth of the deepest snow cover (Snow = 1), cm: what the terrain's displacement and footprints use. */
	UPROPERTY(Config, EditAnywhere, Category = "Snow", meta = (ClampMin = "0", Units = "cm"))
	float MaxSnowDepth = 30.0f;

	/**
	 * The size of the small hollows water gathers in on flat ground, cm: puddles of about this size. The ground's own
	 * lows (from the wind's baked level heights, when the level has them) hold water whatever this is.
	 */
	UPROPERTY(Config, EditAnywhere, Category = "Water", meta = (ClampMin = "20", UIMax = "1000", Units = "cm"))
	float PuddleScale = 150.0f;

	/** How far around a point the ground's lows are judged, cm: a dip narrower than this holds water. */
	UPROPERTY(Config, EditAnywhere, Category = "Water", meta = (ClampMin = "50", UIMax = "2000", Units = "cm"))
	float LowRadius = 300.0f;

	// ---- Surface maps ---------------------------------------------------------------------------

	/**
	 * Draw the surface maps the materials read: the state at every texel around the camera (near, and as far as the
	 * weather map reaches), and the cover map. Off: materials take the view's state everywhere.
	 */
	UPROPERTY(Config, EditAnywhere, Category = "Surface Maps")
	bool bSurfaceMaps = true;

	/** World size of the near map, cm. It moves in eighths of this as the camera does, and integrates afresh each time. */
	UPROPERTY(Config, EditAnywhere, Category = "Surface Maps", meta = (ClampMin = "1000", Units = "cm"))
	float NearMapExtent = 12800.0f;

	/** Real seconds between map updates while nothing forces one (a step going by, the map moving, the zones changing). */
	UPROPERTY(Config, EditAnywhere, Category = "Surface Maps", meta = (ClampMin = "0", Units = "s"))
	float MapInterval = 0.1f;

	/** Real seconds between cover map updates while the near map stays put (shelters may move). */
	UPROPERTY(Config, EditAnywhere, Category = "Surface Maps", meta = (ClampMin = "0", Units = "s"))
	float CoverInterval = 0.5f;

	/** How far below the cover a surface fades from open to covered, cm (a soft edge under the eaves). */
	UPROPERTY(Config, EditAnywhere, Category = "Surface Maps", meta = (ClampMin = "1", Units = "cm"))
	float CoverSoftness = 50.0f;

	UPROPERTY(Config, EditAnywhere, Category = "Surface Maps", meta = (AllowedClasses = "/Script/Engine.TextureRenderTarget2D"))
	FSoftObjectPath NearMapTexture = FSoftObjectPath(TEXT("/DreamDynamicWorld/Surface/RT_DreamSurfaceNear.RT_DreamSurfaceNear"));

	UPROPERTY(Config, EditAnywhere, Category = "Surface Maps", meta = (AllowedClasses = "/Script/Engine.TextureRenderTarget2D"))
	FSoftObjectPath FarMapTexture = FSoftObjectPath(TEXT("/DreamDynamicWorld/Surface/RT_DreamSurfaceFar.RT_DreamSurfaceFar"));

	/** The ice, frost, rime and frozen water beside each state map (RGBA16F, the same size). */
	UPROPERTY(Config, EditAnywhere, Category = "Surface Maps", meta = (AllowedClasses = "/Script/Engine.TextureRenderTarget2D"))
	FSoftObjectPath NearIceMapTexture = FSoftObjectPath(TEXT("/DreamDynamicWorld/Surface/RT_DreamSurfaceNearIce.RT_DreamSurfaceNearIce"));

	UPROPERTY(Config, EditAnywhere, Category = "Surface Maps", meta = (AllowedClasses = "/Script/Engine.TextureRenderTarget2D"))
	FSoftObjectPath FarIceMapTexture = FSoftObjectPath(TEXT("/DreamDynamicWorld/Surface/RT_DreamSurfaceFarIce.RT_DreamSurfaceFarIce"));

	UPROPERTY(Config, EditAnywhere, Category = "Surface Maps", meta = (AllowedClasses = "/Script/Engine.TextureRenderTarget2D"))
	FSoftObjectPath CoverMapTexture = FSoftObjectPath(TEXT("/DreamDynamicWorld/Surface/RT_DreamSurfaceCover.RT_DreamSurfaceCover"));

	// ---- Trails ---------------------------------------------------------------------------------

	/** World size of the trail map (prints in the snow and dust, stirred water) around the camera, cm. */
	UPROPERTY(Config, EditAnywhere, Category = "Trails", meta = (ClampMin = "500", Units = "cm"))
	float TrailExtent = 5120.0f;

	/** Game hours a heavy snowfall takes to fill a print in. */
	UPROPERTY(Config, EditAnywhere, Category = "Trails", meta = (ClampMin = "0.01", Units = "Hours"))
	float PrintFillHours = 1.0f;

	/** Game hours a full wind takes to blow a print away. */
	UPROPERTY(Config, EditAnywhere, Category = "Trails", meta = (ClampMin = "0.01", Units = "Hours"))
	float PrintWindHours = 4.0f;

	/** Game hours a print takes to fade by itself (it settles). */
	UPROPERTY(Config, EditAnywhere, Category = "Trails", meta = (ClampMin = "0.01", Units = "Hours"))
	float PrintFadeHours = 48.0f;

	/** Real seconds the water stirred by a step takes to settle. */
	UPROPERTY(Config, EditAnywhere, Category = "Trails", meta = (ClampMin = "0.05", Units = "s"))
	float RippleSeconds = 1.2f;

	UPROPERTY(Config, EditAnywhere, Category = "Trails", meta = (AllowedClasses = "/Script/Engine.TextureRenderTarget2D"))
	FSoftObjectPath TrailMapTexture = FSoftObjectPath(TEXT("/DreamDynamicWorld/Surface/RT_DreamSurfaceTrails.RT_DreamSurfaceTrails"));

	/** The mask brushes' map beside the cover: per texel the multipliers of wet, water, snow and dust (RGBA16F). */
	UPROPERTY(Config, EditAnywhere, Category = "Surface Maps", meta = (AllowedClasses = "/Script/Engine.TextureRenderTarget2D"))
	FSoftObjectPath MaskMapTexture = FSoftObjectPath(TEXT("/DreamDynamicWorld/Surface/RT_DreamSurfaceMask.RT_DreamSurfaceMask"));
};
