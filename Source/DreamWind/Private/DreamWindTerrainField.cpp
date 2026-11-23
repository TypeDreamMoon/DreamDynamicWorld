// Copyright DreamMoon. All Rights Reserved.

#include "DreamWindTerrainField.h"
#include "DreamWindDemoActors.h"
#include "DreamWindModule.h"
#include "DreamWindPassThroughComponent.h"
#include "DreamWindSettings.h"
#include "DreamWindSliceActor.h"

#include "Components/SceneCaptureComponent2D.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/WorldSettings.h"
#include "GlobalShader.h"
#include "RenderGraphBuilder.h"
#include "RenderGraphUtils.h"
#include "RenderingThread.h"
#include "ShaderParameterStruct.h"
#include "TextureResource.h"

// ---- Shaders -----------------------------------------------------------------------------------------------------

namespace DreamWindTerrainFieldShaders
{
	BEGIN_SHADER_PARAMETER_STRUCT(FWindowParameters, )
		SHADER_PARAMETER(int32, Size)
		SHADER_PARAMETER(FIntPoint, WindowMin)
		SHADER_PARAMETER(float, TexelSize)
	END_SHADER_PARAMETER_STRUCT()

	bool ShouldCompile(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
	}
}

class FDreamWindTerrainScatterCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FDreamWindTerrainScatterCS);
	SHADER_USE_PARAMETER_STRUCT(FDreamWindTerrainScatterCS, FGlobalShader);
	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_STRUCT_INCLUDE(DreamWindTerrainFieldShaders::FWindowParameters, Window)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float4>, CaptureDepth)
		SHADER_PARAMETER(FIntPoint, CaptureSize)
		SHADER_PARAMETER(FIntPoint, RectMin)
		SHADER_PARAMETER(float, CaptureZ)
		SHADER_PARAMETER(float, MaxDepth)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float>, OutHeight)
	END_SHADER_PARAMETER_STRUCT()
	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& P) { return DreamWindTerrainFieldShaders::ShouldCompile(P); }
};
IMPLEMENT_GLOBAL_SHADER(FDreamWindTerrainScatterCS, "/Plugin/DreamDynamicWorld/Private/DreamWindTerrainField.usf", "ScatterCS", SF_Compute);

class FDreamWindTerrainBlurCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FDreamWindTerrainBlurCS);
	SHADER_USE_PARAMETER_STRUCT(FDreamWindTerrainBlurCS, FGlobalShader);
	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_STRUCT_INCLUDE(DreamWindTerrainFieldShaders::FWindowParameters, Window)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float>, BlurIn)
		SHADER_PARAMETER(FIntPoint, BlurAxis)
		SHADER_PARAMETER(int32, BlurRadius)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float>, BlurOut)
	END_SHADER_PARAMETER_STRUCT()
	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& P) { return DreamWindTerrainFieldShaders::ShouldCompile(P); }
};
IMPLEMENT_GLOBAL_SHADER(FDreamWindTerrainBlurCS, "/Plugin/DreamDynamicWorld/Private/DreamWindTerrainField.usf", "BlurCS", SF_Compute);

class FDreamWindTerrainShadowCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FDreamWindTerrainShadowCS);
	SHADER_USE_PARAMETER_STRUCT(FDreamWindTerrainShadowCS, FGlobalShader);
	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_STRUCT_INCLUDE(DreamWindTerrainFieldShaders::FWindowParameters, Window)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float>, ShadowIn)
		SHADER_PARAMETER(FVector2f, UpwindOffset)
		SHADER_PARAMETER(float, StepDistance)
		SHADER_PARAMETER(float, Slope)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float>, ShadowOut)
	END_SHADER_PARAMETER_STRUCT()
	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& P) { return DreamWindTerrainFieldShaders::ShouldCompile(P); }
};
IMPLEMENT_GLOBAL_SHADER(FDreamWindTerrainShadowCS, "/Plugin/DreamDynamicWorld/Private/DreamWindTerrainField.usf", "ShadowCS", SF_Compute);

class FDreamWindTerrainFinalizeCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FDreamWindTerrainFinalizeCS);
	SHADER_USE_PARAMETER_STRUCT(FDreamWindTerrainFinalizeCS, FGlobalShader);
	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_STRUCT_INCLUDE(DreamWindTerrainFieldShaders::FWindowParameters, Window)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float>, FinalHeight)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float>, FinalBlur)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float>, FinalShadow)
		SHADER_PARAMETER(int32, bShape)
		SHADER_PARAMETER(float, ExposureScale)
		SHADER_PARAMETER(float, ExposureGain)
		SHADER_PARAMETER(float, SteerGain)
		SHADER_PARAMETER(float, MaxSteer)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float4>, OutDerived)
	END_SHADER_PARAMETER_STRUCT()
	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& P) { return DreamWindTerrainFieldShaders::ShouldCompile(P); }
};
IMPLEMENT_GLOBAL_SHADER(FDreamWindTerrainFinalizeCS, "/Plugin/DreamDynamicWorld/Private/DreamWindTerrainField.usf", "FinalizeCS", SF_Compute);

// ---- Helpers -----------------------------------------------------------------------------------------------------

namespace DreamWindTerrainFieldImpl
{
	int32 FloorDiv(double Value, double Step)
	{
		return int32(FMath::FloorToDouble(Value / Step));
	}

	/** Splits a cell rectangle into pieces no thicker than MaxThickness along its shorter side. */
	void AddStrips(TArray<FIntRect>& Out, const FIntRect& Rect, int32 MaxThickness, bool bFront)
	{
		if (Rect.Width() <= 0 || Rect.Height() <= 0)
		{
			return;
		}
		TArray<FIntRect> Pieces;
		if (Rect.Width() <= Rect.Height())
		{
			for (int32 X = Rect.Min.X; X < Rect.Max.X; X += MaxThickness)
			{
				Pieces.Add(FIntRect(X, Rect.Min.Y, FMath::Min(X + MaxThickness, Rect.Max.X), Rect.Max.Y));
			}
		}
		else
		{
			for (int32 Y = Rect.Min.Y; Y < Rect.Max.Y; Y += MaxThickness)
			{
				Pieces.Add(FIntRect(Rect.Min.X, Y, Rect.Max.X, FMath::Min(Y + MaxThickness, Rect.Max.Y)));
			}
		}
		if (bFront)
		{
			Out.Insert(Pieces, 0);
		}
		else
		{
			Out.Append(Pieces);
		}
	}

	FTextureRenderTargetResource* ResourceOf(UTextureRenderTarget2D* Target)
	{
		return Target ? Target->GameThread_GetRenderTargetResource() : nullptr;
	}

	void FillWindow(DreamWindTerrainFieldShaders::FWindowParameters& Out, int32 Size, const FIntPoint& WindowMin, float TexelSize)
	{
		Out.Size = Size;
		Out.WindowMin = WindowMin;
		Out.TexelSize = TexelSize;
	}
}

// ---- The field ---------------------------------------------------------------------------------------------------

FDreamWindTerrainField::FDreamWindTerrainField() = default;

FDreamWindTerrainField::~FDreamWindTerrainField()
{
	for (FLevel* Level : { &Near, &Far })
	{
		if (Level->Capture && Level->Capture->IsRegistered())
		{
			Level->Capture->UnregisterComponent();
		}
	}
}

void FDreamWindTerrainField::AddReferencedObjects(FReferenceCollector& Collector)
{
	for (FLevel* Level : { &Near, &Far })
	{
		Collector.AddReferencedObject(Level->Derived);
		Collector.AddReferencedObject(Level->Height);
		Collector.AddReferencedObject(Level->Capture);
		for (TPair<FIntPoint, TObjectPtr<UTextureRenderTarget2D>>& Pair : Level->CaptureTargets)
		{
			Collector.AddReferencedObject(Pair.Value);
		}
	}
}

FVector4f FDreamWindTerrainField::FLevel::Packed() const
{
	if (!bValid || !Derived)
	{
		return FVector4f::Zero();
	}
	const double Extent = double(Size) * TexelSize;
	const double CenterX = (double(WindowMin.X) + Size * 0.5) * TexelSize;
	const double CenterY = (double(WindowMin.Y) + Size * 0.5) * TexelSize;
	return FVector4f(float(CenterX), float(CenterY), float(1.0 / Extent), 1.0f);
}

