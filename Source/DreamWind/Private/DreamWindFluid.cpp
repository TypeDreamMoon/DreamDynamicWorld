// Copyright DreamMoon. All Rights Reserved.

#include "DreamWindFluid.h"
#include "DreamWindModule.h"

#include "DataDrivenShaderPlatformInfo.h"
#include "GlobalShader.h"
#include "RenderGraphBuilder.h"
#include "RenderGraphUtils.h"
#include "RenderTargetPool.h"
#include "RHIStaticStates.h"
#include "SceneView.h"
#include "ShaderParameterStruct.h"
#include "SystemTextures.h"
#include "TextureResource.h"

// -------------------------------------------------------------------------------------------------
// Shaders (DreamWindFluid.usf, DreamWindFluidWalls.usf)
// -------------------------------------------------------------------------------------------------

BEGIN_SHADER_PARAMETER_STRUCT(FDreamWindFluidGrid, )
	SHADER_PARAMETER(FIntVector, Resolution)
	SHADER_PARAMETER(float, CellSize)
	SHADER_PARAMETER(FVector3f, Origin)
	SHADER_PARAMETER(float, DeltaTime)
	SHADER_PARAMETER(int32, SpongeCells)
END_SHADER_PARAMETER_STRUCT()

BEGIN_SHADER_PARAMETER_STRUCT(FDreamWindFluidSources, )
	SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<float4>, Sources)
	SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<uint2>, TileRanges)
	SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<uint>, TileSources)
	SHADER_PARAMETER(FIntVector, Tiles)
END_SHADER_PARAMETER_STRUCT()

class FDreamWindFluidShader : public FGlobalShader
{
public:
	FDreamWindFluidShader() = default;
	FDreamWindFluidShader(const ShaderMetaType::CompiledShaderInitializerType& Initializer) : FGlobalShader(Initializer) {}

	static constexpr int32 GroupSize = 4;

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.SetDefine(TEXT("THREADGROUP_SIZE"), GroupSize);
	}
};

class FDreamWindFluidAdvectCS : public FDreamWindFluidShader
{
	DECLARE_GLOBAL_SHADER(FDreamWindFluidAdvectCS);
	SHADER_USE_PARAMETER_STRUCT(FDreamWindFluidAdvectCS, FDreamWindFluidShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_STRUCT_INCLUDE(FDreamWindFluidGrid, Grid)
		SHADER_PARAMETER_STRUCT_INCLUDE(FDreamWindBackgroundParameters, Background)
		SHADER_PARAMETER_RDG_TEXTURE(Texture3D<float4>, DPrev)
		SHADER_PARAMETER_SAMPLER(SamplerState, DPrevSampler)
		SHADER_PARAMETER_RDG_TEXTURE(Texture3D<float>, ThetaPrev)
		SHADER_PARAMETER_RDG_TEXTURE(Texture3D<float>, PPrev)
		SHADER_PARAMETER(FVector3f, PrevOrigin)
		SHADER_PARAMETER(float, PrevValid)
		SHADER_PARAMETER(FIntVector, Shift)
		SHADER_PARAMETER(float, KeepWalls)
		SHADER_PARAMETER(float, WallThickness)
		SHADER_PARAMETER(FVector4f, FarV0)
		SHADER_PARAMETER(FVector4f, FarV1)
		SHADER_PARAMETER_RDG_TEXTURE(Texture3D<float4>, FarVolume)
		SHADER_PARAMETER_SAMPLER(SamplerState, FarSampler)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture3D<float4>, OutB)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture3D<float4>, OutBC)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture3D<float4>, OutD)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture3D<float>, OutTheta)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture3D<float>, OutWall)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture3D<float>, OutP)
	END_SHADER_PARAMETER_STRUCT()
};
IMPLEMENT_GLOBAL_SHADER(FDreamWindFluidAdvectCS, "/Plugin/DreamDynamicWorld/Private/DreamWindFluid.usf", "AdvectCS", SF_Compute);

class FDreamWindFluidCorrectCS : public FDreamWindFluidShader
{
	DECLARE_GLOBAL_SHADER(FDreamWindFluidCorrectCS);
	SHADER_USE_PARAMETER_STRUCT(FDreamWindFluidCorrectCS, FDreamWindFluidShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_STRUCT_INCLUDE(FDreamWindFluidGrid, Grid)
		SHADER_PARAMETER_RDG_TEXTURE(Texture3D<float4>, DPrev)
		SHADER_PARAMETER(FVector3f, PrevOrigin)
		SHADER_PARAMETER(float, PrevValid)
		SHADER_PARAMETER(FIntVector, Shift)
		SHADER_PARAMETER_RDG_TEXTURE(Texture3D<float4>, B)
		SHADER_PARAMETER_RDG_TEXTURE(Texture3D<float4>, D)
		SHADER_PARAMETER_SAMPLER(SamplerState, DSampler)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture3D<float4>, OutCorrected)
	END_SHADER_PARAMETER_STRUCT()
};
IMPLEMENT_GLOBAL_SHADER(FDreamWindFluidCorrectCS, "/Plugin/DreamDynamicWorld/Private/DreamWindFluid.usf", "CorrectCS", SF_Compute);

