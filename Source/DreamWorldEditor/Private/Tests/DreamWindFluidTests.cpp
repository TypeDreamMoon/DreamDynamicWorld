// Copyright DreamMoon. All Rights Reserved.

// The wind fluid on the wind field test level, in the editor world, read back from its near volume: the air goes round
// the house (faster past its sides), turns back in its lee, eddies at its downwind corners and in the L's corner, and
// stays still inside the walls; once with the walls from the global distance field, once from the terrain map's heights.
// Needs the editor with a renderer.

#include "CoreMinimal.h"
#include "DreamWindSettings.h"
#include "DreamWindQualityTier.h"
#include "DreamWindSubsystem.h"
#include "DreamWindTestMap.h"

#include "Editor.h"
#include "Editor/EditorPerformanceSettings.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"
#include "LevelEditorViewport.h"
#include "Misc/App.h"
#include "Misc/AutomationTest.h"
#include "Misc/PackageName.h"
#include "RHIGlobals.h"
#include "ShaderCompiler.h"
#include "Tests/AutomationCommon.h"
#include "Tests/AutomationEditorCommon.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace DreamWindFluidTests
{
	constexpr EAutomationTestFlags Flags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter;

	/** The near grid centred between the house and the L, on the ground. */
	const FVector GridCenter(1200.0, -1200.0, 200.0);

	UWorld* EditorWorld()
	{
		return GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	}

	UDreamWindSubsystem* Wind()
	{
		return UDreamWindSubsystem::Get(EditorWorld());
	}

	void WaitFrames(int32 Frames)
	{
		TSharedRef<uint64> Start = MakeShared<uint64>(0);
		ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand([Start, Frames]()
		{
			if (*Start == 0)
			{
				*Start = GFrameCounter;
			}
			return GFrameCounter - *Start >= uint64(Frames);
		}));
	}

	void Then(TFunction<void()> Step)
	{
		ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand([Step]() { Step(); return true; }));
	}

	void SetCVar(const TCHAR* Name, int32 Value)
	{
		if (IConsoleVariable* Var = IConsoleManager::Get().FindConsoleVariable(Name))
		{
			Var->Set(Value, ECVF_SetByCode);
		}
	}

	/**
	 * The near volume as read back, averaged over the reads added to it (the lee sheds eddies: one frame is not the
	 * flow): the fluid's deviation per cell (cm/s) and its walls, with world lookups.
	 */
	struct FVolume
	{
		TArray<FVector3f> Deviation;
		TArray<float> Walls;
		int32 Reads = 0;
		FIntVector Size = FIntVector::ZeroValue;
		FVector Min = FVector::ZeroVector;
		FVector Cell = FVector::ZeroVector;

		bool Read(bool bFar = false)
		{
			TArray<FFloat16Color> Texels;
			FIntVector ReadSize;
			FVector4f V0, V1;
			if (!Wind() || !Wind()->ReadVolume(bFar, Texels, ReadSize, V0, V1))
			{
				return false;
			}
			const FVector Extent(1.0 / V1.X, 1.0 / V1.Y, 1.0 / V1.Z);
			const FVector ReadMin = FVector(V0.X, V0.Y, V0.Z) - Extent * 0.5;
			if (Reads == 0 || ReadSize != Size || !ReadMin.Equals(Min, 1.0))
			{
				Size = ReadSize;
				Min = ReadMin;
				Cell = Extent / FVector(Size);
				Deviation.Init(FVector3f::ZeroVector, Texels.Num());
				Walls.Init(0.0f, Texels.Num());
				Reads = 0;
			}
			for (int32 i = 0; i < Texels.Num(); ++i)
			{
				Deviation[i] += FVector3f(Texels[i].R.GetFloat(), Texels[i].G.GetFloat(), Texels[i].B.GetFloat());
				Walls[i] = Texels[i].A.GetFloat();
			}
			++Reads;
			return true;
		}

		int32 IndexOf(const FVector& P) const
		{
			const FIntVector C(FMath::FloorToInt((P.X - Min.X) / Cell.X), FMath::FloorToInt((P.Y - Min.Y) / Cell.Y), FMath::FloorToInt((P.Z - Min.Z) / Cell.Z));
			if (Reads == 0 || C.X < 0 || C.Y < 0 || C.Z < 0 || C.X >= Size.X || C.Y >= Size.Y || C.Z >= Size.Z)
			{
				return INDEX_NONE;
			}
			return (C.Z * Size.Y + C.Y) * Size.X + C.X;
		}

		/** The whole wind at a point: the CPU's background (analytic + terrain map, what consumers add it to) plus the deviation. */
		FVector Velocity(const FVector& P) const
		{
			const int32 I = IndexOf(P);
			const FVector D = I != INDEX_NONE ? FVector(Deviation[I] / float(Reads)) : FVector::ZeroVector;
			return Wind()->SampleWindVelocity(P) + D;
		}

		/** The last read's w: 1 a wall (or the residual divergence while DreamWind.Fluid.ShowDivergence is on); -1 outside. */
		float Wall(const FVector& P) const
		{
			const int32 I = IndexOf(P);
			return I != INDEX_NONE ? Walls[I] : -1.0f;
		}

		/** The vertical vorticity at a point (1/s), from the cells round it; 0 where one of them is a wall. */
		float VorticityZ(const FVector& P) const
		{
			const FVector X(Cell.X, 0.0, 0.0), Y(0.0, Cell.Y, 0.0);
			if (Wall(P) > 0.5f || Wall(P + X) > 0.5f || Wall(P - X) > 0.5f || Wall(P + Y) > 0.5f || Wall(P - Y) > 0.5f)
			{
				return 0.0f;
			}
			const double dVydx = (Velocity(P + X).Y - Velocity(P - X).Y) / (2.0 * Cell.X);
			const double dVxdy = (Velocity(P + Y).X - Velocity(P - Y).X) / (2.0 * Cell.Y);
			return float(dVydx - dVxdy);
		}
	};

	/** A map of the flow at a height, x across, y down: # a wall, < blowing back, else the speed in hundreds of cm/s. */
	void AddMap(FAutomationTestBase& Test, const FVolume& V, const TCHAR* Label, double Z, double X0, double X1, double Y0, double Y1)
	{
		for (double Y = Y0; Y >= Y1; Y -= 100.0)
		{
			FString Row;
			for (double X = X0; X <= X1; X += 100.0)
			{
				const FVector P(X, Y, Z);
				if (V.Wall(P) > 0.5f)
				{
					Row += TEXT('#');
					continue;
				}
				const FVector U = V.Velocity(P);
				Row += U.X < 0.0 ? TEXT('<') : TCHAR(TEXT('0') + FMath::Clamp(int32(U.Size() / 100.0), 0, 9));
			}
			Test.AddInfo(FString::Printf(TEXT("%s y %+5.1f %s"), Label, Y / 100.0, *Row));
		}
	}

	/** The checks, the same for both kinds of walls; maps of the flow when one fails. */
	void CheckFlow(FAutomationTestBase& Test, const FVolume& V, const TCHAR* Walls)
	{
		using namespace DreamWorld::Setup::WindTest;
		if (!Test.TestTrue(FString::Printf(TEXT("%s: the near volume reads back"), Walls), V.Reads > 0))
		{
			return;
		}
		Test.AddInfo(FString::Printf(TEXT("%s: %d reads averaged"), Walls, V.Reads));
		const double Z = 150.0;
		const double Back = HouseCenter.X + HouseSize.X * 0.5;
		const double Side = HouseCenter.Y + HouseSize.Y * 0.5;
		bool bAll = true;
		auto Check = [&Test, &bAll, Walls](const TCHAR* What, bool bOk)
		{
			bAll &= Test.TestTrue(FString::Printf(TEXT("%s: %s"), Walls, What), bOk);
		};

		// the strongest backflow in the lee, 1.5 .. 6.5 m behind, within 2.5 m of the axis
		float MinLee = TNumericLimits<float>::Max();
		for (double Behind = 150.0; Behind <= 650.0; Behind += 100.0)
		{
			for (double Y = -250.0; Y <= 250.0; Y += 100.0)
			{
				const FVector P(Back + Behind, HouseCenter.Y + Y, Z);
				if (V.Wall(P) < 0.5f)
				{
					MinLee = FMath::Min(MinLee, float(V.Velocity(P).X));
				}
			}
		}

		// (the open: 22 m upwind of the house's front, nothing upwind of it; past the side: the fastest 1.5 .. 5.5 m off the
		// wall -- the cell right beside it keeps no speed toward it, and a fine grid has the flow part from the wall at the
		// front corner, the speed-up beyond the bubble)
		const FVector OpenPoint(HouseCenter.X - HouseSize.X * 0.5 - 2200.0, HouseCenter.Y, Z);
		float Past = 0.0f;
		for (double Off = 150.0; Off <= 550.0; Off += 100.0)
		{
			Past = FMath::Max(Past, float(V.Velocity(FVector(HouseCenter.X, Side + Off, Z)).Size()));
		}
		const float Open = V.Velocity(OpenPoint).Size();
		const float Inside = V.Wall(HouseCenter);

		// behind each downwind corner, the strongest spin within a few metres (off the walls)
		auto StrongestSpin = [&V, Z](double X0, double X1, double Y0, double Y1)
		{
			float Best = 0.0f;
			for (double X = X0; X <= X1; X += 100.0)
			{
				for (double Y = FMath::Min(Y0, Y1); Y <= FMath::Max(Y0, Y1); Y += 100.0)
				{
					const float Spin = V.VorticityZ(FVector(X, Y, Z));
					Best = FMath::Abs(Spin) > FMath::Abs(Best) ? Spin : Best;
				}
			}
			return Best;
		};
		const float Left = StrongestSpin(Back + 50.0, Back + 450.0, Side - 250.0, Side + 150.0);
		const float Right = StrongestSpin(Back + 50.0, Back + 450.0, -Side + 250.0, -Side - 150.0);
		// the L's pocket (downwind of its cross-wind arm, under its along-wind one): the air in it turning back, and its spin
		float Pocket = 0.0f;
		float PocketBack = TNumericLimits<float>::Max();
		for (double DX = 150.0; DX <= 750.0; DX += 100.0)
		{
			for (double DY = -150.0; DY >= -950.0; DY -= 100.0)
			{
				const FVector P = CornerPoint + FVector(DX, DY, Z);
				const float Spin = V.VorticityZ(P);
				Pocket = FMath::Abs(Spin) > FMath::Abs(Pocket) ? Spin : Pocket;
				if (V.Wall(P) < 0.5f)
				{
					PocketBack = FMath::Min(PocketBack, float(V.Velocity(P).X));
				}
			}
		}
		const float OpenSpin = V.VorticityZ(OpenPoint);

		Test.AddInfo(FString::Printf(TEXT("%s: the strongest backflow in the house's lee %.0f cm/s; past its side %.0f cm/s, in the open %.0f cm/s; its middle a wall cell (w %.2f)"),
			Walls, MinLee, Past, Open, Inside));
		Test.AddInfo(FString::Printf(TEXT("%s: vertical vorticity (1/s) behind the house's corners %.2f / %.2f, in the L's pocket %.2f (its strongest backflow %.0f cm/s), in the open %.2f"),
			Walls, Left, Right, Pocket, PocketBack, OpenSpin));
		Check(TEXT("in the house's lee the air turns back"), MinLee < 0.0f);
		Check(TEXT("past the house's side the wind is faster than in the open"), Past > Open * 1.05f);
		Check(TEXT("the house is walls"), Inside > 0.5f);
		Check(TEXT("the house's downwind corners spin opposite ways"), Left * Right < 0.0f && FMath::Min(FMath::Abs(Left), FMath::Abs(Right)) > 1.0f);
		// (a pocket 12 m across: its eddy turns at about the wind over its size, slower than a corner's)
		Check(TEXT("the L's pocket holds an eddy (the air in it turns back)"), PocketBack < 0.0f && FMath::Abs(Pocket) > 0.3f);
		Check(TEXT("the open air spins far less"), FMath::Abs(OpenSpin) < 0.5f);
		if (!bAll)
		{
			AddMap(Test, V, *FString::Printf(TEXT("%s, round the house at 1.5 m:"), Walls), Z, -450.0, 3850.0, 1150.0, -1150.0);
			AddMap(Test, V, *FString::Printf(TEXT("%s, round the L at 1.5 m:"), Walls), Z, 650.0, 3350.0, -1850.0, -3950.0);
		}
	}
}

