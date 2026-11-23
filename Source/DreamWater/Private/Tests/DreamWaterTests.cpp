// Copyright DreamMoon. All Rights Reserved.

#include "DreamWaterBody.h"
#include "DreamWaterSubsystem.h"
#include "DreamWeather.h"
#include "DreamWeatherModule.h"
#include "DreamWeatherPreset.h"
#include "DreamWeatherSelector.h"
#include "DreamWeatherSubsystem.h"
#include "DreamWorldSubsystem.h"

#include "Components/DecalComponent.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "UObject/Package.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace DreamWater::Tests
{
	constexpr EAutomationTestFlags Flags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDreamWaterModelTest, "DreamWater.Model", DreamWater::Tests::Flags)
bool FDreamWaterModelTest::RunTest(const FString& Parameters)
{
	using namespace DreamWater::Model;
	const FDreamWaterRules Rules;
	FDreamWaterWeather Rain;
	Rain.Rain = 1.0f;
	Rain.Cloud = 1.0f;
	Rain.Temperature = 12.0f;

	// An hour of full rain: 8 mm on the water and twenty times its surface round it, some 17 cm (less the hour's drain).
	FDreamWaterState State = Advance(FDreamWaterState(), Rules, Rain, 1.0f);
	AddInfo(FString::Printf(TEXT("an hour of full rain: %+.1f cm, turbidity %.2f"), State.LevelOffset, State.Turbidity));
	TestTrue(TEXT("an hour of rain raises it some 16 cm"), State.LevelOffset > 14.0f && State.LevelOffset < 17.0f);
	TestTrue(TEXT("and muddies it"), State.Turbidity > 0.25f);

	// Rain that will not stop fills it to its brim, no further.
	for (int32 i = 0; i < 48; ++i)
	{
		State = Advance(State, Rules, Rain, 0.25f);
	}
	TestNearlyEqual(TEXT("twelve hours of rain: at the brim"), State.LevelOffset, Rules.MaxRise, 0.01f);
	TestEqual(TEXT("the brim is the overflow band"), BandOf(State.LevelOffset, Rules, EDreamWaterBand::High), EDreamWaterBand::Overflow);

	// Dry: it drains halfway back over DrainHours, and the mud halves over ClearHours.
	FDreamWaterWeather Dry;
	Dry.Temperature = -10.0f;   // no evaporation: the drain alone
	FDreamWaterState Wet;
	Wet.LevelOffset = 100.0f;
	Wet.Turbidity = 0.8f;
	FDreamWaterState After = Wet;
	for (int32 i = 0; i < 48; ++i)
	{
		After = Advance(After, Rules, Dry, 0.25f);
	}
	TestNearlyEqual(TEXT("halfway down in DrainHours"), After.LevelOffset, 50.0f, 0.5f);
	After = Wet;
	for (int32 i = 0; i < 144; ++i)
	{
		After = Advance(After, Rules, Dry, 0.25f);
	}
	TestNearlyEqual(TEXT("the mud halves in ClearHours"), After.Turbidity, 0.4f, 0.01f);

	// A warm, clear dry spell draws it below its usual level, slowly; it fills back once that ends.
	FDreamWaterWeather Hot;
	Hot.Temperature = 30.0f;
	FDreamWaterState Spell;
	for (int32 i = 0; i < 24 * 20; ++i)
	{
		Spell = Advance(Spell, Rules, Hot, 1.0f);
	}
	AddInfo(FString::Printf(TEXT("twenty hot, dry days: %+.1f cm"), Spell.LevelOffset));
	TestTrue(TEXT("a dry spell draws it down several cm"), Spell.LevelOffset < -5.0f);

	// The bands, and their margin: a level hovering at an edge does not flicker.
	TestEqual(TEXT("normal"), BandOf(10.0f, Rules, EDreamWaterBand::Normal), EDreamWaterBand::Normal);
	TestEqual(TEXT("high only well past the edge"), BandOf(Rules.HighAbove + 1.0f, Rules, EDreamWaterBand::Normal), EDreamWaterBand::Normal);
	TestEqual(TEXT("high past it"), BandOf(Rules.HighAbove + 3.0f, Rules, EDreamWaterBand::Normal), EDreamWaterBand::High);
	TestEqual(TEXT("and stays high just under it"), BandOf(Rules.HighAbove - 1.0f, Rules, EDreamWaterBand::High), EDreamWaterBand::High);
	TestEqual(TEXT("low"), BandOf(-Rules.LowBelow - 3.0f, Rules, EDreamWaterBand::Normal), EDreamWaterBand::Low);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDreamWaterBodyTest, "DreamWater.Body", DreamWater::Tests::Flags)
