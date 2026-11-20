// Copyright DreamMoon. All Rights Reserved.
//
// Render checks of the sky's cloud renderer (DreamSky.Clouds 2) in the views the game tests cannot reach: a play in
// editor session (its game view, a second local player's split screen, a mirror sphere
// reflecting the sky light), the level editor's own viewport, the two drawing together, and (DreamQA.SkyCrash.SceneCapture)
// a 2D and a cube scene capture. They need a renderer: run them in the editor (not
// -nullrhi), e.g.
//   UnrealEditor.exe <project> -ExecCmds="Automation RunTests DreamQA.SkyRender" -TestExit="Automation Test Queue Empty" -unattended
// (DreamQA.SkyCrash.* on its own: it checks a crash).
// They are named DreamQA.* so the -nullrhi test runner (Invoke-DreamDynamicWorldTests.ps1) leaves them out.
//
// Each view is read back and measured on its sky part: how much of it is cloud (white or grey against the blue), how
// noisy it is (the mean difference between a pixel's luminance and its neighbours'), and how alike two pictures of the
// same view are (the correlation of their luminance). The sky renderer's toon look and the old toon dome draw the same
// clouds, so the two must agree in coverage and correlate; a view that does not accumulate its history shows as noise.
// Every picture is saved under <project>/Saved/DreamQA/.

#include "CoreMinimal.h"
#include "DreamWeatherSettings.h"
#include "DreamWorldSubsystem.h"