class FDreamWindFluidCurlCS : public FDreamWindFluidShader
{
	DECLARE_GLOBAL_SHADER(FDreamWindFluidCurlCS);
	SHADER_USE_PARAMETER_STRUCT(FDreamWindFluidCurlCS, FDreamWindFluidShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_STRUCT_INCLUDE(FDreamWindFluidGrid, Grid)
		SHADER_PARAMETER_RDG_TEXTURE(Texture3D<float4>, B)
		SHADER_PARAMETER_RDG_TEXTURE(Texture3D<float4>, D)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture3D<float4>, OutCurl)
	END_SHADER_PARAMETER_STRUCT()
};
IMPLEMENT_GLOBAL_SHADER(FDreamWindFluidCurlCS, "/Plugin/DreamDynamicWorld/Private/DreamWindFluid.usf", "CurlCS", SF_Compute);

class FDreamWindFluidForcesCS : public FDreamWindFluidShader
{
	DECLARE_GLOBAL_SHADER(FDreamWindFluidForcesCS);
	SHADER_USE_PARAMETER_STRUCT(FDreamWindFluidForcesCS, FDreamWindFluidShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_STRUCT_INCLUDE(FDreamWindFluidGrid, Grid)
		SHADER_PARAMETER_RDG_TEXTURE(Texture3D<float>, Wall)
		SHADER_PARAMETER_RDG_TEXTURE(Texture3D<float4>, B)
		SHADER_PARAMETER_RDG_TEXTURE(Texture3D<float4>, D)
		SHADER_PARAMETER_RDG_TEXTURE(Texture3D<float4>, Curl)
		SHADER_PARAMETER_STRUCT_INCLUDE(FDreamWindFluidSources, SourceLists)
		SHADER_PARAMETER(float, Vorticity)
		SHADER_PARAMETER(float, Smoothing)
		SHADER_PARAMETER(float, NudgeTime)
		SHADER_PARAMETER(float, SpongeTime)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture3D<float4>, OutD)
	END_SHADER_PARAMETER_STRUCT()
};
IMPLEMENT_GLOBAL_SHADER(FDreamWindFluidForcesCS, "/Plugin/DreamDynamicWorld/Private/DreamWindFluid.usf", "ForcesCS", SF_Compute);

class FDreamWindFluidDivergenceCS : public FDreamWindFluidShader
{
	DECLARE_GLOBAL_SHADER(FDreamWindFluidDivergenceCS);
	SHADER_USE_PARAMETER_STRUCT(FDreamWindFluidDivergenceCS, FDreamWindFluidShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_STRUCT_INCLUDE(FDreamWindFluidGrid, Grid)
		SHADER_PARAMETER_RDG_TEXTURE(Texture3D<float>, Wall)
		SHADER_PARAMETER_RDG_TEXTURE(Texture3D<float4>, B)
		SHADER_PARAMETER_RDG_TEXTURE(Texture3D<float4>, D)
		SHADER_PARAMETER_STRUCT_INCLUDE(FDreamWindFluidSources, SourceLists)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture3D<float>, OutDivergence)
	END_SHADER_PARAMETER_STRUCT()
};
IMPLEMENT_GLOBAL_SHADER(FDreamWindFluidDivergenceCS, "/Plugin/DreamDynamicWorld/Private/DreamWindFluid.usf", "DivergenceCS", SF_Compute);

class FDreamWindFluidPressureCS : public FDreamWindFluidShader
{
	DECLARE_GLOBAL_SHADER(FDreamWindFluidPressureCS);
	SHADER_USE_PARAMETER_STRUCT(FDreamWindFluidPressureCS, FDreamWindFluidShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_STRUCT_INCLUDE(FDreamWindFluidGrid, Grid)
		SHADER_PARAMETER_RDG_TEXTURE(Texture3D<float>, Wall)
		SHADER_PARAMETER_RDG_TEXTURE(Texture3D<float>, Divergence)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture3D<float>, Pressure)
		SHADER_PARAMETER(int32, Parity)
		SHADER_PARAMETER(float, Relaxation)
	END_SHADER_PARAMETER_STRUCT()
};
IMPLEMENT_GLOBAL_SHADER(FDreamWindFluidPressureCS, "/Plugin/DreamDynamicWorld/Private/DreamWindFluid.usf", "PressureCS", SF_Compute);