FVector4f FDreamWindTerrainField::GetParams() const
{
	const UDreamWindSettings* S = UDreamWindSettings::Get();
	return FVector4f(S->OcclusionStrength, S->RuntimeShadowFadeBand, S->TerrainSteerScale, S->TerrainSpeedScale);
}

void FDreamWindTerrainField::Stop()
{
	bActive = false;
	Near.bValid = false;
	Far.bValid = false;
	Near.Pending.Reset();
	Far.Pending.Reset();
}

void FDreamWindTerrainField::MarkDirty(const FBox& Bounds)
{
	using namespace DreamWindTerrainFieldImpl;
	for (FLevel* Level : { &Near, &Far })
	{
		if (!Level->bValid)
		{
			continue;
		}
		const FIntRect Box(FloorDiv(Bounds.Min.X, Level->TexelSize), FloorDiv(Bounds.Min.Y, Level->TexelSize),
			FloorDiv(Bounds.Max.X, Level->TexelSize) + 1, FloorDiv(Bounds.Max.Y, Level->TexelSize) + 1);
		const FIntRect Window(Level->WindowMin, Level->WindowMin + FIntPoint(Level->Size));
		FIntRect Clipped = Box;
		Clipped.Clip(Window);
		AddStrips(Level->Pending, Clipped, FMath::Max(Level->Size / 8, 1), true);
	}
}

bool FDreamWindTerrainField::Resolve(FLevel& Level, const FSoftObjectPath& DerivedPath, const FSoftObjectPath& HeightPath, int32 Size, float Extent)
{
	if (!Level.Derived)
	{
		Level.Derived = Cast<UTextureRenderTarget2D>(DerivedPath.TryLoad());
	}
	if (!Level.Height)
	{
		Level.Height = Cast<UTextureRenderTarget2D>(HeightPath.TryLoad());
	}
	if (!Level.Derived || !Level.Height)
	{
		return false;
	}
	const float TexelSize = Extent / float(Size);
	if (Level.Size != Size || !FMath::IsNearlyEqual(Level.TexelSize, TexelSize))
	{
		Level.Size = Size;
		Level.TexelSize = TexelSize;
		Level.bValid = false;
	}
	// Sized to the level, writable by the passes (the assets may have been made at another size).
	for (UTextureRenderTarget2D* Target : { Level.Derived.Get(), Level.Height.Get() })
	{
		const EPixelFormat Format = Target == Level.Derived ? PF_A32B32G32R32F : PF_R32_FLOAT;
		if (Target->SizeX != Size || Target->SizeY != Size || Target->GetFormat() != Format || !Target->bSupportsUAV)
		{
			Target->bSupportsUAV = true;
			Target->AddressX = TA_Wrap;
			Target->AddressY = TA_Wrap;
			Target->InitCustomFormat(Size, Size, Format, true);
			Target->UpdateResourceImmediate(true);
			Level.bValid = false;
		}
	}
	return true;
}

void FDreamWindTerrainField::Follow(FLevel& Level, const FVector& ViewLocation)
{
	using namespace DreamWindTerrainFieldImpl;
	const int32 Step = FMath::Max(Level.Size / 8, 1);
	const FIntPoint Cell(FloorDiv(ViewLocation.X, Level.TexelSize), FloorDiv(ViewLocation.Y, Level.TexelSize));
	// The window moves in eighths of its size: its first cell a multiple of the step, the view near its middle.
	const FIntPoint Min(FMath::RoundToInt(float(Cell.X - Level.Size / 2) / float(Step)) * Step,
		FMath::RoundToInt(float(Cell.Y - Level.Size / 2) / float(Step)) * Step);
	if (!Level.bValid || FMath::Abs(Min.X - Level.WindowMin.X) >= Level.Size || FMath::Abs(Min.Y - Level.WindowMin.Y) >= Level.Size)
	{
		Level.WindowMin = Min;
		Level.Pending.Reset();
		Level.Pending.Add(FIntRect(Min, Min + FIntPoint(Level.Size)));
		Level.bValid = true;
		Level.bDerivePending = true;
		return;
	}
	const FIntPoint Old = Level.WindowMin;
	if (Min == Old)
	{
		return;
	}
	Level.WindowMin = Min;
	const int32 N = Level.Size;
	// What has come into the window (first along x over the new window's y, then along y over its x).
	if (Min.X > Old.X)
	{
		AddStrips(Level.Pending, FIntRect(Old.X + N, Min.Y, Min.X + N, Min.Y + N), Step, true);
	}
	else if (Min.X < Old.X)
	{
		AddStrips(Level.Pending, FIntRect(Min.X, Min.Y, Old.X, Min.Y + N), Step, true);
	}
	const int32 KeepX0 = FMath::Max(Min.X, Old.X);
	const int32 KeepX1 = FMath::Min(Min.X + N, Old.X + N);
	if (Min.Y > Old.Y)
	{
		AddStrips(Level.Pending, FIntRect(KeepX0, Old.Y + N, KeepX1, Min.Y + N), Step, true);
	}
	else if (Min.Y < Old.Y)
	{
		AddStrips(Level.Pending, FIntRect(KeepX0, Min.Y, KeepX1, Old.Y), Step, true);
	}
	// (Pending strips that slid out of the window are clipped when they are captured.)
	Level.bDerivePending = true;
}