namespace DreamWindFluidTests
{
	/** Six reads, 30 frames apart, then the checks on their average. */
	void CheckAveraged(FAutomationTestBase* Test, const TCHAR* Walls)
	{
		TSharedRef<FVolume> V = MakeShared<FVolume>();
		for (int32 i = 0; i < 6; ++i)
		{
			Then([V]() { V->Read(); });
			WaitFrames(30);
		}
		const FString Label = Walls;
		Then([Test, V, Label]() { CheckFlow(*Test, *V, *Label); });
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDreamWindFluidFlowTest, "DreamWind.Gpu.Fluid", DreamWindFluidTests::Flags)
bool FDreamWindFluidFlowTest::RunTest(const FString& Parameters)
{
	using namespace DreamWindFluidTests;
	if (!FApp::CanEverRender() || !GEditor)
	{
		AddWarning(TEXT("Needs the editor with a renderer (not -nullrhi)."));
		return true;
	}
	if (!FPackageName::DoesPackageExist(DreamWorld::Setup::WindTestMapPackage))
	{
		AddError(TEXT("The wind field test level is missing: run Setup -WindFieldTestMap."));
		return false;
	}
	GetMutableDefault<UEditorPerformanceSettings>()->bThrottleCPUWhenNotForeground = false;
	SetCVar(TEXT("DreamWind.Fluid"), 1);
	SetCVar(TEXT("DreamWind.Fluid.DistanceField"), 1);
	// (the high quality level: the grids as tuned)
	SetCVar(TEXT("r.DreamWind.Quality"), 2);
	// The background the deviation is read against must be the same on both sides: the baked map (the same map and
	// the same code on the CPU and the GPU; the runtime map's CPU grid has coarser cells) without its wind shadow (the
	// fluid makes the lee). The walls of the 2.5D fallback then come from the baked map's heights.
	SetCVar(TEXT("DreamWind.RuntimeTerrain"), 0);
	SetCVar(TEXT("DreamWind.Occlusion"), 0);
	FAutomationEditorCommonUtils::LoadMap(DreamWorld::Setup::WindTestMapPackage);
	WaitFrames(10);
	Then([]()
	{
		for (FLevelEditorViewportClient* C : GEditor->GetLevelViewportClients())
		{
			if (C && C->IsPerspective())
			{
				// above the grid, looking down at it: the distance field's clipmaps cover it
				C->SetRealtime(true);
				C->SetViewLocation(GridCenter + FVector(-2000.0, 0.0, 1500.0));
				C->SetViewRotation(FRotator(-35.0, 0.0, 0.0));
				C->Invalidate();
			}
		}
		if (UDreamWindSubsystem* W = Wind())
		{
			W->OverrideVolumeCenter(GridCenter, true);
			// a steady wind: what is measured is the fluid's, not the gusts' and the turbulence's
			FDreamWindGlobalWind Steady = W->GetGlobalWind();
			Steady.Direction = DreamWorld::Setup::WindTest::WindDirection;
			Steady.Speed = DreamWorld::Setup::WindTest::WindSpeed;
			Steady.GustStrength = 0.0f;
			Steady.TurbulenceStrength = 0.0f;
			W->SetGlobalWind(Steady, 0.0f);
		}
		GShaderCompilingManager->FinishAllCompilation();
	});
	WaitFrames(400);
	Then([this]()
	{
		AddInfo(FString::Printf(TEXT("fluid active %s, steps %u, wall lookups %u"), Wind() && Wind()->IsFluidActive() ? TEXT("yes") : TEXT("no"),
			Wind() ? Wind()->GetFluidStepCount() : 0, Wind() ? Wind()->GetFluidWallPassCount() : 0));
		TestTrue(TEXT("the fluid runs"), Wind() && Wind()->IsFluidActive());
		TestTrue(TEXT("the walls were looked up in the distance field"), Wind() && Wind()->GetFluidWallPassCount() > 0);
	});
	CheckAveraged(this, TEXT("distance field walls"));
	// still there a while later (the eddies are not a start-up transient)
	WaitFrames(300);
	CheckAveraged(this, TEXT("distance field walls, later"));

	// What the pressure iterations leave of the divergence (relative to the speed), inside the grid off its sponge.
	Then([]() { SetCVar(TEXT("DreamWind.Fluid.ShowDivergence"), 1); });
	WaitFrames(3);
	Then([this]()
	{
		FVolume V;
		if (!V.Read())
		{
			return;
		}
		TArray<float> Left;
		const int32 Margin = UDreamWindSettings::Get()->FluidSpongeCells + 1;
		for (int32 Z = 1; Z < V.Size.Z - Margin; ++Z)
		{
			for (int32 Y = Margin; Y < V.Size.Y - Margin; ++Y)
			{
				for (int32 X = Margin; X < V.Size.X - Margin; ++X)
				{
					const float W = V.Walls[(Z * V.Size.Y + Y) * V.Size.X + X];
					if (W >= 0.0f)
					{
						Left.Add(W);
					}
				}
			}
		}
		Left.Sort();
		if (Left.Num() > 0)
		{
			double Sum = 0.0;
			for (float W : Left)
			{
				Sum += W;
			}
			AddInfo(FString::Printf(TEXT("residual divergence of the deviation |div D| h / |u| over %d air cells: mean %.4f, 95th percentile %.4f, max %.4f"),
				Left.Num(), Sum / Left.Num(), Left[int32(Left.Num() * 0.95)], Left.Last()));
		}
		SetCVar(TEXT("DreamWind.Fluid.ShowDivergence"), 0);
	});

	Then([]() { SetCVar(TEXT("DreamWind.Fluid.DistanceField"), 0); });
	WaitFrames(400);
	CheckAveraged(this, TEXT("height walls"));

	// The analytic wind's gusts and turbulence pass through the grid: away from the walls the fluid adds little to them
	// (projecting the gust fronts away would cancel them inside the grid).
	Then([]()
	{
		if (UDreamWindSubsystem* W = Wind())
		{
			FDreamWindGlobalWind Gusty = W->GetGlobalWind();
			const FDreamWindGlobalWind Defaults;
			Gusty.GustStrength = Defaults.GustStrength;
			Gusty.TurbulenceStrength = Defaults.TurbulenceStrength;
			W->SetGlobalWind(Gusty, 0.0f);
		}
	});
	WaitFrames(300);
	Then([this]()
	{
		FVolume V;
		if (!TestTrue(TEXT("gusty: the near volume reads back"), V.Read()))
		{
			return;
		}
		// the open upwind of the house: 9 .. 17 m before its front, 8 m either side, 1.5 .. 9.5 m up
		using namespace DreamWorld::Setup::WindTest;
		double Sum = 0.0;
		int32 Count = 0;
		for (double X = -700.0; X <= 100.0; X += 100.0)
		{
			for (double Y = -800.0; Y <= 800.0; Y += 200.0)
			{
				for (double Z = 150.0; Z <= 950.0; Z += 200.0)
				{
					const int32 I = V.IndexOf(FVector(X, Y, Z));
					if (I != INDEX_NONE && V.Walls[I] < 0.5f)
					{
						Sum += V.Deviation[I].Size();
						++Count;
					}
				}
			}
		}
		const double Mean = Count > 0 ? Sum / Count : 0.0;
		AddInfo(FString::Printf(TEXT("gusty: the fluid's mean deviation in the open upwind of the house %.0f cm/s over %d cells"), Mean, Count));
		TestTrue(TEXT("gusty: the gusts pass through the grid (the fluid adds little to them in the open)"), Count > 0 && Mean < 0.15 * WindSpeed);
	});

	Then([]()
	{
		SetCVar(TEXT("DreamWind.Fluid.DistanceField"), -1);
		SetCVar(TEXT("DreamWind.Fluid"), -1);
		SetCVar(TEXT("r.DreamWind.Quality"), -1);
		SetCVar(TEXT("DreamWind.RuntimeTerrain"), -1);
		SetCVar(TEXT("DreamWind.Occlusion"), -1);
		if (UDreamWindSubsystem* W = Wind())
		{
			W->OverrideVolumeCenter(FVector::ZeroVector, false);
		}
		FAutomationEditorCommonUtils::CreateNewMap();
	});
	WaitFrames(2);
	Then([]() { CollectGarbage(GARBAGE_COLLECTION_KEEPFLAGS); });
	return true;
}

// The fluid's disturbances in a calm (what is read is all theirs): a body walking leaves a wake; an explosion before the
// long wall pushes the air out on the open side and far less behind the wall; a rotor's downwash goes down under it and
// spreads out over the ground in a ring.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDreamWindFluidDisturbanceTest, "DreamWind.Gpu.FluidDisturbances", DreamWindFluidTests::Flags)
bool FDreamWindFluidDisturbanceTest::RunTest(const FString& Parameters)
{
	using namespace DreamWindFluidTests;
	using namespace DreamWorld::Setup::WindTest;
	if (!FApp::CanEverRender() || !GEditor)
	{
		AddWarning(TEXT("Needs the editor with a renderer (not -nullrhi)."));
		return true;
	}
	if (!FPackageName::DoesPackageExist(DreamWorld::Setup::WindTestMapPackage))
	{
		AddError(TEXT("The wind field test level is missing: run Setup -WindFieldTestMap."));
		return false;
	}
	GetMutableDefault<UEditorPerformanceSettings>()->bThrottleCPUWhenNotForeground = false;
	SetCVar(TEXT("DreamWind.Fluid"), 1);
	SetCVar(TEXT("DreamWind.Fluid.DistanceField"), 1);
	// (the high quality level: the grids as tuned)
	SetCVar(TEXT("r.DreamWind.Quality"), 2);
	FAutomationEditorCommonUtils::LoadMap(DreamWorld::Setup::WindTestMapPackage);
	WaitFrames(10);

	// Where each part happens: the grid is centred there, the view above it.
	auto LookAt = [](const FVector& Center)
	{
		for (FLevelEditorViewportClient* C : GEditor->GetLevelViewportClients())
		{
			if (C && C->IsPerspective())
			{
				C->SetRealtime(true);
				C->SetViewLocation(Center + FVector(-2000.0, 0.0, 1500.0));
				C->SetViewRotation(FRotator(-35.0, 0.0, 0.0));
				C->Invalidate();
			}
		}
		if (UDreamWindSubsystem* W = Wind())
		{
			W->OverrideVolumeCenter(Center, true);
		}
	};
	Then([LookAt]()
	{
		if (UDreamWindSubsystem* W = Wind())
		{
			FDreamWindGlobalWind Calm = W->GetGlobalWind();
			Calm.Speed = 0.0f;
			Calm.GustStrength = 0.0f;
			Calm.TurbulenceStrength = 0.0f;
			W->SetGlobalWind(Calm, 0.0f);
		}
		LookAt(FVector(-1500.0, 0.0, 200.0));
		GShaderCompilingManager->FinishAllCompilation();
	});
	WaitFrames(200);

	// A body walking 3 m/s along +x, 0.9 m up: behind it the air follows, before it barely.
	struct FWalk
	{
		TWeakObjectPtr<AActor> Actor;
		double Travelled = 0.0;
	};
	TSharedRef<FWalk> Walk = MakeShared<FWalk>();
	const FVector WalkStart(-3000.0, 0.0, 0.0);
	const double WalkSpeed = 300.0;
	Then([Walk, WalkStart]()
	{
		FActorSpawnParameters Spawn;
		Spawn.ObjectFlags |= RF_Transient;
		AActor* Body = EditorWorld()->SpawnActor<AActor>(AActor::StaticClass(), FTransform(WalkStart), Spawn);
		USceneComponent* Root = NewObject<USceneComponent>(Body, TEXT("Root"));
		Body->SetRootComponent(Root);
		Root->RegisterComponent();
		Body->SetActorLocation(WalkStart);
		DreamWorld::Setup::AddWindMover(*Body);
		Walk->Actor = Body;
	});
	ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand([Walk, WalkStart, WalkSpeed]()
	{
		Walk->Travelled += WalkSpeed * FApp::GetDeltaTime();
		if (AActor* Body = Walk->Actor.Get())
		{
			Body->SetActorLocation(WalkStart + FVector(Walk->Travelled, 0.0, 0.0));
		}
		return Walk->Travelled >= 1500.0;
	}));
	Then([this, Walk, WalkStart]()
	{
		FVolume V;
		if (!TestTrue(TEXT("walk: the near volume reads back"), V.Read()))
		{
			return;
		}
		const FVector Body = WalkStart + FVector(Walk->Travelled, 0.0, 90.0);
		auto AlongAt = [&V](const FVector& P)
		{
			const int32 I = V.IndexOf(P);
			return I != INDEX_NONE ? V.Deviation[I].X : 0.0f;
		};
		const float Behind = FMath::Max3(AlongAt(Body - FVector(150.0, 0.0, 0.0)), AlongAt(Body - FVector(250.0, 0.0, 0.0)), AlongAt(Body - FVector(350.0, 0.0, 0.0)));
		const float Ahead = AlongAt(Body + FVector(350.0, 0.0, 0.0));
		const float Aside = AlongAt(Body + FVector(-250.0, 600.0, 0.0));
		AddInfo(FString::Printf(TEXT("walk at 300 cm/s: the air 1.5 .. 3.5 m behind the body follows at %.0f cm/s, 3.5 m before it %.0f, 6 m aside %.0f"), Behind, Ahead, Aside));
		TestTrue(TEXT("walk: the body leaves a wake behind it"), Behind > 40.0f);
		TestTrue(TEXT("walk: far less before it and off to the side"), Behind > 3.0f * FMath::Abs(Ahead) && Behind > 3.0f * FMath::Abs(Aside));
		if (AActor* Actor = Walk->Actor.Get())
		{
			Actor->Destroy();
		}
	});