class FDreamWindFluidProjectCS : public FDreamWindFluidShader
{
	DECLARE_GLOBAL_SHADER(FDreamWindFluidProjectCS);
	SHADER_USE_PARAMETER_STRUCT(FDreamWindFluidProjectCS, FDreamWindFluidShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_STRUCT_INCLUDE(FDreamWindFluidGrid, Grid)
		SHADER_PARAMETER_RDG_TEXTURE(Texture3D<float>, Wall)
		SHADER_PARAMETER_RDG_TEXTURE(Texture3D<float4>, B)
		SHADER_PARAMETER_RDG_TEXTURE(Texture3D<float4>, D)
		SHADER_PARAMETER_RDG_TEXTURE(Texture3D<float>, P)
		SHADER_PARAMETER(float, MaxSpeed)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture3D<float4>, OutD)
	END_SHADER_PARAMETER_STRUCT()
};
IMPLEMENT_GLOBAL_SHADER(FDreamWindFluidProjectCS, "/Plugin/DreamDynamicWorld/Private/DreamWindFluid.usf", "ProjectCS", SF_Compute);

class FDreamWindFluidOutputCS : public FDreamWindFluidShader
{
	DECLARE_GLOBAL_SHADER(FDreamWindFluidOutputCS);
	SHADER_USE_PARAMETER_STRUCT(FDreamWindFluidOutputCS, FDreamWindFluidShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_STRUCT_INCLUDE(FDreamWindFluidGrid, Grid)
		SHADER_PARAMETER_RDG_TEXTURE(Texture3D<float>, Wall)
		SHADER_PARAMETER_RDG_TEXTURE(Texture3D<float4>, B)
		SHADER_PARAMETER_RDG_TEXTURE(Texture3D<float4>, D)
		SHADER_PARAMETER_RDG_TEXTURE(Texture3D<float4>, BC)
		SHADER_PARAMETER(int32, ShowDivergence)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture3D<float4>, OutVolume)
	END_SHADER_PARAMETER_STRUCT()
};
IMPLEMENT_GLOBAL_SHADER(FDreamWindFluidOutputCS, "/Plugin/DreamDynamicWorld/Private/DreamWindFluid.usf", "OutputCS", SF_Compute);

class FDreamWindFluidWallsCS : public FDreamWindFluidShader
{
	DECLARE_GLOBAL_SHADER(FDreamWindFluidWallsCS);
	SHADER_USE_PARAMETER_STRUCT(FDreamWindFluidWallsCS, FDreamWindFluidShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_STRUCT_REF(FViewUniformShaderParameters, View)
		SHADER_PARAMETER(FIntVector, Resolution)
		SHADER_PARAMETER(float, CellSize)
		SHADER_PARAMETER(FVector3f, TranslatedOrigin)
		SHADER_PARAMETER(float, WallThickness)
		SHADER_PARAMETER(int32, RefreshPhase)
		SHADER_PARAMETER(int32, RefreshPeriod)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture3D<float>, Theta)
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return FDreamWindFluidShader::ShouldCompilePermutation(Parameters) && DoesPlatformSupportDistanceFields(Parameters.Platform);
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FDreamWindFluidShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		// The global distance field's textures come with the view's uniform buffer.
		OutEnvironment.SetDefine(TEXT("DISTANCE_FIELD_IN_VIEW_UB"), 1);
	}
};
IMPLEMENT_GLOBAL_SHADER(FDreamWindFluidWallsCS, "/Plugin/DreamDynamicWorld/Private/DreamWindFluidWalls.usf", "WallsCS", SF_Compute);

// -------------------------------------------------------------------------------------------------

namespace DreamWindFluidImpl
{
	/** Every cell slab of this many is looked up in the distance field again once in this many frames. */
	constexpr int32 WallRefreshPeriod = 8;

	TRefCountPtr<IPooledRenderTarget> Allocate(const FIntVector& Resolution, EPixelFormat Format, const TCHAR* Name)
	{
		const FRDGTextureDesc Desc = FRDGTextureDesc::Create3D(Resolution, Format, FClearValueBinding::Black, TexCreate_ShaderResource | TexCreate_UAV);
		return AllocatePooledTexture(Desc, Name);
	}

	FRDGTextureRef Transient(FRDGBuilder& GraphBuilder, const FIntVector& Resolution, EPixelFormat Format, const TCHAR* Name)
	{
		return GraphBuilder.CreateTexture(FRDGTextureDesc::Create3D(Resolution, Format, FClearValueBinding::Black, TexCreate_ShaderResource | TexCreate_UAV), Name);
	}

	FIntVector Groups(const FIntVector& Cells)
	{
		return FComputeShaderUtils::GetGroupCount(Cells, FDreamWindFluidShader::GroupSize);
	}

