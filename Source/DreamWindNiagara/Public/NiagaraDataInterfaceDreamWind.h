// Copyright DreamMoon. All Rights Reserved.

#pragma once

#include "NiagaraDataInterface.h"
#include "NiagaraCommon.h"
#include "DreamWindTypes.h"
#include "NiagaraDataInterfaceDreamWind.generated.h"

struct FNiagaraDataInterfaceGeneratedFunction;
struct FNiagaraFunctionSignature;
struct FVMExternalFunctionBindingInfo;

/**
 * Niagara access to the world's DreamWind field.
 *
 *  SampleWind(Position)   -> Velocity (cm/s), Gust
 *  GetGlobalWind()        -> Direction, Speed
 *
 * CPU sims evaluate the analytic wind + live sources from the shared header (no read-back).
 * GPU sims do the same and can additionally sample the dynamic volume (impulses, wakes, residual).
 */
UCLASS(EditInlineNew, Category = "DreamWind", CollapseCategories, meta = (DisplayName = "Dream Wind"))
class DREAMWINDNIAGARA_API UNiagaraDataInterfaceDreamWind : public UNiagaraDataInterface
{
	GENERATED_UCLASS_BODY()

	BEGIN_SHADER_PARAMETER_STRUCT(FShaderParameters, )
		SHADER_PARAMETER(FVector4f, P0)
		SHADER_PARAMETER(FVector4f, P1)
		SHADER_PARAMETER(FVector4f, P2)
		SHADER_PARAMETER(FVector4f, V0)
		SHADER_PARAMETER(FVector4f, V1)
		SHADER_PARAMETER(FVector4f, V2)
		SHADER_PARAMETER(FVector4f, V3)
		SHADER_PARAMETER(FVector4f, T0)
		SHADER_PARAMETER(FVector4f, T1)
		SHADER_PARAMETER(FVector4f, T2)
		SHADER_PARAMETER(FVector3f, SystemLWCTile)
		SHADER_PARAMETER(uint32, NumSources)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<float4>, Sources)
		SHADER_PARAMETER_RDG_TEXTURE(Texture3D<float4>, VolumeTexture)
		SHADER_PARAMETER_SAMPLER(SamplerState, VolumeSampler)
		SHADER_PARAMETER_RDG_TEXTURE(Texture3D<float4>, VolumeFarTexture)
		SHADER_PARAMETER_SAMPLER(SamplerState, VolumeFarSampler)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float4>, TerrainTexture)
		SHADER_PARAMETER_SAMPLER(SamplerState, TerrainSampler)
	END_SHADER_PARAMETER_STRUCT()

public:
	/** Analytic quality used by SampleWind on both CPU and GPU. */
	UPROPERTY(EditAnywhere, Category = "DreamWind")
	EDreamWindQuality Quality = EDreamWindQuality::Cheap;

	/** GPU sims: add the dynamic volume (impulses, wakes, residual gusts) inside its window. */
	UPROPERTY(EditAnywhere, Category = "DreamWind")
	bool bSampleVolume = true;

	/** Blend live sources into the sample (outside the volume window on GPU, everywhere on CPU). */
	UPROPERTY(EditAnywhere, Category = "DreamWind")
	bool bApplySources = true;

	//~ UObject
	virtual void PostInitProperties() override;

	//~ UNiagaraDataInterface
	virtual void GetVMExternalFunction(const FVMExternalFunctionBindingInfo& BindingInfo, void* InstanceData, FVMExternalFunction& OutFunc) override;
	virtual bool CanExecuteOnTarget(ENiagaraSimTarget Target) const override { return true; }
	virtual bool Equals(const UNiagaraDataInterface* Other) const override;
#if WITH_EDITORONLY_DATA
	virtual bool AppendCompileHash(FNiagaraCompileHashVisitor* InVisitor) const override;
	virtual bool GetFunctionHLSL(const FNiagaraDataInterfaceGPUParamInfo& ParamInfo, const FNiagaraDataInterfaceGeneratedFunction& FunctionInfo, int FunctionInstanceIndex, FString& OutHLSL) override;
	virtual void GetParameterDefinitionHLSL(const FNiagaraDataInterfaceGPUParamInfo& ParamInfo, FString& OutHLSL) override;
#endif
	virtual void BuildShaderParameters(FNiagaraShaderParametersBuilder& ShaderParametersBuilder) const override;
	virtual void SetShaderParameters(const FNiagaraDataInterfaceSetShaderParametersContext& Context) const override;

	virtual bool InitPerInstanceData(void* PerInstanceData, FNiagaraSystemInstance* SystemInstance) override;
	virtual void DestroyPerInstanceData(void* PerInstanceData, FNiagaraSystemInstance* SystemInstance) override;
	virtual int32 PerInstanceDataSize() const override;
	virtual bool HasPreSimulateTick() const override { return true; }
	virtual bool PerInstanceTick(void* PerInstanceData, FNiagaraSystemInstance* SystemInstance, float DeltaSeconds) override;
	virtual void ProvidePerInstanceDataForRenderThread(void* DataForRenderThread, void* PerInstanceData, const FNiagaraSystemInstanceID& SystemInstance) override;

	void VMSampleWind(FVectorVMExternalFunctionContext& Context);
	void VMGetGlobalWind(FVectorVMExternalFunctionContext& Context);

protected:
#if WITH_EDITORONLY_DATA
	virtual void GetFunctionsInternal(TArray<FNiagaraFunctionSignature>& OutFunctions) const override;
#endif
	virtual bool CopyToInternal(UNiagaraDataInterface* Destination) const override;

private:
	static const FName SampleWindName;
	static const FName GetGlobalWindName;
};