	// An explosion 3 m before the long wall (its upwind face), 1 m up.
	const FVector Blast(WallCenter.X - WallSize.X * 0.5 - 300.0, WallCenter.Y, 100.0);
	Then([LookAt, Blast]() { LookAt(Blast + FVector(300.0, 0.0, 100.0)); });
	WaitFrames(200);
	Then([Blast]()
	{
		if (UDreamWindSubsystem* W = Wind())
		{
			W->AddExplosion(Blast, 400.0f, 2000.0f, 0.25f);
		}
	});
	WaitFrames(6);
	Then([this, Blast]()
	{
		FVolume V;
		if (!TestTrue(TEXT("explosion: the near volume reads back"), V.Read()))
		{
			return;
		}
		auto At = [&V](const FVector& P)
		{
			const int32 I = V.IndexOf(P);
			return I != INDEX_NONE ? FVector(V.Deviation[I]) : FVector::ZeroVector;
		};
		// 7 m out: away from the wall (upwind, -x), along it (+y), and behind it (+x: 3.5 m past the wall, off the cells beside it)
		const FVector Open = At(Blast + FVector(-700.0, 0.0, 50.0));
		const FVector Along = At(Blast + FVector(0.0, 700.0, 50.0));
		const FVector Behind = At(Blast + FVector(700.0, 0.0, 50.0));
		AddInfo(FString::Printf(TEXT("explosion (4 m, 2000 cm/s), 7 m out: away from the wall %.0f cm/s outward, along it %.0f, behind the wall %.0f cm/s (%s)"),
			-Open.X, Along.Y, Behind.Size(), *Behind.ToCompactString()));
		TestTrue(TEXT("explosion: the air bursts out on the open side"), -Open.X > 200.0f && Along.Y > 200.0f);
		TestTrue(TEXT("explosion: the wall shelters what is behind it"), Behind.Size() < 0.5f * -Open.X);
	});