	/** Cells per side of a tile of the source lists (DW_FLUID_TILE in DreamWindFluid.usf). */
	constexpr int32 TileSize = 8;

	/**
	 * Which records reach which tiles of a grid: each record's bounding sphere (as the shader spreads small ones over a
	 * cell; a stretched one reaches its stretch ahead) against the tiles' boxes, as boxes of tiles.
	 */
	FDreamWindFluidSources BuildSourceLists(FRDGBuilder& GraphBuilder, FRDGBufferSRVRef Sources, TConstArrayView<DreamWind::Hlsl::FDreamWindSourceData> Records,
		const FIntVector& Res, float CellSize, const FVector3f& Origin)
	{
		using namespace DreamWind::Hlsl;
		const FIntVector Tiles((Res.X + TileSize - 1) / TileSize, (Res.Y + TileSize - 1) / TileSize, (Res.Z + TileSize - 1) / TileSize);
		TArray<TArray<uint32, TInlineAllocator<8>>> Lists;
		Lists.SetNum(Tiles.X * Tiles.Y * Tiles.Z);
		const float TileWidth = CellSize * TileSize;
		for (int32 i = 0; i < Records.Num(); ++i)
		{
			const FDreamWindSourceData& S = Records[i];
			const FVector3f Center(S.PositionRadius.x, S.PositionRadius.y, S.PositionRadius.z);
			const int32 Type = int32(S.Misc.x);
			float Reach;
			if (Type == DW_SOURCE_SHELTER)
			{
				const FVector3f Spread(FMath::Max(S.BoxExtent.x, 0.6f * CellSize), FMath::Max(S.BoxExtent.y, 0.6f * CellSize), FMath::Max(S.BoxExtent.z, 0.6f * CellSize));
				Reach = FMath::Max(S.PositionRadius.w, Spread.Size());
			}
			else
			{
				Reach = FMath::Max(S.PositionRadius.w, 0.75f * CellSize) * (Type == DW_SOURCE_RADIAL ? 1.0f : FMath::Max(S.Misc.w, 1.0f));
			}
			const FVector3f Lo = (Center - FVector3f(Reach) - Origin) / TileWidth;
			const FVector3f Hi = (Center + FVector3f(Reach) - Origin) / TileWidth;
			const FIntVector Min(FMath::Max(FMath::FloorToInt(Lo.X), 0), FMath::Max(FMath::FloorToInt(Lo.Y), 0), FMath::Max(FMath::FloorToInt(Lo.Z), 0));
			const FIntVector Max(FMath::Min(FMath::FloorToInt(Hi.X), Tiles.X - 1), FMath::Min(FMath::FloorToInt(Hi.Y), Tiles.Y - 1), FMath::Min(FMath::FloorToInt(Hi.Z), Tiles.Z - 1));
			for (int32 Z = Min.Z; Z <= Max.Z; ++Z)
			{
				for (int32 Y = Min.Y; Y <= Max.Y; ++Y)
				{
					for (int32 X = Min.X; X <= Max.X; ++X)
					{
						Lists[(Z * Tiles.Y + Y) * Tiles.X + X].Add(uint32(i));
					}
				}
			}
		}
		TArray<FUintVector2> Ranges;
		TArray<uint32> Indices;
		Ranges.Reserve(Lists.Num());
		for (const auto& List : Lists)
		{
			Ranges.Add(FUintVector2(uint32(Indices.Num()), uint32(List.Num())));
			Indices.Append(List);
		}
		if (Indices.Num() == 0)
		{
			Indices.Add(0);   // (never read: every count is 0)
		}
		FDreamWindFluidSources Out;
		Out.Sources = Sources;
		Out.TileRanges = GraphBuilder.CreateSRV(CreateStructuredBuffer(GraphBuilder, TEXT("DreamWind.FluidTileRanges"), Ranges));
		Out.TileSources = GraphBuilder.CreateSRV(CreateStructuredBuffer(GraphBuilder, TEXT("DreamWind.FluidTileSources"), Indices));
		Out.Tiles = Tiles;
		return Out;
	}
}

void FDreamWindFluid::FLevel::Release()
{
	D.SafeRelease();
	for (int32 i = 0; i < 2; ++i)
	{
		Theta[i].SafeRelease();
		Pressure[i].SafeRelease();
	}
	Resolution = FIntVector::ZeroValue;
	bValid = false;
}

