// Copyright DreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "RenderGraphResources.h"
#include "ShaderParameterMacros.h"

class FRDGBuilder;
class FTextureRenderTargetResource;
struct FDreamWindFrameData;

/** What DreamWindBackground.ush reads: the analytic wind's parameters and the terrain maps (baked, runtime near and far). */
BEGIN_SHADER_PARAMETER_STRUCT(FDreamWindBackgroundParameters, )
	SHADER_PARAMETER(FVector4f, P0)
	SHADER_PARAMETER(FVector4f, P1)
	SHADER_PARAMETER(FVector4f, P2)
	SHADER_PARAMETER(FVector4f, T0)
	SHADER_PARAMETER(FVector4f, T1)
	SHADER_PARAMETER(FVector4f, T2)
	SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float4>, TerrainTexture)
	SHADER_PARAMETER_SAMPLER(SamplerState, TerrainSampler)
	SHADER_PARAMETER(FVector4f, L1Near)
	SHADER_PARAMETER(FVector4f, L1Far)
	SHADER_PARAMETER(FVector4f, L1Params)
	SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float4>, L1NearMap)
	SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float4>, L1FarMap)
	SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float>, L1NearHeight)
	SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float>, L1FarHeight)
	SHADER_PARAMETER_SAMPLER(SamplerState, L1Sampler)
END_SHADER_PARAMETER_STRUCT()

/** The render targets behind the background (game thread gathers them, render thread registers them; null = absent). */
struct FDreamWindBackgroundResources
{
	FTextureRenderTargetResource* Terrain = nullptr;
	FTextureRenderTargetResource* L1NearMap = nullptr;
	FTextureRenderTargetResource* L1FarMap = nullptr;
	FTextureRenderTargetResource* L1NearHeight = nullptr;
	FTextureRenderTargetResource* L1FarHeight = nullptr;
};

namespace DreamWind::Background
{
	/** Game thread: this frame's resources. */
	FDreamWindBackgroundResources Gather(const FDreamWindFrameData& Frame);

	/** Render thread: fill the parameters; a missing map switches its part off. */
	void Fill(FRDGBuilder& GraphBuilder, FDreamWindBackgroundParameters& Out, const FDreamWindFrameData& Frame, const FDreamWindBackgroundResources& Resources);
}