	// A rotor 8 m up, 3 m across... its downwash down under it, out over the ground round it.
	const FVector Rotor(-500.0, 600.0, 800.0);
	Then([LookAt, Rotor]()
	{
		LookAt(FVector(Rotor.X, Rotor.Y, 200.0));
		GEngine->Exec(EditorWorld(), *FString::Printf(TEXT("DreamWind.Rotor %.0f %.0f %.0f 300 1500 1000"), Rotor.X, Rotor.Y, Rotor.Z));
	});
	WaitFrames(240);
	Then([this, Rotor]()
	{
		FVolume V;
		if (!TestTrue(TEXT("rotor: the near volume reads back"), V.Read()))
		{
			return;
		}
		auto At = [&V](const FVector& P)
		{
			const int32 I = V.IndexOf(P);
			return I != INDEX_NONE ? FVector(V.Deviation[I]) : FVector::ZeroVector;
		};
		const FVector Under = At(FVector(Rotor.X, Rotor.Y, 350.0));
		double Out = 0.0;
		for (int32 k = 0; k < 8; ++k)
		{
			const FVector Dir(FMath::Cos(k * UE_PI / 4.0), FMath::Sin(k * UE_PI / 4.0), 0.0);
			Out += FVector::DotProduct(At(FVector(Rotor.X, Rotor.Y, 50.0) + Dir * 500.0), Dir) / 8.0;
		}
		AddInfo(FString::Printf(TEXT("rotor (8 m up, 1500 cm/s): 3.5 m up under it %.0f cm/s down; 5 m out over the ground, %.0f cm/s outward on average"), -Under.Z, Out));
		TestTrue(TEXT("rotor: the downwash goes down under it"), -Under.Z > 300.0f);
		TestTrue(TEXT("rotor: over the ground it spreads out in a ring"), Out > 100.0);
	});

