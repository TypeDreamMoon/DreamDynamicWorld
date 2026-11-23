// Copyright DreamMoon. All Rights Reserved.

// Rooms and their openings (UDreamInteriorComponent, UDreamPortalComponent): the view inside is indoors but where its
// doors and windows are open, the weather does not land inside but does right outside, the wind is calm inside and
// blows through an open door. No renderer needed (the fluid's carving is checked with one).

#include "CoreMinimal.h"
#include "DreamWeatherSubsystem.h"
#include "DreamWindSubsystem.h"
#include "DreamWorldInterior.h"
#include "DreamWorldSubsystem.h"

#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace DreamWorldInteriorTests
{
	constexpr EAutomationTestFlags Flags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter;

	/** A 6 x 6 x 3 m room at the origin (its floor at 0), a doorway in its -X wall (out along -X), a window in its -Y wall. */
	struct FRoomWorld
	{
		UWorld* World = nullptr;
		UDreamWorldSubsystem* Clock = nullptr;
		UDreamInteriorComponent* Room = nullptr;
		UDreamPortalComponent* Door = nullptr;
		UDreamPortalComponent* Window = nullptr;

		FRoomWorld()
		{
			World = UWorld::CreateWorld(EWorldType::Game, false);
			Clock = World ? World->GetSubsystem<UDreamWorldSubsystem>() : nullptr;
			if (!World)
			{
				return;
			}
			AActor* Owner = World->SpawnActor<AActor>(AActor::StaticClass(), FTransform::Identity);
			USceneComponent* Root = NewObject<USceneComponent>(Owner, TEXT("Root"));
			Owner->SetRootComponent(Root);
			Root->RegisterComponent();
			Room = NewObject<UDreamInteriorComponent>(Owner, TEXT("Room"));
			Room->Extent = FVector(300.0, 300.0, 150.0);
			Room->SetupAttachment(Root);
			Room->SetRelativeLocation(FVector(0.0, 0.0, 150.0));
			Room->RegisterComponent();
			Door = NewObject<UDreamPortalComponent>(Owner, TEXT("Door"));
			Door->Width = 120.0f;
			Door->Height = 220.0f;
			Door->Openness = 0.0f;
			Door->SetupAttachment(Root);
			Door->SetRelativeLocationAndRotation(FVector(-300.0, 0.0, 110.0), FRotator(0.0, 180.0, 0.0));
			Door->RegisterComponent();
			Window = NewObject<UDreamPortalComponent>(Owner, TEXT("Window"));
			Window->Width = 260.0f;
			Window->Height = 160.0f;
			Window->Openness = 0.0f;
			Window->bVentilate = false;
			Window->SetupAttachment(Root);
			Window->SetRelativeLocationAndRotation(FVector(0.0, -300.0, 160.0), FRotator(0.0, -90.0, 0.0));
			Window->RegisterComponent();
		}

		~FRoomWorld()
		{
			if (World)
			{
				World->DestroyWorld(false);
			}
		}

		FDreamViewOcclusion ViewAt(const FVector& P) const
		{
			FDreamViewOcclusion Open;   // (what the rays say in an empty world: the open)
			Open.bValid = true;
			return DreamWorld::Interiors::Apply(Open, P, Clock->GetInteriors(), Clock->GetPortals());
		}
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDreamWorldInteriorViewTest, "DreamWorld.Interior.View", DreamWorldInteriorTests::Flags)
bool FDreamWorldInteriorViewTest::RunTest(const FString& Parameters)
{
	using namespace DreamWorldInteriorTests;
	FRoomWorld W;
	if (!TestTrue(TEXT("a room world"), W.World && W.Clock && W.Room && W.Door && W.Window))
	{
		return false;
	}
	TestEqual(TEXT("the room and its two openings registered"), W.Clock->GetInteriors().Num() + W.Clock->GetPortals().Num(), 3);
	TestTrue(TEXT("both openings are the room's"), W.Room->HasPortal(*W.Door) && W.Room->HasPortal(*W.Window));
	const FVector Middle(0.0, 0.0, 170.0);
	TestEqual(TEXT("its middle is inside"), W.Room->GetWeightAt(Middle), 1.0f);
	TestEqual(TEXT("outside it, nothing"), W.Room->GetWeightAt(FVector(-400.0, 0.0, 170.0)), 0.0f);
	const float NearWall = W.Room->GetWeightAt(FVector(-280.0, 0.0, 170.0));
	TestTrue(FString::Printf(TEXT("by its wall, part way (%.2f): a doorway is a fade"), NearWall), NearWall > 0.0f && NearWall < 1.0f);

	// All shut: indoors, a roof, every side closed, nothing to hear the outdoors from.
	const FDreamViewOcclusion Shut = W.ViewAt(Middle);
	TestTrue(FString::Printf(TEXT("shut: indoors (%.2f) under a roof (%.2f)"), Shut.Interior, Shut.Overhead), Shut.Interior > 0.99f && Shut.Overhead > 0.99f);
	TestTrue(TEXT("shut: every side closed"), FMath::Min(FMath::Min(Shut.PlusX, Shut.MinusX), FMath::Min(Shut.PlusY, Shut.MinusY)) > 0.99f);
	TestTrue(TEXT("shut: no opening"), Shut.Opening.IsNearlyZero());

	// The door open: less indoors, the -X side open, the outdoors heard from it.
	int32 Changes = 0;
	FBox Changed(ForceInit);
	const FDelegateHandle Handle = W.Clock->OnPortalChanged.AddLambda([&Changes, &Changed](const FBox& Bounds) { ++Changes; Changed = Bounds; });
	W.Door->SetOpenness(1.0f);
	TestEqual(TEXT("opening the door tells the world once"), Changes, 1);
	TestTrue(TEXT("with the doorway's bounds"), Changed.IsInside(W.Door->GetComponentLocation()) && Changed.GetSize().Y > 100.0);
	const FDreamViewOcclusion DoorOpen = W.ViewAt(Middle);
	AddInfo(FString::Printf(TEXT("door open, from the middle: interior %.2f, -X %.2f, +X %.2f, opening %s"), DoorOpen.Interior, DoorOpen.MinusX, DoorOpen.PlusX, *DoorOpen.Opening.ToCompactString()));
	TestTrue(TEXT("door open: less indoors"), DoorOpen.Interior < Shut.Interior);
	TestTrue(TEXT("its side is open, the far side not"), DoorOpen.MinusX < 0.9f && DoorOpen.PlusX > 0.99f);
	TestTrue(TEXT("heard from the door"), DoorOpen.Opening.X < -0.05 && FMath::Abs(DoorOpen.Opening.Y) < 1.0e-3);
	const FDreamViewOcclusion ByDoor = W.ViewAt(FVector(-150.0, 0.0, 170.0));
	TestTrue(FString::Printf(TEXT("nearer the door it fills more of the view (%.2f against %.2f)"), ByDoor.MinusX, DoorOpen.MinusX), ByDoor.MinusX < DoorOpen.MinusX);

	// The glass window opened (it lets no air through, but the light and the sound): the -Y side opens too.
	W.Window->SetOpenness(1.0f);
	const FDreamViewOcclusion Both = W.ViewAt(Middle);
	TestTrue(TEXT("the window: its side opens"), Both.MinusY < 0.99f && Both.Interior < DoorOpen.Interior);
	TestTrue(TEXT("the window lets no air through"), W.Window->GetVentilation() == 0.0f && W.Door->GetVentilation() == 1.0f);

	// Shut again: as before.
	W.Door->SetOpenness(0.0f);
	W.Window->SetOpenness(0.0f);
	TestEqual(TEXT("and every change told"), Changes, 4);
	TestTrue(TEXT("shut again: closed"), W.ViewAt(Middle).MinusX > 0.99f);
	W.Clock->OnPortalChanged.Remove(Handle);

	// Outside the room the rays decide.
	FDreamViewOcclusion Porch;
	Porch.bValid = true;
	Porch.Overhead = 0.7f;
	const FDreamViewOcclusion Out = DreamWorld::Interiors::Apply(Porch, FVector(-1000.0, 0.0, 170.0), W.Clock->GetInteriors(), W.Clock->GetPortals());
	TestEqual(TEXT("outside: the probe's"), Out.Overhead, 0.7f);

	// The clock uses it: a view reported inside is indoors (no walls for the rays here: the room alone says so).
	W.Clock->ReportViewLocation(Middle, true);
	W.Clock->RefreshViewOcclusion();
	TestTrue(FString::Printf(TEXT("the clock's view inside the room is indoors (%.2f)"), W.Clock->GetViewOcclusion().Interior), W.Clock->GetViewOcclusion().Interior > 0.99f);

	// The weather: all of it kept out inside, none right outside the wall.
	TestEqual(TEXT("the weather: kept out anywhere inside"), W.Clock->GetInteriorAt(FVector(-290.0, 0.0, 170.0), true), 1.0f);
	TestEqual(TEXT("and not 20 cm outside the wall (the rain seen through the window falls)"), W.Clock->GetInteriorAt(FVector(0.0, -320.0, 170.0), true), 0.0f);
	if (const UDreamWeatherSubsystem* Weather = W.World->GetSubsystem<UDreamWeatherSubsystem>())
	{
		TestEqual(TEXT("the weather's exposure inside"), Weather->GetExposureAt(Middle), 0.0f);
		TestEqual(TEXT("and right outside the window"), Weather->GetExposureAt(FVector(0.0, -320.0, 170.0)), 1.0f);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDreamWorldInteriorWindTest, "DreamWorld.Interior.Wind", DreamWorldInteriorTests::Flags)
bool FDreamWorldInteriorWindTest::RunTest(const FString& Parameters)
{
	using namespace DreamWorldInteriorTests;
	FRoomWorld W;
	UDreamWindSubsystem* Wind = W.World ? W.World->GetSubsystem<UDreamWindSubsystem>() : nullptr;
	if (!TestTrue(TEXT("a room world with the wind"), W.Clock && Wind))
	{
		return false;
	}
	// A steady 8 m/s along +X: into the doorway (it faces -X, upwind).
	FDreamWindGlobalWind Steady = Wind->GetGlobalWind();
	Steady.Direction = FVector(1.0, 0.0, 0.0);
	Steady.Speed = 800.0f;
	Steady.GustStrength = 0.0f;
	Steady.TurbulenceStrength = 0.0f;
	Wind->SetGlobalWind(Steady, 0.0f);
	Wind->Tick(0.016f);
	const FVector Open(-1500.0, 0.0, 150.0);
	const FVector Corner(200.0, 200.0, 150.0);
	const FVector InDoor(-200.0, 0.0, 110.0);
	const float OpenSpeed = float(Wind->SampleWindVelocity(Open).Size());
	const float ShutCorner = float(Wind->SampleWindVelocity(Corner).Size());
	const float ShutDoor = float(Wind->SampleWindVelocity(InDoor).X);
	AddInfo(FString::Printf(TEXT("door shut: the open %.0f cm/s, the room's far corner %.0f, inside the doorway %.0f along +X"), OpenSpeed, ShutCorner, ShutDoor));
	TestTrue(TEXT("the open blows"), OpenSpeed > 700.0f);
	TestTrue(TEXT("the room is calm"), ShutCorner < 0.1f * OpenSpeed && FMath::Abs(ShutDoor) < 0.1f * OpenSpeed);

	W.Door->SetOpenness(1.0f);
	Wind->Tick(0.016f);
	const float OpenDoor = float(Wind->SampleWindVelocity(InDoor).X);
	const float OpenCorner = float(Wind->SampleWindVelocity(Corner).Size());
	AddInfo(FString::Printf(TEXT("door open: inside the doorway %.0f cm/s along +X, the far corner %.0f"), OpenDoor, OpenCorner));
	TestTrue(TEXT("the door open: the wind blows in through it"), OpenDoor > 0.2f * OpenSpeed);
	TestTrue(TEXT("but the far corner stays calm"), OpenCorner < 0.1f * OpenSpeed);

	// The glass window opened lets no air in.
	W.Door->SetOpenness(0.0f);
	W.Window->SetOpenness(1.0f);
	Steady.Direction = FVector(0.0, 1.0, 0.0);   // into the window (it faces -Y)
	Wind->SetGlobalWind(Steady, 0.0f);
	Wind->Tick(0.016f);
	TestTrue(TEXT("a glass window lets no wind in"), Wind->SampleWindVelocity(FVector(0.0, -200.0, 160.0)).Size() < 0.1f * OpenSpeed);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
