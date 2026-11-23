// Copyright DreamMoon. All Rights Reserved.

// The runtime terrain wind map on the wind field test level, in the editor world: the GPU's map has the house's lee
// downwind of it and none upwind, the CPU grid agrees, and when the house moves the lee follows it within the refresh.
// Needs the editor with a renderer.

#include "CoreMinimal.h"
#include "DreamWindSettings.h"
#include "DreamWindSubsystem.h"
#include "DreamWindTestMap.h"

#include "Editor.h"
#include "Editor/EditorPerformanceSettings.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "LevelEditorViewport.h"
#include "Misc/App.h"
#include "Misc/AutomationTest.h"
#include "Misc/PackageName.h"
#include "ShaderCompiler.h"
#include "TextureResource.h"
#include "Tests/AutomationCommon.h"
#include "Tests/AutomationEditorCommon.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace DreamWindRuntimeTerrainTests
{
	constexpr EAutomationTestFlags Flags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter;

	UWorld* EditorWorld()
	{
		return GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
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

	/** The near level's wind shadow heights (cm) at world points, read back from the GPU's map; NaN when it does not run. */
	TArray<float> GpuShadow(const TArray<FVector2D>& Points)
	{
		TArray<float> Out;
		Out.Init(TNumericLimits<float>::Lowest(), Points.Num());
		const UDreamWindSubsystem* Wind = UDreamWindSubsystem::Get(EditorWorld());
		if (!Wind)
		{
			return Out;
		}
		const FDreamWindFrameData& Frame = Wind->GetFrameData();
		UTextureRenderTarget2D* Map = Frame.L1NearDerived.Get();
		FTextureRenderTargetResource* Resource = Map ? Map->GameThread_GetRenderTargetResource() : nullptr;
		if (!Resource || Frame.L1Near.W < 0.5f)
		{
			return Out;
		}
		TArray<FLinearColor> Pixels;
		if (!Resource->ReadLinearColorPixels(Pixels) || Pixels.Num() != Map->SizeX * Map->SizeY)
		{
			return Out;
		}
		const int32 N = Map->SizeX;
		const double Texel = 1.0 / (double(Frame.L1Near.Z) * N);
		for (int32 i = 0; i < Points.Num(); ++i)
		{
			const int32 X = ((int32(FMath::FloorToDouble(Points[i].X / Texel)) % N) + N) % N;
			const int32 Y = ((int32(FMath::FloorToDouble(Points[i].Y / Texel)) % N) + N) % N;
			Out[i] = Pixels[Y * N + X].A;
		}
		return Out;
	}

	float CpuShadow(const FVector2D& Point)
	{
		const UDreamWindSubsystem* Wind = UDreamWindSubsystem::Get(EditorWorld());
		const FDreamWindTerrainGridData* Grid = Wind ? Wind->GetFrameData().TerrainGrid.Get() : nullptr;
		return Grid && Grid->Weight(Point.X, Point.Y) > 0.0f ? Grid->SampleDerived(Point.X, Point.Y).W : TNumericLimits<float>::Lowest();
	}

	AStaticMeshActor* House()
	{
		for (TActorIterator<AStaticMeshActor> It(EditorWorld()); It; ++It)
		{
			if (It->GetActorLabel() == TEXT("House"))
			{
				return *It;
			}
		}
		return nullptr;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDreamWindRuntimeTerrainTest, "DreamWind.Gpu.RuntimeTerrain", DreamWindRuntimeTerrainTests::Flags)
bool FDreamWindRuntimeTerrainTest::RunTest(const FString& Parameters)
{
	using namespace DreamWindRuntimeTerrainTests;
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
	FAutomationEditorCommonUtils::LoadMap(DreamWorld::Setup::WindTestMapPackage);
	WaitFrames(10);
	Then([]()
	{
		for (FLevelEditorViewportClient* C : GEditor->GetLevelViewportClients())
		{
			if (C && C->IsPerspective())
			{
				C->SetRealtime(true);
				C->SetViewLocation(CameraLocation);
				C->SetViewRotation(CameraRotation);
				C->Invalidate();
			}
		}
		GShaderCompilingManager->FinishAllCompilation();
	});
	WaitFrames(90);

	// 6 m downwind of the house (the wind along +x): deep in its lee; 6 m upwind: open; the ground there is at 0.
	const double LeeX = HouseCenter.X + HouseSize.X * 0.5 + 600.0;
	const double UpX = HouseCenter.X - HouseSize.X * 0.5 - 600.0;
	const TArray<FVector2D> Before = { FVector2D(LeeX, HouseCenter.Y), FVector2D(UpX, HouseCenter.Y) };
	Then([this, Before]()
	{
		const TArray<float> S = GpuShadow(Before);
		const float Expected = HouseSize.Z - UDreamWindSettings::Get()->OcclusionSlope * 600.0f;
		AddInfo(FString::Printf(TEXT("GPU shadow height 6 m behind the house %.0f cm (the wedge says %.0f), 6 m before it %.0f cm"), S[0], Expected, S[1]));
		TestTrue(TEXT("the runtime map runs in the editor world"), S[0] > TNumericLimits<float>::Lowest());
		TestTrue(TEXT("behind the house its lee stands about as high as the wedge"), FMath::Abs(S[0] - Expected) < 100.0f);
		TestTrue(TEXT("before it, nothing shelters"), S[1] < 50.0f);
		const float Cpu = CpuShadow(Before[0]);
		AddInfo(FString::Printf(TEXT("CPU grid there %.0f cm"), Cpu));
		// (4 m cells against 1 m texels: the same lee, a cell's worth of wedge apart at most.)
		TestTrue(TEXT("the CPU grid agrees with the GPU's map behind the house"), FMath::Abs(Cpu - S[0]) < 0.25f * HouseSize.Z);
	});

	// The house moves 30 m across the wind: its lee goes with it within the refresh (no dirty call: the strips find it).
	Then([this]()
	{
		if (AStaticMeshActor* H = House())
		{
			H->GetRootComponent()->SetMobility(EComponentMobility::Movable);
			H->SetActorLocation(H->GetActorLocation() + FVector(0.0, 3000.0, 0.0));
		}
		else
		{
			AddError(TEXT("no House in the level"));
		}
	});
	WaitFrames(120);
	const TArray<FVector2D> After = { FVector2D(LeeX, HouseCenter.Y), FVector2D(LeeX, HouseCenter.Y + 3000.0) };
	Then([this, After]()
	{
		const TArray<float> S = GpuShadow(After);
		AddInfo(FString::Printf(TEXT("after the move: the old lee %.0f cm, the new lee %.0f cm"), S[0], S[1]));
		TestTrue(TEXT("where the house stood, the lee has gone"), S[0] < 50.0f);
		TestTrue(TEXT("behind where it stands now, the lee has come"), S[1] > 0.5f * HouseSize.Z);
		const float Cpu = CpuShadow(After[1]);
		AddInfo(FString::Printf(TEXT("CPU grid at the new lee %.0f cm"), Cpu));
		TestTrue(TEXT("the CPU grid follows it too"), Cpu > 0.5f * HouseSize.Z);
		FAutomationEditorCommonUtils::CreateNewMap();
	});
	WaitFrames(2);
	Then([]() { CollectGarbage(GARBAGE_COLLECTION_KEEPFLAGS); });
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