	// The wind as the eye sees it (SampleWindVisual): asked under the rotor for a few frames, its answer comes back from
	// the GPU with the downwash in it; the CPU sample has none.
	static const FName ProbeKey(TEXT("DreamWindTests.UnderTheRotor"));
	const FVector ProbePoint(Rotor.X, Rotor.Y, 350.0);
	TSharedRef<int32> Asked = MakeShared<int32>(0);
	ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand([Asked, ProbePoint]()
	{
		if (UDreamWindSubsystem* W = Wind())
		{
			W->SampleWindVisual(ProbeKey, ProbePoint);
		}
		return ++(*Asked) >= 12;
	}));
	Then([this, ProbePoint]()
	{
		UDreamWindSubsystem* W = Wind();
		if (!W)
		{
			return;
		}
		const FVector Visual = W->SampleWindVisual(ProbeKey, ProbePoint).Velocity;
		const FVector Plain = W->SampleWindVelocity(ProbePoint);
		AddInfo(FString::Printf(TEXT("probe under the rotor after 12 frames: as seen %s, the CPU sample %s (cm/s)"), *Visual.ToCompactString(), *Plain.ToCompactString()));
		TestTrue(TEXT("probe: the wind as seen carries the downwash, read back from the GPU"), Visual.Z < -300.0 && FMath::Abs(Plain.Z) < 100.0);
		GEngine->Exec(EditorWorld(), TEXT("DreamWind.Rotor off"));
	});

	Then([]()
	{
		SetCVar(TEXT("DreamWind.Fluid.DistanceField"), -1);
		SetCVar(TEXT("DreamWind.Fluid"), -1);
		SetCVar(TEXT("r.DreamWind.Quality"), -1);
		if (UDreamWindSubsystem* W = Wind())
		{
			W->OverrideVolumeCenter(FVector::ZeroVector, false);
		}
		FAutomationEditorCommonUtils::CreateNewMap();
	});
	WaitFrames(2);
	Then([]() { CollectGarbage(GARBAGE_COLLECTION_KEEPFLAGS); });
	return true;
}

// ---- Quality levels ------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDreamWindQualityTiersTest, "DreamWind.Quality.Tiers", DreamWindFluidTests::Flags)
bool FDreamWindQualityTiersTest::RunTest(const FString& Parameters)
{
	const UDreamWindSettings& S = *UDreamWindSettings::Get();
	if (!FApp::CanEverRender() || GMaxRHIFeatureLevel < ERHIFeatureLevel::SM5)
	{
		// (no compute shaders, no renderer: every level runs as low)
		for (int32 Level = 0; Level <= 4; ++Level)
		{
			const FDreamWindQualityTier T = FDreamWindQualityTier::Resolve(S, Level);
			TestTrue(FString::Printf(TEXT("without a renderer level %d runs as low"), Level), T.Level == 0 && !T.bVolume && !T.bRuntimeTerrain);
		}
		return true;
	}
	const FDreamWindQualityTier Low = FDreamWindQualityTier::Resolve(S, 0);
	const FDreamWindQualityTier Medium = FDreamWindQualityTier::Resolve(S, 1);
	const FDreamWindQualityTier High = FDreamWindQualityTier::Resolve(S, 2);
	const FDreamWindQualityTier Epic = FDreamWindQualityTier::Resolve(S, 3);
	const FDreamWindQualityTier Cinematic = FDreamWindQualityTier::Resolve(S, 4);
	TestTrue(TEXT("low: the analytic wind and the baked map only, without its shadow march on the GPU"), !Low.bVolume && !Low.bRuntimeTerrain && !Low.bBakedShadow && High.bBakedShadow);
	TestTrue(TEXT("medium: the runtime terrain map, one grid at the far level's cells, no far level"),
		Medium.bVolume && Medium.bRuntimeTerrain && !Medium.bFar && Medium.NearCell == High.FarCell && Medium.NearResolution.Z == High.NearResolution.Z / 2);
	TestTrue(TEXT("high: the settings"), High.NearResolution == S.VolumeResolution && High.NearCell == S.VolumeVoxelSize
		&& High.NearIterations == S.FluidPressureIterations && High.FarInterval == S.FluidFarStepInterval && High.bFar == S.bVolumeFarLevel);
	TestTrue(TEXT("epic: finer near cells over more of them, the far level every frame"),
		Epic.NearCell < High.NearCell && Epic.NearResolution.X > High.NearResolution.X && Epic.NearIterations > High.NearIterations && Epic.FarInterval == 1);
	TestTrue(TEXT("cinematic: finer still, a wider far level"),
		Cinematic.NearCell < Epic.NearCell && Cinematic.NearResolution.X > Epic.NearResolution.X && Cinematic.FarResolution.X > High.FarResolution.X);
	TestTrue(TEXT("every level's near grid in whole thread groups"),
		Epic.NearResolution.X % 4 == 0 && Epic.NearResolution.Z % 4 == 0 && Cinematic.FarResolution.Z % 4 == 0);
	auto Cells = [](const FDreamWindQualityTier& T) { return T.bVolume ? double(T.NearResolution.X) * T.NearResolution.Y * T.NearResolution.Z : 0.0; };
	TestTrue(TEXT("the near grid grows level by level"), Cells(Low) < Cells(Medium) && Cells(Medium) < Cells(High) && Cells(High) < Cells(Epic) && Cells(Epic) < Cells(Cinematic));
	TestTrue(TEXT("the near level stays inside the far one"), Epic.NearResolution.X * Epic.NearCell < Epic.FarResolution.X * Epic.FarCell
		&& Cinematic.NearResolution.X * Cinematic.NearCell < Cinematic.FarResolution.X * Cinematic.FarCell);
	TestTrue(TEXT("a change of grids is told apart from a change of iterations"), !High.SameLayout(Epic) && !High.SameLayout(Medium) && High.SameLayout(High));
	return true;
}