void FDreamWindTerrainField::RefreshHiddenActors(UWorld& World)
{
	// What the wind passes through: tagged or with a pass-through component, the wind's own grass and debug planes.
	Hidden.Reset();
	for (TActorIterator<AActor> It(&World); It; ++It)
	{
		AActor* Actor = *It;
		if (UDreamWindPassThroughComponent::IsPassThrough(Actor) || Actor->IsA<ADreamWindGrassField>() || Actor->IsA<ADreamWindSliceActor>())
		{
			Hidden.Add(Actor);
		}
	}
	for (FLevel* Level : { &Near, &Far })
	{
		if (Level->Capture)
		{
			Level->Capture->HiddenActors.Reset();
			for (const TWeakObjectPtr<AActor>& Actor : Hidden)
			{
				if (Actor.IsValid())
				{
					Level->Capture->HiddenActors.Add(Actor.Get());
				}
			}
		}
	}
}

void FDreamWindTerrainField::CaptureNext(UWorld& World, FLevel& Level, double ViewZ)
{
	using namespace DreamWindTerrainFieldImpl;
	const UDreamWindSettings* S = UDreamWindSettings::Get();
	const FIntRect Window(Level.WindowMin, Level.WindowMin + FIntPoint(Level.Size));
	FIntRect Rect;
	while (Level.Pending.Num() > 0)
	{
		Rect = Level.Pending[0];
		Level.Pending.RemoveAt(0);
		Rect.Clip(Window);
		if (Rect.Width() > 0 && Rect.Height() > 0)
		{
			break;
		}
		Rect = FIntRect();
	}
	if (Rect.Width() <= 0 || Rect.Height() <= 0)
	{
		// Nothing new in view: capture the next of the level's refresh strips (moving buildings, streamed-in cells).
		const int32 Strips = FMath::Clamp(S->RuntimeRefreshFrames, 4, Level.Size);
		const int32 Width = FMath::Max(Level.Size / Strips, 1);
		const int32 X0 = Level.WindowMin.X + (Level.RefreshNext % Strips) * Width;
		Rect = FIntRect(X0, Level.WindowMin.Y, FMath::Min(X0 + Width, Window.Max.X), Window.Max.Y);
		Level.RefreshNext = (Level.RefreshNext + 1) % Strips;
	}

	if (!Level.Capture)
	{
		USceneCaptureComponent2D* Capture = NewObject<USceneCaptureComponent2D>(World.GetWorldSettings() ? static_cast<UObject*>(World.GetWorldSettings()) : static_cast<UObject*>(&World), NAME_None, RF_Transient);
		Capture->bCaptureEveryFrame = false;
		Capture->bCaptureOnMovement = false;
		Capture->bAlwaysPersistRenderingState = false;
		Capture->ProjectionType = ECameraProjectionMode::Orthographic;
		Capture->CaptureSource = ESceneCaptureSource::SCS_SceneDepth;
		// What does not stop the wind: foliage and grass, particles, translucency, characters, decals.
		TArray<FEngineShowFlagsSetting> Flags;
		for (const TCHAR* Flag : { TEXT("InstancedFoliage"), TEXT("InstancedGrass"), TEXT("Particles"), TEXT("Translucency"), TEXT("SkeletalMeshes"), TEXT("Decals") })
		{
			FEngineShowFlagsSetting& Setting = Flags.AddDefaulted_GetRef();
			Setting.ShowFlagName = Flag;
			Setting.Enabled = false;
		}
		Capture->SetShowFlagSettings(Flags);
		Capture->RegisterComponentWithWorld(&World);
		Level.Capture = Capture;
		LastHiddenScan = -1.0e30;
	}
	const double Now = FPlatformTime::Seconds();
	if (Now - LastHiddenScan > 2.0)
	{
		LastHiddenScan = Now;
		RefreshHiddenActors(World);
	}

	// The capture's image: x along world +Y (the rectangle's cells in y), y along world -X (its cells in x).
	const FIntPoint ImageSize(Rect.Height(), Rect.Width());
	TObjectPtr<UTextureRenderTarget2D>& Target = Level.CaptureTargets.FindOrAdd(ImageSize);
	if (!Target)
	{
		Target = NewObject<UTextureRenderTarget2D>(GetTransientPackage(), NAME_None, RF_Transient);
		Target->RenderTargetFormat = RTF_R32f;
		Target->ClearColor = FLinearColor(1.0e7f, 0.0f, 0.0f, 0.0f);
		Target->InitAutoFormat(ImageSize.X, ImageSize.Y);
		Target->UpdateResourceImmediate(true);
	}
	const double CaptureZ = ViewZ + S->RuntimeCaptureAltitude;
	const FVector Centre((double(Rect.Min.X) + Rect.Width() * 0.5) * Level.TexelSize, (double(Rect.Min.Y) + Rect.Height() * 0.5) * Level.TexelSize, CaptureZ);
	Level.Capture->SetWorldLocationAndRotation(Centre, FRotator(-90.0, 0.0, 0.0));
	Level.Capture->OrthoWidth = float(ImageSize.X) * Level.TexelSize;
	Level.Capture->TextureTarget = Target;
	Level.Capture->CaptureScene();
	++CaptureCount;

	// Then, on the render thread after the capture, into the height ring.
	FTextureRenderTargetResource* CaptureResource = ResourceOf(Target);
	FTextureRenderTargetResource* HeightResource = ResourceOf(Level.Height);
	if (!CaptureResource || !HeightResource)
	{
		return;
	}
	const int32 Size = Level.Size;
	const FIntPoint WindowMin = Level.WindowMin;
	const float TexelSize = Level.TexelSize;
	const FIntPoint RectMin = Rect.Min;
	const float MaxDepth = S->RuntimeCaptureAltitude + 100000.0f;
	ENQUEUE_RENDER_COMMAND(DreamWindTerrainScatter)([CaptureResource, HeightResource, Size, WindowMin, TexelSize, RectMin, ImageSize, CaptureZ, MaxDepth](FRHICommandListImmediate& RHICmdList)
	{
		FRHITexture* CaptureRHI = CaptureResource->GetRenderTargetTexture();
		FRHITexture* HeightRHI = HeightResource->GetRenderTargetTexture();
		if (!CaptureRHI || !HeightRHI)
		{
			return;
		}
		FRDGBuilder GraphBuilder(RHICmdList, RDG_EVENT_NAME("DreamWind.TerrainCapture"));
		auto* Params = GraphBuilder.AllocParameters<FDreamWindTerrainScatterCS::FParameters>();
		DreamWindTerrainFieldImpl::FillWindow(Params->Window, Size, WindowMin, TexelSize);
		Params->CaptureDepth = RegisterExternalTexture(GraphBuilder, CaptureRHI, TEXT("DreamWind.TerrainCaptureDepth"));
		Params->CaptureSize = ImageSize;
		Params->RectMin = RectMin;
		Params->CaptureZ = float(CaptureZ);
		Params->MaxDepth = MaxDepth;
		Params->OutHeight = GraphBuilder.CreateUAV(RegisterExternalTexture(GraphBuilder, HeightRHI, TEXT("DreamWind.TerrainHeight")));
		FComputeShaderUtils::AddPass(GraphBuilder, RDG_EVENT_NAME("Scatter %dx%d", ImageSize.X, ImageSize.Y),
			TShaderMapRef<FDreamWindTerrainScatterCS>(GetGlobalShaderMap(GMaxRHIFeatureLevel)), Params, FComputeShaderUtils::GetGroupCount(ImageSize, 8));
		GraphBuilder.Execute();
	});
	Level.bDerivePending = true;
}

