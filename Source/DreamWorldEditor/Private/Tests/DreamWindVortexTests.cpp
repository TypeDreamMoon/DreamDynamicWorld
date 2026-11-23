// Copyright DreamMoon. All Rights Reserved.

// Whirlwinds (a dust devil, a tornado): the shape of the wind round one (the shared model, the same on the CPU and the
// GPU), one standing in the CPU's wind, and the spawner's -- the same whirlwinds for the same seed, in their seasons and
// hours, spinning up and dying out. No renderer needed.

#include "CoreMinimal.h"
#include "DreamWindSubsystem.h"
#include "DreamWindTypes.h"
#include "DreamWeatherSelector.h"
#include "DreamWindVortex.h"
#include "DreamWorldCelestial.h"
#include "DreamWorldSubsystem.h"

#include "Engine/World.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace DreamWindVortexTests
{
	constexpr EAutomationTestFlags Flags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter;

	FDreamWindSourceDesc Twister(float Spin)
	{
		FDreamWindSourceDesc D;
		D.Type = EDreamWindSourceType::Twister;
		D.Position = FVector::ZeroVector;
		D.Direction = FVector::UpVector;
		D.Radius = 1000.0f;
		D.Stretch = 4.0f;
		D.InnerRadiusFraction = 0.3f;
		D.Strength = 1000.0f * Spin;
		D.Updraft = 500.0f;
		D.Inflow = 300.0f;
		D.InjectRate = 4.0f;
		return D;
	}

	FVector At(const FDreamWindSourceDesc& D, const FVector& P, const FVector& Background = FVector::ZeroVector)
	{
		using namespace DreamWind::Hlsl;
		const FDreamWindSourceData S = UDreamWindSubsystem::PackSource(D, 1.0f);
		const float3 V = DW_ApplySource(float3(float(Background.X), float(Background.Y), float(Background.Z)), S, float3(float(P.X), float(P.Y), float(P.Z)), 1.0f);
		return FVector(V.x, V.y, V.z);
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDreamWindTwisterShapeTest, "DreamWind.Twister.Shape", DreamWindVortexTests::Flags)
bool FDreamWindTwisterShapeTest::RunTest(const FString& Parameters)
{
	using namespace DreamWindVortexTests;
	const FDreamWindSourceDesc Ccw = Twister(1.0f);
	// (the core 3 m across its radius, the whirl 10 m out, 40 m high; 10 m/s round the core's edge)
	const FVector Edge = At(Ccw, FVector(300.0, 0.0, 1000.0));
	TestTrue(FString::Printf(TEXT("round the core's edge, counter-clockwise from above at its full speed (%s)"), *Edge.ToCompactString()), FMath::IsNearlyEqual(Edge.Y, 1000.0, 30.0));
	TestNearlyEqual(TEXT("inside the core it turns as a solid body (half way: half the speed)"), At(Ccw, FVector(150.0, 0.0, 1000.0)).Y, 500.0, 25.0);
	TestNearlyEqual(TEXT("outside it, falling off as 1/r"), At(Ccw, FVector(600.0, 0.0, 1000.0)).Y, 500.0, 25.0);
	TestTrue(TEXT("its core's wall rises at the updraft"), FMath::IsNearlyEqual(At(Ccw, FVector(240.0, 0.0, 1500.0)).Z, 500.0, 10.0));
	TestTrue(TEXT("its axis less (a tube of rising air)"), At(Ccw, FVector(0.0, 0.0, 1500.0)).Z < 0.4 * 500.0 && At(Ccw, FVector(0.0, 0.0, 1500.0)).Z > 0.0);
	const FVector Low = At(Ccw, FVector(300.0, 0.0, 50.0));
	TestTrue(FString::Printf(TEXT("along the ground the air is drawn in (%.0f cm/s inward)"), -Low.X), -Low.X > 200.0 && -Low.X < 300.0);
	TestTrue(TEXT("higher up much less so"), -At(Ccw, FVector(300.0, 0.0, 2500.0)).X < 0.05 * 300.0);
	TestTrue(TEXT("beyond its reach nothing"), At(Ccw, FVector(1500.0, 0.0, 500.0)).IsNearlyZero() && At(Ccw, FVector(0.0, 0.0, 4500.0)).IsNearlyZero());
	TestTrue(TEXT("below the ground nothing"), At(Ccw, FVector(300.0, 0.0, -300.0)).IsNearlyZero());
	TestTrue(TEXT("a clockwise one turns the other way"), At(Twister(-1.0f), FVector(300.0, 0.0, 1000.0)).Y < -950.0);
	const FVector Carried = At(Ccw, FVector(300.0, 0.0, 1000.0), FVector(800.0, 0.0, 0.0));
	TestTrue(FString::Printf(TEXT("added to the wind it stands in, not blended toward (%s)"), *Carried.ToCompactString()), FMath::IsNearlyEqual(Carried.X, 800.0 + Edge.X, 1.0) && FMath::IsNearlyEqual(Carried.Y, Edge.Y, 1.0));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDreamWindTwisterWorldTest, "DreamWind.Twister.InTheWind", DreamWindVortexTests::Flags)
bool FDreamWindTwisterWorldTest::RunTest(const FString& Parameters)
{
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
	UDreamWindSubsystem* Wind = World ? World->GetSubsystem<UDreamWindSubsystem>() : nullptr;
	if (!TestTrue(TEXT("a world with the wind"), Wind != nullptr))
	{
		if (World)
		{
			World->DestroyWorld(false);
		}
		return false;
	}
	FDreamWindGlobalWind Calm = Wind->GetGlobalWind();
	Calm.Speed = 0.0f;
	Calm.GustStrength = 0.0f;
	Calm.TurbulenceStrength = 0.0f;
	Wind->SetGlobalWind(Calm, 0.0f);
	ADreamWindVortex* Devil = World->SpawnActor<ADreamWindVortex>(FVector::ZeroVector, FRotator::ZeroRotator);
	Devil->SetKind(EDreamVortexKind::DustDevil);
	Devil->Intensity = 1.0f;
	Devil->Refresh();
	Wind->Tick(0.016f);
	const FVector Round = Wind->SampleWindVelocity(FVector(Devil->CoreRadius, 0.0, 600.0));
	AddInfo(FString::Printf(TEXT("a dust devil in a calm: at its core's edge 6 m up %s cm/s"), *Round.ToCompactString()));
	TestTrue(TEXT("the air turns round it at its speed"), FMath::IsNearlyEqual(Round.Y, double(Devil->TangentialSpeed), 0.05 * Devil->TangentialSpeed));
	TestTrue(TEXT("and rises in its core's wall"), Wind->SampleWindVelocity(FVector(Devil->CoreRadius * 0.8, 0.0, 1000.0)).Z > 0.8 * Devil->Updraft);
	TestTrue(TEXT("far off it the air is calm"), Wind->SampleWindVelocity(FVector(5000.0, 0.0, 600.0)).IsNearlyZero(1.0));
	// The clock's register (the sky's funnels): a dust devil is not in it; a tornado is, with its funnel.
	const UDreamWorldSubsystem* Clock = World->GetSubsystem<UDreamWorldSubsystem>();
	TestTrue(TEXT("a dust devil draws no funnel"), Clock && Clock->GetVortices().Num() == 0 && Devil->GetVortexId() == 0);
	Devil->SetKind(EDreamVortexKind::Tornado);
	Devil->Refresh();
	const FDreamWorldVortex* Registered = Clock ? Clock->GetVortices().Find(Devil->GetVortexId()) : nullptr;
	TestTrue(TEXT("a tornado is registered with the clock, its funnel as wide as it reaches"), Registered && Registered->bFunnel && Registered->Radius == Devil->Radius
		&& Registered->Strength == 1.0f && Registered->Spin == 1.0f && Clock->GetVortices().Num() == 1);
	Devil->SetKind(EDreamVortexKind::DustDevil);
	Devil->Intensity = 0.0f;
	Devil->Refresh();
	Wind->Tick(0.016f);
	TestTrue(TEXT("died out, nothing"), Wind->SampleWindVelocity(FVector(Devil->CoreRadius, 0.0, 600.0)).IsNearlyZero(1.0));
	TestTrue(TEXT("and out of the register"), Clock && Clock->GetVortices().Num() == 0 && Devil->GetVortexId() == 0);
	World->DestroyWorld(false);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDreamWindVortexSpawnerTest, "DreamWind.Twister.Spawner", DreamWindVortexTests::Flags)
bool FDreamWindVortexSpawnerTest::RunTest(const FString& Parameters)
{
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
	ADreamWindVortexSpawner* Spawner = World ? World->SpawnActor<ADreamWindVortexSpawner>(FVector(1000.0, 2000.0, 0.0), FRotator::ZeroRotator) : nullptr;
	if (!TestNotNull(TEXT("a spawner"), Spawner))
	{
		if (World)
		{
			World->DestroyWorld(false);
		}
		return false;
	}
	// A year of slots from 1 January 2026.
	const double SlotSeconds = double(Spawner->SlotHours) * 3600.0;
	const double Year = DreamWorld::Celestial::FromDateTime(FDateTime(2026, 1, 1));
	const int64 First = FMath::FloorToInt64(Year / SlotSeconds);
	const int64 Count = int64(365.0 * 86400.0 / SlotSeconds);
	int32 Devils[4] = {}, Tornadoes[4] = {};
	bool bSame = true, bHours = true, bEnvelope = true, bNear = true, bDifferent = false;
	ADreamWindVortexSpawner* Other = World->SpawnActor<ADreamWindVortexSpawner>(FVector(1000.0, 2000.0, 0.0), FRotator::ZeroRotator);
	Other->SeedOffset = 7;
	for (int64 Slot = First; Slot < First + Count; ++Slot)
	{
		FDreamVortexPlan A, B, C;
		const bool bA = Spawner->PlanSlot(1337, Slot, A);
		const bool bB = Spawner->PlanSlot(1337, Slot, B);
		bSame &= bA == bB && (!bA || A == B);
		const bool bC = Other->PlanSlot(1337, Slot, C);
		bDifferent |= bA != bC || (bA && !(A == C));
		if (!bA)
		{
			continue;
		}
		const int32 Season = UDreamWeatherSelector::SeasonAt(A.StartSeconds, false);
		(A.Kind == EDreamVortexKind::Tornado ? Tornadoes : Devils)[Season]++;
		if (A.Kind == EDreamVortexKind::DustDevil)
		{
			const double Hour = DreamWorld::Celestial::HoursOfDay(A.StartSeconds);
			bHours &= Hour >= Spawner->DustDevilHours.X && Hour < Spawner->DustDevilHours.Y;
		}
		const double Mid = A.StartSeconds + A.LifetimeSeconds * 0.5;
		bEnvelope &= A.StrengthAt(A.StartSeconds - 1.0) == 0.0f && A.StrengthAt(Mid) == 1.0f && A.StrengthAt(A.EndSeconds()) == 0.0f
			&& A.StrengthAt(A.StartSeconds + A.LifetimeSeconds * 0.05) > 0.0f && A.StrengthAt(A.StartSeconds + A.LifetimeSeconds * 0.05) < 1.0f
			&& A.DecayAt(Mid) == 0.0f && A.DecayAt(A.StartSeconds + A.LifetimeSeconds * 0.875) > 0.3f
			&& FMath::IsNearlyEqual(A.DecayAt(A.StartSeconds + A.LifetimeSeconds * 0.875) + A.StrengthAt(A.StartSeconds + A.LifetimeSeconds * 0.875), 1.0f, 1e-3f);
		bNear &= FVector2D::Distance(A.FootAt(Mid), FVector2D(1000.0, 2000.0)) <= Spawner->PassRadius * 1.2;
	}
	AddInfo(FString::Printf(TEXT("a year: dust devils %d / %d / %d / %d, tornadoes %d / %d / %d / %d (spring, summer, autumn, winter)"),
		Devils[0], Devils[1], Devils[2], Devils[3], Tornadoes[0], Tornadoes[1], Tornadoes[2], Tornadoes[3]));
	TestTrue(TEXT("the same seed and slot, the same whirlwind"), bSame);
	TestTrue(TEXT("another spawner's seed, others"), bDifferent);
	TestTrue(TEXT("most dust devils in summer, none in winter"), Devils[1] > Devils[0] && Devils[1] > Devils[2] && Devils[3] == 0);
	TestTrue(TEXT("tornadoes most in summer, none in winter"), Tornadoes[1] > Tornadoes[2] && Tornadoes[3] == 0 && Tornadoes[1] > 0);
	TestTrue(TEXT("dust devils only in the afternoon's hours"), bHours);
	TestTrue(TEXT("each spins up, holds and dies out, roping out as it does"), bEnvelope);
	TestTrue(TEXT("each passes near the spawner half way through its life"), bNear);

	// What is alive at a moment is what the slots say.
	FDreamVortexPlan Any;
	int64 Found = First;
	while (Found < First + Count && !Spawner->PlanSlot(1337, Found, Any))
	{
		++Found;
	}
	if (TestTrue(TEXT("a whirlwind in the year"), Found < First + Count))
	{
		// (the clock's seed: the spawner asks it)
		const UDreamWorldSubsystem* Clock = UDreamWorldSubsystem::Get(Spawner);
		FDreamVortexPlan Mine;
		const int32 Seed = Clock ? Clock->GetSeed() : 0;
		int64 S = First;
		while (S < First + Count && !Spawner->PlanSlot(Seed, S, Mine))
		{
			++S;
		}
		if (S < First + Count)
		{
			const TArray<FDreamVortexPlan> Alive = Spawner->GetVorticesAt(Mine.StartSeconds + Mine.LifetimeSeconds * 0.5);
			TestTrue(TEXT("alive half way through its life"), Alive.Contains(Mine));
			TestFalse(TEXT("and not after it"), Spawner->GetVorticesAt(Mine.EndSeconds() + 1.0).Contains(Mine));
		}
	}
	World->DestroyWorld(false);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
