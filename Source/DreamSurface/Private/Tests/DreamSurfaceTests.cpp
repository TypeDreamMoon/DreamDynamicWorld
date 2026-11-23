// Copyright DreamMoon. All Rights Reserved.

#include "DreamSurfaceDripLine.h"
#include "DreamSurfaceHlslShim.h"
#include "DreamSurfaceInteraction.h"
#include "DreamSurfaceMaps.h"
#include "DreamSurfaceMask.h"
#include "DreamSurfaceRoofCapture.h"
#include "DreamSurfaceSettings.h"
#include "DreamSurfaceSubsystem.h"
#include "DreamSurfaceVehicleGlass.h"
#include "DreamSurfaceWalker.h"

#include "DreamWaterBody.h"
#include "DreamWeather.h"
#include "DreamWeatherChannels.h"
#include "DreamWeatherModule.h"
#include "DreamWeatherPreset.h"
#include "DreamWeatherSelector.h"
#include "DreamWeatherShelter.h"
#include "DreamWeatherSubsystem.h"
#include "DreamWeatherZone.h"
#include "DreamWorldCelestial.h"
#include "DreamWorldSubsystem.h"

#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Engine/World.h"
#include "Misc/App.h"
#include "Misc/ScopeExit.h"
#include "RenderingThread.h"
#include "TextureResource.h"
#include "Misc/AutomationTest.h"
#include "UObject/Package.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace DreamSurface::Tests
{
	constexpr EAutomationTestFlags Flags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter;
	using namespace DreamWeather::Tags;

	UDreamWeatherPreset* MakePreset(const TCHAR* Name, std::initializer_list<TPair<FGameplayTag, float>> Channels)
	{
		UDreamWeatherPreset* Preset = NewObject<UDreamWeatherPreset>(GetTransientPackage(), MakeUniqueObjectName(GetTransientPackage(), UDreamWeatherPreset::StaticClass(), Name));
		for (const TPair<FGameplayTag, float>& Channel : Channels)
		{
			Preset->Channels.Add(Channel.Key, Channel.Value);
		}
		return Preset;
	}

	UDreamWeatherPreset* Clear() { return MakePreset(TEXT("SurfClear"), { { Channel_CloudCoverage, 0.05f }, { Channel_Wind, 0.3f } }); }
	UDreamWeatherPreset* Rain() { return MakePreset(TEXT("SurfRain"), { { Channel_CloudCoverage, 1.0f }, { Channel_Precipitation, 0.9f }, { Channel_Wind, 0.4f } }); }
	UDreamWeatherPreset* Blizzard() { return MakePreset(TEXT("SurfSnow"), { { Channel_CloudCoverage, 1.0f }, { Channel_Precipitation, 0.9f }, { Channel_SnowRatio, 1.0f }, { Channel_Temperature, -25.0f } }); }
	UDreamWeatherPreset* Thaw() { return MakePreset(TEXT("SurfThaw"), { { Channel_CloudCoverage, 0.05f }, { Channel_Temperature, 10.0f } }); }
	UDreamWeatherPreset* DustStorm() { return MakePreset(TEXT("SurfDust"), { { Channel_CloudCoverage, 0.3f }, { Channel_Dust, 1.0f }, { Channel_Wind, 1.0f } }); }

	/** A sequence of presets, Hours each, repeating. */
	UDreamWeatherSelector_Sequence* Sequence(std::initializer_list<TPair<UDreamWeatherPreset*, float>> Entries)
	{
		UDreamWeatherSelector_Sequence* S = NewObject<UDreamWeatherSelector_Sequence>(GetTransientPackage());
		for (const TPair<UDreamWeatherPreset*, float>& E : Entries)
		{
			S->Entries.Add({ E.Key, E.Value });
		}
		return S;
	}

	/** Midsummer midnight, plus Hours. */
	double At(double Hours) { return DreamWorld::Celestial::FromDateTime(FDateTime(2026, 6, 21, 0, 0, 0)) + Hours * 3600.0; }

	/** A game world with the clock, the weather (a weather actor with the given selector) and the surfaces. */
	struct FSurfaceWorld
	{
		UWorld* World = nullptr;
		UDreamWorldSubsystem* Clock = nullptr;
		UDreamWeatherSubsystem* Weather = nullptr;
		UDreamSurfaceSubsystem* Surface = nullptr;

		explicit FSurfaceWorld(UDreamWeatherSelector* Selector)
		{
			World = UWorld::CreateWorld(EWorldType::Game, false);
			if (!World)
			{
				return;
			}
			Clock = World->GetSubsystem<UDreamWorldSubsystem>();
			Weather = World->GetSubsystem<UDreamWeatherSubsystem>();
			Surface = World->GetSubsystem<UDreamSurfaceSubsystem>();
			FActorSpawnParameters Params;
			Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
			ADreamWeather* Actor = World->SpawnActor<ADreamWeather>(Params);
			if (Actor && Selector)
			{
				Selector->Rename(nullptr, Actor);
				Actor->Selector = Selector;
			}
			FDreamWorldClockConfig Config;
			Config.StartTime = FDateTime(2026, 6, 21, 0, 0, 0);
			Config.bAnimate = false;
			Config.Seed = 5;
			Clock->ApplyClockConfig(Config, true);
			Clock->ReportView(FVector(0, 0, 100), FRotator::ZeroRotator, true);
		}
		~FSurfaceWorld()
		{
			if (World)
			{
				World->DestroyWorld(false);
			}
		}

		/** Sets the clock and runs a frame there. */
		void GoTo(double Seconds)
		{
			Clock->SetWorldSeconds(Seconds);
			Clock->Tick(0.0f);
		}

		/** Runs the clock forward to Seconds a step at a time, a frame at each (the way a session sees the weather). */
		void RunTo(double Seconds)
		{
			const double Step = UDreamSurfaceSubsystem::StepSeconds();
			for (double T = Clock->GetWorldSeconds() + Step; T < Seconds; T += Step)
			{
				GoTo(T);
			}
			GoTo(Seconds);
		}
	};

	FDreamSurfaceDrive Drive(float Rain, float Snow, float Temperature, float Wind, float Sun, float Dust)
	{
		FDreamSurfaceDrive D;
		D.Rain = Rain;
		D.Snow = Snow;
		D.Temperature = Temperature;
		D.Wind = Wind;
		D.Sunlight = Sun;
		D.Dust = Dust;
		return D;
	}

	FDreamSurfaceState Run(FDreamSurfaceState S, const FDreamSurfaceDrive& D, float Hours, int32 Steps)
	{
		const FDreamSurfaceRates& Rates = UDreamSurfaceSettings::Get()->Rates;
		for (int32 i = 0; i < Steps; ++i)
		{
			S = UDreamSurfaceSubsystem::Step(S, D, Rates, Hours / Steps);
		}
		return S;
	}

	/** A drive with the sky and the fog too. */
	FDreamSurfaceDrive Drive(float Rain, float Snow, float Temperature, float Wind, float Sun, float Dust, float OpenSky, float Fog)
	{
		FDreamSurfaceDrive D = Drive(Rain, Snow, Temperature, Wind, Sun, Dust);
		D.OpenSky = OpenSky;
		D.Fog = Fog;
		return D;
	}

	/** What a pair of surface maps (state and ice) holds at the texel holding Point, and that texel's centre. */
	struct FMapTexel
	{
		FDreamSurfaceState State;
		FVector2D Texel = FVector2D::ZeroVector;
		bool bRead = false;
	};

	FMapTexel ReadMaps(const FDreamSurfaceMaps& Maps, bool bNear, const FVector2D& Point)
	{
		FMapTexel Out;
		UTextureRenderTarget2D* Map = bNear ? Maps.GetNearMap() : Maps.GetFarMap();
		UTextureRenderTarget2D* Ice = bNear ? Maps.GetNearIceMap() : Maps.GetFarIceMap();
		UTextureRenderTarget2D* Hail = bNear ? Maps.GetNearHailMap() : Maps.GetFarHailMap();
		if (!Map || !Ice || !Hail)
		{
			return Out;
		}
		TArray<FLinearColor> Pixels;
		TArray<FLinearColor> IcePixels;
		TArray<FLinearColor> HailPixels;
		Map->GameThread_GetRenderTargetResource()->ReadLinearColorPixels(Pixels);
		Ice->GameThread_GetRenderTargetResource()->ReadLinearColorPixels(IcePixels);
		Hail->GameThread_GetRenderTargetResource()->ReadLinearColorPixels(HailPixels);
		const int32 Size = Map->SizeX;
		const FVector2D Center = bNear ? Maps.GetNearCenter() : Maps.GetFarCenter();
		const float Extent = bNear ? Maps.GetNearExtent() : Maps.GetFarExtent();
		const FVector2D UV = (Point - Center) / Extent + 0.5;
		const int32 X = FMath::Clamp(FMath::FloorToInt32(UV.X * Size), 0, Size - 1);
		const int32 Y = FMath::Clamp(FMath::FloorToInt32(UV.Y * Size), 0, Size - 1);
		const int32 Index = Y * Size + X;
		if (!Pixels.IsValidIndex(Index) || !IcePixels.IsValidIndex(Index) || !HailPixels.IsValidIndex(Index))
		{
			return Out;
		}
		const FLinearColor S = Pixels[Index];
		const FLinearColor C = IcePixels[Index];
		const FLinearColor H = HailPixels[Index];
		Out.State = FDreamSurfaceState::FromVectors(FVector4f(S.R, S.G, S.B, S.A), FVector4f(C.R, C.G, C.B, C.A), FVector4f(H.R, 0.0f, 0.0f, 0.0f));
		Out.Texel = Center + (FVector2D(X + 0.5, Y + 0.5) / Size - 0.5) * Extent;
		Out.bRead = true;
		return Out;
	}

	FString Describe(const FDreamSurfaceState& S)
	{
		return FString::Printf(TEXT("wet %.3f water %.3f snow %.3f dust %.3f | ice %.3f frost %.3f rime %.3f frozen %.3f | hail %.3f"),
			S.Wetness, S.Water, S.Snow, S.Dust, S.Ice, S.Frost, S.Rime, S.FrozenWater, S.Hail);
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDreamSurfaceFormulaTest, "DreamSurface.Formula", DreamSurface::Tests::Flags)
bool FDreamSurfaceFormulaTest::RunTest(const FString& Parameters)
{
	using namespace DreamSurface::Tests;
	const FDreamSurfaceState Dry;

	// Rain: the film soaks first, the hollows only fill once it has.
	const FDreamSurfaceState TenMinutes = Run(Dry, Drive(1, 0, 15, 0, 0, 0), 1.0f / 6.0f, 4);
	TestTrue(TEXT("ten minutes of heavy rain wet a surface"), TenMinutes.Wetness > 0.5f);
	TestTrue(TEXT("but fill no hollows yet"), TenMinutes.Water < 0.05f);
	const FDreamSurfaceState TwoHours = Run(Dry, Drive(1, 0, 15, 0, 0, 0), 2.0f, 8);
	TestTrue(TEXT("two hours soak it"), TwoHours.Wetness > 0.99f);
	TestTrue(TEXT("and fill the hollows"), TwoHours.Water > 0.6f);

	// Drying: in the sun and wind faster than in the shade; the film before the puddles.
	const FDreamSurfaceState Sunny = Run(TwoHours, Drive(0, 0, 25, 0.5f, 1, 0), 1.0f, 4);
	const FDreamSurfaceState Shade = Run(TwoHours, Drive(0, 0, 25, 0.0f, 0, 0), 1.0f, 4);
	TestTrue(TEXT("an hour of sun dries the film"), Sunny.Wetness < 0.4f);
	TestTrue(TEXT("the shade dries slower"), Shade.Wetness > Sunny.Wetness + 0.2f);
	TestTrue(TEXT("puddles outlast the film"), Sunny.Water > Sunny.Wetness);
	TestTrue(TEXT("nothing dries while it rains"), Run(TwoHours, Drive(0.5f, 0, 25, 1, 1, 0), 1.0f, 4).Wetness > 0.99f);

	// Snow: settles in the cold, not on warm ground; melts above freezing and the melt wets.
	const FDreamSurfaceState Snowed = Run(Dry, Drive(0, 1, -5, 0, 0, 0), 6.0f, 24);
	TestTrue(TEXT("six hours of heavy snow lay a full cover"), Snowed.Snow > 0.95f);
	const FDreamSurfaceState WarmSnow = Run(Dry, Drive(0, 1, 6, 0, 0, 0), 2.0f, 8);
	TestTrue(TEXT("snow on warm ground melts as it lands"), WarmSnow.Snow < 0.01f);
	TestTrue(TEXT("and wets it as rain would"), WarmSnow.Wetness > 0.99f);
	TestTrue(TEXT("snow holds below freezing"), Run(Snowed, Drive(0, 0, -5, 0, 1, 0), 12.0f, 48).Snow > 0.95f);
	const FDreamSurfaceState Thawed = Run(Snowed, Drive(0, 0, 10, 0, 1, 0), 4.0f, 16);
	TestTrue(TEXT("a sunny thaw melts it"), Thawed.Snow < Snowed.Snow - 0.3f);
	TestTrue(TEXT("and the melt wets the ground"), Thawed.Wetness > 0.5f);

	// Dust: a dust storm lays it, rain washes it.
	const FDreamSurfaceState Dusty = Run(Dry, Drive(0, 0, 30, 1, 1, 1), 2.0f, 8);
	TestTrue(TEXT("a dust storm covers a surface"), Dusty.Dust > 0.9f);
	TestTrue(TEXT("rain washes the dust off"), Run(Dusty, Drive(1, 0, 20, 0, 0, 0), 0.5f, 4).Dust < 0.1f);

	// One long step and many short ones agree while the weather holds.
	const FDreamSurfaceDrive Mixed = Drive(0.3f, 0.2f, 1.0f, 0.4f, 0.5f, 0.2f);
	const FDreamSurfaceState One = Run(Dry, Mixed, 0.25f, 1);
	const FDreamSurfaceState Many = Run(Dry, Mixed, 0.25f, 16);
	TestTrue(TEXT("a step's length barely matters"), One.Equals(Many, 0.03f));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDreamSurfaceColdTest, "DreamSurface.Cold", DreamSurface::Tests::Flags)
bool FDreamSurfaceColdTest::RunTest(const FString& Parameters)
{
	using namespace DreamSurface::Tests;
	const FDreamSurfaceState Dry;
	const FDreamSurfaceState Soaked = Run(Dry, Drive(1, 0, 15, 0, 0, 0), 2.0f, 8);

	// Frost: a clear, still night with the air above freezing takes the ground below it; cloud or wind keep it off.
	const FDreamSurfaceState Frosted = Run(Dry, Drive(0, 0, 2, 0, 0, 0, 1, 0), 4.0f, 16);
	AddInfo(FString::Printf(TEXT("clear still night at 2 C, four hours: %s"), *Describe(Frosted)));
	TestTrue(TEXT("a clear still night at 2 Celsius frosts the ground"), Frosted.Frost > 0.8f);
	TestTrue(TEXT("not under cloud"), Run(Dry, Drive(0, 0, 2, 0, 0, 0, 0, 0), 4.0f, 16).Frost < 0.01f);
	TestTrue(TEXT("not in a wind"), Run(Dry, Drive(0, 0, 2, 1, 0, 0, 1, 0), 4.0f, 16).Frost < 0.01f);
	TestTrue(TEXT("nor while it snows"), Run(Dry, Drive(0, 1, -5, 0, 0, 0, 1, 0), 4.0f, 16).Frost < 0.01f);
	// The morning sun takes it within the half hour.
	const FDreamSurfaceState Morning = Run(Frosted, Drive(0, 0, 1, 0, 1, 0, 1, 0), 0.5f, 4);
	TestTrue(TEXT("the sun melts the frost quickly"), Morning.Frost < 0.05f);
	TestTrue(TEXT("but a shaded spot keeps it"), Run(Frosted, Drive(0, 0, 1, 0, 0, 0, 0.2f, 0), 0.5f, 4).Frost > 0.7f);

	// Black ice: a soaked surface on a clear freezing night glazes over; the day's warmth melts it back into the film.
	const FDreamSurfaceState Glazed = Run(Soaked, Drive(0, 0, 1, 0, 0, 0, 1, 0), 2.0f, 8);
	AddInfo(FString::Printf(TEXT("soaked, then a clear night at 1 C, two hours: %s"), *Describe(Glazed)));
	TestTrue(TEXT("the film freezes into a glaze"), Glazed.Ice > 0.8f);
	TestTrue(TEXT("which is no longer water"), Glazed.Wetness < 0.2f);
	TestTrue(TEXT("the puddles freeze over more slowly"), Glazed.FrozenWater > 0.2f && Glazed.FrozenWater < 0.95f);
	TestTrue(TEXT("and a frozen puddle neither drains nor dries"), FMath::Abs(Glazed.Water - Soaked.Water) < 0.05f);
	const FDreamSurfaceState Melted = Run(Glazed, Drive(0, 0, 4, 0, 1, 0, 1, 0), 2.0f, 8);
	AddInfo(FString::Printf(TEXT("then a sunny 4 C, two hours: %s"), *Describe(Melted)));
	TestTrue(TEXT("a sunny thaw melts the glaze"), Melted.Ice < 0.05f && Melted.FrozenWater < 0.05f);
	TestTrue(TEXT("back into a wet film"), Melted.Wetness > 0.3f);

	// Freezing rain: rain landing on frozen ground freezes as it lands, wetting nothing.
	const FDreamSurfaceState Freezing = Run(Dry, Drive(1, 0, -3, 0, 0, 0, 0, 0), 1.0f, 4);
	TestTrue(TEXT("rain on frozen ground glazes it"), Freezing.Ice > 0.9f);
	TestTrue(TEXT("and does not wet it"), Freezing.Wetness < 0.05f);

	// Rime: a freezing fog builds it, faster in a wind; no fog, none.
	const FDreamSurfaceState Rimed = Run(Dry, Drive(0, 0, -4, 1, 0, 0, 0, 1), 3.0f, 12);
	const FDreamSurfaceState StillRime = Run(Dry, Drive(0, 0, -4, 0, 0, 0, 0, 1), 3.0f, 12);
	AddInfo(FString::Printf(TEXT("freezing fog, three hours: windy rime %.3f, still %.3f"), Rimed.Rime, StillRime.Rime));
	TestTrue(TEXT("a freezing fog in a wind builds rime"), Rimed.Rime > 0.8f);
	TestTrue(TEXT("more slowly in still air"), StillRime.Rime < Rimed.Rime - 0.2f);
	TestTrue(TEXT("no fog, no rime"), Run(Dry, Drive(0, 0, -4, 1, 0, 0, 0, 0), 3.0f, 12).Rime < 0.01f);

	// A step's length barely matters for the cold either.
	const FDreamSurfaceDrive Night = Drive(0.2f, 0.0f, 0.5f, 0.2f, 0.0f, 0.0f, 0.8f, 0.5f);
	const FDreamSurfaceState One = Run(Soaked, Night, 0.25f, 1);
	const FDreamSurfaceState Many = Run(Soaked, Night, 0.25f, 16);
	AddInfo(FString::Printf(TEXT("one step %s / sixteen %s"), *Describe(One), *Describe(Many)));
	TestTrue(TEXT("a step's length barely matters"), One.Equals(Many, 0.05f));

	// The grip: the worst of what lies there.
	using namespace DreamWind::Hlsl;
	const FDreamSurfaceGrip Grip;
	auto GripOf = [&Grip](float4 Cover, float4 Cover2)
	{
		return DSF_Grip(float4(Grip.Ice, Grip.FrozenPuddle, Grip.Frost, Grip.Snow), float4(Grip.Puddle, Grip.Wet, 1.0f, 1.0f), Cover, Cover2);
	};
	TestNearlyEqual(TEXT("dry: full grip"), GripOf(float4(0.0f, 0.0f, 0.0f, 0.0f), float4(0.0f, 0.0f, 0.0f, 0.0f)), 1.0f, 1.0e-4f);
	TestNearlyEqual(TEXT("black ice: the ice's"), GripOf(float4(1.0f, 0.0f, 0.0f, 0.0f), float4(0.0f, 1.0f, 0.0f, 0.0f)), Grip.Ice, 1.0e-4f);
	TestTrue(TEXT("a thin glaze is less slippery"), GripOf(float4(0.3f, 0.0f, 0.0f, 0.0f), float4(0.0f, 0.0f, 0.0f, 0.0f)) > 0.7f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDreamSurfaceFreezingRainTest, "DreamSurface.FreezingRain", DreamSurface::Tests::Flags)
bool FDreamSurfaceFreezingRainTest::RunTest(const FString& Parameters)
{
	using namespace DreamSurface::Tests;
	// Rain with the air some degrees below freezing: ordinary rain falls as snow and lies; freezing rain stays rain and
	// glazes the ground, wetting nothing.
	auto After = [this](bool bFreezing)
	{
		UDreamWeatherSelector_Fixed* Fixed = NewObject<UDreamWeatherSelector_Fixed>();
		Fixed->Preset = MakePreset(TEXT("SurfFreezingRain"), { { Channel_CloudCoverage, 1.0f }, { Channel_Precipitation, 0.6f },
			{ Channel_FreezingRain, bFreezing ? 1.0f : 0.0f }, { Channel_Temperature, -30.0f } });
		FSurfaceWorld W(Fixed);
		W.GoTo(At(12.0));
		const FDreamSurfaceState S = W.Surface ? W.Surface->SampleSurface(FVector(0, 0, 0)) : FDreamSurfaceState();
		AddInfo(FString::Printf(TEXT("%s, air %.1f C: %s"), bFreezing ? TEXT("freezing rain") : TEXT("ordinary rain"),
			W.Weather ? W.Weather->SampleWeather(FVector(0, 0, 0)).Temperature : 0.0f, *Describe(S)));
		return S;
	};
	const FDreamSurfaceState Ordinary = After(false);
	const FDreamSurfaceState Freezing = After(true);
	TestTrue(TEXT("ordinary rain below freezing falls as snow and lies"), Ordinary.Snow > 0.3f && Ordinary.Ice < 0.05f);
	TestTrue(TEXT("freezing rain glazes the ground"), Freezing.Ice > 0.8f);
	TestTrue(TEXT("and lays no snow"), Freezing.Snow < 0.01f);
	TestTrue(TEXT("nor wets it"), Freezing.Wetness < 0.05f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDreamSurfaceColdNightTest, "DreamSurface.ColdNight", DreamSurface::Tests::Flags)
bool FDreamSurfaceColdNightTest::RunTest(const FString& Parameters)
{
	using namespace DreamSurface::Tests;
	UDreamWeatherSelector_Fixed* Fixed = NewObject<UDreamWeatherSelector_Fixed>();
	Fixed->Preset = Clear();
	FSurfaceWorld W(Fixed);
	if (!TestNotNull(TEXT("surface"), W.Surface))
	{
		return false;
	}
	const FVector P(0, 0, 0);
	// Midwinter: the air at three in the morning under the clear preset, and a cold night's preset that brings it to
	// 1.5 Celsius (the ground, under a clear still sky, goes below freezing).
	const double Day = 24.0 * 183.0;
	W.GoTo(At(Day + 3.0));
	const float Air = W.Weather->SampleWeatherAt(P, W.Weather->GetGlobalValues(), At(Day + 3.0)).Temperature;
	const float Offset = 1.5f - Air;
	UDreamWeatherPreset* ColdNight = MakePreset(TEXT("SurfColdNight"), { { Channel_CloudCoverage, 0.05f }, { Channel_Wind, 0.15f }, { Channel_Temperature, Offset } });
	UDreamWeatherPreset* EveningRain = MakePreset(TEXT("SurfEveningRain"), { { Channel_CloudCoverage, 1.0f }, { Channel_Precipitation, 0.9f }, { Channel_Temperature, Offset + 4.0f } });

	// An evening's rain, then the sky clears for the night.
	W.GoTo(At(Day - 6.0));
	W.Weather->ForceWeather(EveningRain, 0.0f);
	W.RunTo(At(Day - 3.0));
	W.Weather->ForceWeather(ColdNight, 0.0f);
	W.RunTo(At(Day + 3.0));
	const FDreamSurfaceState Night = W.Surface->SampleSurface(P);
	const FDreamSurfaceContact OnIt = W.Surface->SampleContact(P, FVector::UpVector);
	AddInfo(FString::Printf(TEXT("air at 3:00 %.1f C under the clear preset, the cold night's offset %.1f; at 3:00 %s, grip %.2f"),
		Air, Offset, *Describe(Night), OnIt.Grip));
	TestTrue(TEXT("the rain's film has frozen into a glaze"), Night.Ice > 0.5f);
	TestTrue(TEXT("frost has grown"), Night.Frost > 0.5f);
	TestTrue(TEXT("the ground is slippery"), OnIt.Grip < 0.4f);
	TestTrue(TEXT("a contact finds ice or frost"), OnIt.Kind == EDreamSurfaceContactKind::Ice || OnIt.Kind == EDreamSurfaceContactKind::Frost);
	TestNearlyEqual(TEXT("GetGripAt is the contact's"), W.Surface->GetGripAt(P), OnIt.Grip, 1.0e-4f);

	// The winter sun by midday: frost and glaze gone, the grip back.
	W.RunTo(At(Day + 13.0));
	const FDreamSurfaceState Noon = W.Surface->SampleSurface(P);
	const float NoonGrip = W.Surface->GetGripAt(P);
	AddInfo(FString::Printf(TEXT("at 13:00 %s, grip %.2f"), *Describe(Noon), NoonGrip));
	TestTrue(TEXT("the sun has taken the frost"), Noon.Frost < 0.05f);
	TestTrue(TEXT("and the glaze"), Noon.Ice < 0.1f);
	TestTrue(TEXT("the grip is back"), NoonGrip > 0.75f);
	TestTrue(TEXT("the view's state has the cold too"), W.Surface->GetViewState().ToIceVector().Equals(W.Surface->SampleSurface(FVector(0, 0, 100)).ToIceVector(), 1.0e-4f));
	return true;
}

namespace DreamSurface::Tests
{
	struct FGlassPatch
	{
		float Water = 0.0f;     // the share of the patch under water
		float Running = 0.0f;   // the share crossed by running drops
		float MaxSlope = 0.0f;
		uint32 Hash = 0;        // of the whole field, to compare two moments
	};

	/** The rain on a metre of glass, sampled every 5 mm. */
	static FGlassPatch GlassPatch(float Time, float Rain, float Wet, float Steep)
	{
		using namespace DreamWind::Hlsl;
		FGlassPatch P;
		int32 Count = 0;
		for (float Y = 0.0f; Y < 100.0f; Y += 0.5f)
		{
			for (float X = 0.0f; X < 100.0f; X += 0.5f)
			{
				const float4 R = DSF_GlassRain(float2(X, Y), Time, Rain, Wet, Steep);
				P.Water += R.z > 0.3f ? 1.0f : 0.0f;
				P.Running += R.w > 0.3f ? 1.0f : 0.0f;
				P.MaxSlope = FMath::Max(P.MaxSlope, FMath::Max(FMath::Abs(R.x), FMath::Abs(R.y)));
				P.Hash = HashCombineFast(P.Hash, GetTypeHash(FMath::RoundToInt32(R.z * 1000.0f)));
				++Count;
			}
		}
		P.Water /= Count;
		P.Running /= Count;
		return P;
	}

	/** Where the drops running down one column of glass are (the mean height of their heads, cm), in a window lower
	 *  than the shortest gap between two drops; false when the window holds none. */
	static bool RunnerHeight(float Column, float Time, float& OutHeight)
	{
		using namespace DreamWind::Hlsl;
		constexpr float Width = 2.5f;
		double Sum = 0.0;
		int32 Count = 0;
		for (float Y = 0.0f; Y < 45.0f; Y += 0.05f)
		{
			for (float X = Column * Width + 0.01f; X < (Column + 1.0f) * Width; X += 0.05f)
			{
				if (DSF_GlassRunners(float2(X, Y), Time, 1.0f, Width).z > 0.9f)
				{
					Sum += Y;
					++Count;
				}
			}
		}
		OutHeight = Count ? float(Sum / Count) : 0.0f;
		return Count > 0;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDreamSurfaceGlassRainTest, "DreamSurface.GlassRain", DreamSurface::Tests::Flags)
bool FDreamSurfaceGlassRainTest::RunTest(const FString& Parameters)
{
	using namespace DreamSurface::Tests;

	const FGlassPatch Dry = GlassPatch(3.0f, 0.0f, 0.0f, 1.0f);
	TestTrue(TEXT("a dry pane has no drops"), Dry.Water == 0.0f && Dry.MaxSlope == 0.0f);

	const FGlassPatch Pouring = GlassPatch(3.0f, 1.0f, 1.0f, 1.0f);
	const FGlassPatch Drizzle = GlassPatch(3.0f, 0.2f, 0.2f, 1.0f);
	AddInfo(FString::Printf(TEXT("water on the pane: pouring %.3f, drizzle %.3f; running %.3f"), Pouring.Water, Drizzle.Water, Pouring.Running));
	TestTrue(TEXT("heavy rain covers part of an upright pane, not all of it"), Pouring.Water > 0.08f && Pouring.Water < 0.7f);
	TestTrue(TEXT("a drizzle fewer drops"), Drizzle.Water < Pouring.Water * 0.7f && Drizzle.Water > 0.0f);
	TestTrue(TEXT("drops run down it"), Pouring.Running > 0.005f);
	TestTrue(TEXT("the water's slope stays bounded"), Pouring.MaxSlope < 4.0f);
	TestTrue(TEXT("the drops come and go while it rains"), GlassPatch(9.0f, 1.0f, 1.0f, 1.0f).Hash != Pouring.Hash);

	const FGlassPatch Flat = GlassPatch(3.0f, 1.0f, 1.0f, 0.0f);
	TestTrue(TEXT("on a flat pane drops sit"), Flat.Water > 0.05f);
	TestTrue(TEXT("but none run"), Flat.Running == 0.0f);

	// After the rain: the sitting drops stay put while the pane is wet, and go as it dries.
	const FGlassPatch After = GlassPatch(3.0f, 0.0f, 0.8f, 1.0f);
	TestTrue(TEXT("drops stay on a wet pane after the rain"), After.Water > 0.05f && After.Running == 0.0f);
	TestTrue(TEXT("and do not move"), GlassPatch(40.0f, 0.0f, 0.8f, 1.0f).Hash == After.Hash);
	TestTrue(TEXT("fewer as it dries"), GlassPatch(3.0f, 0.0f, 0.2f, 1.0f).Water < After.Water * 0.7f);

	// A running drop goes down, at a speed between the slowest and the fastest a drop runs (6..20 cm/s).
	bool bMeasured = false;
	for (float Column = 0.0f; Column < 60.0f && !bMeasured; Column += 1.0f)
	{
		for (float Time = 0.0f; Time < 10.0f && !bMeasured; Time += 0.7f)
		{
			float Before = 0.0f, Later = 0.0f;
			if (RunnerHeight(Column, Time, Before) && RunnerHeight(Column, Time + 0.2f, Later) && Before > 8.0f && Before < 40.0f)
			{
				bMeasured = true;
				const float Speed = (Before - Later) / 0.2f;
				AddInfo(FString::Printf(TEXT("column %.0f: a drop at %.1f cm runs at %.1f cm/s"), Column, Before, Speed));
				TestTrue(TEXT("a running drop goes down at its speed"), Speed > 6.0f * 0.8f && Speed < 20.0f * 1.2f);
			}
		}
	}
	TestTrue(TEXT("a running drop was found to measure"), bMeasured);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDreamSurfaceVehicleGlassTest, "DreamSurface.VehicleGlass", DreamSurface::Tests::Flags)
bool FDreamSurfaceVehicleGlassTest::RunTest(const FString& Parameters)
{
	using namespace DreamWind::Hlsl;
	// The flow: on a still pane the drops run down; at speed the air pushes them up a windscreen, and a side wind across.
	const float4 Still = DSF_GlassFlow(float2(0.0f, 0.0f), 1.0f);
	TestTrue(TEXT("still: they run straight down at gravity's speed"), FMath::IsNearlyEqual(Still.y, -1.0f, 1.0e-4f) && FMath::IsNearlyEqual(Still.z, 1.0f, 1.0e-4f) && Still.w < 1.0e-4f);
	const float4 Slow = DSF_GlassFlow(float2(0.0f, 300.0f), 0.5f);
	TestTrue(TEXT("a light air up the pane does not move them"), FMath::IsNearlyEqual(Slow.y, -1.0f, 1.0e-4f));
	const float4 Motorway = DSF_GlassFlow(float2(0.0f, 2400.0f * 0.87f), 0.5f);   // 86 km/h on a windscreen raked 30 degrees
	AddInfo(FString::Printf(TEXT("86 km/h on a raked windscreen: flow (%.2f, %.2f), speed %.2f, stretch %.2f"), Motorway.x, Motorway.y, Motorway.z, Motorway.w));
	TestTrue(TEXT("at speed the drops go up the windscreen"), Motorway.y > 0.5f);
	TestTrue(TEXT("and streak"), Motorway.w > 0.4f);
	const float4 Side = DSF_GlassFlow(float2(1800.0f, 0.0f), 1.0f);
	TestTrue(TEXT("a wind across a side window drives them across and down"), Side.x > 0.3f && Side.y < -0.3f);
	TestNearlyEqual(TEXT("a windscreen meeting the rain at 97 km/h catches four times as much"), DSF_GlassRainGain(1.0f, 2700.0f), 4.0f, 1.0e-4f);
	TestNearlyEqual(TEXT("a rear window none more"), DSF_GlassRainGain(0.0f, 2700.0f), 1.0f, 1.0e-4f);

	// The wipers: a blade swinging from 0 to 180 degrees once a second, started at 0: the top of its arc (90 degrees) is
	// swept a quarter of the way through each swing and again three quarters through.
	const float Half = 3.14159265f * 0.5f;
	TestTrue(TEXT("not swept before the blade gets there"), DSF_WiperSince(Half, 0.0f, 3.14159265f, 0.2f, 0.0f, 1.0f) > 1.0e5f);
	TestNearlyEqual(TEXT("swept on the way out"), DSF_WiperSince(Half, 0.0f, 3.14159265f, 0.3f, 0.0f, 1.0f), 0.05f, 1.0e-3f);
	TestNearlyEqual(TEXT("and on the way back"), DSF_WiperSince(Half, 0.0f, 3.14159265f, 0.8f, 0.0f, 1.0f), 0.05f, 1.0e-3f);
	TestNearlyEqual(TEXT("the next swing"), DSF_WiperSince(Half, 0.0f, 3.14159265f, 1.3f, 0.0f, 1.0f), 0.05f, 1.0e-3f);
	TestTrue(TEXT("beyond its arc never"), DSF_WiperSince(-0.3f, 0.0f, 3.14159265f, 5.0f, 0.0f, 1.0f) > 1.0e5f);
	TestTrue(TEXT("parked never"), DSF_WiperSince(Half, 0.0f, 3.14159265f, 5.0f, 0.0f, 0.0f) > 1.0e5f);
	TestNearlyEqual(TEXT("just swept: clear"), DSF_WiperClear(0.0f, 1.0f).x, 1.0f, 1.0e-4f);
	TestTrue(TEXT("a heavy rain lays the drops again within seconds"), DSF_WiperClear(4.0f, 1.0f).x < 0.01f);
	TestTrue(TEXT("a drizzle more slowly"), DSF_WiperClear(4.0f, 0.1f).x > 0.3f);

	// What a wiper clears holds no water; elsewhere the rain lies on the pane.
	const float4 Flow = DSF_GlassFlow(float2(0.0f, 0.0f), 0.5f);
	float Wiped = 0.0f, Rained = 0.0f;
	for (int32 i = 0; i < 400; ++i)
	{
		const float2 At(float(i % 20) * 0.731f, float(i / 20) * 0.917f);
		Wiped += DSF_GlassRainFlow(At, 1.0f, 1.0f, 1.0f, Flow, 1.0f).z;
		Rained += DSF_GlassRainFlow(At, 1.0f, 1.0f, 1.0f, Flow, 0.0f).z;
	}
	TestTrue(TEXT("a pane just wiped is clear"), Wiped < 1.0e-4f);
	TestTrue(TEXT("one in the rain is not"), Rained > 20.0f);

	// The component writes the glass's velocity in each glass mesh's own frame and the wipers' state.
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
	if (!TestNotNull(TEXT("world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(false); };
	AActor* Car = World->SpawnActor<AActor>(FVector::ZeroVector, FRotator(0.0, 90.0, 0.0));
	USceneComponent* Root = NewObject<USceneComponent>(Car, TEXT("Root"));
	Car->SetRootComponent(Root);
	Root->RegisterComponent();
	Root->SetWorldRotation(FRotator(0.0, 90.0, 0.0));
	UStaticMeshComponent* Screen = NewObject<UStaticMeshComponent>(Car, TEXT("Screen"));
	Screen->SetupAttachment(Root);
	Screen->SetRelativeRotation(FRotator(-30.0, 0.0, 0.0));
	Screen->ComponentTags.Add(TEXT("DreamGlass"));
	Screen->RegisterComponent();
	UStaticMeshComponent* Body = NewObject<UStaticMeshComponent>(Car, TEXT("Body"));
	Body->SetupAttachment(Root);
	Body->RegisterComponent();
	UDreamSurfaceVehicleGlassComponent* Glass = NewObject<UDreamSurfaceVehicleGlassComponent>(Car, TEXT("Glass"));
	Glass->ExtraVelocity = FVector(1000.0, 0.0, 0.0);
	Glass->RegisterComponent();
	TestEqual(TEXT("it writes to the tagged glass only"), Glass->GetGlass().Num(), 1);
	Glass->TickComponent(0.016f, LEVELTICK_All, nullptr);
	TestTrue(TEXT("the car's forward speed, in its world"), Glass->GetGlassVelocity().Equals(FVector(0.0, 1000.0, 0.0), 0.5));
	const TArray<float>& Data = Screen->GetCustomPrimitiveData().Data;
	if (TestTrue(TEXT("the glass has the data"), Data.Num() >= UDreamSurfaceVehicleGlassComponent::DataIndex + 8))
	{
		const FVector Local(Data[24], Data[25], Data[26]);
		const FVector Expected = Screen->GetComponentTransform().InverseTransformVectorNoScale(FVector(0.0, 1000.0, 0.0));
		AddInfo(FString::Printf(TEXT("velocity in the windscreen's frame (%.0f, %.0f, %.0f)"), Local.X, Local.Y, Local.Z));
		TestTrue(TEXT("in the windscreen's own frame"), Local.Equals(Expected, 0.5));
		TestEqual(TEXT("wipers parked"), Data[28], 0.0f);
	}
	Glass->WiperPeriod = 1.5f;
	Glass->SetWipers(true);
	const TArray<float>& Wiping = Screen->GetCustomPrimitiveData().Data;
	TestTrue(TEXT("wipers running"), Wiping.Num() > 29 && FMath::IsNearlyEqual(Wiping[28], 1.5f));
	TestTrue(TEXT("the body has none"), Body->GetCustomPrimitiveData().Data.Num() == 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDreamSurfaceEaveDripTest, "DreamSurface.EaveDrips", DreamSurface::Tests::Flags)
bool FDreamSurfaceEaveDripTest::RunTest(const FString& Parameters)
{
	using namespace DreamSurface::Tests;
	{
		using namespace DreamWind::Hlsl;
		TestNearlyEqual(TEXT("in a downpour the eave runs"), DSF_EaveDrip(1.0f, 1.0f, 0.0f, 0.55f), 1.0f, 1.0e-4f);
		TestNearlyEqual(TEXT("a soaked roof after the rain drips as hard"), DSF_EaveDrip(0.0f, 1.0f, 0.0f, 0.55f), 1.0f, 1.0e-4f);
		const float Drying = DSF_EaveDrip(0.0f, 0.7f, 0.0f, 0.55f);
		TestTrue(TEXT("a drying roof drips less"), Drying > 0.0f && Drying < 0.3f);
		TestNearlyEqual(TEXT("a damp one not at all"), DSF_EaveDrip(0.0f, 0.5f, 0.0f, 0.55f), 0.0f, 1.0e-4f);
		TestNearlyEqual(TEXT("a frozen one not at all"), DSF_EaveDrip(0.0f, 1.0f, 0.6f, 0.55f), 0.0f, 1.0e-4f);
	}

	// An eave in a world: an afternoon's rain, then the sky clears.
	UDreamWeatherSelector_Fixed* Fixed = NewObject<UDreamWeatherSelector_Fixed>();
	Fixed->Preset = Clear();
	FSurfaceWorld W(Fixed);
	if (!TestNotNull(TEXT("surface"), W.Surface))
	{
		return false;
	}
	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	ADreamSurfaceDripLine* Eave = W.World->SpawnActor<ADreamSurfaceDripLine>(FVector(0.0, 0.0, 300.0), FRotator::ZeroRotator, Params);
	Eave->Drips->Length = 400.0f;
	W.GoTo(At(11.0));
	W.Weather->ForceWeather(Rain(), 0.0f);
	W.RunTo(At(13.0));
	Eave->Drips->Refresh();
	const float Raining = Eave->Drips->GetDripStrength();
	W.Weather->ForceWeather(Clear(), 0.0f);
	W.RunTo(At(13.25));
	Eave->Drips->Refresh();
	const float JustAfter = Eave->Drips->GetDripStrength();
	W.RunTo(At(17.0));
	Eave->Drips->Refresh();
	const float Later = Eave->Drips->GetDripStrength();
	AddInfo(FString::Printf(TEXT("drip strength: in the rain %.2f, a quarter hour after %.2f, four hours after %.2f"), Raining, JustAfter, Later));
	TestTrue(TEXT("the eave runs in the rain"), Raining > 0.8f);
	TestTrue(TEXT("and drips on just after it"), JustAfter > 0.2f);
	TestTrue(TEXT("dry hours later"), Later < 0.01f);
	TestNearlyEqual(TEXT("drops a second: strength x per metre x length"), Eave->Drips->GetDropRate(), Later * Eave->Drips->DropsPerMetre * 4.0f, 1.0e-3f);

	// Dripping, it stirs the puddles below (the view is close).
	W.RunTo(At(13.25));
	W.Weather->ForceWeather(Rain(), 0.0f);
	W.RunTo(At(14.0));
	Eave->Drips->Refresh();
	const int32 Before = W.Surface->GetPendingStamps().Num();
	Eave->Drips->TickComponent(1.0f, LEVELTICK_All, nullptr);
	TestTrue(TEXT("the drops ring the puddles below"), W.Surface->GetPendingStamps().Num() > Before);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDreamSurfaceWalkerTest, "DreamSurface.Walker", DreamSurface::Tests::Flags)
bool FDreamSurfaceWalkerTest::RunTest(const FString& Parameters)
{
	using namespace DreamSurface::Tests;
	UDreamWeatherSelector_Fixed* Fixed = NewObject<UDreamWeatherSelector_Fixed>();
	Fixed->Preset = Rain();
	FSurfaceWorld W(Fixed);
	if (!TestNotNull(TEXT("surface"), W.Surface))
	{
		return false;
	}
	W.GoTo(At(12.0));
	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	ADreamSurfaceWalker* Walker = W.World->SpawnActor<ADreamSurfaceWalker>(FVector(500.0, 0.0, 0.0), FRotator::ZeroRotator, Params);
	Walker->Center = FVector2D(0.0, 0.0);
	Walker->Radii = FVector2D(500.0, 300.0);
	Walker->LeftFoot->bTraceGround = false;
	Walker->RightFoot->bTraceGround = false;
	W.Surface->TakePendingStamps();

	// Ten seconds at 1.2 m/s: some 12 m round the loop, a foot down every 65 cm, in turn.
	FVector Last = Walker->GetActorLocation();
	double Walked = 0.0;
	for (int32 i = 0; i < 100; ++i)
	{
		Walker->WalkFor(0.1f);
		Walked += FVector::Dist2D(Walker->GetActorLocation(), Last);
		Last = Walker->GetActorLocation();
		const FVector2D P(Last);
		TestTrue(TEXT("it stays on its loop"), FMath::IsNearlyEqual(FMath::Square(P.X / 500.0) + FMath::Square(P.Y / 300.0), 1.0, 0.02));
	}
	const TArray<FVector4f> Stamps = W.Surface->TakePendingStamps();
	AddInfo(FString::Printf(TEXT("walked %.0f cm in 10 s, %d steps"), Walked, Walker->GetStepCount()));
	TestNearlyEqual(TEXT("at its speed"), Walked, 1200.0, 30.0);
	TestEqual(TEXT("a step every half stride"), Walker->GetStepCount(), 18);
	TestEqual(TEXT("each a print"), Stamps.Num() / 2, Walker->GetStepCount());
	// The feet step in turn: consecutive prints lie either side of the way it goes.
	if (Stamps.Num() >= 6)
	{
		const FVector2D A(Stamps[0].X, Stamps[0].Y), B(Stamps[2].X, Stamps[2].Y), C(Stamps[4].X, Stamps[4].Y);
		const FVector2D Ahead = (C - A).GetSafeNormal();
		const double SideA = FVector2D::CrossProduct(Ahead, A - B), SideC = FVector2D::CrossProduct(Ahead, C - B);
		TestTrue(TEXT("left, right, left"), SideA * SideC > 0.0 && FMath::Abs(SideA) > 1.0);
	}
	TestTrue(TEXT("its prints stir the puddles"), Stamps.Num() >= 2 && Stamps[1].Y > 0.5f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDreamSurfaceWhereWaterTest, "DreamSurface.WhereWaterAndSnowLie", DreamSurface::Tests::Flags)
bool FDreamSurfaceWhereWaterTest::RunTest(const FString& Parameters)
{
	using namespace DreamWind::Hlsl;

	// The ground's lows: a dip is low, a rise is not, a box beside the point does not make it a dip.
	TestTrue(TEXT("a dip lies low"), DSF_GroundLow(-20.0f, float4(0, 0, 0, 0), float4(0, 0, 0, 0)) > 15.0f);
	TestTrue(TEXT("a crest does not"), DSF_GroundLow(20.0f, float4(0, 0, 0, 0), float4(0, 0, 0, 0)) < 0.0f);
	TestTrue(TEXT("a box beside flat ground is no dip"), FMath::Abs(DSF_GroundLow(0.0f, float4(100, 0, 0, 0), float4(0, 0, 120, 0))) < 1.0f);

	// Full of water, flat hard ground holds puddles only in its deepest hollows; a real dip all over; soft ground faintly.
	int32 Count = 0;
	float Flat = 0.0f, InDip = 0.0f, Soft = 0.0f;
	for (float Y = 0.0f; Y < 3000.0f; Y += 10.0f)
	{
		for (float X = 0.0f; X < 3000.0f; X += 10.0f)
		{
			const float Hollow = DSF_Hollow(X, Y, 150.0f);
			Flat += DSF_PuddleCover(1.0f, 1.0f, DSF_PuddleDepth(Hollow, 0.0f), 0.0f);
			InDip += DSF_PuddleCover(1.0f, 1.0f, DSF_PuddleDepth(Hollow, 30.0f), 0.0f);
			Soft += DSF_PuddleCover(1.0f, 1.0f, DSF_PuddleDepth(Hollow, 0.0f), 1.0f);
			++Count;
		}
	}
	Flat /= Count;
	InDip /= Count;
	Soft /= Count;
	AddInfo(FString::Printf(TEXT("puddles when full: flat %.3f, in a dip %.3f, flat soft ground %.3f"), Flat, InDip, Soft));
	TestTrue(TEXT("flat ground: puddles in the deepest hollows, not everywhere"), Flat > 0.02f && Flat < 0.2f);
	TestTrue(TEXT("a dip a quarter of a metre deep holds water all over"), InDip > 0.95f);
	TestTrue(TEXT("soft ground's puddles are faint"), Soft < Flat * 0.4f);
	TestTrue(TEXT("no water, no puddles"), DSF_PuddleCover(0.0f, 1.0f, 1.0f, 0.0f) < 0.01f);

	// Snow: a full cover is whole on what faces up, a steep slope keeps less; where it thins out it breaks up over
	// metres (soft, patchy), not along one line.
	float MinFull = 1.0f;
	int32 Partial = 0, Thin = 0;
	for (float X = 0.0f; X < 6000.0f; X += 25.0f)
	{
		MinFull = FMath::Min(MinFull, DSF_SnowCover(1.0f, 1.0f, DSF_Hollow(X, 0.0f, 260.0f), DSF_SnowEdge(X, 0.0f)));
		const float Half = DSF_SnowCover(0.4f, 1.0f, DSF_Hollow(X, 0.0f, 260.0f), DSF_SnowEdge(X, 0.0f));
		Partial += (Half > 0.1f && Half < 0.9f) ? 1 : 0;
		++Thin;
	}
	TestTrue(TEXT("a full cover is whole on flat ground"), MinFull > 0.99f);
	TestTrue(TEXT("a thinning cover is soft and patchy"), Partial > Thin / 5);
	TestTrue(TEXT("a steep slope keeps less snow"), DSF_SnowCover(0.6f, 0.5f, 0.5f, 0.5f) < DSF_SnowCover(0.6f, 1.0f, 0.5f, 0.5f));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDreamSurfaceHollowPatternTest, "DreamSurface.HollowPattern", DreamSurface::Tests::Flags)
bool FDreamSurfaceHollowPatternTest::RunTest(const FString& Parameters)
{
	using namespace DreamWind::Hlsl;

	// The hollow pattern (puddles, the snow's edge, the wander of a snow border) is continuous: no step where it crosses a
	// line of its lattice, of either octave, near the origin or kilometres out. The lattice corners are hashed in integer
	// arithmetic (DSF_LatticeHash), so every cell sharing a corner gets the same value from it on the GPU too.
	const float Scale = 600.0f;
	float WorstStep = 0.0f;
	for (const float Far : { 0.0f, 250000.0f, -1200000.0f })
	{
		for (int32 Line = -6; Line <= 6; ++Line)
		{
			for (const float Octave : { 1.0f, 2.03f })
			{
				// Where the octave's lattice has a line: X / Scale * Octave (+ 17 in the second) is a whole number.
				const float Offset = Octave > 1.5f ? 17.0f : 0.0f;
				const float N = FMath::Floor(Far / Scale * Octave + Offset) + float(Line);
				const float X = (N - Offset) / Octave * Scale;
				for (const float Y : { 37.0f, 911.0f, -2203.0f })
				{
					WorstStep = FMath::Max(WorstStep, FMath::Abs(DSF_Hollow(X + 0.5f, Y, Scale) - DSF_Hollow(X - 0.5f, Y, Scale)));
					WorstStep = FMath::Max(WorstStep, FMath::Abs(DSF_Hollow(Y, X + 0.5f, Scale) - DSF_Hollow(Y, X - 0.5f, Scale)));
				}
			}
		}
	}
	AddInfo(FString::Printf(TEXT("largest change across a centimetre at a lattice line: %.4f"), WorstStep));
	TestTrue(TEXT("no step at the lattice lines"), WorstStep < 0.02f);

	// It spans its range evenly: about half on either side of the middle, reaching both ends.
	float Sum = 0.0f, Lo = 1.0f, Hi = 0.0f;
	int32 Count = 0;
	for (float Y = 0.0f; Y < 20000.0f; Y += 97.0f)
	{
		for (float X = 0.0f; X < 20000.0f; X += 89.0f)
		{
			const float H = DSF_Hollow(X, Y, Scale);
			Sum += H;
			Lo = FMath::Min(Lo, H);
			Hi = FMath::Max(Hi, H);
			++Count;
		}
	}
	const float Mean = Sum / float(Count);
	AddInfo(FString::Printf(TEXT("hollow pattern: mean %.3f, range %.3f..%.3f"), Mean, Lo, Hi));
	TestTrue(TEXT("centred on the middle"), FMath::Abs(Mean - 0.5f) < 0.06f);
	TestTrue(TEXT("reaching towards both ends"), Lo < 0.25f && Hi > 0.75f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDreamSurfaceWeatherAtTest, "DreamSurface.WeatherAtTime", DreamSurface::Tests::Flags)
bool FDreamSurfaceWeatherAtTest::RunTest(const FString& Parameters)
{
	using namespace DreamSurface::Tests;
	UDreamWeatherSelector_Fixed* Fixed = NewObject<UDreamWeatherSelector_Fixed>();
	Fixed->Preset = Rain();
	FSurfaceWorld W(Fixed);
	if (!TestNotNull(TEXT("weather"), W.Weather))
	{
		return false;
	}
	W.GoTo(At(14.0));
	const FVector P(500, 0, 0);
	const FDreamWeatherSample Now = W.Weather->SampleWeather(P);
	const FDreamWeatherSample Same = W.Weather->SampleWeatherAt(P, W.Weather->GetGlobalValues(), W.Clock->GetWorldSeconds());
	TestEqual(TEXT("now through SampleWeatherAt is SampleWeather: rain"), Same.Rain, Now.Rain);
	TestEqual(TEXT("temperature"), Same.Temperature, Now.Temperature);
	TestEqual(TEXT("exposure"), Same.Exposure, Now.Exposure);
	// The same weather twelve hours earlier: the small hours are colder than the afternoon.
	const FDreamWeatherSample Night = W.Weather->SampleWeatherAt(P, W.Weather->GetGlobalValues(), At(2.0));
	TestTrue(TEXT("a past moment has that moment's temperature"), Night.Temperature < Now.Temperature - 1.0f);
	TestEqual(TEXT("and the weather given"), Night.Precipitation, Now.Precipitation);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDreamSurfaceTimelineTest, "DreamSurface.Timeline", DreamSurface::Tests::Flags)
bool FDreamSurfaceTimelineTest::RunTest(const FString& Parameters)
{
	using namespace DreamSurface::Tests;
	// Six hours clear, six hours rain, repeating.
	FSurfaceWorld Lived(Sequence({ { Clear(), 6.0f }, { Rain(), 6.0f } }));
	FSurfaceWorld Jumped(Sequence({ { Clear(), 6.0f }, { Rain(), 6.0f } }));
	if (!TestNotNull(TEXT("surface"), Lived.Surface) || !TestNotNull(TEXT("surface"), Jumped.Surface))
	{
		return false;
	}
	const FVector P(0, 0, 0);
	// One world lives through a day step by step, the other jumps straight to its end.
	Lived.GoTo(At(0.0));
	Lived.RunTo(At(30.0));
	Jumped.GoTo(At(30.0));
	TestTrue(TEXT("the surfaces run"), Lived.Surface->IsActive());
	TestTrue(TEXT("the session records the weather it sees"), Lived.Surface->GetNumRecordedSteps() > 10);

	const double Start = FPlatformTime::Seconds();
	const FDreamSurfaceState A = Lived.Surface->SampleSurface(P);
	AddInfo(FString::Printf(TEXT("one SampleSurface: %.3f ms (%d steps)"), (FPlatformTime::Seconds() - Start) * 1000.0, UDreamSurfaceSubsystem::MemorySteps()));
	const FDreamSurfaceState B = Jumped.Surface->SampleSurface(P);
	TestTrue(TEXT("asking twice gives the same"), A.Equals(Lived.Surface->SampleSurface(P), 0.0f));
	TestTrue(TEXT("a world that jumped there agrees with one that lived there"), A.Equals(B, 1.0e-5f));

	// The schedule's rain runs from hour 6 to 12, 18 to 24, 30 to 36.
	const FDreamSurfaceState Raining = Lived.Surface->SampleSurfaceAt(P, At(35.0));
	const FDreamSurfaceState AfterRain = Lived.Surface->SampleSurfaceAt(P, At(38.0));
	const FDreamSurfaceState Before = Lived.Surface->SampleSurfaceAt(P, At(29.0));
	TestTrue(TEXT("soaked deep in the rain"), Raining.Wetness > 0.95f && Raining.Water > 0.5f);
	TestTrue(TEXT("drying after it"), AfterRain.Wetness < Raining.Wetness);
	TestTrue(TEXT("still drying before the next"), Before.Wetness < 0.5f);
	TestTrue(TEXT("the view's open-sky state follows"), Lived.Surface->GetViewState().Equals(Lived.Surface->SampleSurface(FVector(0, 0, 100)), 1.0e-4f));

	// A jump back: what was seen after it is forgotten, and the past still gives the same.
	Lived.GoTo(At(20.0));
	TestTrue(TEXT("a jump back keeps the past"), Lived.Surface->SampleSurfaceAt(P, At(19.0)).Equals(Jumped.Surface->SampleSurfaceAt(P, At(19.0)), 1.0e-5f));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDreamSurfaceSnowTest, "DreamSurface.SnowAndDust", DreamSurface::Tests::Flags)
bool FDreamSurfaceSnowTest::RunTest(const FString& Parameters)
{
	using namespace DreamSurface::Tests;
	FSurfaceWorld W(Sequence({ { Blizzard(), 12.0f }, { Thaw(), 12.0f }, { DustStorm(), 12.0f }, { Clear(), 12.0f } }));
	if (!TestNotNull(TEXT("surface"), W.Surface))
	{
		return false;
	}
	const FVector P(0, 0, 0);
	W.GoTo(At(96.0));
	// The cycle runs on the world's own clock: find the hour the blizzard begins.
	double Begin = -1.0;
	for (double H = 48.0; H < 96.0 && Begin < 0.0; H += 0.25)
	{
		const float Before = W.Weather->EvaluateSchedule(At(H - 0.25)).Get(FDreamWeatherChannels::SnowRatio);
		const float After = W.Weather->EvaluateSchedule(At(H)).Get(FDreamWeatherChannels::SnowRatio);
		Begin = Before < 0.5f && After >= 0.5f ? H : -1.0;
	}
	if (!TestTrue(TEXT("the schedule has a blizzard"), Begin > 0.0))
	{
		return false;
	}
	const FDreamSurfaceState Blizzard = W.Surface->SampleSurfaceAt(P, At(Begin + 11.0));
	TestTrue(TEXT("a blizzard lays deep snow"), Blizzard.Snow > 0.9f);
	W.GoTo(At(Begin + 11.0));
	TestNearlyEqual(TEXT("its depth"), W.Surface->GetSnowDepthAt(P), Blizzard.Snow * UDreamSurfaceSettings::Get()->MaxSnowDepth, 1.0e-3f);
	// Then the thaw: it melts, and the melt wets the ground under it.
	const FDreamSurfaceState Thawing = W.Surface->SampleSurfaceAt(P, At(Begin + 15.0));
	TestTrue(TEXT("the thaw melts the snow"), Thawing.Snow < Blizzard.Snow - 0.3f);
	TestTrue(TEXT("and wets the ground"), Thawing.Wetness > 0.3f);
	const FDreamSurfaceState Dusty = W.Surface->SampleSurfaceAt(P, At(Begin + 35.0));
	TestTrue(TEXT("a dust storm leaves dust"), Dusty.Dust > 0.8f);
	AddInfo(FString::Printf(TEXT("blizzard from hour %.2f: snow %.2f; thaw +3 h: snow %.2f wet %.2f; dust storm: dust %.2f"),
		Begin, Blizzard.Snow, Thawing.Snow, Thawing.Wetness, Dusty.Dust));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDreamSurfaceShelterTest, "DreamSurface.Shelter", DreamSurface::Tests::Flags)
bool FDreamSurfaceShelterTest::RunTest(const FString& Parameters)
{
	using namespace DreamSurface::Tests;
	UDreamWeatherSelector_Fixed* Fixed = NewObject<UDreamWeatherSelector_Fixed>();
	Fixed->Preset = Rain();
	FSurfaceWorld W(Fixed);
	if (!TestNotNull(TEXT("surface"), W.Surface))
	{
		return false;
	}
	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	ADreamWeatherShelter* Hut = W.World->SpawnActor<ADreamWeatherShelter>(FVector(0, 0, 100), FRotator::ZeroRotator, Params);
	Hut->Shelter->Extent = FVector(300, 300, 200);
	W.GoTo(At(12.0));
	const FDreamSurfaceState Inside = W.Surface->SampleSurface(FVector(0, 0, 0));
	const FDreamSurfaceState Outside = W.Surface->SampleSurface(FVector(3000, 0, 0));
	TestTrue(TEXT("soaked out in the rain"), Outside.Wetness > 0.95f);
	TestTrue(TEXT("dry under the roof"), Inside.Wetness < 0.01f && Inside.Water < 0.01f);
	TestTrue(TEXT("the view's state is the open sky's, roof or not"), W.Surface->GetViewState().Wetness > 0.95f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDreamSurfaceForcedTest, "DreamSurface.ForcedWeather", DreamSurface::Tests::Flags)
bool FDreamSurfaceForcedTest::RunTest(const FString& Parameters)
{
	using namespace DreamSurface::Tests;
	UDreamWeatherSelector_Fixed* Fixed = NewObject<UDreamWeatherSelector_Fixed>();
	Fixed->Preset = Clear();
	FSurfaceWorld W(Fixed);
	if (!TestNotNull(TEXT("surface"), W.Surface))
	{
		return false;
	}
	const FVector P(0, 0, 0);
	W.GoTo(At(10.0));
	TestTrue(TEXT("dry under a clear schedule"), W.Surface->SampleSurface(P).Wetness < 0.01f);
	// Forced rain is what this session saw: the steps it ran through remember it, though the schedule never rained.
	W.Weather->ForceWeather(Rain(), 0.0f);
	W.Clock->Tick(1.0f);
	W.RunTo(At(11.0));
	const FDreamSurfaceState Forced = W.Surface->SampleSurface(P);
	TestTrue(TEXT("forced rain wets the ground"), Forced.Wetness > 0.9f);
	TestTrue(TEXT("asked again, the same"), Forced.Equals(W.Surface->SampleSurface(P), 0.0f));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDreamSurfaceMaskTest, "DreamSurface.Mask", DreamSurface::Tests::Flags)
bool FDreamSurfaceMaskTest::RunTest(const FString& Parameters)
{
	using namespace DreamSurface::Tests;
	UDreamWeatherSelector_Fixed* Fixed = NewObject<UDreamWeatherSelector_Fixed>();
	Fixed->Preset = Rain();
	FSurfaceWorld W(Fixed);
	if (!TestNotNull(TEXT("surface"), W.Surface))
	{
		return false;
	}
	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	// A circle 10 m across kept dry, its edge fading over 2 m; a box beside it that doubles the standing water.
	ADreamSurfaceMask* Dry = W.World->SpawnActor<ADreamSurfaceMask>(FVector(0, 0, 0), FRotator::ZeroRotator, Params);
	Dry->Mask->Shape = EDreamSurfaceMaskShape::Circle;
	Dry->Mask->Extent = FVector2D(500.0, 500.0);
	Dry->Mask->FadeDistance = 200.0f;
	Dry->Mask->Wetness = 0.0f;
	Dry->Mask->Water = 0.0f;
	ADreamSurfaceMask* Gutter = W.World->SpawnActor<ADreamSurfaceMask>(FVector(3000, 0, 0), FRotator(0, 30, 0), Params);
	Gutter->Mask->Extent = FVector2D(200.0, 50.0);
	Gutter->Mask->FadeDistance = 0.0f;
	Gutter->Mask->Wetness = 1.0f;
	Gutter->Mask->Water = 2.0f;
	Gutter->Mask->Snow = 1.0f;
	Gutter->Mask->Dust = 1.0f;
	W.GoTo(At(10.75));

	const FDreamSurfaceState Inside = W.Surface->SampleSurface(FVector(100, 100, 0));
	const FDreamSurfaceState Edge = W.Surface->SampleSurface(FVector(600, 0, 0));
	const FDreamSurfaceState Open = W.Surface->SampleSurface(FVector(-2000, 0, 0));
	TestTrue(TEXT("it rains outside the brush"), Open.Wetness > 0.95f);
	TestEqual(TEXT("dry inside"), Inside.Wetness, 0.0f);
	TestTrue(TEXT("half way across the soft edge"), Edge.Wetness > 0.3f && Edge.Wetness < 0.7f);
	TestNearlyEqual(TEXT("the brush's own multipliers there"), W.Surface->GetMaskAt(FVector(600, 0, 0)).X, 0.5f, 1.0e-3f);
	// The gutter doubles the water, held to 1; a point just outside its turned box is untouched.
	const FDreamSurfaceState InGutter = W.Surface->SampleSurface(FVector(3000, 0, 0));
	const FDreamSurfaceState BesideGutter = W.Surface->SampleSurface(FVector(3000, 120, 0));
	TestTrue(TEXT("the gutter holds more water"), InGutter.Water >= BesideGutter.Water);
	TestTrue(TEXT("never above full"), InGutter.Water <= 1.0f);
	TestNearlyEqual(TEXT("the turned box leaves a point beside it"), W.Surface->GetMaskAt(FVector(3000, 120, 0)).Y, 1.0f, 1.0e-4f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDreamSurfaceRoofShelterTest, "DreamSurface.RoofCapture.Shelter", DreamSurface::Tests::Flags)
bool FDreamSurfaceRoofShelterTest::RunTest(const FString& Parameters)
{
	using namespace DreamSurface::Tests;
	UDreamWeatherSelector_Fixed* Fixed = NewObject<UDreamWeatherSelector_Fixed>();
	Fixed->Preset = Rain();
	FSurfaceWorld W(Fixed);
	if (!TestNotNull(TEXT("surface"), W.Surface))
	{
		return false;
	}
	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	ADreamSurfaceRoofCapture* Box = W.World->SpawnActor<ADreamSurfaceRoofCapture>(FVector(0, 0, 200), FRotator::ZeroRotator, Params);
	Box->Roof->Extent = FVector(400.0, 400.0, 200.0);
	Box->Roof->Resolution = 16;
	// A roof at 300 cm over the box's +x half (the image's top half: rows 0..7), ground at 0 elsewhere.
	TArray<float> Heights;
	Heights.Init(0.0f, 16 * 16);
	for (int32 Y = 0; Y < 8; ++Y)
	{
		for (int32 X = 0; X < 16; ++X)
		{
			Heights[Y * 16 + X] = 300.0f;
		}
	}
	Box->Roof->SetHeightsForTest(Heights);
	W.GoTo(At(10.75));

	TestNearlyEqual(TEXT("the roof's height over the +x half"), Box->Roof->GetHeightAt(200.0, 0.0), 300.0f, 1.0e-3f);
	TestNearlyEqual(TEXT("the ground over the -x half"), Box->Roof->GetHeightAt(-200.0, 0.0), 0.0f, 1.0e-3f);
	TestTrue(TEXT("nothing outside the box"), Box->Roof->GetHeightAt(900.0, 0.0) < -1.0e8f);
	TestEqual(TEXT("under the roof: sheltered"), W.Weather->GetExposureAt(FVector(200, 0, 0)), 0.0f);
	TestEqual(TEXT("on the ground beside it: open"), W.Weather->GetExposureAt(FVector(-200, 0, 0)), 1.0f);
	TestEqual(TEXT("on the roof itself: open"), W.Weather->GetExposureAt(FVector(200, 0, 300)), 1.0f);
	TestTrue(TEXT("dry under the roof"), W.Surface->SampleSurface(FVector(200, 0, 0)).Wetness < 0.01f);
	TestTrue(TEXT("wet beside it"), W.Surface->SampleSurface(FVector(-200, 0, 0)).Wetness > 0.95f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDreamSurfaceGpuRoofTest, "DreamSurface.Gpu.RoofCapture", DreamSurface::Tests::Flags)
bool FDreamSurfaceGpuRoofTest::RunTest(const FString& Parameters)
{
	using namespace DreamSurface::Tests;
	if (!FApp::CanEverRender() || GUsingNullRHI)
	{
		AddInfo(TEXT("No RHI: skipped (run it in an editor that renders)."));
		return true;
	}
	UDreamWeatherSelector_Fixed* Fixed = NewObject<UDreamWeatherSelector_Fixed>();
	Fixed->Preset = Rain();
	FSurfaceWorld W(Fixed);
	if (!TestNotNull(TEXT("surface"), W.Surface))
	{
		return false;
	}
	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	// A slab 3 m up over the +x half of a capture box turned 30 degrees.
	const FRotator Turn(0.0, 30.0, 0.0);
	AStaticMeshActor* Slab = W.World->SpawnActor<AStaticMeshActor>(Turn.RotateVector(FVector(200, 0, 300)), Turn, Params);
	Slab->GetStaticMeshComponent()->SetMobility(EComponentMobility::Movable);
	Slab->GetStaticMeshComponent()->SetStaticMesh(LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube")));
	Slab->SetActorScale3D(FVector(4.0, 8.0, 0.2));
	ADreamSurfaceRoofCapture* Box = W.World->SpawnActor<ADreamSurfaceRoofCapture>(FVector(0, 0, 200), Turn, Params);
	Box->Roof->Extent = FVector(400.0, 400.0, 200.0);
	Box->Roof->Resolution = 64;
	W.Clock->ReportView(FVector(0, 0, 0), FRotator::ZeroRotator, true);
	W.GoTo(At(10.75));
	Box->Roof->CaptureNow();
	TestTrue(TEXT("it captured"), Box->Roof->GetCaptureCount() > 0);

	// The slab's top is at 310 cm (3 m, 20 cm thick).
	const FVector OverSlab = Turn.RotateVector(FVector(200, 0, 0));
	const FVector Beside = Turn.RotateVector(FVector(-200, 0, 0));
	const float Top = Box->Roof->GetHeightAt(OverSlab.X, OverSlab.Y);
	AddInfo(FString::Printf(TEXT("captured height over the slab %.1f, beside it %.1f"), Top, Box->Roof->GetHeightAt(Beside.X, Beside.Y)));
	TestTrue(TEXT("the slab's top, captured"), FMath::Abs(Top - 310.0f) < 6.0f);
	TestTrue(TEXT("nothing beside it (the image is the right way round)"), Box->Roof->GetHeightAt(Beside.X, Beside.Y) < 50.0f);
	TestTrue(TEXT("sheltered under the slab"), W.Weather->GetExposureAt(OverSlab) < 0.01f);

	// The cover map has the slab too.
	W.Clock->Tick(0.0f);
	W.Clock->Tick(1.0f);
	FlushRenderingCommands();
	const FDreamSurfaceMaps* Maps = W.Surface->GetMaps();
	UTextureRenderTarget2D* Cover = Maps ? Maps->GetCoverMap() : nullptr;
	if (!TestNotNull(TEXT("cover map (run the Setup)"), Cover))
	{
		return false;
	}
	TArray<FLinearColor> Pixels;
	Cover->GameThread_GetRenderTargetResource()->ReadLinearColorPixels(Pixels);
	const int32 Size = Cover->SizeX;
	const FVector2D UV = (FVector2D(OverSlab) - Maps->GetNearCenter()) / Maps->GetNearExtent() + 0.5;
	const FLinearColor Texel = Pixels[FMath::Clamp(int32(UV.Y * Size), 0, Size - 1) * Size + FMath::Clamp(int32(UV.X * Size), 0, Size - 1)];
	AddInfo(FString::Printf(TEXT("cover map over the slab: height %.1f, strength %.2f"), Texel.R, Texel.G));
	TestTrue(TEXT("the cover map holds the slab's height"), FMath::Abs(Texel.R - 310.0f) < 6.0f && Texel.G > 0.99f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDreamSurfaceInteractionTest, "DreamSurface.Interaction", DreamSurface::Tests::Flags)
bool FDreamSurfaceInteractionTest::RunTest(const FString& Parameters)
{
	using namespace DreamSurface::Tests;
	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	auto MakeFoot = [&Params](UWorld* World, const FVector& At)
	{
		AActor* Walker = World->SpawnActor<AActor>(AActor::StaticClass(), FTransform(At), Params);
		UDreamSurfaceInteractionComponent* Foot = NewObject<UDreamSurfaceInteractionComponent>(Walker);
		Walker->SetRootComponent(Foot);
		Foot->bTraceGround = false;
		Foot->RegisterComponent();
		Foot->SetWorldLocation(At);
		return Foot;
	};
	{
		// Deep snow: a step finds snow and how deep, and leaves a print to draw.
		UDreamWeatherSelector_Fixed* Fixed = NewObject<UDreamWeatherSelector_Fixed>();
		Fixed->Preset = Blizzard();
		FSurfaceWorld W(Fixed);
		if (!TestNotNull(TEXT("surface"), W.Surface))
		{
			return false;
		}
		W.GoTo(At(12.0));
		UDreamSurfaceInteractionComponent* Foot = MakeFoot(W.World, FVector(100, 100, 0));
		const FDreamSurfaceContact Contact = Foot->PrintNow();
		TestEqual(TEXT("a step in deep snow finds snow"), Contact.Kind, EDreamSurfaceContactKind::Snow);
		TestTrue(TEXT("and how deep"), Contact.SnowDepth > 10.0f);
		TestEqual(TEXT("one print waits to be drawn"), W.Surface->GetPendingStamps().Num(), 2);
		// It prints again once it has moved a step, not before.
		Foot->SetWorldLocation(FVector(105, 100, 0));
		Foot->TickComponent(0.1f, LEVELTICK_All, nullptr);
		TestEqual(TEXT("no print within a step"), Foot->GetPrintCount(), 1);
		Foot->SetWorldLocation(FVector(130, 100, 0));
		Foot->TickComponent(0.1f, LEVELTICK_All, nullptr);
		TestEqual(TEXT("a print a step on"), Foot->GetPrintCount(), 2);
	}
	{
		// After hours of rain: at the bottom of a hollow the foot stands in a puddle, on a crest only on wet ground.
		UDreamWeatherSelector_Fixed* Fixed = NewObject<UDreamWeatherSelector_Fixed>();
		Fixed->Preset = Rain();
		FSurfaceWorld W(Fixed);
		if (!TestNotNull(TEXT("surface"), W.Surface))
		{
			return false;
		}
		W.GoTo(At(12.0));
		FVector Dip(0.0), Crest(0.0);
		float Deepest = -1.0f, Highest = 2.0f;
		for (int32 i = 0; i < 400; ++i)
		{
			const FVector P(i * 37.0, i * 11.0, 0.0);
			// The materials' puddle pattern (DSM_Realistic): small hollows of Puddle Scale.
			const float H = DreamWind::Hlsl::DSF_Hollow(float(P.X) + 57.0f, float(P.Y) - 23.0f, UDreamSurfaceSettings::Get()->PuddleScale);
			if (H > Deepest) { Deepest = H; Dip = P; }
			if (H < Highest) { Highest = H; Crest = P; }
		}
		TestEqual(TEXT("in a hollow: a puddle"), MakeFoot(W.World, Dip)->PrintNow().Kind, EDreamSurfaceContactKind::Puddle);
		TestEqual(TEXT("on a crest: wet ground"), MakeFoot(W.World, Crest)->PrintNow().Kind, EDreamSurfaceContactKind::Wet);
		const FDreamSurfaceContact Contact = W.Surface->SampleContact(Dip, FVector::UpVector);
		TestTrue(TEXT("the puddle's share"), Contact.Puddle > 0.5f);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDreamSurfaceGpuTrailsTest, "DreamSurface.Gpu.Trails", DreamSurface::Tests::Flags)
bool FDreamSurfaceGpuTrailsTest::RunTest(const FString& Parameters)
{
	using namespace DreamSurface::Tests;
	if (!FApp::CanEverRender() || GUsingNullRHI)
	{
		AddInfo(TEXT("No RHI: skipped (run it in an editor that renders)."));
		return true;
	}
	UDreamWeatherSelector_Fixed* Fixed = NewObject<UDreamWeatherSelector_Fixed>();
	Fixed->Preset = Blizzard();
	FSurfaceWorld W(Fixed);
	if (!TestNotNull(TEXT("surface"), W.Surface))
	{
		return false;
	}
	// Every frame draws the maps here (the frames run faster than real time).
	UDreamSurfaceSettings* Settings = GetMutableDefault<UDreamSurfaceSettings>();
	const float Interval = Settings->MapInterval;
	Settings->MapInterval = 0.0f;
	ON_SCOPE_EXIT { Settings->MapInterval = Interval; };

	W.Clock->ReportView(FVector(0, 0, 0), FRotator::ZeroRotator, true);
	W.GoTo(At(12.0));
	const FVector Print(300, -200, 0);
	W.Surface->Stamp(Print, 30.0f, 1.0f, true, true);
	W.Clock->Tick(0.0f);
	FlushRenderingCommands();

	const FDreamSurfaceMaps* Maps = W.Surface->GetMaps();
	UTextureRenderTarget2D* Trails = Maps ? Maps->GetTrailMap() : nullptr;
	if (!TestNotNull(TEXT("trail map (run the Setup)"), Trails))
	{
		return false;
	}
	auto TrailAt = [Trails, Maps](const FVector& P)
	{
		TArray<FLinearColor> Pixels;
		Trails->GameThread_GetRenderTargetResource()->ReadLinearColorPixels(Pixels);
		const int32 Size = Trails->SizeX;
		const FVector2D UV = (FVector2D(P) - Maps->GetTrailCenter()) / UDreamSurfaceSettings::Get()->TrailExtent + 0.5;
		return Pixels[FMath::Clamp(int32(UV.Y * Size), 0, Size - 1) * Size + FMath::Clamp(int32(UV.X * Size), 0, Size - 1)];
	};
	const FLinearColor Fresh = TrailAt(Print);
	const FLinearColor Beside = TrailAt(Print + FVector(200, 0, 0));
	AddInfo(FString::Printf(TEXT("fresh print %.2f (stirred %.2f), beside it %.2f"), Fresh.R, Fresh.G, Beside.R));
	TestTrue(TEXT("the print is pressed in"), Fresh.R > 0.95f);
	TestTrue(TEXT("nothing beside it"), Beside.R < 0.01f);
	// An hour of heavy snow fills it.
	W.GoTo(At(13.0));
	FlushRenderingCommands();
	const FLinearColor Filled = TrailAt(Print);
	AddInfo(FString::Printf(TEXT("after an hour of snow %.2f"), Filled.R));
	TestTrue(TEXT("new snow fills the print"), Filled.R < Fresh.R * 0.6f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDreamSurfaceGpuWavesTest, "DreamSurface.Gpu.PuddleWaves", DreamSurface::Tests::Flags)
bool FDreamSurfaceGpuWavesTest::RunTest(const FString& Parameters)
{
	using namespace DreamSurface::Tests;
	if (!FApp::CanEverRender() || GUsingNullRHI)
	{
		AddInfo(TEXT("No RHI: skipped (run it in an editor that renders)."));
		return true;
	}
	UDreamWeatherSelector_Fixed* Fixed = NewObject<UDreamWeatherSelector_Fixed>();
	Fixed->Preset = Rain();
	FSurfaceWorld W(Fixed);
	if (!TestNotNull(TEXT("surface"), W.Surface))
	{
		return false;
	}
	W.Clock->ReportView(FVector(0, 0, 0), FRotator::ZeroRotator, true);
	W.GoTo(At(12.0));
	FlushRenderingCommands();
	const FDreamSurfaceMaps* Maps = W.Surface->GetMaps();
	UTextureRenderTarget2D* Trails = Maps ? Maps->GetTrailMap() : nullptr;
	if (!TestNotNull(TEXT("trail map (run the Setup)"), Trails))
	{
		return false;
	}
	// The water's surface (the trail map's blue) at a point.
	auto HeightAt = [Trails, Maps](const FVector2D& P)
	{
		TArray<FLinearColor> Pixels;
		Trails->GameThread_GetRenderTargetResource()->ReadLinearColorPixels(Pixels);
		const int32 Size = Trails->SizeX;
		const FVector2D UV = (P - Maps->GetTrailCenter()) / UDreamSurfaceSettings::Get()->TrailExtent + 0.5;
		return Pixels[FMath::Clamp(int32(UV.Y * Size), 0, Size - 1) * Size + FMath::Clamp(int32(UV.X * Size), 0, Size - 1)].B;
	};
	// Frames in real time (the waves run on it), drawing the trail map each one.
	auto RunFor = [&W](double Seconds)
	{
		const double End = FPlatformTime::Seconds() + Seconds;
		while (FPlatformTime::Seconds() < End)
		{
			FPlatformProcess::Sleep(0.02f);
			W.Clock->Tick(0.0f);
			FlushRenderingCommands();
		}
	};
	// A step into a puddle: the water under the foot is pushed away.
	const FVector2D Foot(300.0, -200.0);
	W.Surface->Stamp(FVector(Foot, 0.0), 12.0f, 0.0f, false, true);
	W.Clock->Tick(0.0f);
	FlushRenderingCommands();
	const float Pushed = HeightAt(Foot);
	// A moment later a ring runs out from it.
	float Crest = 0.0f;
	float Trough = 0.0f;
	const double Start = FPlatformTime::Seconds();
	while (FPlatformTime::Seconds() - Start < 1.0)
	{
		RunFor(0.05);
		for (double R = 15.0; R <= 60.0; R += 5.0)
		{
			const float H = HeightAt(Foot + FVector2D(R, 0.0));
			Crest = FMath::Max(Crest, H);
			Trough = FMath::Min(Trough, H);
		}
	}
	// And a few seconds on the water has flowed back level.
	RunFor(5.0);
	const float After = HeightAt(Foot);
	AddInfo(FString::Printf(TEXT("under the foot %.2f; around it, a crest %.3f and a trough %.3f within a second; %.3f five seconds on"), Pushed, Crest, Trough, After));
	TestTrue(TEXT("a step pushes the water away"), Pushed < -0.9f);
	TestTrue(TEXT("a wave runs out from it"), Crest > 0.01f || Trough < -0.01f);
	TestTrue(TEXT("and the water flows back level"), FMath::Abs(After) < 0.05f);
	TestTrue(TEXT("nothing far away"), FMath::Abs(HeightAt(Foot + FVector2D(1500.0, 0.0))) < 1.0e-3f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDreamSurfaceGpuTest, "DreamSurface.Gpu.MatchesCpu", DreamSurface::Tests::Flags)
bool FDreamSurfaceGpuTest::RunTest(const FString& Parameters)
{
	using namespace DreamSurface::Tests;
	if (!FApp::CanEverRender() || GUsingNullRHI)
	{
		AddInfo(TEXT("No RHI: skipped (run it in an editor that renders)."));
		return true;
	}
	FSurfaceWorld W(Sequence({ { Clear(), 6.0f }, { Rain(), 6.0f } }));
	if (!TestNotNull(TEXT("surface"), W.Surface))
	{
		return false;
	}
	// A zone that keeps the rain off a circle 40 m east: the map applies it per texel, the query at its point.
	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	ADreamWeatherZone* Dry = W.World->SpawnActor<ADreamWeatherZone>(FVector(4000, 0, 0), FRotator::ZeroRotator, Params);
	Dry->Zone->Shape = EDreamWeatherZoneShape::Circle;
	Dry->Zone->Extent = FVector2D(1500, 1500);
	Dry->Zone->BlendDistance = 600.0f;
	Dry->Zone->Channels.Add(Channel_Precipitation, 0.0f);
	// And a cold zone 40 m west where what falls is snow: its snow ratio and temperature reach the map too.
	ADreamWeatherZone* Cold = W.World->SpawnActor<ADreamWeatherZone>(FVector(-4000, 0, 0), FRotator::ZeroRotator, Params);
	Cold->Zone->Shape = EDreamWeatherZoneShape::Circle;
	Cold->Zone->Extent = FVector2D(1500, 1500);
	Cold->Zone->BlendDistance = 600.0f;
	Cold->Zone->Channels.Add(Channel_SnowRatio, 1.0f);
	Cold->Zone->Channels.Add(Channel_Temperature, -25.0f);

	W.Clock->ReportView(FVector(0, 0, 0), FRotator::ZeroRotator, true);
	// An hour and a bit after the rain: drying, the water still standing (values between the ends).
	W.GoTo(At(37.3));
	W.Clock->Tick(0.0f);
	FlushRenderingCommands();

	const FDreamSurfaceMaps* Maps = W.Surface->GetMaps();
	UTextureRenderTarget2D* Near = Maps ? Maps->GetNearMap() : nullptr;
	UTextureRenderTarget2D* Far = Maps ? Maps->GetFarMap() : nullptr;
	if (!TestNotNull(TEXT("near map (run the Setup)"), Near) || !TestNotNull(TEXT("far map"), Far))
	{
		return false;
	}
	TestTrue(TEXT("the maps were drawn and integrated"), Maps->GetIntegrateCount() >= 2);

	auto Check = [this, &W, Maps](bool bNear, const FVector2D& Point, const TCHAR* What)
	{
		const FMapTexel Gpu = ReadMaps(*Maps, bNear, Point);
		if (!TestTrue(FString::Printf(TEXT("%s: the maps read"), What), Gpu.bRead))
		{
			return;
		}
		const FDreamSurfaceState Cpu = W.Surface->SampleSurface(FVector(Gpu.Texel, 0.0));
		AddInfo(FString::Printf(TEXT("%s: GPU %s / CPU %s"), What, *Describe(Gpu.State), *Describe(Cpu)));
		TestTrue(FString::Printf(TEXT("%s: the map holds the query's state"), What), Cpu.Equals(Gpu.State, 0.01f));
	};
	Check(true, FVector2D(-1200, 300), TEXT("near, in the rain"));
	Check(true, FVector2D(4000, 0), TEXT("near, in the dry zone"));
	Check(true, FVector2D(5800, 0), TEXT("near, at the zone's edge"));
	Check(true, FVector2D(-4000, 200), TEXT("near, in the cold zone"));
	Check(false, FVector2D(0, 0), TEXT("far, at the view"));
	TestTrue(TEXT("the cold zone holds snow"), W.Surface->SampleSurface(FVector(-4000, 200, 0)).Snow > 0.3f);
	return true;
}


IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDreamSurfaceGpuMovingZoneTest, "DreamSurface.Gpu.MovingZone", DreamSurface::Tests::Flags)
bool FDreamSurfaceGpuMovingZoneTest::RunTest(const FString& Parameters)
{
	using namespace DreamSurface::Tests;
	if (!FApp::CanEverRender() || GUsingNullRHI)
	{
		AddInfo(TEXT("No RHI: skipped (run it in an editor that renders)."));
		return true;
	}
	FSurfaceWorld W(Sequence({ { Rain(), 24.0f } }));
	if (!TestNotNull(TEXT("surface"), W.Surface))
	{
		return false;
	}
	// A cold storm 3 km across drifting east at 6 km/h, what falls in it snow: it set out 30 km west at 30:00 and stands
	// over the view at 35:00, having just arrived; it passed over a point 9 km west an hour and a half before. The maps
	// lay its snow along its way, as a query does - not all its past where it has got to.
	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	ADreamWeatherZone* Storm = W.World->SpawnActor<ADreamWeatherZone>(FVector(-3000000.0, 0, 0), FRotator::ZeroRotator, Params);
	Storm->Zone->Shape = EDreamWeatherZoneShape::Circle;
	Storm->Zone->Extent = FVector2D(150000.0, 150000.0);
	Storm->Zone->BlendDistance = 50000.0f;
	Storm->Zone->Channels.Add(Channel_SnowRatio, 1.0f);
	Storm->Zone->Channels.Add(Channel_Temperature, -25.0f);
	Storm->Zone->Motion.bEnabled = true;
	Storm->Zone->Motion.StartSeconds = At(30.0);
	Storm->Zone->Motion.Velocity = FVector2D(6.0, 0.0);
	Storm->Zone->Motion.LifetimeHours = 0.0f;
	Storm->Zone->Motion.FadeInHours = 0.0f;

	W.Clock->ReportView(FVector(0, 0, 0), FRotator::ZeroRotator, true);
	auto CheckMaps = [this, &W](const TCHAR* When)
	{
		FlushRenderingCommands();
		const FDreamSurfaceMaps* Maps = W.Surface->GetMaps();
		UTextureRenderTarget2D* Near = Maps ? Maps->GetNearMap() : nullptr;
		UTextureRenderTarget2D* Far = Maps ? Maps->GetFarMap() : nullptr;
		if (!TestNotNull(TEXT("near map (run the Setup)"), Near) || !TestNotNull(TEXT("far map"), Far))
		{
			return;
		}
		// The map's texel holding a point, against the query at that texel's centre.
		auto Read = [this, &W, When, Maps](bool bNear, const FVector2D& Point, const TCHAR* What)
		{
			const FMapTexel Gpu = ReadMaps(*Maps, bNear, Point);
			const FDreamSurfaceState Cpu = W.Surface->SampleSurface(FVector(Gpu.Texel, 0.0));
			AddInfo(FString::Printf(TEXT("%s, %s: GPU %s / CPU %s"), When, What, *Describe(Gpu.State), *Describe(Cpu)));
			TestTrue(FString::Printf(TEXT("%s, %s: the map holds the query's state"), When, What), Gpu.bRead && Cpu.Equals(Gpu.State, 0.01f));
			return Gpu.State.Snow;
		};
		const float NearHere = Read(true, FVector2D(0, 0), TEXT("near map at the view"));
		const float FarHere = Read(false, FVector2D(0, 0), TEXT("far map at the view"));
		Read(false, FVector2D(-900000.0, 0), TEXT("far map 9 km west"));
		// Where the maps overlap they agree (the far map's texel is coarse: the same storm, not the same texel).
		TestTrue(FString::Printf(TEXT("%s: near and far agree at the view (%.3f, %.3f)"), When, NearHere, FarHere), FMath::Abs(NearHere - FarHere) < 0.1f);
	};

	W.RunTo(At(35.0));
	CheckMaps(TEXT("as the storm arrives"));
	const float Arriving = W.Surface->SampleSurface(FVector(0, 0, 0)).Snow;
	const float PassedOver = W.Surface->SampleSurface(FVector(-900000.0, 0, 0)).Snow;
	TestTrue(FString::Printf(TEXT("the storm has only begun to whiten the view (%.3f)"), Arriving), Arriving < 0.6f);
	AddInfo(FString::Printf(TEXT("9 km west, passed over 1.5 h ago, rained on since: snow %.3f"), PassedOver));

	// A forced change of weather and a jump of the clock: the maps follow at once.
	W.Weather->ForceWeather(Clear(), 0.0f);
	W.GoTo(At(36.2));
	CheckMaps(TEXT("after a jump and a forced clear sky"));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDreamSurfaceGpuPolygonTest, "DreamSurface.Gpu.PolygonZone", DreamSurface::Tests::Flags)
bool FDreamSurfaceGpuPolygonTest::RunTest(const FString& Parameters)
{
	using namespace DreamSurface::Tests;
	if (!FApp::CanEverRender() || GUsingNullRHI)
	{
		AddInfo(TEXT("No RHI: skipped (run it in an editor that renders)."));
		return true;
	}
	FSurfaceWorld W(Sequence({ { Clear(), 6.0f }, { Rain(), 6.0f } }));
	if (!TestNotNull(TEXT("surface"), W.Surface))
	{
		return false;
	}
	// A cold L around the view where what falls is snow; the quarter north-east of its centre is cut out and gets rain.
	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	ADreamWeatherZone* Cold = W.World->SpawnActor<ADreamWeatherZone>(FVector(0, 0, 0), FRotator(0.0, 20.0, 0.0), Params);
	Cold->Zone->SetPolygonPoints({ FVector2D(-3000, -3000), FVector2D(3000, -3000), FVector2D(3000, 0), FVector2D(0, 0), FVector2D(0, 3000), FVector2D(-3000, 3000) });
	Cold->Zone->BlendDistance = 400.0f;
	Cold->Zone->Channels.Add(Channel_SnowRatio, 1.0f);
	Cold->Zone->Channels.Add(Channel_Temperature, -25.0f);

	W.Clock->ReportView(FVector(0, 0, 0), FRotator::ZeroRotator, true);
	W.GoTo(At(37.3));
	W.Clock->Tick(0.0f);
	FlushRenderingCommands();

	const FDreamSurfaceMaps* Maps = W.Surface->GetMaps();
	UTextureRenderTarget2D* Near = Maps ? Maps->GetNearMap() : nullptr;
	if (!TestNotNull(TEXT("near map (run the Setup)"), Near))
	{
		return false;
	}
	// Points in the zone's frame (turned by its 20 degrees into the world).
	const FTransform Frame(FRotator(0.0, 20.0, 0.0));
	auto Check = [&](const FVector2D& Local, const TCHAR* What) -> FDreamSurfaceState
	{
		const FVector World = Frame.TransformPosition(FVector(Local, 0.0));
		const FMapTexel Gpu = ReadMaps(*Maps, true, FVector2D(World));
		const FDreamSurfaceState Cpu = W.Surface->SampleSurface(FVector(Gpu.Texel, 0.0));
		AddInfo(FString::Printf(TEXT("%s: GPU %s / CPU %s"), What, *Describe(Gpu.State), *Describe(Cpu)));
		TestTrue(FString::Printf(TEXT("%s: the map holds the query's state"), What), Gpu.bRead && Cpu.Equals(Gpu.State, 0.01f));
		return Cpu;
	};
	const FDreamSurfaceState Arm = Check(FVector2D(1500, -1500), TEXT("in an arm"));
	Check(FVector2D(-1500, 1500), TEXT("in the other arm"));
	const FDreamSurfaceState Cut = Check(FVector2D(1800, 1800), TEXT("in the cut-out quarter"));
	Check(FVector2D(1500, 150), TEXT("by the cut's edge"));
	Check(FVector2D(150, 1500), TEXT("by its other edge"));
	Check(FVector2D(3200, -1000), TEXT("just outside"));
	TestTrue(TEXT("snow in the L"), Arm.Snow > 0.3f);
	TestTrue(TEXT("none in the cut"), Cut.Snow < 0.02f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDreamSurfaceGpuIceTest, "DreamSurface.Gpu.Ice", DreamSurface::Tests::Flags)
bool FDreamSurfaceGpuIceTest::RunTest(const FString& Parameters)
{
	using namespace DreamSurface::Tests;
	if (!FApp::CanEverRender() || GUsingNullRHI)
	{
		AddInfo(TEXT("No RHI: skipped (run it in an editor that renders)."));
		return true;
	}
	UDreamWeatherSelector_Fixed* Fixed = NewObject<UDreamWeatherSelector_Fixed>();
	Fixed->Preset = Clear();
	FSurfaceWorld W(Fixed);
	if (!TestNotNull(TEXT("surface"), W.Surface))
	{
		return false;
	}
	const double Day = 24.0 * 183.0;
	W.GoTo(At(Day + 3.0));
	const float Offset = 1.5f - W.Weather->SampleWeatherAt(FVector::ZeroVector, W.Weather->GetGlobalValues(), At(Day + 3.0)).Temperature;
	UDreamWeatherPreset* ColdNight = MakePreset(TEXT("SurfColdNight"), { { Channel_CloudCoverage, 0.05f }, { Channel_Wind, 0.15f }, { Channel_Temperature, Offset } });
	UDreamWeatherPreset* EveningRain = MakePreset(TEXT("SurfEveningRain"), { { Channel_CloudCoverage, 1.0f }, { Channel_Precipitation, 0.9f }, { Channel_Temperature, Offset + 4.0f } });

	// A freezing fog 40 m east, windy: rime there; its fog and wind reach the maps per texel as the query has them.
	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	ADreamWeatherZone* Fog = W.World->SpawnActor<ADreamWeatherZone>(FVector(4000, 0, 0), FRotator::ZeroRotator, Params);
	Fog->Zone->Shape = EDreamWeatherZoneShape::Circle;
	Fog->Zone->Extent = FVector2D(1500, 1500);
	Fog->Zone->BlendDistance = 600.0f;
	Fog->Zone->Channels.Add(Channel_Fog, 1.0f);
	Fog->Zone->Channels.Add(Channel_Wind, 0.8f);
	Fog->Zone->Channels.Add(Channel_CloudCoverage, 0.9f);
	// Cloud keeps a night milder (less of the daily swing): well below the clear night's air, so freezing.
	Fog->Zone->Channels.Add(Channel_Temperature, Offset - 10.0f);
	// And a freezing rain 40 m west: its supercooled share reaches the maps per texel as the query has it.
	ADreamWeatherZone* Glaze = W.World->SpawnActor<ADreamWeatherZone>(FVector(-4000, 0, 0), FRotator::ZeroRotator, Params);
	Glaze->Zone->Shape = EDreamWeatherZoneShape::Circle;
	Glaze->Zone->Extent = FVector2D(1500, 1500);
	Glaze->Zone->BlendDistance = 600.0f;
	Glaze->Zone->Channels.Add(Channel_CloudCoverage, 1.0f);
	Glaze->Zone->Channels.Add(Channel_Precipitation, 0.6f);
	Glaze->Zone->Channels.Add(Channel_FreezingRain, 1.0f);
	Glaze->Zone->Channels.Add(Channel_Temperature, Offset - 10.0f);

	W.Clock->ReportView(FVector(0, 0, 0), FRotator::ZeroRotator, true);
	W.GoTo(At(Day - 6.0));
	W.Weather->ForceWeather(EveningRain, 0.0f);
	W.RunTo(At(Day - 3.0));
	W.Weather->ForceWeather(ColdNight, 0.0f);

	auto CheckMaps = [this, &W](const TCHAR* When)
	{
		FlushRenderingCommands();
		const FDreamSurfaceMaps* Maps = W.Surface->GetMaps();
		if (!TestNotNull(TEXT("maps"), Maps) || !TestNotNull(TEXT("near ice map (run the Setup)"), Maps->GetNearIceMap()) || !TestNotNull(TEXT("far ice map"), Maps->GetFarIceMap()))
		{
			return;
		}
		auto Read = [this, &W, When, Maps](bool bNear, const FVector2D& Point, const TCHAR* What)
		{
			const FMapTexel Gpu = ReadMaps(*Maps, bNear, Point);
			const FDreamSurfaceState Cpu = W.Surface->SampleSurface(FVector(Gpu.Texel, 0.0));
			AddInfo(FString::Printf(TEXT("%s, %s: GPU %s / CPU %s"), When, What, *Describe(Gpu.State), *Describe(Cpu)));
			TestTrue(FString::Printf(TEXT("%s, %s: the map holds the query's state"), When, What), Gpu.bRead && Cpu.Equals(Gpu.State, 0.01f));
			return Gpu.State;
		};
		const FDreamSurfaceState NearHere = Read(true, FVector2D(0, 0), TEXT("near at the view"));
		const FDreamSurfaceState FarHere = Read(false, FVector2D(0, 0), TEXT("far at the view"));
		Read(true, FVector2D(4000, 0), TEXT("near in the freezing fog"));
		Read(true, FVector2D(5800, 0), TEXT("near at the fog's edge"));
		Read(true, FVector2D(-4000, 0), TEXT("near in the freezing rain"));
		Read(true, FVector2D(-5800, 0), TEXT("near at the freezing rain's edge"));
		TestTrue(FString::Printf(TEXT("%s: near and far agree on the cold at the view"), When), NearHere.ToIceVector().Equals(FarHere.ToIceVector(), 0.1f));
	};

	W.RunTo(At(Day + 3.0));
	CheckMaps(TEXT("at 3:00"));
	TestTrue(TEXT("frost at the view"), W.Surface->SampleSurface(FVector(0, 0, 0)).Frost > 0.5f);
	TestTrue(TEXT("rime in the freezing fog"), W.Surface->SampleSurface(FVector(4000, 0, 0)).Rime > 0.5f);
	const FDreamSurfaceState Glazed = W.Surface->SampleSurface(FVector(-4000, 0, 0));
	TestTrue(TEXT("a glaze, not snow, in the freezing rain"), Glazed.Ice > 0.5f && Glazed.Snow < 0.01f);
	// Into the morning's thaw, values between the ends; then a jump.
	W.RunTo(At(Day + 9.5));
	CheckMaps(TEXT("at 9:30"));
	W.GoTo(At(Day + 12.0));
	CheckMaps(TEXT("after a jump to noon"));
	return true;
}


IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDreamSurfaceHailTest, "DreamSurface.Hail", DreamSurface::Tests::Flags)
bool FDreamSurfaceHailTest::RunTest(const FString& Parameters)
{
	using namespace DreamSurface::Tests;
	using namespace DreamWind::Hlsl;
	const FDreamSurfaceRates Rates;
	const float4 R4(Rates.R4().X, Rates.R4().Y, Rates.R4().Z, Rates.R4().W);
	const float4 R5(Rates.R5().X, Rates.R5().Y, Rates.R5().Z, Rates.R5().W);
	auto Hail = [&](float Start, float Landing, float Air, float Hours)
	{
		// Overcast (no sun), still air.
		const float4 D0(0.0f, 0.0f, Air, 0.0f);
		const float4 D1(0.0f, 0.0f, 0.0f, 0.0f);
		return DSF_StepHail(float4(Start, 0.0f, 0.0f, 0.0f), D0, D1, float4(Landing, 0.0f, 0.0f, 0.0f), R4, R5, Hours).x;
	};
	// It heaps up within minutes of a full hailstorm, even on warm summer ground.
	TestNearlyEqual(TEXT("a full cover in a cold storm's settle time"), Hail(0.0f, 1.0f, 2.0f, Rates.HailSettleHours * 1.05f), 1.0f, 0.02f);
	float Summer = 0.0f;
	for (int32 i = 0; i < 20; ++i)
	{
		Summer = Hail(Summer, 0.6f, 22.0f, 1.0f / 60.0f);
	}
	TestTrue(*FString::Printf(TEXT("twenty minutes of a summer hailstorm whitens the ground (%.2f)"), Summer), Summer > 0.6f);
	// Then it melts: within minutes on warm ground, slowly near freezing, not at all on frozen ground.
	float Warm = 1.0f, Cool = 1.0f, Frozen = 1.0f;
	for (int32 i = 0; i < 45; ++i)
	{
		Warm = Hail(Warm, 0.0f, 22.0f, 1.0f / 60.0f);
		Cool = Hail(Cool, 0.0f, 1.0f, 1.0f / 60.0f);
		Frozen = Hail(Frozen, 0.0f, -5.0f, 1.0f / 60.0f);
	}
	TestNearlyEqual(TEXT("three quarters of an hour after: gone from warm ground"), Warm, 0.0f, 1e-4f);
	TestTrue(*FString::Printf(TEXT("still lying near freezing (%.2f)"), Cool), Cool > 0.5f && Cool < 1.0f);
	TestNearlyEqual(TEXT("all of it on frozen ground"), Frozen, 1.0f, 1e-5f);
	// The melt wets the surface.
	const float4 Dry(0.0f, 0.0f, 0.0f, 0.0f);
	const float4 Still(0.0f, 0.0f, 0.0f, 0.0f);
	auto ToHlsl = [](const FVector4f& V) { return float4(V.X, V.Y, V.Z, V.W); };
	const float4 Melting = DSF_Step(Dry, Dry, float4(1.0f, 0.0f, 0.0f, 0.0f), float4(0.0f, 0.0f, 22.0f, 0.0f), Still, Dry, ToHlsl(Rates.R0()), ToHlsl(Rates.R1()),
		ToHlsl(Rates.R2()), ToHlsl(Rates.R3()), R4, R5, 0.1f);
	TestTrue(*FString::Printf(TEXT("melting hail wets the surface (%.2f)"), Melting.x), Melting.x > 0.05f);

	// What lands: the hail on its own, the sleet half as rain and half as snow, the rest as before.
	const float4 Weather(1.0f, 0.5f, 0.8f, 0.0f);
	const float4 Air(10.0f, 0.2f, 0.0f, 0.5f);
	const float4 Plain = DSF_OpenD0(Weather, Air, 0.0f, float2(0.0f, 0.0f));
	TestTrue(TEXT("no hail, no sleet: all of it rain on a mild day"), FMath::IsNearlyEqual(Plain.x, 0.8f, 1e-5f) && FMath::IsNearlyEqual(Plain.y, 0.0f, 1e-5f)
		&& FMath::IsNearlyEqual(DSF_OpenD2(Weather, Air, 0.0f, float2(0.0f, 0.0f)).x, 0.0f, 1e-6f));
	const float4 Sleet = DSF_OpenD0(Weather, Air, 0.0f, float2(0.0f, 1.0f));
	TestTrue(TEXT("sleet: half rain, half snow"), FMath::IsNearlyEqual(Sleet.x, 0.4f, 1e-5f) && FMath::IsNearlyEqual(Sleet.y, 0.4f, 1e-5f));
	const float4 Storm = DSF_OpenD0(Weather, Air, 0.0f, float2(0.5f, 0.0f));
	TestNearlyEqual(TEXT("half hail: the rest rain"), Storm.x, 0.4f, 1e-5f);
	TestNearlyEqual(TEXT("half hail: the hail landing"), DSF_OpenD2(Weather, Air, 0.0f, float2(0.5f, 0.0f)).x, 0.4f, 1e-5f);

	// In a world: a summer hailstorm whitens the ground, the contact says hail; an hour after it, gone and the ground wet.
	UDreamWeatherSelector_Fixed* Fixed = NewObject<UDreamWeatherSelector_Fixed>();
	Fixed->Preset = Clear();
	FSurfaceWorld W(Fixed);
	if (!TestNotNull(TEXT("surface"), W.Surface))
	{
		return false;
	}
	const double Day = 24.0 * 183.0;
	W.GoTo(At(Day + 14.0));
	const float Offset = 22.0f - W.Weather->SampleWeatherAt(FVector::ZeroVector, W.Weather->GetGlobalValues(), At(Day + 15.0)).Temperature;
	UDreamWeatherPreset* HailStorm = MakePreset(TEXT("SurfHailStorm"), { { Channel_CloudCoverage, 1.0f }, { Channel_Precipitation, 0.9f }, { Channel_Hail, 0.7f },
		{ Channel_Temperature, Offset } });
	UDreamWeatherPreset* After = MakePreset(TEXT("SurfAfterHail"), { { Channel_CloudCoverage, 0.4f }, { Channel_Temperature, Offset } });
	W.Weather->ForceWeather(HailStorm, 0.0f);
	W.RunTo(At(Day + 15.0) - 300.0);
	W.Weather->ForceWeather(After, 0.0f);
	W.RunTo(At(Day + 15.0));
	const FDreamSurfaceState Lying = W.Surface->SampleSurface(FVector::ZeroVector);
	AddInfo(FString::Printf(TEXT("five minutes after the storm: %s"), *Describe(Lying)));
	TestTrue(TEXT("hail lying after the storm"), Lying.Hail > 0.3f);
	const FDreamSurfaceContact Contact = W.Surface->SampleContact(FVector::ZeroVector, FVector::UpVector);
	TestTrue(TEXT("the contact: hail, less grip"), (Contact.Kind == EDreamSurfaceContactKind::Hail || Lying.Hail < 0.4f) && Contact.Grip < 1.0f);
	W.RunTo(At(Day + 16.0));
	const FDreamSurfaceState Gone = W.Surface->SampleSurface(FVector::ZeroVector);
	AddInfo(FString::Printf(TEXT("an hour later: %s"), *Describe(Gone)));
	TestTrue(TEXT("melted within the hour"), Gone.Hail < 0.05f);
	TestTrue(TEXT("and the ground wet from it and the rain"), Gone.Wetness > 0.2f);
	return true;
}

// The wind where a surface lies (the wind's terrain map's factor there): a lee gathers the falling snow and dust, an
// exposed ridge keeps less and is blown clean of a cold cover; the open is as it was; a lee dries slower, rimes less.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDreamSurfaceWindDriftTest, "DreamSurface.WindDrift", DreamSurface::Tests::Flags)
bool FDreamSurfaceWindDriftTest::RunTest(const FString& Parameters)
{
	using namespace DreamWind::Hlsl;
	const FDreamSurfaceRates Rates;
	auto H = [](const FVector4f& V) { return float4(V.X, V.Y, V.Z, V.W); };
	const float4 R0 = H(Rates.R0()), R1 = H(Rates.R1()), R2 = H(Rates.R2()), R3 = H(Rates.R3()), R4 = H(Rates.R4()), R5 = H(Rates.R5());
	const float4 None(0.0f, 0.0f, 0.0f, 0.0f);
	// One drive in the open, then as the surface lies at a wind factor.
	struct FAt
	{
		float4 D0;
		float4 D2;
	};
	auto At = [](const float4& OpenD0, float Factor)
	{
		FAt A;
		A.D2 = DSF_LocalD2(float4(0.0f, 0.0f, 0.0f, 0.0f), OpenD0, Factor);
		A.D0 = DSF_LocalD0(OpenD0, Factor);
		return A;
	};
	auto Run = [&](float4 S, const float4& OpenD0, const float4& D1, float Factor, float Hours, int32 Steps)
	{
		const FAt A = At(OpenD0, Factor);
		for (int32 i = 0; i < Steps; ++i)
		{
			S = DSF_Step(S, None, None, A.D0, D1, A.D2, R0, R1, R2, R3, R4, R5, Hours / Steps);
		}
		return S;
	};

	// In the open the wind changes nothing: the step is the one it was.
	const float4 Windy(0.0f, 0.6f, -4.0f, 0.8f);
	const FAt Open = At(Windy, 1.0f);
	TestTrue(TEXT("the open: no lee, no exposure, the weather's wind"), Open.D2.y == 0.0f && Open.D2.z == 0.0f && Open.D0.w == Windy.w);
	const float4 Plain = DSF_Step(None, None, None, Windy, None, None, R0, R1, R2, R3, R4, R5, 2.0f);
	TestNearlyEqual(TEXT("the open: the snow after two hours as before"), DSF_Step(None, None, None, Open.D0, None, Open.D2, R0, R1, R2, R3, R4, R5, 2.0f).z, Plain.z, 1e-6f);

	// A windy snowfall: behind a wall (a fifth of the wind reaches), in the open, on a ridge (half as much again).
	const float Lee = Run(None, Windy, None, 0.2f, 2.0f, 8).z;
	const float OpenSnow = Run(None, Windy, None, 1.0f, 2.0f, 8).z;
	const float Ridge = Run(None, Windy, None, 1.5f, 2.0f, 8).z;
	AddInfo(FString::Printf(TEXT("two hours of a windy snowfall: in a lee %.2f, in the open %.2f, on a ridge %.2f"), Lee, OpenSnow, Ridge));
	TestTrue(TEXT("snow heaps up in the lee"), Lee > OpenSnow * 1.5f);
	TestTrue(TEXT("and less of it stays on the ridge"), Ridge < OpenSnow * 0.8f);
	// No snow falling, a strong cold wind: the ridge is blown clean, the open keeps its cover.
	const float4 Gale(0.0f, 0.0f, -6.0f, 1.0f);
	const float RidgeAfter = Run(float4(0.0f, 0.0f, 0.8f, 0.0f), Gale, None, 1.5f, 6.0f, 12).z;
	const float OpenAfter = Run(float4(0.0f, 0.0f, 0.8f, 0.0f), Gale, None, 1.0f, 6.0f, 12).z;
	AddInfo(FString::Printf(TEXT("six hours of a cold gale over a cover of 0.8: the ridge %.2f, the open %.2f"), RidgeAfter, OpenAfter));
	TestTrue(TEXT("the gale blows the ridge's snow off"), RidgeAfter < 0.6f);
	TestNearlyEqual(TEXT("and leaves the open's"), OpenAfter, 0.8f, 1e-4f);

	// A dust storm: more settles in the lee, the ridge stays cleaner.
	const float4 DustyAir(0.4f, 0.6f, 0.0f, 0.0f);
	const float4 DryWind(0.0f, 0.0f, 25.0f, 0.8f);
	const float DustLee = Run(None, DryWind, DustyAir, 0.2f, 1.0f, 4).w;
	const float DustOpen = Run(None, DryWind, DustyAir, 1.0f, 1.0f, 4).w;
	const float DustRidge = Run(None, DryWind, DustyAir, 1.5f, 1.0f, 4).w;
	AddInfo(FString::Printf(TEXT("an hour of a dust storm: in a lee %.2f, in the open %.2f, on a ridge %.2f"), DustLee, DustOpen, DustRidge));
	TestTrue(TEXT("dust gathers in the lee"), DustLee > DustOpen * 1.3f);
	TestTrue(TEXT("less on the ridge"), DustRidge < DustOpen * 0.8f);

	// A wet surface in the sun and a wind: the lee stays wet longer.
	const float4 Breeze(0.0f, 0.0f, 15.0f, 1.0f);
	const float4 Sun(0.8f, 0.0f, 0.5f, 0.0f);
	const float WetLee = Run(float4(1.0f, 0.0f, 0.0f, 0.0f), Breeze, Sun, 0.2f, 1.0f, 4).x;
	const float WetOpen = Run(float4(1.0f, 0.0f, 0.0f, 0.0f), Breeze, Sun, 1.0f, 1.0f, 4).x;
	TestTrue(*FString::Printf(TEXT("the lee dries slower (%.2f against %.2f)"), WetLee, WetOpen), WetLee > WetOpen);

	// A freezing fog in a wind: less rime in the lee.
	const float4 Fog(0.0f, 0.0f, 0.0f, 1.0f);
	const float4 Freezing(0.0f, 0.0f, -5.0f, 1.0f);
	const FAt FogLee = At(Freezing, 0.2f);
	const float RimeLee = DSF_StepIce(None, None, FogLee.D0, Fog, R0, R3, R4, 0.5f).z;
	const float RimeOpen = DSF_StepIce(None, None, Freezing, Fog, R0, R3, R4, 0.5f).z;
	TestTrue(*FString::Printf(TEXT("less rime in the lee (%.2f against %.2f)"), RimeLee, RimeOpen), RimeLee < RimeOpen * 0.7f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDreamSurfaceGpuHailTest, "DreamSurface.Gpu.Hail", DreamSurface::Tests::Flags)
bool FDreamSurfaceGpuHailTest::RunTest(const FString& Parameters)
{
	using namespace DreamSurface::Tests;
	if (!FApp::CanEverRender() || GUsingNullRHI)
	{
		AddInfo(TEXT("No RHI: skipped (run it in an editor that renders)."));
		return true;
	}
	UDreamWeatherSelector_Fixed* Fixed = NewObject<UDreamWeatherSelector_Fixed>();
	Fixed->Preset = Clear();
	FSurfaceWorld W(Fixed);
	if (!TestNotNull(TEXT("surface"), W.Surface))
	{
		return false;
	}
	const double Day = 24.0 * 183.0;
	W.GoTo(At(Day + 14.0));
	const float Offset = 18.0f - W.Weather->SampleWeatherAt(FVector::ZeroVector, W.Weather->GetGlobalValues(), At(Day + 15.0)).Temperature;
	UDreamWeatherPreset* Showers = MakePreset(TEXT("SurfShowers"), { { Channel_CloudCoverage, 1.0f }, { Channel_Precipitation, 0.7f }, { Channel_Hail, 0.3f },
		{ Channel_Temperature, Offset } });
	UDreamWeatherPreset* After = MakePreset(TEXT("SurfAfterShowers"), { { Channel_CloudCoverage, 0.5f }, { Channel_Temperature, Offset } });

	// A hailstorm 40 m east and a sleet 40 m west, by zones: their hail and sleet reach the maps per texel as the query
	// has them.
	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	ADreamWeatherZone* Storm = W.World->SpawnActor<ADreamWeatherZone>(FVector(4000, 0, 0), FRotator::ZeroRotator, Params);
	Storm->Zone->Shape = EDreamWeatherZoneShape::Circle;
	Storm->Zone->Extent = FVector2D(1500, 1500);
	Storm->Zone->BlendDistance = 600.0f;
	Storm->Zone->Channels.Add(Channel_CloudCoverage, 1.0f);
	Storm->Zone->Channels.Add(Channel_Precipitation, 1.0f);
	Storm->Zone->Channels.Add(Channel_Hail, 0.9f);
	ADreamWeatherZone* Slush = W.World->SpawnActor<ADreamWeatherZone>(FVector(-4000, 0, 0), FRotator::ZeroRotator, Params);
	Slush->Zone->Shape = EDreamWeatherZoneShape::Circle;
	Slush->Zone->Extent = FVector2D(1500, 1500);
	Slush->Zone->BlendDistance = 600.0f;
	Slush->Zone->Channels.Add(Channel_CloudCoverage, 1.0f);
	Slush->Zone->Channels.Add(Channel_Precipitation, 0.8f);
	Slush->Zone->Channels.Add(Channel_Sleet, 1.0f);
	Slush->Zone->Channels.Add(Channel_Hail, 0.0f);
	Slush->Zone->Channels.Add(Channel_Temperature, Offset - 17.0f);

	W.Clock->ReportView(FVector(0, 0, 0), FRotator::ZeroRotator, true);
	W.Weather->ForceWeather(Showers, 0.0f);
	W.RunTo(At(Day + 15.0) - 900.0);

	auto CheckMaps = [this, &W](const TCHAR* When)
	{
		const FDreamSurfaceMaps* Maps = W.Surface->GetMaps();
		if (!TestNotNull(TEXT("maps"), Maps) || !TestNotNull(TEXT("near hail map (run the Setup)"), Maps->GetNearHailMap()) || !TestNotNull(TEXT("far hail map"), Maps->GetFarHailMap()))
		{
			return;
		}
		// The maps drawn at this very moment (between steps they redraw at most every Map Interval of real time, and the
		// clock has just been run on much faster than that: the hail melts within minutes).
		const_cast<FDreamSurfaceMaps*>(Maps)->RequestIntegrate();
		W.GoTo(W.Clock->GetWorldSeconds());
		FlushRenderingCommands();
		auto Read = [this, &W, When, Maps](bool bNear, const FVector2D& Point, const TCHAR* What)
		{
			const FMapTexel Gpu = ReadMaps(*Maps, bNear, Point);
			const FDreamSurfaceState Cpu = W.Surface->SampleSurface(FVector(Gpu.Texel, 0.0));
			AddInfo(FString::Printf(TEXT("%s, %s: GPU %s / CPU %s"), When, What, *Describe(Gpu.State), *Describe(Cpu)));
			TestTrue(FString::Printf(TEXT("%s, %s: the map holds the query's state"), When, What), Gpu.bRead && Cpu.Equals(Gpu.State, 0.01f));
			return Gpu.State;
		};
		const FDreamSurfaceState NearHere = Read(true, FVector2D(0, 0), TEXT("near at the view"));
		const FDreamSurfaceState FarHere = Read(false, FVector2D(0, 0), TEXT("far at the view"));
		Read(true, FVector2D(4000, 0), TEXT("near in the hailstorm"));
		Read(true, FVector2D(5800, 0), TEXT("near at the hailstorm's edge"));
		Read(true, FVector2D(-4000, 0), TEXT("near in the sleet"));
		Read(true, FVector2D(-5800, 0), TEXT("near at the sleet's edge"));
		TestTrue(FString::Printf(TEXT("%s: near and far agree on the hail at the view"), When), FMath::IsNearlyEqual(NearHere.Hail, FarHere.Hail, 0.1f));
	};

	CheckMaps(TEXT("in the showers"));
	TestTrue(TEXT("hail heaped up in the hailstorm"), W.Surface->SampleSurface(FVector(4000, 0, 0)).Hail > 0.6f);
	const FDreamSurfaceState Sleet = W.Surface->SampleSurface(FVector(-4000, 0, 0));
	TestTrue(TEXT("no hail in the sleet, but snow and wet from it"), Sleet.Hail < 0.01f && Sleet.Snow > 0.0f && Sleet.Wetness > 0.2f);
	// The showers pass: the hail melting, values between the ends; then a jump.
	W.Weather->ForceWeather(After, 0.0f);
	W.RunTo(At(Day + 15.0) + 300.0);
	CheckMaps(TEXT("melting"));
	W.GoTo(At(Day + 17.0));
	CheckMaps(TEXT("after a jump"));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDreamSurfaceFloodTest, "DreamSurface.Flood", DreamSurface::Tests::Flags)
bool FDreamSurfaceFloodTest::RunTest(const FString& Parameters)
{
	using namespace DreamSurface::Tests;
	// The shared formula: under water the film and the hollows are full, snow, dust and hail gone; the silt builds.
	{
		using namespace DreamWind::Hlsl;
		const float4 Wet = DSF_FloodWater(float4(0.1f, 0.0f, 0.8f, 0.5f), DSF_Flooded(20.0f));
		TestTrue(TEXT("formula: flooded, full and washed"), Wet.x == 1.0f && Wet.y == 1.0f && Wet.z == 0.0f && Wet.w == 0.0f);
		TestEqual(TEXT("formula: dry land untouched"), DSF_FloodWater(float4(0.1f, 0.2f, 0.3f, 0.4f), DSF_Flooded(0.0f)).z, 0.3f);
		const float4 S3 = DSF_StepFlood(float4(0.6f, 0.0f, 0.0f, 0.0f), 1.0f, 1.0f, float4(0.0f, 0.0f, 15.0f, 0.0f), 3.0f);
		TestTrue(TEXT("formula: the hail floats off, the silt builds"), S3.x == 0.0f && S3.y > 0.99f && FMath::IsNearlyEqual(S3.z, 0.5f, 1.0e-4f));
	}

	UDreamWeatherSelector_Fixed* Fixed = NewObject<UDreamWeatherSelector_Fixed>();
	Fixed->Preset = Clear();
	FSurfaceWorld W(Fixed);
	if (!TestNotNull(TEXT("surface"), W.Surface))
	{
		return false;
	}
	W.GoTo(At(10.0));
	// A muddy pond 20 m square, its surface at 0, held where it is.
	FActorSpawnParameters Spawn;
	Spawn.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	AActor* Pond = W.World->SpawnActor<AActor>(AActor::StaticClass(), FTransform::Identity, Spawn);
	USceneComponent* Root = NewObject<USceneComponent>(Pond, TEXT("Root"));
	Pond->SetRootComponent(Root);
	Root->RegisterComponent();
	UDreamWaterBodyComponent* Body = NewObject<UDreamWaterBodyComponent>(Pond, TEXT("Water"));
	Body->Extent = FVector2D(1000.0, 1000.0);
	Body->Depth = 100.0f;
	Body->bHold = true;
	Body->bCaustics = false;
	Body->SetupAttachment(Root);
	Body->RegisterComponent();
	FDreamWaterState Muddy;
	Muddy.Turbidity = 0.8f;
	Body->SetState(Muddy);

	const FVector Under(200, 0, -30);
	const FVector Bank(200, 0, 40);
	const FVector Beside(5000, 0, -30);
	W.RunTo(At(12.0));
	const FDreamSurfaceState Flooded = W.Surface->SampleSurface(Under);
	AddInfo(FString::Printf(TEXT("under 30 cm of water for two hours: flooded %.2f, wet %.2f, water %.2f, silt %.2f"), Flooded.Flooded, Flooded.Wetness, Flooded.Water, Flooded.Silt));
	TestTrue(TEXT("under the water: flooded"), Flooded.Flooded > 0.95f);
	TestTrue(TEXT("soaked and the hollows full"), Flooded.Wetness > 0.99f && Flooded.Water > 0.99f);
	TestTrue(TEXT("the muddy water leaves silt"), Flooded.Silt > 0.2f);
	TestTrue(TEXT("the bank above it stays dry"), W.Surface->SampleSurface(Bank).Flooded < 0.01f && W.Surface->SampleSurface(Bank).Wetness < 0.05f);
	TestTrue(TEXT("beside the pond, dry"), W.Surface->SampleSurface(Beside).Flooded < 0.01f);

	// The water goes down 60 cm: the place comes out of it, wet, and dries in the sun over the afternoon.
	FDreamWaterState Down = Body->GetState();
	Down.LevelOffset = -60.0f;
	Body->SetState(Down);
	W.RunTo(At(12.75));
	const FDreamSurfaceState After = W.Surface->SampleSurface(Under);
	AddInfo(FString::Printf(TEXT("45 min after the water went down: flooded %.2f, wet %.2f, water %.2f, silt %.2f"), After.Flooded, After.Wetness, After.Water, After.Silt));
	TestTrue(TEXT("out of the water"), After.Flooded < 0.5f);
	TestTrue(TEXT("a wet mark where it stood"), After.Wetness > 0.7f && After.Water > 0.5f);
	W.RunTo(At(20.0));
	const FDreamSurfaceState Evening = W.Surface->SampleSurface(Under);
	AddInfo(FString::Printf(TEXT("by the evening: flooded %.2f, wet %.2f, water %.2f, silt %.2f"), Evening.Flooded, Evening.Wetness, Evening.Water, Evening.Silt));
	TestTrue(TEXT("dried by the evening"), Evening.Wetness < After.Wetness && Evening.Flooded < 0.01f);
	TestTrue(TEXT("the silt stays a while"), Evening.Silt > 0.1f);
	TestTrue(TEXT("asked again, the same"), Evening.Equals(W.Surface->SampleSurface(Under), 0.0f));
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
