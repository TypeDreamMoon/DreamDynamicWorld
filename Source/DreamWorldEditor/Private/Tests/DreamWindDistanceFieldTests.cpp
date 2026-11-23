// Copyright DreamMoon. All Rights Reserved.

// The engine's global distance field as the wind reads it (the fluid wind's obstacles): a slice through the wind test
// level's house, with Lumen as the project has it, with Lumen and distance field AO off, and then with the wind asking for
// it. Needs the editor with a renderer.

#include "CoreMinimal.h"
#include "DreamWindDistanceField.h"
#include "DreamWindSubsystem.h"
#include "DreamWindTestMap.h"

#include "Editor.h"
#include "Editor/EditorPerformanceSettings.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"
#include "LevelEditorViewport.h"
#include "Misc/App.h"
#include "Misc/AutomationTest.h"
#include "Misc/PackageName.h"
#include "ShaderCompiler.h"
#include "TextureResource.h"
#include "Tests/AutomationCommon.h"
#include "Tests/AutomationEditorCommon.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace DreamWindDistanceFieldTests
{
	constexpr EAutomationTestFlags Flags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter;

	/** What a slice said. */
	struct FSlice
	{
		bool bWritten = false;
		/** The pass wrote the texture (its alpha is 1 everywhere it ran). */
		bool bShaderRan = false;
		int32 Clipmaps = 0;
		float Centre = 0.0f;   // m, the house's middle
		float Corner = 0.0f;   // m, the slice's corner, some ten metres off the house
	};

	struct FState
	{
		TObjectPtr<UTextureRenderTarget2D> Target;
		uint32 Awaited = 0;
		int32 Frames = 0;
		TMap<FString, FSlice> Slices;
		TMap<FString, int32> SavedCVars;
	};

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

	void SetCVar(FState& State, const TCHAR* Name, int32 Value)
	{
		if (IConsoleVariable* V = IConsoleManager::Get().FindConsoleVariable(Name))
		{
			if (!State.SavedCVars.Contains(Name))
			{
				State.SavedCVars.Add(Name, V->GetInt());
			}
			V->Set(Value, ECVF_SetByCode);
		}
	}

	/** A slice through the house at 1.5 m, 30 m wide, read back under Name. */
	void Probe(const TSharedRef<FState>& State, const FString& Name)
	{
		Then([State]()
		{
			UDreamWindSubsystem* Wind = UDreamWindSubsystem::Get(EditorWorld());
			const FVector House = DreamWorld::Setup::WindTest::HouseCenter;
			State->Awaited = Wind ? Wind->RequestDistanceFieldProbe(State->Target, FVector(House.X, House.Y, 150.0), 3000.0f) : 0;
			State->Frames = 0;
		});
		ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand([State]()
		{
			const UDreamWindSubsystem* Wind = UDreamWindSubsystem::Get(EditorWorld());
			return State->Awaited == 0 || !Wind || Wind->GetDistanceProbeCount() >= State->Awaited || ++State->Frames > 240;
		}));
		WaitFrames(2);
		Then([State, Name]()
		{
			FSlice Slice;
			FTextureRenderTargetResource* Resource = State->Target->GameThread_GetRenderTargetResource();
			TArray<FFloat16Color> Pixels;
			const UDreamWindSubsystem* Wind = UDreamWindSubsystem::Get(EditorWorld());
			Slice.bWritten = Wind && State->Awaited > 0 && Wind->GetDistanceProbeCount() >= State->Awaited;
			if (Slice.bWritten && Resource && Resource->ReadFloat16Pixels(Pixels) && Pixels.Num() == State->Target->SizeX * State->Target->SizeY)
			{
				const int32 N = State->Target->SizeX;
				const FFloat16Color& C = Pixels[(N / 2) * N + N / 2];
				const FFloat16Color& K = Pixels[1 * N + 1];
				Slice.bShaderRan = C.A.GetFloat() > 0.5f;
				Slice.Clipmaps = FMath::RoundToInt(C.G.GetFloat());
				Slice.Centre = C.R.GetFloat();
				Slice.Corner = K.R.GetFloat();
			}
			State->Slices.Add(Name, Slice);
		});
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDreamWindDistanceFieldProbeTest, "DreamWind.Gpu.DistanceFieldProbe", DreamWindDistanceFieldTests::Flags)
bool FDreamWindDistanceFieldProbeTest::RunTest(const FString& Parameters)
{
	using namespace DreamWindDistanceFieldTests;
	if (!FApp::CanEverRender() || !GEditor)
	{
		AddWarning(TEXT("Needs the editor with a renderer (not -nullrhi)."));
		return true;
	}
	if (!FPackageName::DoesPackageExist(DreamWorld::Setup::WindTestMapPackage))
	{
		AddError(TEXT("The wind test level is missing: run Setup -WindTestMap."));
		return false;
	}
	AddInfo(FString::Printf(TEXT("Global distance field supported here: %s"), DreamWind::DistanceField::IsSupported() ? TEXT("yes") : TEXT("no (Generate Mesh Distance Fields off, or no SM5)")));
	GetMutableDefault<UEditorPerformanceSettings>()->bThrottleCPUWhenNotForeground = false;
	FAutomationEditorCommonUtils::LoadMap(DreamWorld::Setup::WindTestMapPackage);

	TSharedRef<FState> State = MakeShared<FState>();
	State->Target = NewObject<UTextureRenderTarget2D>(GetTransientPackage(), NAME_None, RF_Transient);
	State->Target->bSupportsUAV = true;
	State->Target->InitCustomFormat(64, 64, PF_FloatRGBA, true);
	State->Target->UpdateResourceImmediate(true);
	State->Target->AddToRoot();

	WaitFrames(10);
	Then([]()
	{
		for (FLevelEditorViewportClient* C : GEditor->GetLevelViewportClients())
		{
			if (C && C->IsPerspective())
			{
				C->SetRealtime(true);
				C->SetViewLocation(DreamWorld::Setup::WindTest::CameraLocation);
				C->SetViewRotation(DreamWorld::Setup::WindTest::CameraRotation);
				C->Invalidate();
			}
		}
		GShaderCompilingManager->FinishAllCompilation();
	});
	WaitFrames(60);

	// 1. As the project has it.
	Probe(State, TEXT("Project"));

	// 2. Lumen and distance field AO off, nothing else asking for the global distance field.
	Then([State]()
	{
		SetCVar(*State, TEXT("r.DynamicGlobalIlluminationMethod"), 0);
		SetCVar(*State, TEXT("r.ReflectionMethod"), 2);
		SetCVar(*State, TEXT("r.DistanceFieldAO"), 0);
	});
	WaitFrames(30);
	Probe(State, TEXT("NoLumen"));

	// 3. The wind asks for it.
	Then([]() { DreamWind::DistanceField::Request(true); });
	WaitFrames(30);
	Probe(State, TEXT("Requested"));

	Then([this, State]()
	{
		DreamWind::DistanceField::Request(false);
		for (const TPair<FString, int32>& Saved : State->SavedCVars)
		{
			if (IConsoleVariable* V = IConsoleManager::Get().FindConsoleVariable(*Saved.Key))
			{
				V->Set(Saved.Value, ECVF_SetByCode);
			}
		}
		for (const TPair<FString, FSlice>& S : State->Slices)
		{
			AddInfo(FString::Printf(TEXT("%s: written %s (shader %s), %d clipmaps, %.2f m at the house's middle, %.2f m at the slice's corner"),
				*S.Key, S.Value.bWritten ? TEXT("yes") : TEXT("no"), S.Value.bShaderRan ? TEXT("ran") : TEXT("did not run"), S.Value.Clipmaps, S.Value.Centre, S.Value.Corner));
		}
		const IConsoleVariable* RayTracing = IConsoleManager::Get().FindConsoleVariable(TEXT("r.RayTracing"));
		AddInfo(FString::Printf(TEXT("Hardware ray tracing (r.RayTracing) %d: Lumen with it does not ask for the global distance field."), RayTracing ? RayTracing->GetInt() : -1));
		if (DreamWind::DistanceField::IsSupported())
		{
			const FSlice& Project = State->Slices.FindRef(TEXT("Project"));
			const FSlice& Requested = State->Slices.FindRef(TEXT("Requested"));
			TestTrue(TEXT("the probe pass runs"), Project.bShaderRan && Requested.bShaderRan);
			TestTrue(FString::Printf(TEXT("asked for by the wind, the global distance field is there without Lumen (%d clipmaps)"), Requested.Clipmaps), Requested.Clipmaps > 0);
			TestTrue(FString::Printf(TEXT("the house reads as solid (%.2f m)"), Requested.Centre), Requested.Centre < 0.3f);
			TestTrue(FString::Printf(TEXT("and the slice's corner, 1.5 m over open ground, as open (%.2f m)"), Requested.Corner), Requested.Corner > 1.0f);
		}
		State->Target->RemoveFromRoot();
		FAutomationEditorCommonUtils::CreateNewMap();
	});
	WaitFrames(2);
	Then([]() { CollectGarbage(GARBAGE_COLLECTION_KEEPFLAGS); });
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