void FDreamWindFluid::EnqueueStep(FStepArgs&& Args)
{
	check(IsInGameThread());
	TSharedRef<FDreamWindFluid, ESPMode::ThreadSafe> Self = AsShared();
	ENQUEUE_RENDER_COMMAND(DreamWindFluidStep)([Self, Args = MoveTemp(Args)](FRHICommandListImmediate& RHICmdList)
	{
		FRDGBuilder GraphBuilder(RHICmdList, RDG_EVENT_NAME("DreamWind.Fluid"));
		Self->Step_RenderThread(GraphBuilder, Args);
		GraphBuilder.Execute();
	});
}

void FDreamWindFluid::EnqueueReset()
{
	TSharedRef<FDreamWindFluid, ESPMode::ThreadSafe> Self = AsShared();
	ENQUEUE_RENDER_COMMAND(DreamWindFluidReset)([Self](FRHICommandListImmediate&)
	{
		Self->Near.Release();
		Self->Far.Release();
		Self->bDistanceFieldWalls = false;
	});
}

void FDreamWindFluid::Step_RenderThread(FRDGBuilder& GraphBuilder, const FStepArgs& Args)
{
	check(IsInRenderingThread());
	if (Args.bReset)
	{
		Near.Release();
		Far.Release();
	}
	bDistanceFieldWalls = Args.bDistanceField;

	FDreamWindBackgroundParameters Background;
	DreamWind::Background::Fill(GraphBuilder, Background, Args.Frame, Args.Background);

	// The records: the sources, then the moving bodies' (five float4 each; at least one so the buffer is never empty).
	using namespace DreamWind::Hlsl;
	TArray<FDreamWindSourceData> Records(Args.Frame.Sources);
	Records.Append(Args.Frame.Movers);
	TArray<FVector4f> SourceData;
	SourceData.Reserve(FMath::Max(Records.Num(), 1) * DW_SOURCE_FLOAT4S);
	for (const FDreamWindSourceData& S : Records)
	{
		SourceData.Add(ToVector4f(S.PositionRadius));
		SourceData.Add(ToVector4f(S.DirectionStrength));
		SourceData.Add(ToVector4f(S.Misc));
		SourceData.Add(ToVector4f(S.BoxExtent));
		SourceData.Add(ToVector4f(S.BoxRotation));
	}
	if (SourceData.Num() == 0)
	{
		SourceData.Init(FVector4f::Zero(), DW_SOURCE_FLOAT4S);
	}
	FRDGBufferSRVRef Sources = GraphBuilder.CreateSRV(CreateStructuredBuffer(GraphBuilder, TEXT("DreamWind.FluidSources"), SourceData));

	// The far level first: the near one's background carries its deviation.
	FRDGTextureRef FarVolume = nullptr;
	if (Args.Far.bEnabled && !Args.Far.bStep && Far.bValid && !Args.bReset)
	{
		// (between its steps: as the last one left it, in the window it had)
		FRHITexture* FarRHI = Args.Far.Target ? Args.Far.Target->GetRenderTargetTexture() : nullptr;
		if (FarRHI && FarRHI->GetSizeXYZ() == Far.Resolution)
		{
			FarVolume = RegisterExternalTexture(GraphBuilder, FarRHI, TEXT("DreamWind.VolumeRT"));
		}
	}
	else if (Args.Far.bEnabled)
	{
		RDG_EVENT_SCOPE(GraphBuilder, "Far");
		FarVolume = StepLevel(GraphBuilder, Far, Args.Far, Args, Background, Sources, Records, nullptr, nullptr);
	}
	else
	{
		Far.Release();
	}
	if (Args.Near.bEnabled)
	{
		RDG_EVENT_SCOPE(GraphBuilder, "Near");
		StepLevel(GraphBuilder, Near, Args.Near, Args, Background, Sources, Records, FarVolume, FarVolume ? &Args.Far : nullptr);
	}
	else
	{
		Near.Release();
	}
	StepCount.fetch_add(1);
}