void FDreamWindTerrainField::Derive(FLevel& Level, const FVector2D& WindDir2)
{
	using namespace DreamWindTerrainFieldImpl;
	const UDreamWindSettings* S = UDreamWindSettings::Get();
	FTextureRenderTargetResource* HeightResource = ResourceOf(Level.Height);
	FTextureRenderTargetResource* DerivedResource = ResourceOf(Level.Derived);
	if (!HeightResource || !DerivedResource)
	{
		return;
	}
	const int32 Size = Level.Size;
	const FIntPoint WindowMin = Level.WindowMin;
	const float TexelSize = Level.TexelSize;
	const bool bShape = Level.bShape;
	const FDreamWindTerrainBakeSettings Shape = S->RuntimeShape;
	const int32 BlurRadius = FMath::Clamp(FMath::RoundToInt(Shape.BlurRadius / TexelSize / 2.0f), 1, 32);
	// The sweep: step k reaches 2^k texels farther, so P steps cover 2^P - 1 texels of lee.
	const int32 Steps = FMath::Clamp(FMath::CeilToInt(FMath::Log2(Level.ShadowRange / TexelSize + 1.0f)), 1, 10);
	const float Slope = FMath::Max(S->OcclusionSlope, 0.01f);
	const FVector2f Upwind = FVector2f(-WindDir2);
	++DeriveCount;
	ENQUEUE_RENDER_COMMAND(DreamWindTerrainDerive)([HeightResource, DerivedResource, Size, WindowMin, TexelSize, bShape, Shape, BlurRadius, Steps, Slope, Upwind](FRHICommandListImmediate& RHICmdList)
	{
		FRHITexture* HeightRHI = HeightResource->GetRenderTargetTexture();
		FRHITexture* DerivedRHI = DerivedResource->GetRenderTargetTexture();
		if (!HeightRHI || !DerivedRHI)
		{
			return;
		}
		FRDGBuilder GraphBuilder(RHICmdList, RDG_EVENT_NAME("DreamWind.TerrainDerive"));
		FGlobalShaderMap* ShaderMap = GetGlobalShaderMap(GMaxRHIFeatureLevel);
		const FIntPoint Extent(Size, Size);
		const FIntVector Groups = FComputeShaderUtils::GetGroupCount(Extent, 8);
		FRDGTextureRef Height = RegisterExternalTexture(GraphBuilder, HeightRHI, TEXT("DreamWind.TerrainHeight"));
		const FRDGTextureDesc Desc = FRDGTextureDesc::Create2D(Extent, PF_R32_FLOAT, FClearValueBinding::Black, TexCreate_ShaderResource | TexCreate_UAV);

		// The terrain's large shapes (far level): the heights blurred along x, then y.
		FRDGTextureRef Blur = Height;
		if (bShape)
		{
			FRDGTextureRef Temp = GraphBuilder.CreateTexture(Desc, TEXT("DreamWind.TerrainBlurX"));
			FRDGTextureRef Out = GraphBuilder.CreateTexture(Desc, TEXT("DreamWind.TerrainBlur"));
			for (int32 Axis = 0; Axis < 2; ++Axis)
			{
				auto* P = GraphBuilder.AllocParameters<FDreamWindTerrainBlurCS::FParameters>();
				DreamWindTerrainFieldImpl::FillWindow(P->Window, Size, WindowMin, TexelSize);
				P->BlurIn = Axis == 0 ? Height : Temp;
				P->BlurAxis = Axis == 0 ? FIntPoint(1, 0) : FIntPoint(0, 1);
				P->BlurRadius = BlurRadius;
				P->BlurOut = GraphBuilder.CreateUAV(Axis == 0 ? Temp : Out);
				FComputeShaderUtils::AddPass(GraphBuilder, RDG_EVENT_NAME("Blur %s", Axis == 0 ? TEXT("X") : TEXT("Y")), TShaderMapRef<FDreamWindTerrainBlurCS>(ShaderMap), P, Groups);
			}
			Blur = Out;
		}

		// The wind shadow: from the heights, each step looking twice as far upwind as the last.
		FRDGTextureRef Shadow = Height;
		for (int32 k = 0; k < Steps; ++k)
		{
			FRDGTextureRef Next = GraphBuilder.CreateTexture(Desc, TEXT("DreamWind.TerrainShadow"));
			const float Reach = float(1 << k);
			auto* P = GraphBuilder.AllocParameters<FDreamWindTerrainShadowCS::FParameters>();
			DreamWindTerrainFieldImpl::FillWindow(P->Window, Size, WindowMin, TexelSize);
			P->ShadowIn = Shadow;
			P->UpwindOffset = Upwind * Reach;
			P->StepDistance = Reach * TexelSize;
			P->Slope = Slope;
			P->ShadowOut = GraphBuilder.CreateUAV(Next);
			FComputeShaderUtils::AddPass(GraphBuilder, RDG_EVENT_NAME("Shadow %d", k), TShaderMapRef<FDreamWindTerrainShadowCS>(ShaderMap), P, Groups);
			Shadow = Next;
		}

		auto* P = GraphBuilder.AllocParameters<FDreamWindTerrainFinalizeCS::FParameters>();
		DreamWindTerrainFieldImpl::FillWindow(P->Window, Size, WindowMin, TexelSize);
		P->FinalHeight = Height;
		P->FinalBlur = Blur;
		P->FinalShadow = Shadow;
		P->bShape = bShape ? 1 : 0;
		P->ExposureScale = Shape.ExposureScale;
		P->ExposureGain = Shape.ExposureGain;
		P->SteerGain = Shape.SteerGain;
		P->MaxSteer = Shape.MaxSteer;
		P->OutDerived = GraphBuilder.CreateUAV(RegisterExternalTexture(GraphBuilder, DerivedRHI, TEXT("DreamWind.TerrainDerived")));
		FComputeShaderUtils::AddPass(GraphBuilder, RDG_EVENT_NAME("Finalize"), TShaderMapRef<FDreamWindTerrainFinalizeCS>(ShaderMap), P, Groups);
		GraphBuilder.Execute();
	});
	Level.bDerivePending = false;
}

