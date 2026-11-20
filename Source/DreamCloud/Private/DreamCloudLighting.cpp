// Copyright DreamMoon. All Rights Reserved.

#include "DreamCloudLighting.h"
#include "DreamCloudField.h"

#include "DataDrivenShaderPlatformInfo.h"
#include "GlobalShader.h"
#include "RenderGraphBuilder.h"
#include "RenderGraphUtils.h"
#include "RHIStaticStates.h"
#include "ShaderParameterStruct.h"
#include "SystemTextures.h"
#include "TextureResource.h"

class FDreamCloudLightMapCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FDreamCloudLightMapCS);
	SHADER_USE_PARAMETER_STRUCT(FDreamCloudLightMapCS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER(FVector3f, BoxSize)
		SHADER_PARAMETER(float, Inflate)
		SHADER_PARAMETER(FVector3f, LightDir)
		SHADER_PARAMETER(float, Edge)
		SHADER_PARAMETER(float, SigmaPerUnit)
		SHADER_PARAMETER(float, HolesValid)
		SHADER_PARAMETER(uint32, Steps)
		SHADER_PARAMETER(FIntVector, Resolution)
		SHADER_PARAMETER_RDG_TEXTURE(Texture3D, Field)
		SHADER_PARAMETER_SAMPLER(SamplerState, FieldSampler)
		SHADER_PARAMETER_RDG_TEXTURE(Texture3D, Holes)
		SHADER_PARAMETER_SAMPLER(SamplerState, HolesSampler)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture3D<float>, OutLight)
	END_SHADER_PARAMETER_STRUCT()

	static constexpr int32 ThreadGroupSize = 4;

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.SetDefine(TEXT("THREADGROUP_SIZE"), ThreadGroupSize);
	}
};

IMPLEMENT_GLOBAL_SHADER(FDreamCloudLightMapCS, "/Plugin/DreamDynamicWorld/Private/DreamCloudLightMap.usf", "MainCS", SF_Compute);

namespace DreamCloudLighting
{
	FIntVector ResolutionFor(const FVector& BoxSize, int32 Longest)
	{
		const double Largest = FMath::Max(BoxSize.GetMax(), 1.0);
		FIntVector R;
		for (int32 Axis = 0; Axis < 3; ++Axis)
		{
			R[Axis] = FMath::Clamp(FMath::CeilToInt(Longest * BoxSize[Axis] / Largest), 4, Longest);
		}
		return R;
	}

	float SampleGrid(TConstArrayView<float> Grid, const FIntVector& N, const FVector& BoxSize, const FVector& Point)
	{
		if (Grid.Num() != N.X * N.Y * N.Z || Grid.Num() == 0)
		{
			return 0.0f;
		}
		const FVector U = (Point / BoxSize + FVector(0.5)) * FVector(N) - FVector(0.5);
		const FVector C(FMath::Clamp(U.X, 0.0, N.X - 1.0), FMath::Clamp(U.Y, 0.0, N.Y - 1.0), FMath::Clamp(U.Z, 0.0, N.Z - 1.0));
		const int32 X0 = FMath::Min(int32(C.X), N.X - 1);
		const int32 Y0 = FMath::Min(int32(C.Y), N.Y - 1);
		const int32 Z0 = FMath::Min(int32(C.Z), N.Z - 1);
		const int32 X1 = FMath::Min(X0 + 1, N.X - 1);
		const int32 Y1 = FMath::Min(Y0 + 1, N.Y - 1);
		const int32 Z1 = FMath::Min(Z0 + 1, N.Z - 1);
		auto At = [&](int32 X, int32 Y, int32 Z) { return Grid[X + N.X * (Y + N.Y * Z)]; };
		const float Fx = float(C.X - X0);
		const float Fy = float(C.Y - Y0);
		const float Fz = float(C.Z - Z0);
		const float C00 = FMath::Lerp(At(X0, Y0, Z0), At(X1, Y0, Z0), Fx);
		const float C10 = FMath::Lerp(At(X0, Y1, Z0), At(X1, Y1, Z0), Fx);
		const float C01 = FMath::Lerp(At(X0, Y0, Z1), At(X1, Y0, Z1), Fx);
		const float C11 = FMath::Lerp(At(X0, Y1, Z1), At(X1, Y1, Z1), Fx);
		return FMath::Lerp(FMath::Lerp(C00, C10, Fy), FMath::Lerp(C01, C11, Fy), Fz);
	}

	float TransmittanceAt(const FDreamCloudField& Field, TConstArrayView<float> Holes, const FIntVector& HoleResolution,
		const FLightMapParams& Params, const FVector& Point)
	{
		// The field's samples are relative to the box's centre, as the shader's.
		const FVector Half = 0.5 * Field.Size;
		const FVector L = FVector(Params.LightDir).GetSafeNormal();
		double Exit = UE_BIG_NUMBER;
		for (int32 Axis = 0; Axis < 3; ++Axis)
		{
			if (FMath::Abs(L[Axis]) > 1.0e-8)
			{
				const double Bound = L[Axis] > 0.0 ? Half[Axis] : -Half[Axis];
				Exit = FMath::Min(Exit, (Bound - Point[Axis]) / L[Axis]);
			}
		}
		Exit = FMath::Max(Exit, 0.0);
		const int32 Steps = FMath::Max(Params.Steps, 1);
		const double Dt = Exit / Steps;
		double Tau = 0.0;
		for (int32 i = 0; i < Steps; ++i)
		{
			const FVector Q = Point + L * ((i + 0.5) * Dt);
			const float D = Field.Sample(Q + Field.Center) - Params.Inflate;
			float Density = FMath::Clamp(-D / FMath::Max(Params.Edge, 1.0e-3f), 0.0f, 1.0f);
			if (Holes.Num() > 0)
			{
				Density *= 1.0f - FMath::SmoothStep(0.35f, 0.65f, SampleGrid(Holes, HoleResolution, Field.Size, Q));
			}
			Tau += Density;
		}
		return float(FMath::Exp(-Tau * Dt * Params.SigmaPerUnit));
	}