FRDGTextureRef FDreamWindFluid::StepLevel(FRDGBuilder& GraphBuilder, FLevel& Level, const FLevelArgs& LevelArgs, const FStepArgs& Args,
	const FDreamWindBackgroundParameters& Background, FRDGBufferSRVRef Sources, TConstArrayView<DreamWind::Hlsl::FDreamWindSourceData> Records,
	FRDGTextureRef FarVolume, const FLevelArgs* FarArgs)
{
	using namespace DreamWindFluidImpl;
	FRHITexture* TargetRHI = LevelArgs.Target ? LevelArgs.Target->GetRenderTargetTexture() : nullptr;
	const FIntVector Res = LevelArgs.Resolution;
	if (!TargetRHI || Res.X <= 0 || Res.Y <= 0 || Res.Z <= 0 || TargetRHI->GetSizeXYZ() != Res || !EnumHasAnyFlags(TargetRHI->GetFlags(), TexCreate_UAV))
	{
		Level.Release();
		return nullptr;
	}

	// The grid's state, new when the resolution or the cell size changed.
	if (!Level.D.IsValid() || Level.Resolution != Res || Level.CellSize != LevelArgs.CellSize)
	{
		Level.Release();
		Level.D = Allocate(Res, PF_FloatRGBA, TEXT("DreamWind.FluidD"));
		for (int32 i = 0; i < 2; ++i)
		{
			Level.Theta[i] = Allocate(Res, PF_R32_FLOAT, TEXT("DreamWind.FluidTheta"));
			Level.Pressure[i] = Allocate(Res, PF_R32_FLOAT, TEXT("DreamWind.FluidPressure"));
		}
		Level.Resolution = Res;
		Level.CellSize = LevelArgs.CellSize;
		Level.bValid = false;
	}

	// Rebased by whole cells; a jump of more than a quarter of the grid starts afresh.
	const FVector3f ShiftCells = (LevelArgs.Origin - Level.Origin) / LevelArgs.CellSize;
	const FIntVector Shift(FMath::RoundToInt(ShiftCells.X), FMath::RoundToInt(ShiftCells.Y), FMath::RoundToInt(ShiftCells.Z));
	const bool bJump = FMath::Abs(Shift.X) * 4 > Res.X || FMath::Abs(Shift.Y) * 4 > Res.Y || FMath::Abs(Shift.Z) * 4 > Res.Z;
	const bool bPrevValid = Level.bValid && !bJump;

	FDreamWindFluidGrid Grid;
	Grid.Resolution = Res;
	Grid.CellSize = LevelArgs.CellSize;
	Grid.Origin = LevelArgs.Origin;
	Grid.DeltaTime = LevelArgs.DeltaTime;
	Grid.SpongeCells = FMath::Max(Args.SpongeCells, 0);
	Level.WallThickness = LevelArgs.WallThickness;

	FRDGTextureRef DPersistent = GraphBuilder.RegisterExternalTexture(Level.D, TEXT("DreamWind.FluidD"));
	FRDGTextureRef ThetaPrev = GraphBuilder.RegisterExternalTexture(Level.Theta[Level.Current], TEXT("DreamWind.FluidTheta"));
	FRDGTextureRef ThetaNext = GraphBuilder.RegisterExternalTexture(Level.Theta[1 - Level.Current], TEXT("DreamWind.FluidTheta"));
	FRDGTextureRef PressurePrev = GraphBuilder.RegisterExternalTexture(Level.Pressure[Level.Current], TEXT("DreamWind.FluidPressure"));
	FRDGTextureRef PressureNext = GraphBuilder.RegisterExternalTexture(Level.Pressure[1 - Level.Current], TEXT("DreamWind.FluidPressure"));
	FRDGTextureRef B = Transient(GraphBuilder, Res, PF_FloatRGBA, TEXT("DreamWind.FluidB"));
	FRDGTextureRef BC = Transient(GraphBuilder, Res, PF_FloatRGBA, TEXT("DreamWind.FluidBConsumers"));
	FRDGTextureRef DAdvected = Transient(GraphBuilder, Res, PF_FloatRGBA, TEXT("DreamWind.FluidDAdvected"));
	FRDGTextureRef DForced = Transient(GraphBuilder, Res, PF_FloatRGBA, TEXT("DreamWind.FluidDForced"));
	FRDGTextureRef Wall = Transient(GraphBuilder, Res, PF_R32_FLOAT, TEXT("DreamWind.FluidWall"));
	FRDGTextureRef Divergence = Transient(GraphBuilder, Res, PF_R32_FLOAT, TEXT("DreamWind.FluidDivergence"));
	FRDGTextureRef FarTexture = FarVolume ? FarVolume : GraphBuilder.RegisterExternalTexture(GSystemTextures.VolumetricBlackDummy);
	FRHISamplerState* Trilinear = TStaticSamplerState<SF_Trilinear, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();
	const FGlobalShaderMap* ShaderMap = GetGlobalShaderMap(GMaxRHIFeatureLevel);
	const FIntVector Cells = Groups(Res);
	const FDreamWindFluidSources SourceLists = BuildSourceLists(GraphBuilder, Sources, Records, Res, LevelArgs.CellSize, LevelArgs.Origin);

	{
		auto* P = GraphBuilder.AllocParameters<FDreamWindFluidAdvectCS::FParameters>();
		P->Grid = Grid;
		P->Background = Background;
		P->DPrev = DPersistent;
		P->DPrevSampler = Trilinear;
		P->ThetaPrev = ThetaPrev;
		P->PPrev = PressurePrev;
		P->PrevOrigin = Level.Origin;
		P->PrevValid = bPrevValid ? 1.0f : 0.0f;
		P->Shift = Shift;
		P->KeepWalls = Args.bDistanceField ? 1.0f : 0.0f;
		P->WallThickness = Level.WallThickness;
		P->FarV0 = FarArgs ? FarArgs->V0 : FVector4f::Zero();
		P->FarV1 = FarArgs ? FVector4f(FarArgs->V1.X, FarArgs->V1.Y, FarArgs->V1.Z, 1.0f) : FVector4f::Zero();
		P->FarVolume = FarTexture;
		P->FarSampler = Trilinear;
		P->OutB = GraphBuilder.CreateUAV(B);
		P->OutBC = GraphBuilder.CreateUAV(BC);
		P->OutD = GraphBuilder.CreateUAV(DAdvected);
		P->OutTheta = GraphBuilder.CreateUAV(ThetaNext);
		P->OutWall = GraphBuilder.CreateUAV(Wall);
		P->OutP = GraphBuilder.CreateUAV(PressureNext);
		FComputeShaderUtils::AddPass(GraphBuilder, RDG_EVENT_NAME("Advect"), TShaderMapRef<FDreamWindFluidAdvectCS>(ShaderMap), P, Cells);
	}

	FRDGTextureRef DIntoForces = DAdvected;
	if (LevelArgs.bMacCormack && bPrevValid)
	{
		DIntoForces = Transient(GraphBuilder, Res, PF_FloatRGBA, TEXT("DreamWind.FluidDCorrected"));
		auto* P = GraphBuilder.AllocParameters<FDreamWindFluidCorrectCS::FParameters>();
		P->Grid = Grid;
		P->DPrev = DPersistent;
		P->PrevOrigin = Level.Origin;
		P->PrevValid = 1.0f;
		P->Shift = Shift;
		P->B = B;
		P->D = DAdvected;
		P->DSampler = Trilinear;
		P->OutCorrected = GraphBuilder.CreateUAV(DIntoForces);
		FComputeShaderUtils::AddPass(GraphBuilder, RDG_EVENT_NAME("Correct"), TShaderMapRef<FDreamWindFluidCorrectCS>(ShaderMap), P, Cells);
	}
	FRDGTextureRef Curl = nullptr;
	if (LevelArgs.Vorticity > 0.0f)
	{
		Curl = Transient(GraphBuilder, Res, PF_FloatRGBA, TEXT("DreamWind.FluidCurl"));
		auto* P = GraphBuilder.AllocParameters<FDreamWindFluidCurlCS::FParameters>();
		P->Grid = Grid;
		P->B = B;
		P->D = DIntoForces;
		P->OutCurl = GraphBuilder.CreateUAV(Curl);
		FComputeShaderUtils::AddPass(GraphBuilder, RDG_EVENT_NAME("Curl"), TShaderMapRef<FDreamWindFluidCurlCS>(ShaderMap), P, Cells);
	}
	{
		auto* P = GraphBuilder.AllocParameters<FDreamWindFluidForcesCS::FParameters>();
		P->Grid = Grid;
		P->Wall = Wall;
		P->B = B;
		P->D = DIntoForces;
		P->Curl = Curl ? Curl : GraphBuilder.RegisterExternalTexture(GSystemTextures.VolumetricBlackDummy);
		P->SourceLists = SourceLists;
		P->Vorticity = Curl ? LevelArgs.Vorticity : 0.0f;
		P->Smoothing = FMath::Clamp(LevelArgs.Smoothing, 0.0f, 1.0f);
		P->NudgeTime = FMath::Max(Args.NudgeTime, 0.01f);
		P->SpongeTime = FMath::Max(Args.SpongeTime, 0.001f);
		P->OutD = GraphBuilder.CreateUAV(DForced);
		FComputeShaderUtils::AddPass(GraphBuilder, RDG_EVENT_NAME("Forces %d records", Records.Num()), TShaderMapRef<FDreamWindFluidForcesCS>(ShaderMap), P, Cells);
	}

	if (LevelArgs.PressureIterations > 0)
	{
		{
			auto* P = GraphBuilder.AllocParameters<FDreamWindFluidDivergenceCS::FParameters>();
			P->Grid = Grid;
			P->Wall = Wall;
			P->B = B;
			P->D = DForced;
			P->SourceLists = SourceLists;
			P->OutDivergence = GraphBuilder.CreateUAV(Divergence);
			FComputeShaderUtils::AddPass(GraphBuilder, RDG_EVENT_NAME("Divergence"), TShaderMapRef<FDreamWindFluidDivergenceCS>(ShaderMap), P, Cells);
		}
		RDG_EVENT_SCOPE(GraphBuilder, "Pressure %d", LevelArgs.PressureIterations);
		const FIntVector HalfCells = Groups(FIntVector((Res.X + 1) / 2, Res.Y, Res.Z));
		FRDGTextureUAVRef PressureUAV = GraphBuilder.CreateUAV(PressureNext);
		for (int32 Iteration = 0; Iteration < LevelArgs.PressureIterations; ++Iteration)
		{
			for (int32 Parity = 0; Parity < 2; ++Parity)
			{
				auto* P = GraphBuilder.AllocParameters<FDreamWindFluidPressureCS::FParameters>();
				P->Grid = Grid;
				P->Wall = Wall;
				P->Divergence = Divergence;
				P->Pressure = PressureUAV;
				P->Parity = Parity;
				P->Relaxation = FMath::Clamp(Args.Relaxation, 1.0f, 1.9f);
				FComputeShaderUtils::AddPass(GraphBuilder, RDG_EVENT_NAME("RedBlack"), TShaderMapRef<FDreamWindFluidPressureCS>(ShaderMap), P, HalfCells);
			}
		}
	}
	{
		auto* P = GraphBuilder.AllocParameters<FDreamWindFluidProjectCS::FParameters>();
		P->Grid = Grid;
		P->Wall = Wall;
		P->B = B;
		P->D = DForced;
		P->P = PressureNext;
		P->MaxSpeed = FMath::Max(Args.MaxSpeed, 100.0f);
		P->OutD = GraphBuilder.CreateUAV(DPersistent);
		FComputeShaderUtils::AddPass(GraphBuilder, RDG_EVENT_NAME("Project"), TShaderMapRef<FDreamWindFluidProjectCS>(ShaderMap), P, Cells);
	}

	FRDGTextureRef Target = RegisterExternalTexture(GraphBuilder, TargetRHI, TEXT("DreamWind.VolumeRT"));
	{
		auto* P = GraphBuilder.AllocParameters<FDreamWindFluidOutputCS::FParameters>();
		P->Grid = Grid;
		P->Wall = Wall;
		P->B = B;
		P->D = DPersistent;
		P->BC = BC;
		P->ShowDivergence = Args.bShowDivergence ? 1 : 0;
		P->OutVolume = GraphBuilder.CreateUAV(Target);
		FComputeShaderUtils::AddPass(GraphBuilder, RDG_EVENT_NAME("Output"), TShaderMapRef<FDreamWindFluidOutputCS>(ShaderMap), P, Cells);
	}
	GraphBuilder.SetTextureAccessFinal(Target, ERHIAccess::SRVMask);

	Level.Current = 1 - Level.Current;
	Level.Origin = LevelArgs.Origin;
	Level.bValid = true;
	return Target;
}

