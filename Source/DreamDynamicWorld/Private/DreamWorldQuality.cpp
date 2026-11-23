// Copyright DreamMoon. All Rights Reserved.

#include "DreamWorldQuality.h"
#include "HAL/IConsoleManager.h"

static TAutoConsoleVariable<int32> CVarDreamWorldQuality(
	TEXT("r.DreamWorld.Quality"), -1,
	TEXT("Dream Dynamic World's quality for every group of its features: 0 low, 1 medium, 2 high, 3 epic, 4 cinematic; -1 = each")
	TEXT(" group follows its engine scalability group (sg.EffectsQuality, sg.TextureQuality, sg.ShadowQuality)."),
	ECVF_Scalability);

static TAutoConsoleVariable<int32> CVarDreamWindQuality(
	TEXT("r.DreamWind.Quality"), -1,
	TEXT("The wind field's quality, 0 low .. 4 cinematic (the wind system takes it); -1 = r.DreamWorld.Quality, or sg.EffectsQuality."),
	ECVF_Scalability);

static TAutoConsoleVariable<int32> CVarDreamSurfaceQuality(
	TEXT("r.DreamSurface.Quality"), -1,
	TEXT("The surface maps' quality, 0 low .. 4 cinematic (the surfaces take it); -1 = r.DreamWorld.Quality, or sg.TextureQuality."),
	ECVF_Scalability);

namespace DreamWorld::Quality
{
	static int32 EngineLevel(const TCHAR* Name)
	{
		const IConsoleVariable* Variable = IConsoleManager::Get().FindConsoleVariable(Name);
		return Variable ? FMath::Clamp(Variable->GetInt(), 0, 4) : 3;
	}

	// (Low, medium, high, epic -- the defaults, as tuned --, cinematic.)
	static const FDreamWorldQualitySetting Sky[] = {
		{ TEXT("DreamSky.Clouds.Flat"),                 { TEXT("1"), TEXT("-1"), TEXT("-1"), TEXT("-1"), TEXT("-1") } },
		{ TEXT("DreamSky.CloudRender.Downsample"),      { TEXT("4"), TEXT("3"), TEXT("2"), TEXT("2"), TEXT("1") } },
		{ TEXT("DreamSky.CloudRender.Samples"),         { TEXT("64"), TEXT("96"), TEXT("128"), TEXT("160"), TEXT("256") } },
		{ TEXT("DreamSky.CloudRender.SunMapSlices"),    { TEXT("8"), TEXT("8"), TEXT("6"), TEXT("6"), TEXT("1") } },
		{ TEXT("DreamSky.LightShafts"),                 { TEXT("0"), TEXT("0"), TEXT("1"), TEXT("1"), TEXT("1") } },
		{ TEXT("DreamSky.LightShafts.Samples"),         { TEXT("8"), TEXT("12"), TEXT("16"), TEXT("24"), TEXT("48") } },
		{ TEXT("DreamSky.SkyDetail"),                   { TEXT("0"), TEXT("1"), TEXT("2"), TEXT("2"), TEXT("2") } },
	};
	static const FDreamWorldQualitySetting Particles[] = {
		{ TEXT("DreamWeather.ParticleDensity"),         { TEXT("0.35"), TEXT("0.6"), TEXT("0.85"), TEXT("1"), TEXT("1") } },
	};
	static const FDreamWorldQualitySetting Fog[] = {
		{ TEXT("DreamSky.VolumetricFog"),               { TEXT("0"), TEXT("0"), TEXT("-1"), TEXT("-1"), TEXT("-1") } },
		{ TEXT("DreamWeather.Fog.Volumetric"),          { TEXT("0"), TEXT("1"), TEXT("1"), TEXT("1"), TEXT("1") } },
		{ TEXT("DreamWeather.Fog.Samples"),             { TEXT("16"), TEXT("16"), TEXT("24"), TEXT("32"), TEXT("64") } },
	};

	static int32 Applied[5] = { -2, -2, -2, -2, -2 };
}

int32 UDreamWorldQuality::GetLevel(EDreamWorldQualityGroup Group)
{
	using namespace DreamWorld::Quality;
	const int32 Own = CVarDreamWorldQuality.GetValueOnAnyThread();
	if (Group == EDreamWorldQualityGroup::Wind && CVarDreamWindQuality.GetValueOnAnyThread() >= 0)
	{
		return FMath::Min(CVarDreamWindQuality.GetValueOnAnyThread(), 4);
	}
	if (Group == EDreamWorldQualityGroup::Surface && CVarDreamSurfaceQuality.GetValueOnAnyThread() >= 0)
	{
		return FMath::Min(CVarDreamSurfaceQuality.GetValueOnAnyThread(), 4);
	}
	if (Own >= 0)
	{
		return FMath::Min(Own, 4);
	}
	switch (Group)
	{
	case EDreamWorldQualityGroup::Surface: return EngineLevel(TEXT("sg.TextureQuality"));
	case EDreamWorldQualityGroup::Fog: return EngineLevel(TEXT("sg.ShadowQuality"));
	default: return EngineLevel(TEXT("sg.EffectsQuality"));
	}
}

void UDreamWorldQuality::SetQualityOverride(int32 Level)
{
	CVarDreamWorldQuality->Set(FMath::Clamp(Level, -1, 4), ECVF_SetByCode);
}

int32 UDreamWorldQuality::GetQualityOverride()
{
	return CVarDreamWorldQuality.GetValueOnGameThread();
}

TConstArrayView<FDreamWorldQualitySetting> UDreamWorldQuality::QualityTable(EDreamWorldQualityGroup Group)
{
	using namespace DreamWorld::Quality;
	switch (Group)
	{
	case EDreamWorldQualityGroup::Sky: return Sky;
	case EDreamWorldQualityGroup::Particles: return Particles;
	case EDreamWorldQualityGroup::Fog: return Fog;
	default: return {};
	}
}

void UDreamWorldQuality::Apply(bool bForce)
{
	using namespace DreamWorld::Quality;
	for (int32 G = 0; G < 5; ++G)
	{
		const EDreamWorldQualityGroup Group = EDreamWorldQualityGroup(G);
		const int32 Level = GetLevel(Group);
		if (!bForce && Applied[G] == Level)
		{
			continue;
		}
		Applied[G] = Level;
		for (const FDreamWorldQualitySetting& Setting : QualityTable(Group))
		{
			// (At the scalability's priority: a value set by hand, at a higher one, stays.)
			if (IConsoleVariable* Variable = IConsoleManager::Get().FindConsoleVariable(Setting.Name))
			{
				Variable->Set(Setting.Values[Level], ECVF_SetByScalability);
			}
		}
	}
}