	void ComputeTransmittance(const FDreamCloudField& Field, TConstArrayView<float> Holes, const FIntVector& HoleResolution,
		const FLightMapParams& Params, TArray<float>& Out)
	{
		const FIntVector N = Params.Resolution;
		Out.SetNumUninitialized(N.X * N.Y * N.Z);
		for (int32 Z = 0; Z < N.Z; ++Z)
		{
			for (int32 Y = 0; Y < N.Y; ++Y)
			{
				for (int32 X = 0; X < N.X; ++X)
				{
					const FVector P = (FVector(X + 0.5, Y + 0.5, Z + 0.5) / FVector(N) - FVector(0.5)) * Field.Size;
					Out[X + N.X * (Y + N.Y * Z)] = TransmittanceAt(Field, Holes, HoleResolution, Params, P);
				}
			}
		}
	}

	void EnqueueLightMap(FTextureResource* Field, FTextureRenderTargetResource* Holes, FTextureRenderTargetResource* Target,
		const FVector& BoxSize, const FLightMapParams& Params)
	{
		check(IsInGameThread());
		if (!Field || !Target)
		{
			return;
		}
		const FVector3f Box(BoxSize);
		ENQUEUE_RENDER_COMMAND(DreamCloudLightMap)(
			[Field, Holes, Target, Box, Params](FRHICommandListImmediate& RHICmdList)
			{
				FRHITexture* FieldRHI = Field->TextureRHI;
				FRHITexture* TargetRHI = Target->GetRenderTargetTexture();
				if (!FieldRHI || !TargetRHI || !EnumHasAnyFlags(TargetRHI->GetFlags(), TexCreate_UAV) || TargetRHI->GetSizeXYZ() != Params.Resolution)
				{
					return;
				}
				FRDGBuilder GraphBuilder(RHICmdList, RDG_EVENT_NAME("DreamCloudLightMap"));
				FRDGTextureRef Out = RegisterExternalTexture(GraphBuilder, TargetRHI, TEXT("DreamCloud.LightMap"));
				FRDGTextureRef FieldRDG = RegisterExternalTexture(GraphBuilder, FieldRHI, TEXT("DreamCloud.Field"));
				FRHITexture* HolesRHI = Holes ? Holes->GetRenderTargetTexture() : nullptr;
				FRDGTextureRef HolesRDG = HolesRHI ? RegisterExternalTexture(GraphBuilder, HolesRHI, TEXT("DreamCloud.Holes"))
					: GraphBuilder.RegisterExternalTexture(GSystemTextures.VolumetricBlackDummy);

				FDreamCloudLightMapCS::FParameters* P = GraphBuilder.AllocParameters<FDreamCloudLightMapCS::FParameters>();
				P->BoxSize = Box;
				P->Inflate = Params.Inflate;
				P->LightDir = Params.LightDir.GetSafeNormal();
				P->Edge = Params.Edge;
				P->SigmaPerUnit = Params.SigmaPerUnit;
				P->HolesValid = HolesRHI ? 1.0f : 0.0f;
				P->Steps = uint32(FMath::Max(Params.Steps, 1));
				P->Resolution = Params.Resolution;
				P->Field = FieldRDG;
				P->FieldSampler = TStaticSamplerState<SF_Trilinear, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();
				P->Holes = HolesRDG;
				P->HolesSampler = TStaticSamplerState<SF_Trilinear, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();
				P->OutLight = GraphBuilder.CreateUAV(Out);

				TShaderMapRef<FDreamCloudLightMapCS> Shader(GetGlobalShaderMap(GMaxRHIFeatureLevel));
				FComputeShaderUtils::AddPass(GraphBuilder, RDG_EVENT_NAME("DreamCloudLightMap %dx%dx%d", Params.Resolution.X, Params.Resolution.Y, Params.Resolution.Z),
					Shader, P, FComputeShaderUtils::GetGroupCount(Params.Resolution, FDreamCloudLightMapCS::ThreadGroupSize));
				GraphBuilder.SetTextureAccessFinal(Out, ERHIAccess::SRVMask);
				GraphBuilder.Execute();
			});
	}

	void EnqueueHoleUpload(FTextureRenderTargetResource* Target, const FIntVector& Resolution, TArray<uint8>&& Texels)
	{
		check(IsInGameThread());
		if (!Target || Texels.Num() != Resolution.X * Resolution.Y * Resolution.Z)
		{
			return;
		}
		ENQUEUE_RENDER_COMMAND(DreamCloudHoles)(
			[Target, Resolution, Texels = MoveTemp(Texels)](FRHICommandListImmediate& RHICmdList)
			{
				FRHITexture* TargetRHI = Target->GetRenderTargetTexture();
				if (!TargetRHI || TargetRHI->GetSizeXYZ() != Resolution)
				{
					return;
				}
				const FUpdateTextureRegion3D Region(0, 0, 0, 0, 0, 0, Resolution.X, Resolution.Y, Resolution.Z);
				RHICmdList.Transition(FRHITransitionInfo(TargetRHI, ERHIAccess::Unknown, ERHIAccess::CopyDest));
				RHICmdList.UpdateTexture3D(TargetRHI, 0, Region, Resolution.X, Resolution.X * Resolution.Y, Texels.GetData());
				RHICmdList.Transition(FRHITransitionInfo(TargetRHI, ERHIAccess::CopyDest, ERHIAccess::SRVMask));
			});
	}
}