#include "Camera/PlayerCameraManager.h"
#include "Components/SkyLightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Editor.h"
#include "Editor/EditorPerformanceSettings.h"
#include "Engine/GameViewportClient.h"
#include "Engine/SceneCapture2D.h"
#include "Engine/SceneCaptureCube.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Engine/TextureRenderTargetCube.h"
#include "Engine/World.h"
#include "Components/SceneCaptureComponent2D.h"
#include "Components/SceneCaptureComponentCube.h"
#include "GameFramework/PlayerController.h"
#include "HAL/FileManager.h"
#include "HAL/IConsoleManager.h"
#include "ImageUtils.h"
#include "Kismet/GameplayStatics.h"
#include "LevelEditorViewport.h"
#include "Materials/Material.h"
#include "Materials/MaterialExpressionConstant.h"
#include "Materials/MaterialExpressionConstant3Vector.h"
#include "Misc/App.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "ShaderCompiler.h"
#include "Tests/AutomationCommon.h"
#include "Tests/AutomationEditorCommon.h"
#include "UObject/UObjectIterator.h"
#include "UnrealClient.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace DreamQA
{
	constexpr EAutomationTestFlags Flags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter;
	static const TCHAR* SkyTestMap = TEXT("/DreamDynamicWorld/Sky/Maps/Lvl_DreamSkyTest");
	static const FVector CameraAt(600.0, 600.0, 260.0);
	static const FRotator CameraLooks(8.0, 220.0, 0.0);

	/** A way of drawing the clouds: DreamSky.Clouds and DreamSky.CloudRender.Look, and a short name for the pictures. */
	struct FMode
	{
		const TCHAR* Name;
		int32 Clouds;
		int32 Look;
	};

	struct FPicture
	{
		TArray<FColor> Pixels;
		FIntPoint Size = FIntPoint::ZeroValue;
		bool IsValid() const { return Size.X > 0 && Size.Y > 0 && Pixels.Num() == Size.X * Size.Y; }
	};

	struct FStats
	{
		float Coverage = 0.0f; // share of the region that is cloud
		float Noise = 0.0f;    // mean |luminance - mean of the 4 neighbours| (8-bit levels)
		float Mean = 0.0f;     // mean luminance
		float Deviation = 0.0f;
	};

	float Luma(const FColor& C)
	{
		return 0.2126f * C.R + 0.7152f * C.G + 0.0722f * C.B;
	}

	FStats Measure(const FPicture& P, const FIntRect& R)
	{
		FStats S;
		double Sum = 0.0;
		double Sum2 = 0.0;
		double NoiseSum = 0.0;
		int32 Count = 0;
		int32 Clouds = 0;
		for (int32 Y = FMath::Max(R.Min.Y, 1); Y < FMath::Min(R.Max.Y, P.Size.Y - 1); ++Y)
		{
			for (int32 X = FMath::Max(R.Min.X, 1); X < FMath::Min(R.Max.X, P.Size.X - 1); ++X)
			{
				const FColor& C = P.Pixels[X + Y * P.Size.X];
				const float L = Luma(C);
				const float Around = 0.25f * (Luma(P.Pixels[X - 1 + Y * P.Size.X]) + Luma(P.Pixels[X + 1 + Y * P.Size.X])
					+ Luma(P.Pixels[X + (Y - 1) * P.Size.X]) + Luma(P.Pixels[X + (Y + 1) * P.Size.X]));
				NoiseSum += FMath::Abs(L - Around);
				Sum += L;
				Sum2 += double(L) * L;
				// Cloud: hardly bluer than red (white, grey, lavender) and not dark; the sky behind is clearly blue by day.
				if (int32(C.B) - int32(C.R) < 30 && L > 45.0f)
				{
					++Clouds;
				}
				++Count;
			}
		}
		if (Count > 0)
		{
			S.Coverage = float(Clouds) / Count;
			S.Noise = float(NoiseSum / Count);
			S.Mean = float(Sum / Count);
			S.Deviation = float(FMath::Sqrt(FMath::Max(Sum2 / Count - S.Mean * S.Mean, 0.0)));
		}
		return S;
	}

	/** The correlation of two pictures' luminance over a region (pictures of one size). */
	float Correlation(const FPicture& A, const FPicture& B, const FIntRect& R)
	{
		if (A.Size != B.Size)
		{
			return 0.0f;
		}
		double Sa = 0.0, Sb = 0.0, Saa = 0.0, Sbb = 0.0, Sab = 0.0;
		int32 N = 0;
		for (int32 Y = R.Min.Y; Y < FMath::Min(R.Max.Y, A.Size.Y); ++Y)
		{
			for (int32 X = R.Min.X; X < FMath::Min(R.Max.X, A.Size.X); ++X)
			{
				const double La = Luma(A.Pixels[X + Y * A.Size.X]);
				const double Lb = Luma(B.Pixels[X + Y * B.Size.X]);
				Sa += La; Sb += Lb; Saa += La * La; Sbb += Lb * Lb; Sab += La * Lb;
				++N;
			}
		}
		if (N == 0)
		{
			return 0.0f;
		}
		const double Cov = Sab / N - (Sa / N) * (Sb / N);
		const double Va = Saa / N - (Sa / N) * (Sa / N);
		const double Vb = Sbb / N - (Sb / N) * (Sb / N);
		return Va > 1.0e-6 && Vb > 1.0e-6 ? float(Cov / FMath::Sqrt(Va * Vb)) : 0.0f;
	}

	/** The mean absolute luminance change between two pictures over a region. */
	float Change(const FPicture& A, const FPicture& B, const FIntRect& R)
	{
		if (A.Size != B.Size)
		{
			return -1.0f;
		}
		double Sum = 0.0;
		int32 N = 0;
		for (int32 Y = R.Min.Y; Y < FMath::Min(R.Max.Y, A.Size.Y); ++Y)
		{
			for (int32 X = R.Min.X; X < FMath::Min(R.Max.X, A.Size.X); ++X)
			{
				Sum += FMath::Abs(Luma(A.Pixels[X + Y * A.Size.X]) - Luma(B.Pixels[X + Y * B.Size.X]));
				++N;
			}
		}
		return N > 0 ? float(Sum / N) : -1.0f;
	}

	void Save(const FPicture& P, const FString& Name)
	{
		if (!P.IsValid())
		{
			return;
		}
		TArray<FColor> Opaque = P.Pixels;
		for (FColor& C : Opaque)
		{
			C.A = 255;
		}
		TArray64<uint8> Png;
		FImageUtils::PNGCompressImageArray(P.Size.X, P.Size.Y, Opaque, Png);
		FFileHelper::SaveArrayToFile(Png, *FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("DreamQA"), Name + TEXT(".png")));
	}

	FPicture ReadViewport(FViewport* Viewport)
	{
		FPicture P;
		if (Viewport && Viewport->ReadPixels(P.Pixels))
		{
			P.Size = Viewport->GetSizeXY();
		}
		return P;
	}

	FPicture ReadTarget(UTextureRenderTarget2D* Target)
	{
		FPicture P;
		FTextureRenderTargetResource* Resource = Target ? Target->GameThread_GetRenderTargetResource() : nullptr;
		if (Resource && Resource->ReadPixels(P.Pixels))
		{
			P.Size = FIntPoint(Target->SizeX, Target->SizeY);
		}
		return P;
	}

	FPicture ReadCubeFace(UTextureRenderTargetCube* Target, ECubeFace Face)
	{
		FPicture P;
		FTextureRenderTargetResource* Resource = Target ? Target->GameThread_GetRenderTargetResource() : nullptr;
		if (Resource && Resource->ReadPixels(P.Pixels, FReadSurfaceDataFlags(RCM_UNorm, Face)))
		{
			P.Size = FIntPoint(Target->SizeX, Target->SizeX);
		}
		return P;
	}

	void SetClouds(int32 Clouds, int32 Look)
	{
		if (IConsoleVariable* V = IConsoleManager::Get().FindConsoleVariable(TEXT("DreamSky.Clouds")))
		{
			V->Set(Clouds, ECVF_SetByCode);
		}
		if (IConsoleVariable* V = IConsoleManager::Get().FindConsoleVariable(TEXT("DreamSky.CloudRender.Look")))
		{
			V->Set(Look, ECVF_SetByCode);
		}
	}

	/** 15:00 under a cloudy sky, the clock stopped; no lightning (a flash brightens whatever frame it lands in). */
	void SetWorld(UWorld* World)
	{
		GetMutableDefault<UDreamWeatherSettings>()->StrikesPerMinute = 0.0f;
		if (UDreamWorldSubsystem* Clock = World ? World->GetSubsystem<UDreamWorldSubsystem>() : nullptr)
		{
			Clock->SetPaused(true);
			Clock->SetTimeOfDay(15.0f);
		}
		GEngine->Exec(World, TEXT("DreamWeather.Set WP_Cloudy 0"));
	}

	void RecaptureSkyLights(UWorld* World)
	{
		for (TObjectIterator<USkyLightComponent> It; It; ++It)
		{
			if (It->GetWorld() == World)
			{
				It->SetCaptureIsDirty();
				It->RecaptureSky();
			}
		}
	}

	/** A mirror: metallic, nearly smooth, near white. */
	UMaterial* MakeMirrorMaterial()
	{
		UMaterial* M = NewObject<UMaterial>(GetTransientPackage(), TEXT("M_DreamQAMirror"), RF_Transient);
		UMaterialExpressionConstant* Metal = NewObject<UMaterialExpressionConstant>(M);
		Metal->R = 1.0f;
		UMaterialExpressionConstant* Rough = NewObject<UMaterialExpressionConstant>(M);
		Rough->R = 0.03f;
		UMaterialExpressionConstant3Vector* Base = NewObject<UMaterialExpressionConstant3Vector>(M);
		Base->Constant = FLinearColor(0.95f, 0.95f, 0.95f);
		M->GetExpressionCollection().AddExpression(Metal);
		M->GetExpressionCollection().AddExpression(Rough);
		M->GetExpressionCollection().AddExpression(Base);
		M->GetEditorOnlyData()->Metallic.Expression = Metal;
		M->GetEditorOnlyData()->Roughness.Expression = Rough;
		M->GetEditorOnlyData()->BaseColor.Expression = Base;
		M->PostEditChange();
		return M;
	}

	/** Waits Frames engine frames. */
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

	/** The sky part of a view: its upper 40 % (the cameras look a little up, the horizon is lower). */
	FIntRect SkyOf(const FPicture& P, int32 Top = 0, int32 Height = -1)
	{
		const int32 H = Height < 0 ? P.Size.Y : Height;
		return FIntRect(0, Top, P.Size.X, Top + int32(H * 0.4f));
	}

	FString Describe(const FStats& S)
	{
		return FString::Printf(TEXT("cover %.3f noise %.2f mean %.1f deviation %.1f"), S.Coverage, S.Noise, S.Mean, S.Deviation);
	}
}

