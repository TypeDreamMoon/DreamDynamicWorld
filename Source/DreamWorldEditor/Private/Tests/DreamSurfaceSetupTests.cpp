// Copyright DreamMoon. All Rights Reserved.

#include "DreamSurfaceSetup.h"

#include "DreamWindGlobalActor.h"
#include "DreamWindTerrainMap.h"

#include "Engine/Level.h"
#include "Engine/World.h"
#include "Misc/AutomationTest.h"
#include "Misc/PackageName.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDreamSurfaceLevelBakedRoofsTest, "DreamWorld.SurfaceLevel.BakedRoofs",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FDreamSurfaceLevelBakedRoofsTest::RunTest(const FString& Parameters)
{
	const TCHAR* Package = DreamWorld::Setup::SurfaceTestMapPackage;
	UWorld* World = LoadObject<UWorld>(nullptr, *FString::Printf(TEXT("%s.%s"), Package, *FPackageName::GetShortName(Package)));
	if (!TestNotNull(TEXT("the surface test level (run Setup -SurfaceTestMap)"), World))
	{
		return false;
	}
	const UDreamWindTerrainMap* Map = nullptr;
	for (AActor* Actor : World->PersistentLevel->Actors)
	{
		if (const ADreamWindGlobal* Wind = Cast<ADreamWindGlobal>(Actor))
		{
			Map = Wind->TerrainMap;
		}
	}
	if (!TestTrue(TEXT("a baked wind terrain map"), Map && Map->IsValidMap()))
	{
		return false;
	}
	// The level's heights take in its roofs (the bake traces the level's collision, which must be there by then):
	// the canopy's roof tops out at 320 cm, the pergola's at 300 cm; the open ground stays low.
	auto HeightAt = [Map](double X, double Y) { return double(Map->Sample(FVector2D(X, Y)).W) + Map->HeightRange.X; };
	TestTrue(*FString::Printf(TEXT("the canopy's roof (%.0f cm)"), HeightAt(1700.0, 700.0)), HeightAt(1700.0, 700.0) > 280.0);
	TestTrue(*FString::Printf(TEXT("the pergola's roof (%.0f cm)"), HeightAt(2500.0, -500.0)), HeightAt(2500.0, -500.0) > 260.0);
	TestTrue(*FString::Printf(TEXT("the open ground (%.0f cm)"), HeightAt(300.0, 300.0)), HeightAt(300.0, 300.0) < 50.0);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
