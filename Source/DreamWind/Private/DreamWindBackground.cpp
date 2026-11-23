// Copyright DreamMoon. All Rights Reserved.

#include "DreamWindBackground.h"
#include "DreamWindSubsystem.h"

#include "Engine/TextureRenderTarget2D.h"
#include "RenderGraphBuilder.h"
#include "RenderGraphUtils.h"
#include "RHIStaticStates.h"
#include "SystemTextures.h"
#include "TextureResource.h"

namespace DreamWindBackgroundImpl
{
	FTextureRenderTargetResource* ResourceOf(const TWeakObjectPtr<UTextureRenderTarget2D>& Target)
	{
		return Target.IsValid() ? Target->GameThread_GetRenderTargetResource() : nullptr;
	}

	FRDGTextureRef Register(FRDGBuilder& GraphBuilder, FTextureRenderTargetResource* Resource, const TCHAR* Name)
	{
		FRHITexture* RHI = Resource ? Resource->GetRenderTargetTexture() : nullptr;
		return RHI ? RegisterExternalTexture(GraphBuilder, RHI, Name) : nullptr;
	}
}

FDreamWindBackgroundResources DreamWind::Background::Gather(const FDreamWindFrameData& Frame)
{
	using namespace DreamWindBackgroundImpl;
	FDreamWindBackgroundResources Out;
	Out.Terrain = Frame.T1.X > 0.5f ? ResourceOf(Frame.TerrainTexture) : nullptr;
	if (Frame.L1Near.W > 0.5f)
	{
		Out.L1NearMap = ResourceOf(Frame.L1NearDerived);
		Out.L1NearHeight = ResourceOf(Frame.L1NearHeight);
	}
	if (Frame.L1Far.W > 0.5f)
	{
		Out.L1FarMap = ResourceOf(Frame.L1FarDerived);
		Out.L1FarHeight = ResourceOf(Frame.L1FarHeight);
	}
	return Out;
}

void DreamWind::Background::Fill(FRDGBuilder& GraphBuilder, FDreamWindBackgroundParameters& Out, const FDreamWindFrameData& Frame, const FDreamWindBackgroundResources& Resources)
{
	using namespace DreamWindBackgroundImpl;
	FRDGTextureRef Dummy = GraphBuilder.RegisterExternalTexture(GSystemTextures.BlackDummy);
	Out.P0 = Frame.P0;
	Out.P1 = Frame.P1;
	Out.P2 = Frame.P2;
	Out.T0 = Frame.T0;
	Out.T1 = Frame.T1;
	Out.T2 = Frame.T2;
	FRDGTextureRef Terrain = Frame.T1.X > 0.5f ? Register(GraphBuilder, Resources.Terrain, TEXT("DreamWind.TerrainRT")) : nullptr;
	if (!Terrain)
	{
		Out.T1.X = 0.0f;
	}
	Out.TerrainTexture = Terrain ? Terrain : Dummy;
	Out.TerrainSampler = TStaticSamplerState<SF_Bilinear, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();

	// The runtime terrain wind map, a level only with both its maps (the derived one and the heights).
	FRDGTextureRef NearMap = Frame.L1Near.W > 0.5f ? Register(GraphBuilder, Resources.L1NearMap, TEXT("DreamWind.L1Near")) : nullptr;
	FRDGTextureRef NearHeight = NearMap ? Register(GraphBuilder, Resources.L1NearHeight, TEXT("DreamWind.L1NearHeight")) : nullptr;
	FRDGTextureRef FarMap = Frame.L1Far.W > 0.5f ? Register(GraphBuilder, Resources.L1FarMap, TEXT("DreamWind.L1Far")) : nullptr;
	FRDGTextureRef FarHeight = FarMap ? Register(GraphBuilder, Resources.L1FarHeight, TEXT("DreamWind.L1FarHeight")) : nullptr;
	const bool bNear = NearMap && NearHeight;
	const bool bFar = FarMap && FarHeight;
	Out.L1Near = bNear ? Frame.L1Near : FVector4f::Zero();
	Out.L1Far = bFar ? Frame.L1Far : FVector4f::Zero();
	Out.L1Params = Frame.L1Params;
	Out.L1NearMap = bNear ? NearMap : Dummy;
	Out.L1FarMap = bFar ? FarMap : Dummy;
	Out.L1NearHeight = bNear ? NearHeight : Dummy;
	Out.L1FarHeight = bFar ? FarHeight : Dummy;
	Out.L1Sampler = TStaticSamplerState<SF_Bilinear, AM_Wrap, AM_Wrap, AM_Wrap>::GetRHI();
}