void FDreamWindFluid::AddWallPasses_RenderThread(FRDGBuilder& GraphBuilder, const FSceneView& View)
{
	check(IsInRenderingThread());
	// Once a frame (the first perspective view), and only while the walls come from the distance field.
	const uint32 Frame = View.Family ? View.Family->FrameNumber : 0;
	if (!bDistanceFieldWalls || Frame == LastWallFrame || !DoesPlatformSupportDistanceFields(View.GetShaderPlatform()))
	{
		return;
	}
	LastWallFrame = Frame;
	WallPhase = (WallPhase + 1) % DreamWindFluidImpl::WallRefreshPeriod;
	RDG_EVENT_SCOPE(GraphBuilder, "DreamWind.FluidWalls");
	AddWallPass(GraphBuilder, View, Far);
	AddWallPass(GraphBuilder, View, Near);
	WallPassCount.fetch_add(1);
}

void FDreamWindFluid::AddWallPass(FRDGBuilder& GraphBuilder, const FSceneView& View, FLevel& Level)
{
	if (!Level.bValid || !Level.Theta[Level.Current].IsValid())
	{
		return;
	}
	FRDGTextureRef Theta = GraphBuilder.RegisterExternalTexture(Level.Theta[Level.Current], TEXT("DreamWind.FluidTheta"));
	auto* P = GraphBuilder.AllocParameters<FDreamWindFluidWallsCS::FParameters>();
	P->View = View.ViewUniformBuffer;
	P->Resolution = Level.Resolution;
	P->CellSize = Level.CellSize;
	P->TranslatedOrigin = FVector3f(FVector(Level.Origin) + View.ViewMatrices.GetPreViewTranslation());
	P->WallThickness = Level.WallThickness;
	P->RefreshPhase = WallPhase;
	P->RefreshPeriod = DreamWindFluidImpl::WallRefreshPeriod;
	P->Theta = GraphBuilder.CreateUAV(Theta);
	FComputeShaderUtils::AddPass(GraphBuilder, RDG_EVENT_NAME("Walls %dx%dx%d", Level.Resolution.X, Level.Resolution.Y, Level.Resolution.Z),
		TShaderMapRef<FDreamWindFluidWallsCS>(GetGlobalShaderMap(View.GetFeatureLevel())), P, DreamWindFluidImpl::Groups(Level.Resolution));
}
