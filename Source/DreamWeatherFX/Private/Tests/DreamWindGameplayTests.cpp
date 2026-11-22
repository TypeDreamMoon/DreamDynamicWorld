// Copyright DreamMoon. All Rights Reserved.

#include "DreamWindCameraShake.h"
#include "DreamWindForceComponent.h"
#include "DreamWindSubsystem.h"

#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/World.h"
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "UObject/Package.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDreamWindForceDragTest, "DreamWind.Gameplay.Drag", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FDreamWindForceDragTest::RunTest(const FString& Parameters)
{
	// 10 m/s on a square metre with Cd 1: 61 N, along the wind.
	const FVector F = UDreamWindForceComponent::Drag(FVector(1000.0, 0.0, 0.0), 1.0f, 1.0f);
	TestNearlyEqual(TEXT("10 m/s on 1 m^2: about 61 N"), F.X, 61.25, 0.01);
	TestTrue(TEXT("along the wind"), FMath::IsNearlyZero(F.Y) && FMath::IsNearlyZero(F.Z));
	TestNearlyEqual(TEXT("twice the speed, four times the force"), UDreamWindForceComponent::Drag(FVector(2000.0, 0.0, 0.0), 1.0f, 1.0f).X, 4.0 * 61.25, 0.01);
	TestTrue(TEXT("still air: no force"), UDreamWindForceComponent::Drag(FVector::ZeroVector, 1.0f, 1.0f).IsZero());
	TestTrue(TEXT("against the wind: pushed back"), UDreamWindForceComponent::Drag(FVector(-1000.0, 0.0, 0.0), 1.0f, 1.0f).X < 0.0);

	// A crate simulating physics in a wind from the west (15 m/s nominal, shaped and gusting) is pushed east.
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
	if (!TestNotNull(TEXT("world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(false); };
	UDreamWindSubsystem* Wind = UDreamWindSubsystem::Get(World);
	UStaticMesh* Cube = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
	if (!TestNotNull(TEXT("wind"), Wind) || !TestNotNull(TEXT("cube"), Cube))
	{
		return false;
	}
	Wind->SetGlobalWindDirectionAndSpeed(FVector(1.0, 0.0, 0.0), 1500.0f, 0.0f);
	FActorSpawnParameters Spawn;
	Spawn.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	AStaticMeshActor* Crate = World->SpawnActor<AStaticMeshActor>(FVector(0.0, 0.0, 100.0), FRotator::ZeroRotator, Spawn);
	Crate->GetStaticMeshComponent()->SetMobility(EComponentMobility::Movable);
	Crate->GetStaticMeshComponent()->SetStaticMesh(Cube);
	Crate->GetStaticMeshComponent()->SetSimulatePhysics(true);
	UDreamWindForceComponent* Force = NewObject<UDreamWindForceComponent>(Crate);
	Force->RegisterComponent();
	Force->ApplyWind();
	const FVector Pushed = Force->GetLastForce(Crate->GetStaticMeshComponent());
	if (Crate->GetStaticMeshComponent()->IsSimulatingPhysics())
	{
		TestTrue(FString::Printf(TEXT("the wind pushes the crate downwind (%s N)"), *Pushed.ToString()), Pushed.X > 10.0 && FMath::Abs(Pushed.Y) < Pushed.X);
		Force->MaxForce = 20.0f;
		Force->ApplyWind();
		TestTrue(TEXT("no more than MaxForce"), Force->GetLastForce(Crate->GetStaticMeshComponent()).Size() <= 20.0 + 1.0e-3);
	}
	else
	{
		AddWarning(TEXT("The crate does not simulate in this world (no physics scene): only the drag formula was tested."));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDreamWindCameraShakeTest, "DreamWind.Gameplay.CameraShake", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FDreamWindCameraShakeTest::RunTest(const FString& Parameters)
{
	const UDreamWindCameraModifier* Shake = GetDefault<UDreamWindCameraModifier>();
	TestEqual(TEXT("a breeze does not shake"), Shake->IntensityFor(600.0f, 1.0f, 0.0f), 0.0f);
	TestNearlyEqual(TEXT("a gale shakes in full"), Shake->IntensityFor(3000.0f, 1.0f, 0.0f), 1.0f, 1.0e-4f);
	const float Middle = Shake->IntensityFor(1900.0f, 1.0f, 0.0f);
	TestTrue(TEXT("in between, some"), Middle > 0.0f && Middle < 1.0f);
	TestTrue(TEXT("a gust shakes more"), Shake->IntensityFor(1900.0f, 1.3f, 0.0f) > Middle);
	TestEqual(TEXT("indoors: still"), Shake->IntensityFor(3000.0f, 1.0f, 1.0f), 0.0f);
	TestNearlyEqual(TEXT("in a doorway: half"), Shake->IntensityFor(3000.0f, 1.0f, 0.5f), 0.5f, 1.0e-4f);
	return true;
}

#endif