namespace DreamWindFluidTests
{
	/** What the volume fade did while the level changed, frame by frame. */
	struct FFadeWatch
	{
		int32 Frames = 0;
		float Last = 1.0f;
		float Lowest = 1.0f;
		/** The largest change in one frame, against what half a second's fade allows in that frame. */
		float WorstStep = 0.0f;
		float WorstAllowed = 1.0f;
		bool bRoseBeforeSwitch = false;
		int32 SwitchFrame = INDEX_NONE;
		float FadeBeforeSwitch = 1.0f;
		/** The new grid's mean deviation the frame after it started (cm/s). */
		double MeanAtStart = -1.0;
	};

	double MeanDeviation(UDreamWindSubsystem* W, bool bFar = false)
	{
		TArray<FFloat16Color> Texels;
		FIntVector Size;
		FVector4f V0, V1;
		if (!W || !W->ReadVolume(bFar, Texels, Size, V0, V1))
		{
			return -1.0;
		}
		double Sum = 0.0;
		int32 Count = 0;
		for (const FFloat16Color& T : Texels)
		{
			if (T.A.GetFloat() < 0.5f)
			{
				Sum += FVector3f(T.R.GetFloat(), T.G.GetFloat(), T.B.GetFloat()).Size();
				++Count;
			}
		}
		return Count > 0 ? Sum / Count : 0.0;
	}

