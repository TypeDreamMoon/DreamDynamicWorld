// Copyright DreamMoon. All Rights Reserved.

#include "DreamAmbiencePreset.h"
#include "DreamWeather.h"
#include "DreamWeatherAudioModel.h"
#include "DreamWeatherAudioSettings.h"
#include "DreamWeatherAudioSubsystem.h"
#include "DreamWeatherFallPaths.h"
#include "DreamWeatherFXSubsystem.h"
#include "DreamWeatherModule.h"
#include "DreamWeatherPreset.h"
#include "DreamWeatherSelector.h"
#include "DreamWeatherSettings.h"
#include "DreamWeatherSubsystem.h"
#include "DreamWorldSubsystem.h"

#include "Algo/Count.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/World.h"
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "UObject/Package.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace DreamWeatherAudio::Tests
{
	constexpr EAutomationTestFlags Flags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter;

	FDreamViewOcclusion Open()
	{
		FDreamViewOcclusion O;
		O.bValid = true;
		return O;
	}

	FDreamViewOcclusion ClosedRoom()
	{
		FDreamViewOcclusion O = Open();
		O.Interior = 1.0f;
		O.Overhead = 1.0f;
		O.PlusX = O.MinusX = O.PlusY = O.MinusY = 1.0f;
		return O;
	}

	FDreamWeatherVoiceMix Voice(const FDreamWeatherVoiceMix (&Mixes)[(int32)EDreamWeatherVoice::Num], EDreamWeatherVoice V)
	{
		return Mixes[(int32)V];
	}

	UDreamWeatherPreset* MakePreset(const TCHAR* Name, std::initializer_list<TPair<FGameplayTag, float>> Channels)
	{
		UDreamWeatherPreset* Preset = NewObject<UDreamWeatherPreset>(GetTransientPackage(), MakeUniqueObjectName(GetTransientPackage(), UDreamWeatherPreset::StaticClass(), Name));
		for (const TPair<FGameplayTag, float>& Channel : Channels)
		{
			Preset->Channels.Add(Channel.Key, Channel.Value);
		}
		return Preset;
	}

	/** A box of solid geometry (the engine's cube, which blocks visibility traces), tagged. */
	AStaticMeshActor* AddBox(UWorld* World, const FVector& Center, const FVector& Size, FName Tag)
	{
		UStaticMesh* Mesh = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
		if (!Mesh)
		{
			return nullptr;
		}
		FActorSpawnParameters Spawn;
		Spawn.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		AStaticMeshActor* Actor = World->SpawnActor<AStaticMeshActor>(Center, FRotator::ZeroRotator, Spawn);
		Actor->GetStaticMeshComponent()->SetMobility(EComponentMobility::Movable);
		Actor->GetStaticMeshComponent()->SetStaticMesh(Mesh);
		Actor->SetActorScale3D(Size / 100.0);
		if (!Tag.IsNone())
		{
			Actor->Tags.Add(Tag);
		}
		return Actor;
	}

	/** A game world with weather fixed to Preset, the view at Location looking north. */
	struct FWorld
	{
		UWorld* World = nullptr;
		UDreamWorldSubsystem* Clock = nullptr;
		UDreamWeatherSubsystem* Weather = nullptr;
		UDreamWeatherAudioSubsystem* Audio = nullptr;
		UDreamWeatherSelector_Fixed* Fixed = nullptr;

		explicit FWorld(UDreamWeatherPreset* Preset, const FVector& Location = FVector(0, 0, 170))
		{
			World = UWorld::CreateWorld(EWorldType::Game, false);
			if (!World)
			{
				return;
			}
			Clock = World->GetSubsystem<UDreamWorldSubsystem>();
			Weather = UDreamWeatherSubsystem::Get(World);
			Audio = UDreamWeatherAudioSubsystem::Get(World);
			Fixed = NewObject<UDreamWeatherSelector_Fixed>();
			Fixed->Preset = Preset;
			FActorSpawnParameters Spawn;
			Spawn.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
			ADreamWeather* Actor = World->SpawnActor<ADreamWeather>(Spawn);
			Fixed->Rename(nullptr, Actor);
			Actor->Selector = Fixed;
			FDreamWorldClockConfig Config;
			Config.StartTime = FDateTime(2026, 6, 21, 12, 0, 0);
			Clock->ApplyClockConfig(Config, true);
			Clock->ReportView(Location, FRotator(0, 90, 0), true);
		}

		~FWorld()
		{
			if (World)
			{
				World->DestroyWorld(false);
			}
		}

		bool IsValid() const { return World && Clock && Weather && Audio; }

		void Settle(int32 Frames = 10, float Delta = 0.05f)
		{
			Clock->RefreshViewOcclusion();
			for (int32 i = 0; i < Frames; ++i)
			{
				Clock->Tick(Delta);
			}
		}
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDreamWeatherAudioOcclusionTest, "DreamWeather.Audio.Occlusion", DreamWeatherAudio::Tests::Flags)
bool FDreamWeatherAudioOcclusionTest::RunTest(const FString& Parameters)
{
	using namespace DreamWeatherAudio;
	using namespace DreamWeatherAudio::Tests;
	const FVector Right(1.0, 0.0, 0.0);

	const FDreamOutdoorOcclusion InTheOpen = Occlude(Open(), Right, 0.2f, 600.0f);
	TestEqual(TEXT("the open: full loudness"), InTheOpen.Volume, 1.0f);
	TestEqual(TEXT("the open: not muffled"), InTheOpen.Muffle, 0.0f);
	TestEqual(TEXT("the open: from all around"), InTheOpen.Balance, 0.0f);
	TestNearlyEqual(TEXT("the open: 20 kHz"), InTheOpen.CutoffHz, 20000.0f, 1.0f);

	const FDreamOutdoorOcclusion Closed = Occlude(ClosedRoom(), Right, 0.2f, 600.0f);
	TestNearlyEqual(TEXT("a closed room: the interior's loudness"), Closed.Volume, 0.2f, 1.0e-4f);
	TestNearlyEqual(TEXT("a closed room: fully muffled"), Closed.Muffle, 1.0f, 1.0e-4f);
	TestNearlyEqual(TEXT("a closed room: the lowest cutoff"), Closed.CutoffHz, 600.0f, 1.0f);

	FDreamViewOcclusion Doorway = ClosedRoom();
	Doorway.Interior = 0.7f;
	Doorway.PlusX = 0.0f;   // the door is on +X: the listener's right
	const FDreamOutdoorOcclusion Door = Occlude(Doorway, Right, 0.2f, 600.0f);
	TestTrue(FString::Printf(TEXT("a doorway: louder than a closed room (%.2f)"), Door.Volume), Door.Volume > Closed.Volume && Door.Volume < 1.0f);
	TestTrue(FString::Printf(TEXT("a doorway: less muffled (%.2f)"), Door.Muffle), Door.Muffle < Closed.Muffle && Door.Muffle > 0.0f);
	TestTrue(FString::Printf(TEXT("a doorway on the right: heard from the right (%.2f)"), Door.Balance), Door.Balance > 0.5f);
	const FDreamOutdoorOcclusion Turned = Occlude(Doorway, -Right, 0.2f, 600.0f);
	TestNearlyEqual(TEXT("turn around: the door is on the left"), Turned.Balance, -Door.Balance, 1.0e-4f);

	float L = 0.0f, R = 0.0f;
	Place(0.8f, Door.Balance, L, R);
	TestTrue(TEXT("placed right: the right keeps its loudness, the left fades"), FMath::IsNearlyEqual(R, 0.8f) && L < R);

	FDreamViewOcclusion Unknown;
	TestEqual(TEXT("before the probe's first round: the open"), Occlude(Unknown, Right, 0.2f, 600.0f).Volume, 1.0f);

	// The cutoff runs log-spaced and only down.
	float Last = 1.0e9f;
	for (int32 i = 0; i <= 10; ++i)
	{
		const float Hz = MuffleToCutoff(i / 10.0f, 600.0f);
		TestTrue(TEXT("muffling only lowers the cutoff"), Hz < Last);
		Last = Hz;
	}
	TestNearlyEqual(TEXT("halfway is the geometric mean"), MuffleToCutoff(0.5f, 600.0f), FMath::Sqrt(600.0f * 20000.0f), 5.0f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDreamWeatherAudioMixTest, "DreamWeather.Audio.Mix", DreamWeatherAudio::Tests::Flags)
bool FDreamWeatherAudioMixTest::RunTest(const FString& Parameters)
{
	using namespace DreamWeatherAudio;
	using namespace DreamWeatherAudio::Tests;
	const FDreamWeatherAudioTuning Tuning;
	FDreamWeatherVoiceMix Mixes[(int32)EDreamWeatherVoice::Num];

	FDreamWeatherAudioInputs Calm;
	Calm.Occlusion = Open();
	Mix(Calm, Tuning, Mixes);
	for (const EDreamWeatherVoice V : TEnumRange<EDreamWeatherVoice>())
	{
		TestEqual(*FString::Printf(TEXT("a calm dry day: voice %d silent"), int32(V)), Mixes[(int32)V].Loudest(), 0.0f);
	}

	FDreamWeatherAudioInputs Drizzle = Calm;
	Drizzle.Rain = 0.15f;
	Mix(Drizzle, Tuning, Mixes);
	TestTrue(TEXT("a drizzle is the light rain"), Voice(Mixes, EDreamWeatherVoice::RainLight).Loudest() > Voice(Mixes, EDreamWeatherVoice::RainHeavy).Loudest());

	FDreamWeatherAudioInputs Downpour = Calm;
	Downpour.Rain = 1.0f;
	Mix(Downpour, Tuning, Mixes);
	const FDreamWeatherVoiceMix OpenHeavy = Voice(Mixes, EDreamWeatherVoice::RainHeavy);
	TestTrue(TEXT("a downpour is the heavy rain"), OpenHeavy.Loudest() > Voice(Mixes, EDreamWeatherVoice::RainLight).Loudest());
	TestEqual(TEXT("no roof, no leaves: no rain on them"), Voice(Mixes, EDreamWeatherVoice::RainOnRoof).Loudest() + Voice(Mixes, EDreamWeatherVoice::RainOnLeaves).Loudest(), 0.0f);
	TestEqual(TEXT("in the open the rain is from all around"), OpenHeavy.Left, OpenHeavy.Right);

	// Indoors: the rain outside goes quiet and dull, the roof over the head drums.
	FDreamWeatherAudioInputs Indoors = Downpour;
	Indoors.Occlusion = ClosedRoom();
	Mix(Indoors, Tuning, Mixes);
	const FDreamWeatherVoiceMix InHeavy = Voice(Mixes, EDreamWeatherVoice::RainHeavy);
	const FDreamWeatherVoiceMix InRoof = Voice(Mixes, EDreamWeatherVoice::RainOnRoof);
	TestTrue(TEXT("indoors: the rain outside is quieter"), InHeavy.Loudest() < 0.5f * OpenHeavy.Loudest());
	TestTrue(TEXT("indoors: and muffled"), InHeavy.Muffle > 0.9f);
	TestTrue(FString::Printf(TEXT("indoors: the roof is loud (%.2f)"), InRoof.Loudest()), InRoof.Loudest() > 0.9f);
	TestTrue(TEXT("indoors: the roof is only a little dull"), InRoof.Muffle < 0.3f);

	// Under the trees.
	FDreamWeatherAudioInputs Grove = Downpour;
	Grove.Surfaces[1] = 0.4f;
	Grove.Surfaces[3] = 0.6f;
	Mix(Grove, Tuning, Mixes);
	TestTrue(TEXT("rain on leaves under trees"), Voice(Mixes, EDreamWeatherVoice::RainOnLeaves).Loudest() > 0.9f);

	// A gale.
	FDreamWeatherAudioInputs Gale = Calm;
	Gale.WindSpeed = 2500.0f;
	Gale.WindGust = 1.6f;
	Mix(Gale, Tuning, Mixes);
	TestNearlyEqual(TEXT("a gale is the full wind"), Voice(Mixes, EDreamWeatherVoice::Wind).Loudest(), 1.0f, 1.0e-4f);
	TestTrue(TEXT("and it whistles"), Voice(Mixes, EDreamWeatherVoice::WindGust).Loudest() > 0.5f);
	FDreamWeatherAudioInputs Breeze = Calm;
	Breeze.WindSpeed = 300.0f;
	Mix(Breeze, Tuning, Mixes);
	TestTrue(TEXT("a breeze: soft wind, no whistle"), Voice(Mixes, EDreamWeatherVoice::Wind).Loudest() < 0.4f && Voice(Mixes, EDreamWeatherVoice::WindGust).Loudest() == 0.0f);

	// A slot's volume scales its voice.
	FDreamWeatherAudioTuning Quiet = Tuning;
	Quiet.SlotVolume[(int32)EDreamWeatherVoice::Wind] = 0.5f;
	Mix(Gale, Quiet, Mixes);
	TestNearlyEqual(TEXT("a slot at half volume"), Voice(Mixes, EDreamWeatherVoice::Wind).Loudest(), 0.5f, 1.0e-4f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDreamWeatherAudioThunderModelTest, "DreamWeather.Audio.ThunderModel", DreamWeatherAudio::Tests::Flags)
bool FDreamWeatherAudioThunderModelTest::RunTest(const FString& Parameters)
{
	using namespace DreamWeatherAudio;
	using namespace DreamWeatherAudio::Tests;
	const FDreamWeatherAudioTuning Tuning;
	const FVector Listener(0.0, 0.0, 170.0);
	const FVector Right(1.0, 0.0, 0.0);
	const FDreamOutdoorOcclusion Outside = Occlude(Open(), Right, 0.2f, 600.0f);

	FDreamLightningStrike Near;
	Near.Location = FVector(100000.0, 0.0, 0.0);   // 1 km to the right
	Near.Intensity = 1.0f;
	Near.ThunderDelay = 2.9f;
	FDreamThunder NearThunder;
	TestTrue(TEXT("a strike 1 km away is heard"), Thunder(Near, Listener, Right, Outside, Tuning, NearThunder));
	TestTrue(TEXT("as a clap"), NearThunder.bNear);
	TestEqual(TEXT("after the strike's own thunder delay"), NearThunder.Delay, 2.9f);
	TestTrue(TEXT("from the right"), NearThunder.Mix.Right > NearThunder.Mix.Left);

	FDreamLightningStrike Far = Near;
	Far.Location = FVector(0.0, 800000.0, 0.0);   // 8 km ahead
	Far.ThunderDelay = 23.3f;
	FDreamThunder FarThunder;
	TestTrue(TEXT("a strike 8 km away is heard"), Thunder(Far, Listener, Right, Outside, Tuning, FarThunder));
	TestFalse(TEXT("as a rumble"), FarThunder.bNear);
	TestTrue(TEXT("quieter"), FarThunder.Mix.Loudest() < NearThunder.Mix.Loudest());
	TestTrue(TEXT("and duller"), FarThunder.Mix.Muffle > NearThunder.Mix.Muffle);
	TestEqual(TEXT("ahead: both sides"), FarThunder.Mix.Left, FarThunder.Mix.Right);

	FDreamLightningStrike Beyond = Near;
	Beyond.Location = FVector(2000000.0, 0.0, 0.0);
	FDreamThunder Unheard;
	TestFalse(TEXT("20 km away is beyond hearing"), Thunder(Beyond, Listener, Right, Outside, Tuning, Unheard));

	const FDreamOutdoorOcclusion Inside = Occlude(ClosedRoom(), Right, 0.2f, 600.0f);
	FDreamThunder Indoors;
	Thunder(Near, Listener, Right, Inside, Tuning, Indoors);
	TestTrue(TEXT("indoors: duller"), Indoors.Mix.Muffle > NearThunder.Mix.Muffle);
	TestTrue(TEXT("indoors: thunder carries through walls better than rain"), Indoors.Mix.Loudest() > Inside.Volume * NearThunder.Mix.Loudest());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDreamWeatherAudioSeasonsTest, "DreamWeather.Audio.Seasons", DreamWeatherAudio::Tests::Flags)
bool FDreamWeatherAudioSeasonsTest::RunTest(const FString& Parameters)
{
	float W[4];
	float Year = 0.0f;
	FDreamAmbienceParams::SeasonWeights(FDateTime(2026, 4, 15), false, W, Year);
	TestNearlyEqual(TEXT("mid April: spring"), W[0], 1.0f, 1.0e-3f);
	FDreamAmbienceParams::SeasonWeights(FDateTime(2026, 4, 15), true, W, Year);
	TestNearlyEqual(TEXT("mid April in the south: autumn"), W[2], 1.0f, 1.0e-3f);
	FDreamAmbienceParams::SeasonWeights(FDateTime(2026, 6, 1), false, W, Year);
	TestNearlyEqual(TEXT("1 June: half spring"), W[0], 0.5f, 0.03f);
	TestNearlyEqual(TEXT("1 June: half summer"), W[1], 0.5f, 0.03f);
	FDreamAmbienceParams::SeasonWeights(FDateTime(2027, 1, 15), false, W, Year);
	TestNearlyEqual(TEXT("mid January: winter"), W[3], 1.0f, 1.0e-3f);
	TestTrue(FString::Printf(TEXT("mid January is late in the year from March (%.3f)"), Year), Year > 0.85f && Year < 0.92f);
	for (int32 Day = 0; Day < 365; Day += 7)
	{
		FDreamAmbienceParams::SeasonWeights(FDateTime(2026, 1, 1) + FTimespan::FromDays(Day), false, W, Year);
		TestNearlyEqual(TEXT("the seasons always sum to one"), W[0] + W[1] + W[2] + W[3], 1.0f, 1.0e-4f);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDreamWeatherAudioSubsystemTest, "DreamWeather.Audio.Subsystem", DreamWeatherAudio::Tests::Flags)
bool FDreamWeatherAudioSubsystemTest::RunTest(const FString& Parameters)
{
	using namespace DreamWeatherAudio::Tests;
	using namespace DreamWeather::Tags;
	FWorld W(MakePreset(TEXT("Storm"), { { Channel_CloudCoverage, 1.0f }, { Channel_CloudDarkness, 1.0f }, { Channel_Precipitation, 1.0f }, { Channel_Wind, 1.0f } }));
	if (!TestTrue(TEXT("world"), W.IsValid()))
	{
		return false;
	}
	W.Settle();
	TestTrue(TEXT("it pours: the heavy rain plays"), W.Audio->GetVoiceMix(EDreamWeatherVoice::RainHeavy).Loudest() > 0.5f);
	TestTrue(FString::Printf(TEXT("the wind at the listener is read from the wind field (%.0f cm/s)"), W.Audio->GetLastInputs().WindSpeed),
		W.Audio->GetLastInputs().WindSpeed > 500.0f);
	TestTrue(TEXT("and heard"), W.Audio->GetVoiceMix(EDreamWeatherVoice::Wind).Loudest() > 0.3f);
	TestEqual(TEXT("nothing around: the rain lands nowhere near"), W.Audio->GetVoiceMix(EDreamWeatherVoice::RainOnRoof).Loudest(), 0.0f);

	// A strike 2 km away: heard after its delay, once.
	W.Weather->StrikeLightning(FVector(200000.0, 0.0, 0.0), 1.0f);
	const TArray<FDreamThunder>& Pending = W.Audio->GetPendingThunder();
	if (TestEqual(TEXT("the thunder is on its way"), Pending.Num(), 1))
	{
		TestTrue(TEXT("a near clap"), Pending[0].bNear);
		TestNearlyEqual(TEXT("with the strike's delay"), Pending[0].Delay, W.Weather->GetLastStrike().ThunderDelay, 1.0e-3f);
	}
	const float Delay = W.Weather->GetLastStrike().ThunderDelay;
	for (float T = 0.0f; T < Delay - 0.5f; T += 0.1f)
	{
		W.Clock->Tick(0.1f);
	}
	TestEqual(TEXT("not heard before the delay"), W.Audio->GetThunderHeardCount(), 0);
	for (int32 i = 0; i < 10; ++i)
	{
		W.Clock->Tick(0.1f);
	}
	TestEqual(TEXT("heard once after it"), W.Audio->GetThunderHeardCount(), 1);
	TestEqual(TEXT("nothing left on its way"), W.Audio->GetPendingThunder().Num(), 0);

	// The ambience's numbers follow the clock and the weather.
	const FDreamAmbienceParams& P = W.Audio->GetAmbienceParams();
	TestNearlyEqual(TEXT("noon"), P.TimeOfDay, 0.5f, 0.01f);
	TestTrue(TEXT("a June noon is a summer day"), P.Phase.Day > 0.9f && P.Seasons[1] > 0.4f);
	TestTrue(TEXT("in the rain"), P.Rain > 0.5f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDreamWeatherAudioSurfacesTest, "DreamWeather.Audio.Surfaces", DreamWeatherAudio::Tests::Flags)
bool FDreamWeatherAudioSurfacesTest::RunTest(const FString& Parameters)
{
	using namespace DreamWeatherAudio::Tests;
	using namespace DreamWeather::Tags;
	using ESurface = FDreamWeatherFallPaths::ESurface;
	UDreamWeatherPreset* Rain = MakePreset(TEXT("Rain"), { { Channel_CloudCoverage, 1.0f }, { Channel_Precipitation, 1.0f } });

	// Under a roof: a closed room around the listener, its roof tagged.
	{
		FWorld W(Rain);
		if (!TestTrue(TEXT("world"), W.IsValid()) || !AddBox(W.World, FVector(0, 0, -50), FVector(20000, 20000, 100), NAME_None))
		{
			return false;
		}
		AddBox(W.World, FVector(0, 0, 420), FVector(1200, 1200, 40), FDreamWeatherFallPaths::RoofTag);
		AddBox(W.World, FVector(600, 0, 200), FVector(40, 1200, 400), NAME_None);
		AddBox(W.World, FVector(-600, 0, 200), FVector(40, 1200, 400), NAME_None);
		AddBox(W.World, FVector(0, 600, 200), FVector(1200, 40, 400), NAME_None);
		AddBox(W.World, FVector(0, -600, 200), FVector(1200, 40, 400), NAME_None);
		W.Settle(20);
		const UDreamWeatherFXSubsystem* FX = UDreamWeatherFXSubsystem::Get(W.World);
		const TArray<uint8>& Surfaces = FX->GetPaths(UDreamWeatherFXSubsystem::RainLayer).GetSurfaces();
		const int32 Roofs = Algo::CountIf(Surfaces, [](uint8 S) { return S == uint8(ESurface::Roof); });
		const int32 Ground = Algo::CountIf(Surfaces, [](uint8 S) { return S == uint8(ESurface::Ground); });
		TestTrue(FString::Printf(TEXT("paths land on the roof (%d) and on the ground outside (%d)"), Roofs, Ground), Roofs > 0 && Ground > 0);
		TestTrue(FString::Printf(TEXT("the view probe calls it an interior (%.2f)"), W.Clock->GetViewOcclusion().Interior), W.Clock->GetViewOcclusion().Interior > 0.5f);
		TestTrue(TEXT("the roof drums"), W.Audio->GetVoiceMix(EDreamWeatherVoice::RainOnRoof).Loudest() > 0.5f);
		TestTrue(TEXT("the rain outside is muffled"), W.Audio->GetVoiceMix(EDreamWeatherVoice::RainHeavy).Muffle > 0.5f);
		TestTrue(TEXT("and so is the project's outdoor sound"), W.Audio->GetOutdoorOcclusion().Volume < 0.6f);
	}

	// Under trees: crowns tagged as leaves all around.
	{
		FWorld W(Rain);
		if (!TestTrue(TEXT("world"), W.IsValid()) || !AddBox(W.World, FVector(0, 0, -50), FVector(20000, 20000, 100), NAME_None))
		{
			return false;
		}
		AddBox(W.World, FVector(0, 0, 900), FVector(6000, 6000, 300), FDreamWeatherFallPaths::LeavesTag);
		W.Settle(20);
		float Shares[(int32)ESurface::Num];
		UDreamWeatherFXSubsystem::Get(W.World)->GetPaths(UDreamWeatherFXSubsystem::RainLayer).GetSurfaceShares(FVector(0, 0, 170), 1500.0, Shares);
		TestTrue(FString::Printf(TEXT("the rain lands on leaves (%.2f)"), Shares[(int32)ESurface::Leaves]), Shares[(int32)ESurface::Leaves] > 0.8f);
		TestTrue(TEXT("rain on leaves plays"), W.Audio->GetVoiceMix(EDreamWeatherVoice::RainOnLeaves).Intensity > 0.9f
			&& W.Audio->GetVoiceMix(EDreamWeatherVoice::RainOnLeaves).Loudest() > 0.0f);
		TestTrue(TEXT("a canopy is not a roof"), W.Audio->GetVoiceMix(EDreamWeatherVoice::RainOnRoof).Intensity < 0.2f);
	}
	return true;
}

#endif