void FDreamWindTerrainField::Update(UWorld& World, const FVector& ViewLocation, const FVector& WindDir, bool bRedrawAll)
{
	const UDreamWindSettings* S = UDreamWindSettings::Get();
	if (!Resolve(Near, S->TerrainNearTexture, S->HeightNearTexture, FMath::Clamp(S->RuntimeNearResolution, 64, 1024), FMath::Max(S->RuntimeNearExtent, 1000.0f))
		|| !Resolve(Far, S->TerrainFarTexture, S->HeightFarTexture, FMath::Clamp(S->RuntimeFarResolution, 64, 1024), FMath::Max(S->RuntimeFarExtent, 10000.0f)))
	{
		Stop();
		return;
	}
	if (bRedrawAll || !bActive)
	{
		Near.bValid = false;
		Far.bValid = false;
	}
	bActive = true;
	Near.ShadowRange = FMath::Max(S->OcclusionRange, 0.0f);
	Near.bShape = false;
	Far.ShadowRange = FMath::Max(S->RuntimeFarShadowRange, 0.0f);
	Far.bShape = true;

	Follow(Near, ViewLocation);
	Follow(Far, ViewLocation);
	CaptureNext(World, Near, ViewLocation.Z);
	CaptureNext(World, Far, ViewLocation.Z);

	// The lee follows the wind: derive again once it has turned by more than a few degrees.
	const FVector2D Dir2 = FVector2D(WindDir.X, WindDir.Y).GetSafeNormal();
	const bool bTurned = (Dir2 | DerivedWindDir) < FMath::Cos(FMath::DegreesToRadians(5.0f));
	for (FLevel* Level : { &Near, &Far })
	{
		if (Level->bDerivePending || bTurned)
		{
			Derive(*Level, Dir2.IsNearlyZero() ? FVector2D(1.0, 0.0) : Dir2);
		}
	}
	if (bTurned)
	{
		DerivedWindDir = Dir2;
	}
}