	/** Switches to a level and follows the fade until the new grids run in full (or MaxFrames). */
	void SwitchLevel(FAutomationTestBase* Test, int32 Level, int32 MaxFrames = 150)
	{
		TSharedRef<FFadeWatch> Watch = MakeShared<FFadeWatch>();
		TSharedRef<int32> From = MakeShared<int32>(-1);
		Then([Level, Watch, From]()
		{
			*From = Wind() ? Wind()->GetQualityTier().Level : -1;
			Watch->Last = Wind() ? Wind()->GetVolumeFade() : 0.0f;
			SetCVar(TEXT("r.DreamWind.Quality"), Level);
		});
		ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand([Level, Watch, MaxFrames]()
		{
			UDreamWindSubsystem* W = Wind();
			if (!W)
			{
				return true;
			}
			const float Fade = W->GetVolumeFade();
			const bool bSwitched = W->GetQualityTier().Level == Level;
			const float Allowed = FMath::Max(float(FApp::GetDeltaTime()), 1.0f / 240.0f) / 0.5f * 1.5f + 0.01f;
			const float Step = FMath::Abs(Fade - Watch->Last);
			if (Watch->Frames > 0 && Step / Allowed > Watch->WorstStep / Watch->WorstAllowed)
			{
				Watch->WorstStep = Step;
				Watch->WorstAllowed = Allowed;
			}
			if (!bSwitched && Fade > Watch->Last + 0.001f)
			{
				Watch->bRoseBeforeSwitch = true;
			}
			if (bSwitched && Watch->SwitchFrame == INDEX_NONE)
			{
				Watch->SwitchFrame = Watch->Frames;
				Watch->FadeBeforeSwitch = Watch->Last;
			}
			else if (bSwitched && Watch->MeanAtStart < 0.0 && Fade > 0.0f)
			{
				Watch->MeanAtStart = MeanDeviation(W);
			}
			Watch->Lowest = FMath::Min(Watch->Lowest, Fade);
			Watch->Last = Fade;
			++Watch->Frames;
			const bool bDone = bSwitched && (Fade >= 1.0f || !W->GetQualityTier().bVolume);
			return bDone || Watch->Frames >= MaxFrames;
		}));
		Then([Test, Level, Watch, From]()
		{
			const bool bLayoutChanged = Watch->Lowest < 0.5f;
			Test->AddInfo(FString::Printf(TEXT("quality %d -> %d: switched after %d frames (fade just before %.2f, lowest %.2f), largest step %.3f of %.3f allowed; the new grid's mean deviation at its start %.1f cm/s"),
				*From, Level, Watch->SwitchFrame, Watch->FadeBeforeSwitch, Watch->Lowest, Watch->WorstStep, Watch->WorstAllowed, Watch->MeanAtStart));
			Test->TestTrue(FString::Printf(TEXT("quality %d: the wind takes the level"), Level), Watch->SwitchFrame != INDEX_NONE);
			Test->TestTrue(FString::Printf(TEXT("quality %d: the volume fades, it never jumps"), Level), Watch->WorstStep <= Watch->WorstAllowed);
			Test->TestFalse(FString::Printf(TEXT("quality %d: it fades out before the grids change"), Level), Watch->bRoseBeforeSwitch);
			if (bLayoutChanged)
			{
				Test->TestTrue(FString::Printf(TEXT("quality %d: the grids change only once it is out"), Level), Watch->FadeBeforeSwitch <= 0.05f || *From == 0);
			}
		});
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDreamWindFluidQualityTest, "DreamWind.Gpu.FluidQuality", DreamWindFluidTests::Flags)
bool FDreamWindFluidQualityTest::RunTest(const FString& Parameters)
{
	using namespace DreamWindFluidTests;
	if (!FApp::CanEverRender() || !GEditor)
	{
		AddWarning(TEXT("Needs the editor with a renderer (not -nullrhi)."));
		return true;
	}
	if (!FPackageName::DoesPackageExist(DreamWorld::Setup::WindTestMapPackage))
	{
		AddError(TEXT("The wind field test level is missing: run Setup -WindFieldTestMap."));
		return false;
	}
	GetMutableDefault<UEditorPerformanceSettings>()->bThrottleCPUWhenNotForeground = false;
	SetCVar(TEXT("DreamWind.Fluid"), 1);
	SetCVar(TEXT("DreamWind.Fluid.DistanceField"), 1);
	SetCVar(TEXT("DreamWind.RuntimeTerrain"), 0);
	SetCVar(TEXT("DreamWind.Occlusion"), 0);
	SetCVar(TEXT("r.DreamWind.Quality"), 2);
	FAutomationEditorCommonUtils::LoadMap(DreamWorld::Setup::WindTestMapPackage);
	WaitFrames(10);
	Then([]()
	{
		for (FLevelEditorViewportClient* C : GEditor->GetLevelViewportClients())
		{
			if (C && C->IsPerspective())
			{
				C->SetRealtime(true);
				C->SetViewLocation(GridCenter + FVector(-2000.0, 0.0, 1500.0));
				C->SetViewRotation(FRotator(-35.0, 0.0, 0.0));
				C->Invalidate();
			}
		}
		if (UDreamWindSubsystem* W = Wind())
		{
			W->OverrideVolumeCenter(GridCenter, true);
			FDreamWindGlobalWind Steady = W->GetGlobalWind();
			Steady.Direction = DreamWorld::Setup::WindTest::WindDirection;
			Steady.Speed = DreamWorld::Setup::WindTest::WindSpeed;
			Steady.GustStrength = 0.0f;
			Steady.TurbulenceStrength = 0.0f;
			W->SetGlobalWind(Steady, 0.0f);
		}
		GShaderCompilingManager->FinishAllCompilation();
	});
	WaitFrames(300);
	auto CheckGrid = [this](const TCHAR* What, bool bFar, const FIntVector& Size, float Cell)
	{
		TArray<FFloat16Color> Texels;
		FIntVector ReadSize = FIntVector::ZeroValue;
		FVector4f V0, V1;
		const bool bRead = Wind() && Wind()->ReadVolume(bFar, Texels, ReadSize, V0, V1);
		const float ReadCell = bRead ? 1.0f / (V1.X * float(ReadSize.X)) : 0.0f;
		AddInfo(FString::Printf(TEXT("%s: %s %dx%dx%d at %.0f cm"), What, bFar ? TEXT("far") : TEXT("near"), ReadSize.X, ReadSize.Y, ReadSize.Z, ReadCell));
		TestTrue(FString::Printf(TEXT("%s: the %s volume is %dx%dx%d at %.0f cm"), What, bFar ? TEXT("far") : TEXT("near"), Size.X, Size.Y, Size.Z, Cell),
			bRead && ReadSize == Size && FMath::IsNearlyEqual(ReadCell, Cell, 1.0f));
	};
	Then([this, CheckGrid]()
	{
		const FDreamWindQualityTier& T = Wind()->GetQualityTier();
		TestEqual(TEXT("high: the wind runs at it"), T.Level, 2);
		TestEqual(TEXT("high: in full"), Wind()->GetVolumeFade(), 1.0f);
		CheckGrid(TEXT("high"), false, T.NearResolution, T.NearCell);
		CheckGrid(TEXT("high"), true, T.FarResolution, T.FarCell);
	});

	// Epic: the volume fades out, the finer grids start where the consumers had the wind without them, and fade in; the
	// flow round the house as at high.
	SwitchLevel(this, 3);
	WaitFrames(300);
	Then([this, CheckGrid]()
	{
		const FDreamWindQualityTier& T = Wind()->GetQualityTier();
		CheckGrid(TEXT("epic"), false, T.NearResolution, T.NearCell);
		CheckGrid(TEXT("epic"), true, T.FarResolution, T.FarCell);
	});
	CheckAveraged(this, TEXT("epic"));

	SwitchLevel(this, 4);
	WaitFrames(300);
	Then([this, CheckGrid]()
	{
		const FDreamWindQualityTier& T = Wind()->GetQualityTier();
		CheckGrid(TEXT("cinematic"), false, T.NearResolution, T.NearCell);
		CheckGrid(TEXT("cinematic"), true, T.FarResolution, T.FarCell);
	});
	CheckAveraged(this, TEXT("cinematic"));

	// Medium: one coarse grid and the runtime terrain map; low: neither.
	Then([]() { SetCVar(TEXT("DreamWind.RuntimeTerrain"), -1); });
	SwitchLevel(this, 1);
	WaitFrames(200);
	TSharedRef<uint32> Captures = MakeShared<uint32>(0);
	Then([this, CheckGrid, Captures]()
	{
		const FDreamWindQualityTier& T = Wind()->GetQualityTier();
		CheckGrid(TEXT("medium"), false, T.NearResolution, T.NearCell);
		TArray<FFloat16Color> Texels;
		FIntVector Size;
		FVector4f V0, V1;
		TestFalse(TEXT("medium: no far level"), Wind()->ReadVolume(true, Texels, Size, V0, V1));
		FVolume V;
		V.Read();
		TestTrue(TEXT("medium: the house is walls in the coarse grid"), V.Wall(DreamWorld::Setup::WindTest::HouseCenter) > 0.5f);
		*Captures = Wind()->GetTerrainCaptureCount();
	});
	WaitFrames(60);
	Then([this, Captures]()
	{
		TestTrue(FString::Printf(TEXT("medium: the runtime terrain map runs (%u -> %u captures)"), *Captures, Wind()->GetTerrainCaptureCount()), Wind()->GetTerrainCaptureCount() > *Captures);
	});
	SwitchLevel(this, 0);
	WaitFrames(10);
	Then([this, Captures]()
	{
		TestFalse(TEXT("low: no volume"), Wind()->IsVolumeActive());
		TestFalse(TEXT("low: no fluid"), Wind()->IsFluidActive());
		*Captures = Wind()->GetTerrainCaptureCount();
	});
	WaitFrames(60);
	Then([this, Captures]()
	{
		TestEqual(TEXT("low: no runtime terrain map"), Wind()->GetTerrainCaptureCount(), *Captures);
	});
	// and back: from nothing, the grids start at once and fade in
	SwitchLevel(this, 2);
	Then([this]()
	{
		TestTrue(TEXT("back at high: the fluid runs"), Wind()->IsFluidActive() && Wind()->GetVolumeFade() >= 1.0f);
	});

	Then([]()
	{
		SetCVar(TEXT("DreamWind.Fluid.DistanceField"), -1);
		SetCVar(TEXT("DreamWind.Fluid"), -1);
		SetCVar(TEXT("DreamWind.RuntimeTerrain"), -1);
		SetCVar(TEXT("DreamWind.Occlusion"), -1);
		SetCVar(TEXT("r.DreamWind.Quality"), -1);
		if (UDreamWindSubsystem* W = Wind())
		{
			W->OverrideVolumeCenter(FVector::ZeroVector, false);
		}
		FAutomationEditorCommonUtils::CreateNewMap();
	});
	WaitFrames(2);
	Then([]() { CollectGarbage(GARBAGE_COLLECTION_KEEPFLAGS); });
	return true;
}