bool FDreamWaterBodyTest::RunTest(const FString& Parameters)
{
	using namespace DreamWeather::Tags;
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
	if (!TestNotNull(TEXT("world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(false); };
	UDreamWorldSubsystem* Clock = World->GetSubsystem<UDreamWorldSubsystem>();
	UDreamWaterSubsystem* Water = UDreamWaterSubsystem::Get(World);
	if (!TestNotNull(TEXT("the water subsystem"), Water))
	{
		return false;
	}

	// Steady rain.
	UDreamWeatherPreset* Rain = NewObject<UDreamWeatherPreset>(GetTransientPackage(), MakeUniqueObjectName(GetTransientPackage(), UDreamWeatherPreset::StaticClass(), TEXT("SteadyRain")));
	Rain->Channels.Add(Channel_CloudCoverage, 1.0f);
	Rain->Channels.Add(Channel_Precipitation, 1.0f);
	Rain->Channels.Add(Channel_Temperature, 5.0f);
	UDreamWeatherSelector_Fixed* Fixed = NewObject<UDreamWeatherSelector_Fixed>();
	Fixed->Preset = Rain;
	FActorSpawnParameters Spawn;
	Spawn.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	ADreamWeather* Weather = World->SpawnActor<ADreamWeather>(Spawn);
	Fixed->Rename(nullptr, Weather);
	Weather->Selector = Fixed;
	FDreamWorldClockConfig Config;
	Config.StartTime = FDateTime(2026, 6, 15, 12, 0, 0);
	Config.bAnimate = false;
	Clock->ApplyClockConfig(Config, true);
	Clock->Tick(0.016f);

	// A pond 20 m square, its usual surface at -50, 2 m deep, its mesh riding on the level.
	AActor* Pond = World->SpawnActor<AActor>(AActor::StaticClass(), FTransform(FVector(0, 0, -50)), Spawn);
	USceneComponent* Root = NewObject<USceneComponent>(Pond, TEXT("Root"));
	Pond->SetRootComponent(Root);
	Root->RegisterComponent();
	Pond->SetActorLocation(FVector(0, 0, -50));
	UDreamWaterBodyComponent* Body = NewObject<UDreamWaterBodyComponent>(Pond, TEXT("Water"));
	Body->Extent = FVector2D(1000.0, 1000.0);
	Body->Depth = 200.0f;
	Body->bMoveOwner = true;
	Body->SetupAttachment(Root);
	Body->RegisterComponent();
	TestEqual(TEXT("registered"), Water->GetBodies().Num(), 1);
	TestNearlyEqual(TEXT("its usual surface"), Body->GetBaseHeight(), -50.0f, 0.01f);

	int32 Changes = 0;
	EDreamWaterBand Seen = EDreamWaterBand::Normal;
	Body->OnBandChangedNative.AddLambda([&Changes, &Seen](UDreamWaterBodyComponent*, EDreamWaterBand, EDreamWaterBand Now) { ++Changes; Seen = Now; });

	// Four hours of the rain over it: high, muddy, the pond's mesh lifted with it.
	const FDreamWaterWeather Over = Water->WeatherAt(*Body);
	AddInfo(FString::Printf(TEXT("the rain over the pond: %.2f, %.1f C"), Over.Rain, Over.Temperature));
	TestTrue(TEXT("it rains over the pond"), Over.Rain > 0.5f);
	Water->AdvanceAll(4.0f);
	AddInfo(FString::Printf(TEXT("after four hours: %+.1f cm, turbidity %.2f, %d band change(s)"), Body->GetLevelOffset(), Body->GetTurbidity(), Changes));
	TestTrue(TEXT("risen into the high band"), Body->GetBand() == EDreamWaterBand::High || Body->GetBand() == EDreamWaterBand::Overflow);
	TestTrue(TEXT("the band change was told"), Changes >= 1 && Seen == Body->GetBand());
	TestTrue(TEXT("muddy"), Body->GetTurbidity() > 0.3f);
	TestNearlyEqual(TEXT("the pond's mesh lifted with the level"), float(Pond->GetActorLocation().Z), -50.0f + Body->GetLevelOffset(), 0.6f);
	TestNearlyEqual(TEXT("its usual surface stays"), Body->GetBaseHeight(), -50.0f, 0.01f);

	// Where it is.
	TestEqual(TEXT("found over the pond"), Water->FindWaterAt(FVector(300, -200, 500)), Body);
	TestNull(TEXT("not beside it"), Water->FindWaterAt(FVector(3000, 0, 0)));
	TestNearlyEqual(TEXT("a metre under the surface"), Body->GetDepthBelowSurface(FVector(0, 0, Body->GetSurfaceHeight() - 100.0f)), 100.0f, 0.01f);
	TestEqual(TEXT("not under it above the surface"), Body->GetDepthBelowSurface(FVector(0, 0, Body->GetSurfaceHeight() + 10.0f)), 0.0f);
	TestEqual(TEXT("not under it below the bottom"), Body->GetDepthBelowSurface(FVector(0, 0, -400.0f)), 0.0f);

	// The view under the water: DDW_Water's depth.
	Clock->ReportView(FVector(0, 0, Body->GetSurfaceHeight() - 60.0f), FRotator::ZeroRotator, true);
	Clock->Tick(0.016f);
	const FDreamViewWater View = Water->GetViewWater();
	AddInfo(FString::Printf(TEXT("the view's water: surface %.0f, depth %.1f, turbidity %.2f"), View.SurfaceHeight, View.Depth, View.Turbidity));
	TestEqual(TEXT("the view is in the pond"), View.Body.Get(), Body);
	TestNearlyEqual(TEXT("60 cm under it"), View.Depth, 60.0f, 2.0f);

	// The caustics: a decal from just over the surface down past the bottom, over the water's area.
	if (const UDecalComponent* Decal = Body->GetCausticsDecal(); TestNotNull(TEXT("the caustics' decal (compile M_DreamWaterCaustics)"), Decal))
	{
		const float Middle = float(Decal->GetComponentLocation().Z);
		TestNearlyEqual(TEXT("the decal midway between the surface and the bottom"), Middle, 0.5f * (Body->GetSurfaceHeight() + Body->GetBottomHeight()), 1.0f);
		TestTrue(TEXT("down past the bottom, up past the surface"), Decal->DecalSize.X >= 0.5 * (Body->GetSurfaceHeight() - Body->GetBottomHeight()));
		TestTrue(TEXT("over the water's area"), FMath::IsNearlyEqual(Decal->DecalSize.Y, Body->Extent.Y) && FMath::IsNearlyEqual(Decal->DecalSize.Z, Body->Extent.X));
	}

	// The world's snapshot keeps it.
	const FDreamWaterState Kept = Body->GetState();
	const FDreamWorldSnapshot Snapshot = Clock->CaptureSnapshot();
	Body->SetState(FDreamWaterState());
	Clock->ApplySnapshot(Snapshot);
	TestNearlyEqual(TEXT("the snapshot restores the level"), Body->GetLevelOffset(), Kept.LevelOffset, 0.01f);
	TestNearlyEqual(TEXT("and the turbidity"), Body->GetTurbidity(), Kept.Turbidity, 0.01f);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