// ---------------------------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDreamQASkyRenderPIETest, "DreamQA.SkyRender.PlayInEditor", DreamQA::Flags)
bool FDreamQASkyRenderPIETest::RunTest(const FString& Parameters)
{
	using namespace DreamQA;
	if (!FApp::CanEverRender() || !GEditor)
	{
		AddWarning(TEXT("Needs the editor with a renderer (not -nullrhi)."));
		return true;
	}
	GetMutableDefault<UEditorPerformanceSettings>()->bThrottleCPUWhenNotForeground = false;
	FAutomationEditorCommonUtils::LoadMap(SkyTestMap);
	// Only the play session draws (DreamQA.SkyRender.EditorAndPIE has the editor viewport draw with it).
	for (FLevelEditorViewportClient* C : GEditor->GetLevelViewportClients())
	{
		if (C)
		{
			C->SetRealtime(false);
		}
	}
	UMaterial* Mirror = MakeMirrorMaterial();
	Mirror->AddToRoot();

	struct FState
	{
		TMap<FString, FPicture> Pictures;
		TWeakObjectPtr<ASceneCapture2D> Capture;
		TWeakObjectPtr<ASceneCaptureCube> CaptureCube;
		TWeakObjectPtr<AStaticMeshActor> Sphere;
		TObjectPtr<UTextureRenderTarget2D> Target;
		TObjectPtr<UTextureRenderTargetCube> CubeTarget;
		FIntRect SphereRect;
	};
	TSharedRef<FState> State = MakeShared<FState>();
	auto PlayWorld = []() { return GEditor ? GEditor->PlayWorld.Get() : nullptr; };
	auto Viewport = [PlayWorld]() -> FViewport* { UWorld* W = PlayWorld(); return W && W->GetGameViewport() ? W->GetGameViewport()->Viewport : nullptr; };

	ADD_LATENT_AUTOMATION_COMMAND(FStartPIECommand(false));
	ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand([PlayWorld]() { return PlayWorld() != nullptr; }));
	WaitFrames(30);
	Then([PlayWorld]() { SetWorld(PlayWorld()); GShaderCompilingManager->FinishAllCompilation(); });

	// 1. The game view, every look, against the old toon dome.
	const FMode GameLooks[] = { { TEXT("Game_RT"), 2, 0 }, { TEXT("Game_RR"), 2, 1 }, { TEXT("Game_RP"), 2, 2 }, { TEXT("Game_OT"), 0, 0 } };
	for (const FMode& L : GameLooks)
	{
		const FString Name = L.Name;
		const FIntPoint Mode(L.Clouds, L.Look);
		Then([Mode]() { SetClouds(Mode.X, Mode.Y); });
		WaitFrames(90);
		Then([State, Name, Viewport]() { State->Pictures.Add(Name, ReadViewport(Viewport())); Save(State->Pictures[Name], Name); });
	}
	Then([this, State]()
	{
		const FPicture& RT = State->Pictures[TEXT("Game_RT")];
		const FPicture& OT = State->Pictures[TEXT("Game_OT")];
		if (!TestTrue(TEXT("the play in editor view reads back"), RT.IsValid() && OT.IsValid()))
		{
			return;
		}
		const FIntRect Sky = SkyOf(RT);
		for (const TCHAR* Name : { TEXT("Game_RT"), TEXT("Game_RR"), TEXT("Game_RP"), TEXT("Game_OT") })
		{
			AddInfo(FString::Printf(TEXT("%s %dx%d: %s"), Name, RT.Size.X, RT.Size.Y, *Describe(Measure(State->Pictures[Name], Sky))));
		}
		const FStats A = Measure(RT, Sky);
		const FStats B = Measure(OT, Sky);
		const float Corr = Correlation(RT, OT, Sky);
		AddInfo(FString::Printf(TEXT("PIE: renderer toon vs old toon correlation %.3f"), Corr));
		TestTrue(TEXT("PIE: the renderer's toon clouds cover the sky as the old dome's do"), FMath::Abs(A.Coverage - B.Coverage) < 0.35f * FMath::Max(B.Coverage, 0.05f));
		TestTrue(TEXT("PIE: they stand where the old dome's do"), Corr > 0.6f);
		TestTrue(TEXT("PIE: the realistic look is not noisier than the old dome by much"), Measure(State->Pictures[TEXT("Game_RR")], Sky).Noise < 2.0f * B.Noise + 1.0f);
	});

	// 2. A mirror sphere in front of the camera reflects the sky light (its capture of the dome, which shows the renderer's
	// panorama): the clouds in it, the right way round, about as sharp as the old dome's.
	Then([State, PlayWorld, Mirror]()
	{
		UWorld* W = PlayWorld();
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		const FVector At = CameraAt + FRotator(0.0, 220.0, 0.0).Vector() * 450.0 + FVector(0.0, 0.0, 40.0);
		AStaticMeshActor* Sphere = W->SpawnActor<AStaticMeshActor>(At, FRotator::ZeroRotator, Params);
		Sphere->GetStaticMeshComponent()->SetMobility(EComponentMobility::Movable);
		Sphere->GetStaticMeshComponent()->SetStaticMesh(LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Sphere.Sphere")));
		Sphere->GetStaticMeshComponent()->SetMaterial(0, Mirror);
		Sphere->SetActorScale3D(FVector(3.0));
		State->Sphere = Sphere;
		// Where it lands on screen.
		if (APlayerController* PC = W->GetFirstPlayerController())
		{
			FVector2D Centre, Top;
			PC->ProjectWorldLocationToScreen(At, Centre);
			PC->ProjectWorldLocationToScreen(At + FVector(0.0, 0.0, 150.0), Top);
			const int32 Radius = FMath::Max(8, int32((Centre - Top).Size() * 0.85));
			// The upper half: the sky reflected (the lower half reflects the ground).
			State->SphereRect = FIntRect(int32(Centre.X) - Radius, int32(Centre.Y) - Radius, int32(Centre.X) + Radius, int32(Centre.Y));
		}
		GShaderCompilingManager->FinishAllCompilation();
	});
	// The mirror's shaders compile for the new primitive first (taken too soon, the sphere shows the default material).
	WaitFrames(240);
	Then([]() { GShaderCompilingManager->FinishAllCompilation(); });
	WaitFrames(30);
	const FMode MirrorLooks[] = { { TEXT("RT"), 2, 0 }, { TEXT("RR"), 2, 1 }, { TEXT("OT"), 0, 0 }, { TEXT("OR"), 1, 0 } };
	for (const FMode& L : MirrorLooks)
	{
		const FString Name = L.Name;
		const FIntPoint Mode(L.Clouds, L.Look);
		Then([Mode, PlayWorld]()
		{
			SetClouds(Mode.X, Mode.Y);
			if (Mode.X == 1)
			{
				GEngine->Exec(PlayWorld(), TEXT("DreamSky.CloudMaterial /DreamDynamicWorld/Sky/M_DreamVolumetricCloud.M_DreamVolumetricCloud"));
				GShaderCompilingManager->FinishAllCompilation();
			}
		});
		WaitFrames(30);
		Then([PlayWorld]() { RecaptureSkyLights(PlayWorld()); });
		WaitFrames(60);
		Then([State, Name, Viewport]() { State->Pictures.Add(TEXT("Mirror_") + Name, ReadViewport(Viewport())); Save(State->Pictures[TEXT("Mirror_") + Name], TEXT("Mirror_") + Name); });
	}
	Then([this, State]()
	{
		const FIntRect R = State->SphereRect;
		AddInfo(FString::Printf(TEXT("mirror sphere's upper half on screen: %d,%d .. %d,%d"), R.Min.X, R.Min.Y, R.Max.X, R.Max.Y));
		for (const TCHAR* Name : { TEXT("RT"), TEXT("RR"), TEXT("OT"), TEXT("OR") })
		{
			AddInfo(FString::Printf(TEXT("Mirror %s: %s"), Name, *Describe(Measure(State->Pictures[FString(TEXT("Mirror_")) + Name], R))));
		}
		const FPicture& RT = State->Pictures[TEXT("Mirror_RT")];
		const FPicture& OT = State->Pictures[TEXT("Mirror_OT")];
		const FStats A = Measure(RT, R);
		const FStats B = Measure(OT, R);
		const float Corr = Correlation(RT, OT, R);
		AddInfo(FString::Printf(TEXT("mirror: renderer toon vs old toon correlation %.3f"), Corr));
		TestTrue(TEXT("mirror: the reflection varies (clouds in it, not a flat sky colour)"), A.Deviation > 0.5f * B.Deviation && B.Deviation > 2.0f);
		TestTrue(TEXT("mirror: the clouds stand where the old dome's do (the right way round)"), Corr > 0.5f);
		TestTrue(TEXT("mirror: not much blurrier than the old dome's"), A.Noise > 0.5f * B.Noise);
		if (State->Sphere.IsValid())
		{
			State->Sphere->Destroy();
		}
	});

	// 3. Split screen: a second local player; both halves must show the clouds the old dome shows in them.
	Then([this, PlayWorld]()
	{
		UWorld* W = PlayWorld();
		if (UGameViewportClient* Client = W->GetGameViewport())
		{
			Client->SetForceDisableSplitscreen(false);
		}
		APlayerController* Second = UGameplayStatics::CreatePlayer(W, -1, true);
		TestNotNull(TEXT("a second local player"), Second);
		if (Second)
		{
			// Look the way the first player does, from 20 m to the side, a little higher.
			Second->SetControlRotation(FRotator(12.0, 200.0, 0.0));
			if (APawn* Pawn = Second->GetPawn())
			{
				Pawn->SetActorLocation(CameraAt + FVector(0.0, 2000.0, 300.0));
			}
		}
	});
	for (const FMode& L : { FMode{ TEXT("RT"), 2, 0 }, FMode{ TEXT("OT"), 0, 0 } })
	{
		const FString Name = L.Name;
		const FIntPoint Mode(L.Clouds, L.Look);
		Then([Mode]() { SetClouds(Mode.X, Mode.Y); });
		WaitFrames(90);
		Then([State, Name, Viewport]() { State->Pictures.Add(TEXT("Split_") + Name, ReadViewport(Viewport())); Save(State->Pictures[TEXT("Split_") + Name], TEXT("Split_") + Name); });
	}
	Then([this, State, PlayWorld]()
	{
		const FPicture& RT = State->Pictures[TEXT("Split_RT")];
		const FPicture& OT = State->Pictures[TEXT("Split_OT")];
		if (!TestTrue(TEXT("split screen reads back"), RT.IsValid() && OT.IsValid()))
		{
			return;
		}
		const int32 Half = RT.Size.Y / 2;
		for (int32 View = 0; View < 2; ++View)
		{
			const FIntRect Sky = SkyOf(RT, View * Half, Half);
			const FStats A = Measure(RT, Sky);
			const FStats B = Measure(OT, Sky);
			const float Corr = Correlation(RT, OT, Sky);
			AddInfo(FString::Printf(TEXT("split view %d renderer: %s | old dome: %s | correlation %.3f"), View, *Describe(A), *Describe(B), Corr));
			TestTrue(FString::Printf(TEXT("split view %d: the renderer's clouds are there"), View), A.Coverage > 0.5f * B.Coverage && B.Coverage > 0.01f);
			TestTrue(FString::Printf(TEXT("split view %d: where the old dome's are"), View), Corr > 0.5f);
			TestTrue(FString::Printf(TEXT("split view %d: not noisy"), View), A.Noise < 2.0f * B.Noise + 1.0f);
		}
		if (UWorld* W = PlayWorld())
		{
			if (APlayerController* Second = UGameplayStatics::GetPlayerController(W, 1))
			{
				UGameplayStatics::RemovePlayer(Second, true);
			}
		}
	});

	ADD_LATENT_AUTOMATION_COMMAND(FEndPlayMapCommand());
	Then([Mirror]() { Mirror->RemoveFromRoot(); SetClouds(-1, 0); });
	return true;
}