// ---- A play session ------------------------------------------------------------------------------------------------
// While it plays, only it solves and draws the wind's volumes; the editor world's materials leave them alone. When it
// ends the editor world solves them afresh: nothing of the play session's wind (a blast) is left in them.

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDreamWindFluidPIETest, "DreamWind.Gpu.FluidPlayInEditor", DreamWindFluidTests::Flags)
bool FDreamWindFluidPIETest::RunTest(const FString& Parameters)
{
	using namespace DreamWindFluidTests;
	if (!FApp::CanEverRender() || !GEditor)
	{
		AddWarning(TEXT("Needs the editor with a renderer (not -nullrhi)."));
		return true;
	}
	if (!FPackageName::DoesPackageExist(DreamWorld::Setup::WindTestMapPackage))
	{
		AddError(TEXT("The wind field test level is missing: run Setup -WindFieldTestMap."));
		return false;
	}
	GetMutableDefault<UEditorPerformanceSettings>()->bThrottleCPUWhenNotForeground = false;
	SetCVar(TEXT("DreamWind.Fluid"), 1);
	SetCVar(TEXT("r.DreamWind.Quality"), 2);
	FAutomationEditorCommonUtils::LoadMap(DreamWorld::Setup::WindTestMapPackage);
	WaitFrames(10);
	Then([]()
	{
		for (FLevelEditorViewportClient* C : GEditor->GetLevelViewportClients())
		{
			if (C && C->IsPerspective())
			{
				C->SetRealtime(true);
				C->SetViewLocation(GridCenter + FVector(-2000.0, 0.0, 1500.0));
				C->SetViewRotation(FRotator(-35.0, 0.0, 0.0));
				C->Invalidate();
			}
		}
		if (UDreamWindSubsystem* W = Wind())
		{
			W->OverrideVolumeCenter(GridCenter, true);
		}
		GShaderCompilingManager->FinishAllCompilation();
	});
	WaitFrames(120);

	// In the open upwind of the house, away from the clearing's bursts.
	const FVector Blast(-1000.0, -600.0, 150.0);
	auto Peak = [Blast](UDreamWindSubsystem* W)
	{
		TArray<FFloat16Color> Texels;
		FIntVector Size;
		FVector4f V0, V1;
		if (!W || !W->ReadVolume(false, Texels, Size, V0, V1))
		{
			return -1.0f;
		}
		const FVector Extent(1.0 / V1.X, 1.0 / V1.Y, 1.0 / V1.Z);
		const FVector Min = FVector(V0.X, V0.Y, V0.Z) - Extent * 0.5;
		const FVector Cell = Extent / FVector(Size);
		float Best = 0.0f;
		for (int32 Z = 0; Z < Size.Z; ++Z)
		{
			for (int32 Y = 0; Y < Size.Y; ++Y)
			{
				for (int32 X = 0; X < Size.X; ++X)
				{
					const FVector P = Min + (FVector(X, Y, Z) + 0.5) * Cell;
					if (FVector::Dist(P, Blast) < 600.0)
					{
						const FFloat16Color& T = Texels[(Z * Size.Y + Y) * Size.X + X];
						Best = FMath::Max(Best, FVector3f(T.R.GetFloat(), T.G.GetFloat(), T.B.GetFloat()).Size());
					}
				}
			}
		}
		return Best;
	};
	auto PlayWind = []() { return GEditor && GEditor->PlayWorld ? UDreamWindSubsystem::Get(GEditor->PlayWorld.Get()) : nullptr; };

	TSharedRef<uint32> EditorSteps = MakeShared<uint32>(0);
	TSharedRef<float> Before = MakeShared<float>(0.0f);
	TSharedRef<float> InPlay = MakeShared<float>(0.0f);
	Then([this, Peak, Before]()
	{
		TestTrue(TEXT("the editor world runs the fluid with nothing playing"), Wind() && Wind()->IsFluidActive());
		*Before = Peak(Wind());
	});
	Then([]()
	{
		FRequestPlaySessionParams Params;
		Params.WorldType = EPlaySessionWorldType::PlayInEditor;
		GEditor->RequestPlaySession(Params);
	});
	ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand([]() { return GEditor->PlayWorld != nullptr; }));
	WaitFrames(2);
	Then([PlayWind, EditorSteps]()
	{
		if (UDreamWindSubsystem* W = PlayWind())
		{
			W->OverrideVolumeCenter(GridCenter, true);
		}
		*EditorSteps = Wind() ? Wind()->GetFluidStepCount() : 0;
	});
	WaitFrames(90);
	Then([this, PlayWind, EditorSteps]()
	{
		UDreamWindSubsystem* W = PlayWind();
		TestTrue(TEXT("the play session runs the fluid"), W && W->IsFluidActive() && W->GetVolumeFade() >= 1.0f);
		TestFalse(TEXT("the editor world's materials leave the volumes alone while it plays"), Wind() && Wind()->IsVolumeActive());
		TestFalse(TEXT("nor does it solve them"), Wind() && Wind()->IsFluidActive());
		if (W)
		{
			W->AddExplosion(FVector(-1000.0, -600.0, 150.0), 400.0f, 2000.0f, 0.25f);
		}
	});
	WaitFrames(6);
	Then([PlayWind, Peak, InPlay]() { *InPlay = Peak(PlayWind()); });
	ADD_LATENT_AUTOMATION_COMMAND(FEndPlayMapCommand());
	ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand([]() { return GEditor->PlayWorld == nullptr; }));
	WaitFrames(3);
	Then([this, Peak, Before, InPlay, EditorSteps]()
	{
		const float After = Peak(Wind());
		AddInfo(FString::Printf(TEXT("the strongest deviation within 6 m of the blast: before the play session %.0f cm/s, in it just after the blast %.0f, back in the editor %.0f"), *Before, *InPlay, After));
		TestTrue(TEXT("back in the editor, it runs the fluid again"), Wind() && Wind()->IsFluidActive() && Wind()->GetFluidStepCount() > *EditorSteps);
		TestTrue(TEXT("the play session's blast was in its volume"), *InPlay > 500.0f);
		TestTrue(TEXT("and none of it is left in the editor's"), After >= 0.0f && After < FMath::Max(*Before * 2.0f, 0.2f * *InPlay));
	});
	Then([]()
	{
		SetCVar(TEXT("DreamWind.Fluid"), -1);
		SetCVar(TEXT("r.DreamWind.Quality"), -1);
		if (UDreamWindSubsystem* W = Wind())
		{
			W->OverrideVolumeCenter(FVector::ZeroVector, false);
		}
		FAutomationEditorCommonUtils::CreateNewMap();
	});
	WaitFrames(2);
	Then([]() { CollectGarbage(GARBAGE_COLLECTION_KEEPFLAGS); });
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