// ---------------------------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDreamQASkyRenderEditorViewportTest, "DreamQA.SkyRender.EditorViewport", DreamQA::Flags)
bool FDreamQASkyRenderEditorViewportTest::RunTest(const FString& Parameters)
{
	using namespace DreamQA;
	if (!FApp::CanEverRender() || !GEditor)
	{
		AddWarning(TEXT("Needs the editor with a renderer (not -nullrhi)."));
		return true;
	}
	GetMutableDefault<UEditorPerformanceSettings>()->bThrottleCPUWhenNotForeground = false;
	FAutomationEditorCommonUtils::LoadMap(SkyTestMap);

	struct FState
	{
		TMap<FString, FPicture> Pictures;
	};
	TSharedRef<FState> State = MakeShared<FState>();
	auto Client = []() -> FLevelEditorViewportClient*
	{
		for (FLevelEditorViewportClient* C : GEditor->GetLevelViewportClients())
		{
			if (C && C->IsPerspective() && C->Viewport)
			{
				return C;
			}
		}
		return nullptr;
	};

	Then([this, Client]()
	{
		FLevelEditorViewportClient* C = Client();
		if (!TestNotNull(TEXT("a perspective level editor viewport"), C))
		{
			return;
		}
		// Realtime, at the test level's camera.
		C->SetRealtime(true);
		C->SetViewLocation(CameraAt);
		C->SetViewRotation(CameraLooks);
		C->Invalidate();
		SetWorld(GEditor->GetEditorWorldContext().World());
		GShaderCompilingManager->FinishAllCompilation();
	});
	const FMode EditorLooks[] = { { TEXT("RR"), 2, 1 }, { TEXT("RT"), 2, 0 }, { TEXT("OT"), 0, 0 } };
	for (const FMode& L : EditorLooks)
	{
		const FString Name = L.Name;
		const FIntPoint Mode(L.Clouds, L.Look);
		Then([Mode]() { SetClouds(Mode.X, Mode.Y); });
		WaitFrames(120);
		// Two pictures a frame apart: a view that accumulates changes little from one to the next and is smooth; one that
		// does not is noisy (the jitter shows) and, if the jitter moves per frame, changes much.
		Then([State, Name, Client]() { FLevelEditorViewportClient* C = Client(); State->Pictures.Add(TEXT("Editor_") + Name + TEXT("_a"), ReadViewport(C ? C->Viewport : nullptr)); });
		WaitFrames(1);
		Then([State, Name, Client]()
		{
			FLevelEditorViewportClient* C = Client();
			State->Pictures.Add(TEXT("Editor_") + Name + TEXT("_b"), ReadViewport(C ? C->Viewport : nullptr));
			Save(State->Pictures[TEXT("Editor_") + Name + TEXT("_a")], TEXT("Editor_") + Name + TEXT("_a"));
			Save(State->Pictures[TEXT("Editor_") + Name + TEXT("_b")], TEXT("Editor_") + Name + TEXT("_b"));
		});
	}
	Then([this, State]()
	{
		const FPicture& OT = State->Pictures[TEXT("Editor_OT_a")];
		if (!TestTrue(TEXT("the editor viewport reads back"), OT.IsValid()))
		{
			return;
		}
		const FIntRect Sky = SkyOf(OT);
		const FStats Old = Measure(OT, Sky);
		for (const TCHAR* Name : { TEXT("RR"), TEXT("RT"), TEXT("OT") })
		{
			const FPicture& A = State->Pictures[FString::Printf(TEXT("Editor_%s_a"), Name)];
			const FPicture& B = State->Pictures[FString::Printf(TEXT("Editor_%s_b"), Name)];
			AddInfo(FString::Printf(TEXT("editor viewport %s %dx%d: %s, change to the next frame %.2f"), Name, A.Size.X, A.Size.Y, *Describe(Measure(A, Sky)), Change(A, B, Sky)));
		}
		const FStats Realistic = Measure(State->Pictures[TEXT("Editor_RR_a")], Sky);
		TestTrue(TEXT("editor viewport: the realistic look is not noisier than the old dome by much (its history accumulates)"), Realistic.Noise < 2.0f * Old.Noise + 1.0f);
		TestTrue(TEXT("editor viewport: the toon look neither"), Measure(State->Pictures[TEXT("Editor_RT_a")], Sky).Noise < 2.0f * Old.Noise + 1.0f);
		SetClouds(-1, 0);
	});
	return true;
}

// ---------------------------------------------------------------------------------------------------------------------

// Two view families drawing the sky renderer in one frame: the level editor's realtime viewport and a play in editor
// session, for 300 frames (the renderer keeps per-frame state, the sun maps, that only the first view family draws).
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDreamQASkyRenderEditorAndPIETest, "DreamQA.SkyRender.EditorAndPIE", DreamQA::Flags)
bool FDreamQASkyRenderEditorAndPIETest::RunTest(const FString& Parameters)
{
	using namespace DreamQA;
	if (!FApp::CanEverRender() || !GEditor)
	{
		AddWarning(TEXT("Needs the editor with a renderer (not -nullrhi)."));
		return true;
	}
	GetMutableDefault<UEditorPerformanceSettings>()->bThrottleCPUWhenNotForeground = false;
	FAutomationEditorCommonUtils::LoadMap(SkyTestMap);
	for (FLevelEditorViewportClient* C : GEditor->GetLevelViewportClients())
	{
		if (C && C->IsPerspective())
		{
			C->SetRealtime(true);
		}
	}
	SetClouds(2, 1);
	WaitFrames(60);
	ADD_LATENT_AUTOMATION_COMMAND(FStartPIECommand(false));
	ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand([]() { return GEditor->PlayWorld != nullptr; }));
	WaitFrames(300);
	Then([this]() { AddInfo(TEXT("the editor viewport and the play session drew the sky renderer together for 300 frames")); });
	ADD_LATENT_AUTOMATION_COMMAND(FEndPlayMapCommand());
	Then([]() { SetClouds(-1, 0); });
	return true;
}

// ---------------------------------------------------------------------------------------------------------------------

// Scene captures in a play session: a 2D one and a cube one, each a view family of its own drawn in the same frame as
// the game view. Run it on its own (it is not under DreamQA.SkyRender): the renderer's clouds must be in both captures,
// where the old dome draws them; a renderer that keeps per-frame state between view families wrongly stops the editor
// here (RDG validation: "... registered with pass March ... is not part of the graph").
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDreamQASkyCrashSceneCaptureTest, "DreamQA.SkyCrash.SceneCapture", DreamQA::Flags)
bool FDreamQASkyCrashSceneCaptureTest::RunTest(const FString& Parameters)
{
	using namespace DreamQA;
	if (!FApp::CanEverRender() || !GEditor)
	{
		AddWarning(TEXT("Needs the editor with a renderer (not -nullrhi)."));
		return true;
	}
	GetMutableDefault<UEditorPerformanceSettings>()->bThrottleCPUWhenNotForeground = false;
	FAutomationEditorCommonUtils::LoadMap(SkyTestMap);
	for (FLevelEditorViewportClient* C : GEditor->GetLevelViewportClients())
	{
		if (C)
		{
			C->SetRealtime(false);
		}
	}
	struct FState
	{
		TMap<FString, FPicture> Pictures;
		TWeakObjectPtr<ASceneCapture2D> Capture;
		TWeakObjectPtr<ASceneCaptureCube> CaptureCube;
		TObjectPtr<UTextureRenderTarget2D> Target;
		TObjectPtr<UTextureRenderTargetCube> CubeTarget;
	};
	TSharedRef<FState> State = MakeShared<FState>();
	auto PlayWorld = []() { return GEditor ? GEditor->PlayWorld.Get() : nullptr; };
	ADD_LATENT_AUTOMATION_COMMAND(FStartPIECommand(false));
	ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand([PlayWorld]() { return PlayWorld() != nullptr; }));
	WaitFrames(30);
	Then([PlayWorld]() { SetWorld(PlayWorld()); GShaderCompilingManager->FinishAllCompilation(); });
	// A 2D scene capture looking up the way the camera looks, a cube one at the camera.
	Then([State, PlayWorld]()
	{
		UWorld* W = PlayWorld();
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		ASceneCapture2D* Capture = W->SpawnActor<ASceneCapture2D>(CameraAt, FRotator(30.0, 220.0, 0.0), Params);
		State->Target = NewObject<UTextureRenderTarget2D>();
		State->Target->AddToRoot();
		State->Target->InitCustomFormat(640, 320, PF_B8G8R8A8, false);
		State->Target->UpdateResourceImmediate(true);
		USceneCaptureComponent2D* C = Capture->GetCaptureComponent2D();
		C->TextureTarget = State->Target;
		C->CaptureSource = SCS_FinalColorLDR;
		C->FOVAngle = 90.0f;
		C->bCaptureEveryFrame = true;
		C->bAlwaysPersistRenderingState = true;
		State->Capture = Capture;
		ASceneCaptureCube* Cube = W->SpawnActor<ASceneCaptureCube>(CameraAt, FRotator::ZeroRotator, Params);
		State->CubeTarget = NewObject<UTextureRenderTargetCube>();
		State->CubeTarget->AddToRoot();
		State->CubeTarget->Init(256, PF_B8G8R8A8);
		State->CubeTarget->UpdateResourceImmediate(true);
		Cube->GetCaptureComponentCube()->TextureTarget = State->CubeTarget;
		Cube->GetCaptureComponentCube()->bCaptureEveryFrame = true;
		State->CaptureCube = Cube;
	});
	for (const FMode& L : { FMode{ TEXT("RT"), 2, 0 }, FMode{ TEXT("OT"), 0, 0 } })
	{
		const FString Name = L.Name;
		const FIntPoint Mode(L.Clouds, L.Look);
		Then([Mode]() { SetClouds(Mode.X, Mode.Y); });
		WaitFrames(90);
		Then([State, Name]()
		{
			State->Pictures.Add(TEXT("Capture2D_") + Name, ReadTarget(State->Target));
			State->Pictures.Add(TEXT("CaptureCubeUp_") + Name, ReadCubeFace(State->CubeTarget, CubeFace_PosZ));
			State->Pictures.Add(TEXT("CaptureCubeSide_") + Name, ReadCubeFace(State->CubeTarget, CubeFace_NegX));
			for (const TCHAR* Kind : { TEXT("Capture2D_"), TEXT("CaptureCubeUp_"), TEXT("CaptureCubeSide_") })
			{
				Save(State->Pictures[Kind + Name], Kind + Name);
			}
		});
	}
	Then([this, State]()
	{
		for (const TCHAR* Kind : { TEXT("Capture2D_"), TEXT("CaptureCubeUp_"), TEXT("CaptureCubeSide_") })
		{
			const FPicture& A = State->Pictures[FString(Kind) + TEXT("RT")];
			const FPicture& B = State->Pictures[FString(Kind) + TEXT("OT")];
			if (!TestTrue(FString::Printf(TEXT("%s reads back"), Kind), A.IsValid() && B.IsValid()))
			{
				continue;
			}
			// The 2D capture and the cube's upward face are sky all over; the side face has its sky in the upper half.
			const FIntRect R = FString(Kind) == TEXT("CaptureCubeSide_") ? FIntRect(0, 0, A.Size.X, A.Size.Y / 2) : FIntRect(0, 0, A.Size.X, A.Size.Y);
			const FStats Sa = Measure(A, R);
			const FStats Sb = Measure(B, R);
			const float Corr = Correlation(A, B, R);
			AddInfo(FString::Printf(TEXT("%s renderer: %s | old dome: %s | correlation %.3f"), Kind, *Describe(Sa), *Describe(Sb), Corr));
			TestTrue(FString::Printf(TEXT("%s: the renderer's clouds are in the capture"), Kind), Sa.Coverage > 0.5f * Sb.Coverage && Sb.Coverage > 0.01f);
			TestTrue(FString::Printf(TEXT("%s: where the old dome's are"), Kind), Corr > 0.5f);
		}
		State->Target->RemoveFromRoot();
		State->CubeTarget->RemoveFromRoot();
		if (State->Capture.IsValid())
		{
			State->Capture->Destroy();
		}
		if (State->CaptureCube.IsValid())
		{
			State->CaptureCube->Destroy();
		}
	});

	ADD_LATENT_AUTOMATION_COMMAND(FEndPlayMapCommand());
	Then([]() { SetClouds(-1, 0); });
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
