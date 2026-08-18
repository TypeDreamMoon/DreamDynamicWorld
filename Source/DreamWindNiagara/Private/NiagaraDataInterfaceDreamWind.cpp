// Copyright DreamMoon. All Rights Reserved.

#include "NiagaraDataInterfaceDreamWind.h"

#include "DreamWindSubsystem.h"
#include "DreamWindHlslShim.h"

#include "NiagaraCompileHashVisitor.h"
#include "NiagaraGpuComputeDispatchInterface.h"
#include "NiagaraShaderParametersBuilder.h"
#include "NiagaraSystemInstance.h"
#include "NiagaraTypes.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Engine/TextureRenderTargetVolume.h"
#include "Engine/World.h"
#include "RenderGraphBuilder.h"
#include "RenderGraphUtils.h"
#include "RHIStaticStates.h"
#include "TextureResource.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(NiagaraDataInterfaceDreamWind)

#define LOCTEXT_NAMESPACE "NiagaraDataInterfaceDreamWind"

const FName UNiagaraDataInterfaceDreamWind::SampleWindName(TEXT("SampleWind"));
const FName UNiagaraDataInterfaceDreamWind::GetGlobalWindName(TEXT("GetGlobalWind"));

static const TCHAR* GDreamWindDITemplateShaderFile = TEXT("/Plugin/DreamWind/Private/NiagaraDataInterfaceDreamWindTemplate.ush");

// -------------------------------------------------------------------------------------------------
// Instance data
// -------------------------------------------------------------------------------------------------

/** Game thread. */
struct FNDIDreamWindInstanceData_GT
{
	FDreamWindFrameData Frame;
	FNiagaraLWCConverter LWCConverter;
	bool bHaveWind = false;
};

/** Game -> render thread payload, and the render-thread copy. */
struct FNDIDreamWindInstanceData_RT
{
	FVector4f P0 = FVector4f(1, 0, 0, 0);
	FVector4f P1 = FVector4f::Zero();
	FVector4f P2 = FVector4f::Zero();
	FVector4f V0 = FVector4f::Zero();
	FVector4f V1 = FVector4f::Zero();
	FVector4f V2 = FVector4f::Zero();
	FVector4f V3 = FVector4f::Zero();
	FVector4f T0 = FVector4f::Zero();
	FVector4f T1 = FVector4f::Zero();
	FVector4f T2 = FVector4f::Zero();
	TArray<FVector4f> Sources;                   // DW_SOURCE_FLOAT4S float4 per source
	TWeakObjectPtr<UTexture> VolumeTexture;      // read on the render thread only through TextureReferenceRHI
	TWeakObjectPtr<UTexture> VolumeFarTexture;
	TWeakObjectPtr<UTexture> TerrainTexture;

	// per graph-builder transient state
	FRDGBufferSRVRef TransientSourcesSRV = nullptr;
	FRDGTextureRef TransientVolumeTexture = nullptr;
	FRDGTextureRef TransientVolumeFarTexture = nullptr;
	FRDGTextureRef TransientTerrainTexture = nullptr;
};

/** Registers a render target's RHI texture with the graph once per builder; null when it is not usable. */
static FRDGTextureRef RegisterTextureForDispatch(const FNiagaraDataInterfaceSetShaderParametersContext& Context, FRDGBuilder& GraphBuilder, const TWeakObjectPtr<UTexture>& WeakTexture, FRDGTextureRef& InOutTransient, const TCHAR* Name, ETextureDimension Dimension)
{
	if (InOutTransient == nullptr)
	{
		// The weak pointer is only dereferenced for its RHI reference, which is safe on the render thread.
		if (UTexture* Texture = WeakTexture.GetEvenIfUnreachable())
		{
			FTextureReferenceRHIRef TextureRef = Texture->TextureReference.TextureReferenceRHI;
			if (TextureRef.IsValid())
			{
				FTextureRHIRef Resolved = TextureRef->GetReferencedTexture();
				if (Resolved.IsValid() && Resolved->GetDesc().Dimension == Dimension)
				{
					InOutTransient = GraphBuilder.FindExternalTexture(Resolved);
					if (InOutTransient == nullptr)
					{
						InOutTransient = GraphBuilder.RegisterExternalTexture(CreateRenderTarget(Resolved, Name));
						GraphBuilder.UseInternalAccessMode(InOutTransient);
						Context.GetRDGExternalAccessQueue().Add(InOutTransient);
					}
				}
			}
		}
	}
	return InOutTransient;
}

struct FNDIDreamWindProxy : public FNiagaraDataInterfaceProxy
{
	virtual int32 PerInstanceDataPassedToRenderThreadSize() const override { return sizeof(FNDIDreamWindInstanceData_RT); }

	virtual void ConsumePerInstanceDataFromGameThread(void* PerInstanceData, const FNiagaraSystemInstanceID& InstanceID) override
	{
		FNDIDreamWindInstanceData_RT* FromGT = static_cast<FNDIDreamWindInstanceData_RT*>(PerInstanceData);
		FNDIDreamWindInstanceData_RT& Data = InstanceData_RT.FindOrAdd(InstanceID);
		Data = MoveTemp(*FromGT);
		Data.TransientSourcesSRV = nullptr;
		Data.TransientVolumeTexture = nullptr;
		Data.TransientVolumeFarTexture = nullptr;
		Data.TransientTerrainTexture = nullptr;
		FromGT->~FNDIDreamWindInstanceData_RT();
	}

	virtual void PostSimulate(const FNDIGpuComputePostSimulateContext& Context) override
	{
		if (Context.IsFinalPostSimulate())
		{
			if (FNDIDreamWindInstanceData_RT* Data = InstanceData_RT.Find(Context.GetSystemInstanceID()))
			{
				Data->TransientSourcesSRV = nullptr;
				Data->TransientVolumeTexture = nullptr;
				Data->TransientVolumeFarTexture = nullptr;
				Data->TransientTerrainTexture = nullptr;
			}
		}
	}

	TMap<FNiagaraSystemInstanceID, FNDIDreamWindInstanceData_RT> InstanceData_RT;
};

// -------------------------------------------------------------------------------------------------

UNiagaraDataInterfaceDreamWind::UNiagaraDataInterfaceDreamWind(FObjectInitializer const& ObjectInitializer)
	: Super(ObjectInitializer)
{
	Proxy.Reset(new FNDIDreamWindProxy());
}

void UNiagaraDataInterfaceDreamWind::PostInitProperties()
{
	Super::PostInitProperties();
	if (HasAnyFlags(RF_ClassDefaultObject))
	{
		ENiagaraTypeRegistryFlags Flags = ENiagaraTypeRegistryFlags::AllowAnyVariable | ENiagaraTypeRegistryFlags::AllowParameter;
		FNiagaraTypeRegistry::Register(FNiagaraTypeDefinition(GetClass()), Flags);
	}
}

bool UNiagaraDataInterfaceDreamWind::CopyToInternal(UNiagaraDataInterface* Destination) const
{
	if (!Super::CopyToInternal(Destination))
	{
		return false;
	}
	UNiagaraDataInterfaceDreamWind* Dest = CastChecked<UNiagaraDataInterfaceDreamWind>(Destination);
	Dest->Quality = Quality;
	Dest->bSampleVolume = bSampleVolume;
	Dest->bApplySources = bApplySources;
	return true;
}

bool UNiagaraDataInterfaceDreamWind::Equals(const UNiagaraDataInterface* Other) const
{
	if (!Super::Equals(Other))
	{
		return false;
	}
	const UNiagaraDataInterfaceDreamWind* O = CastChecked<const UNiagaraDataInterfaceDreamWind>(Other);
	return O->Quality == Quality && O->bSampleVolume == bSampleVolume && O->bApplySources == bApplySources;
}

// -------------------------------------------------------------------------------------------------
// Per instance
// -------------------------------------------------------------------------------------------------

int32 UNiagaraDataInterfaceDreamWind::PerInstanceDataSize() const
{
	return sizeof(FNDIDreamWindInstanceData_GT);
}

bool UNiagaraDataInterfaceDreamWind::InitPerInstanceData(void* PerInstanceData, FNiagaraSystemInstance* SystemInstance)
{
	new (PerInstanceData) FNDIDreamWindInstanceData_GT();
	return true;
}

void UNiagaraDataInterfaceDreamWind::DestroyPerInstanceData(void* PerInstanceData, FNiagaraSystemInstance* SystemInstance)
{
	FNDIDreamWindInstanceData_GT* InstanceData = static_cast<FNDIDreamWindInstanceData_GT*>(PerInstanceData);
	InstanceData->~FNDIDreamWindInstanceData_GT();

	ENQUEUE_RENDER_COMMAND(NDIDreamWind_RemoveInstance)
	(
		[RT_Proxy = GetProxyAs<FNDIDreamWindProxy>(), RT_InstanceID = SystemInstance->GetId()](FRHICommandListImmediate&)
		{
			RT_Proxy->InstanceData_RT.Remove(RT_InstanceID);
		}
	);
}

bool UNiagaraDataInterfaceDreamWind::PerInstanceTick(void* PerInstanceData, FNiagaraSystemInstance* SystemInstance, float DeltaSeconds)
{
	FNDIDreamWindInstanceData_GT* InstanceData = static_cast<FNDIDreamWindInstanceData_GT*>(PerInstanceData);
	if (!InstanceData || !SystemInstance)
	{
		return false;
	}

	InstanceData->LWCConverter = SystemInstance->GetLWCConverter();
	InstanceData->bHaveWind = false;

	if (UWorld* World = SystemInstance->GetWorld())
	{
		if (const UDreamWindSubsystem* Wind = World->GetSubsystem<UDreamWindSubsystem>())
		{
			InstanceData->Frame = Wind->GetFrameData();
			InstanceData->bHaveWind = Wind->IsWindEnabled() || Wind->GetTickCount() == 0;
		}
	}
	if (!InstanceData->bHaveWind)
	{
		InstanceData->Frame = FDreamWindFrameData();
		InstanceData->Frame.P0 = FVector4f(1, 0, 0, 0);
	}
	return false;
}

void UNiagaraDataInterfaceDreamWind::ProvidePerInstanceDataForRenderThread(void* DataForRenderThread, void* PerInstanceData, const FNiagaraSystemInstanceID& SystemInstance)
{
	const FNDIDreamWindInstanceData_GT* GT = static_cast<FNDIDreamWindInstanceData_GT*>(PerInstanceData);
	FNDIDreamWindInstanceData_RT* RT = new (DataForRenderThread) FNDIDreamWindInstanceData_RT();

	using namespace DreamWind::Hlsl;
	RT->P0 = GT->Frame.P0;
	RT->P1 = GT->Frame.P1;
	RT->P2 = GT->Frame.P2;
	RT->V0 = GT->Frame.V0;
	RT->V1 = GT->Frame.V1;
	RT->V2 = GT->Frame.V2;
	RT->V3 = GT->Frame.V3;
	RT->T0 = GT->Frame.T0;
	RT->T1 = GT->Frame.T1;
	RT->T2 = GT->Frame.T2;
	RT->Sources.Reserve(GT->Frame.Sources.Num() * DW_SOURCE_FLOAT4S);
	for (const FDreamWindSourceData& S : GT->Frame.Sources)
	{
		RT->Sources.Add(ToVector4f(S.PositionRadius));
		RT->Sources.Add(ToVector4f(S.DirectionStrength));
		RT->Sources.Add(ToVector4f(S.Misc));
		RT->Sources.Add(ToVector4f(S.BoxExtent));
		RT->Sources.Add(ToVector4f(S.BoxRotation));
	}
	RT->VolumeTexture = GT->Frame.VolumeTexture.Get();
	RT->VolumeFarTexture = GT->Frame.VolumeFarTexture.Get();
	RT->TerrainTexture = GT->Frame.TerrainTexture.Get();
}

// -------------------------------------------------------------------------------------------------
// Functions
// -------------------------------------------------------------------------------------------------

#if WITH_EDITORONLY_DATA
void UNiagaraDataInterfaceDreamWind::GetFunctionsInternal(TArray<FNiagaraFunctionSignature>& OutFunctions) const
{
	{
		FNiagaraFunctionSignature& Sig = OutFunctions.AddDefaulted_GetRef();
		Sig.Name = SampleWindName;
		Sig.bMemberFunction = true;
		Sig.bRequiresContext = false;
		Sig.bSupportsCPU = true;
		Sig.bSupportsGPU = true;
		Sig.SetDescription(LOCTEXT("SampleWindDesc", "Wind velocity (cm/s, world space) and gust factor at a position: analytic wind + sources, plus the dynamic volume on GPU."));
		Sig.AddInput(FNiagaraVariable(FNiagaraTypeDefinition(GetClass()), TEXT("Wind")));
		Sig.AddInput(FNiagaraVariable(FNiagaraTypeDefinition::GetPositionDef(), TEXT("Position")));
		Sig.AddOutput(FNiagaraVariable(FNiagaraTypeDefinition::GetVec3Def(), TEXT("Velocity")));
		Sig.AddOutput(FNiagaraVariable(FNiagaraTypeDefinition::GetFloatDef(), TEXT("Gust")));
	}
	{
		FNiagaraFunctionSignature& Sig = OutFunctions.AddDefaulted_GetRef();
		Sig.Name = GetGlobalWindName;
		Sig.bMemberFunction = true;
		Sig.bRequiresContext = false;
		Sig.bSupportsCPU = true;
		Sig.bSupportsGPU = true;
		Sig.SetDescription(LOCTEXT("GetGlobalWindDesc", "The global wind direction (unit) and mean speed (cm/s)."));
		Sig.AddInput(FNiagaraVariable(FNiagaraTypeDefinition(GetClass()), TEXT("Wind")));
		Sig.AddOutput(FNiagaraVariable(FNiagaraTypeDefinition::GetVec3Def(), TEXT("Direction")));
		Sig.AddOutput(FNiagaraVariable(FNiagaraTypeDefinition::GetFloatDef(), TEXT("Speed")));
	}
}
#endif

void UNiagaraDataInterfaceDreamWind::GetVMExternalFunction(const FVMExternalFunctionBindingInfo& BindingInfo, void* InstanceData, FVMExternalFunction& OutFunc)
{
	if (BindingInfo.Name == SampleWindName)
	{
		OutFunc = FVMExternalFunction::CreateLambda([this](FVectorVMExternalFunctionContext& Context) { this->VMSampleWind(Context); });
	}
	else if (BindingInfo.Name == GetGlobalWindName)
	{
		OutFunc = FVMExternalFunction::CreateLambda([this](FVectorVMExternalFunctionContext& Context) { this->VMGetGlobalWind(Context); });
	}
}

void UNiagaraDataInterfaceDreamWind::VMSampleWind(FVectorVMExternalFunctionContext& Context)
{
	using namespace DreamWind::Hlsl;

	VectorVM::FUserPtrHandler<FNDIDreamWindInstanceData_GT> InstData(Context);
	FNDIInputParam<FNiagaraPosition> InPosition(Context);
	FNDIOutputParam<FVector3f> OutVelocity(Context);
	FNDIOutputParam<float> OutGust(Context);

	const FDreamWindParams P = InstData->Frame.GetParams();
	const TArray<FDreamWindSourceData>& Sources = InstData->Frame.Sources;
	const bool bFull = Quality == EDreamWindQuality::Full;
	const bool bHave = InstData->bHaveWind;
	const FDreamWindTerrainParams TP = InstData->Frame.GetTerrainParams();
	const FDreamWindTerrainCpuData* Terrain = (TP.Enabled > 0.0f && InstData->Frame.TerrainCpu.IsValid()) ? InstData->Frame.TerrainCpu.Get() : nullptr;

	for (int32 i = 0; i < Context.GetNumInstances(); ++i)
	{
		const FNiagaraPosition SimPos = InPosition.GetAndAdvance();
		if (!bHave)
		{
			OutVelocity.SetAndAdvance(FVector3f::ZeroVector);
			OutGust.SetAndAdvance(1.0f);
			continue;
		}
		const FVector WorldPos = InstData->LWCConverter.ConvertSimulationPositionToWorld(SimPos);
		const float3 Pos = ToHlsl(WorldPos);
		FDreamWindSample S = bFull ? DW_SampleAnalytic(P, Pos) : DW_SampleAnalyticCheap(P, Pos);
		if (Terrain)
		{
			S = Terrain->ApplyAll(S, TP, Pos);
		}
		float3 V = S.Velocity;
		if (bApplySources)
		{
			for (const FDreamWindSourceData& Src : Sources)
			{
				V = DW_ApplySource(V, Src, Pos, 1.0f);
			}
		}
		OutVelocity.SetAndAdvance(ToVector3f(V));
		OutGust.SetAndAdvance(S.Gust);
	}
}

void UNiagaraDataInterfaceDreamWind::VMGetGlobalWind(FVectorVMExternalFunctionContext& Context)
{
	VectorVM::FUserPtrHandler<FNDIDreamWindInstanceData_GT> InstData(Context);
	FNDIOutputParam<FVector3f> OutDirection(Context);
	FNDIOutputParam<float> OutSpeed(Context);

	const FVector4f& P0 = InstData->Frame.P0;
	const FVector3f Dir(P0.X, P0.Y, P0.Z);
	const float Speed = InstData->bHaveWind ? P0.W : 0.0f;
	for (int32 i = 0; i < Context.GetNumInstances(); ++i)
	{
		OutDirection.SetAndAdvance(Dir);
		OutSpeed.SetAndAdvance(Speed);
	}
}

// -------------------------------------------------------------------------------------------------
// GPU
// -------------------------------------------------------------------------------------------------

#if WITH_EDITORONLY_DATA
bool UNiagaraDataInterfaceDreamWind::AppendCompileHash(FNiagaraCompileHashVisitor* InVisitor) const
{
	if (!Super::AppendCompileHash(InVisitor))
	{
		return false;
	}
	InVisitor->UpdateShaderFile(GDreamWindDITemplateShaderFile);
	InVisitor->UpdateShaderFile(TEXT("/Plugin/DreamWind/Shared/DreamWindShared.h"));
	InVisitor->UpdateShaderParameters<FShaderParameters>();
	InVisitor->UpdatePOD(TEXT("Quality"), static_cast<int32>(Quality));
	InVisitor->UpdatePOD(TEXT("SampleVolume"), bSampleVolume ? 1 : 0);
	InVisitor->UpdatePOD(TEXT("ApplySources"), bApplySources ? 1 : 0);
	return true;
}

bool UNiagaraDataInterfaceDreamWind::GetFunctionHLSL(const FNiagaraDataInterfaceGPUParamInfo& ParamInfo, const FNiagaraDataInterfaceGeneratedFunction& FunctionInfo, int FunctionInstanceIndex, FString& OutHLSL)
{
	return FunctionInfo.DefinitionName == SampleWindName || FunctionInfo.DefinitionName == GetGlobalWindName;
}

void UNiagaraDataInterfaceDreamWind::GetParameterDefinitionHLSL(const FNiagaraDataInterfaceGPUParamInfo& ParamInfo, FString& OutHLSL)
{
	const TMap<FString, FStringFormatArg> TemplateArgs =
	{
		{TEXT("ParameterName"),   ParamInfo.DataInterfaceHLSLSymbol},
		{TEXT("AnalyticFunction"), Quality == EDreamWindQuality::Full ? TEXT("DW_SampleAnalytic") : TEXT("DW_SampleAnalyticCheap")},
		{TEXT("SampleVolume"),    bSampleVolume ? TEXT("1") : TEXT("0")},
		{TEXT("ApplySources"),    bApplySources ? TEXT("1") : TEXT("0")},
	};
	AppendTemplateHLSL(OutHLSL, GDreamWindDITemplateShaderFile, TemplateArgs);
}
#endif

void UNiagaraDataInterfaceDreamWind::BuildShaderParameters(FNiagaraShaderParametersBuilder& ShaderParametersBuilder) const
{
	ShaderParametersBuilder.AddNestedStruct<FShaderParameters>();
}

void UNiagaraDataInterfaceDreamWind::SetShaderParameters(const FNiagaraDataInterfaceSetShaderParametersContext& Context) const
{
	FNDIDreamWindProxy& DIProxy = Context.GetProxy<FNDIDreamWindProxy>();
	FNDIDreamWindInstanceData_RT* Data = DIProxy.InstanceData_RT.Find(Context.GetSystemInstanceID());
	FRDGBuilder& GraphBuilder = Context.GetGraphBuilder();
	FShaderParameters* SP = Context.GetParameterNestedStruct<FShaderParameters>();

	SP->SystemLWCTile = Context.GetSystemLWCTile();
	SP->VolumeSampler = TStaticSamplerState<SF_Trilinear, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();
	SP->VolumeFarSampler = SP->VolumeSampler;
	SP->TerrainSampler = TStaticSamplerState<SF_Bilinear, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();
	FRDGTextureRef BlackVolume = Context.GetComputeDispatchInterface().GetBlackTexture(GraphBuilder, ETextureDimension::Texture3D);
	FRDGTextureRef BlackTexture = Context.GetComputeDispatchInterface().GetBlackTexture(GraphBuilder, ETextureDimension::Texture2D);

	if (!Data)
	{
		SP->P0 = FVector4f(1, 0, 0, 0);
		SP->P1 = SP->P2 = SP->V0 = SP->V1 = SP->V2 = SP->V3 = SP->T0 = SP->T1 = SP->T2 = FVector4f::Zero();
		SP->NumSources = 0;
		static const TArray<FVector4f> Empty;
		SP->Sources = GraphBuilder.CreateSRV(CreateStructuredBuffer(GraphBuilder, TEXT("DreamWind.NDISources.Dummy"), Empty));
		SP->VolumeTexture = BlackVolume;
		SP->VolumeFarTexture = BlackVolume;
		SP->TerrainTexture = BlackTexture;
		return;
	}

	SP->P0 = Data->P0;
	SP->P1 = Data->P1;
	SP->P2 = Data->P2;
	SP->V0 = Data->V0;
	SP->V1 = Data->V1;
	SP->V2 = Data->V2;
	SP->V3 = Data->V3;
	SP->T0 = Data->T0;
	SP->T1 = Data->T1;
	SP->T2 = Data->T2;
	SP->NumSources = uint32(Data->Sources.Num() / DW_SOURCE_FLOAT4S);

	if (Data->TransientSourcesSRV == nullptr)
	{
		// Copies the data into the graph, so the instance array may change under it later in the frame.
		FRDGBufferRef Buffer = CreateStructuredBuffer(GraphBuilder, TEXT("DreamWind.NDISources"), Data->Sources);
		Data->TransientSourcesSRV = GraphBuilder.CreateSRV(Buffer);
	}
	SP->Sources = Data->TransientSourcesSRV;

	FRDGTextureRef VolumeRDG = nullptr;
	FRDGTextureRef VolumeFarRDG = nullptr;
	if (bSampleVolume)
	{
		if (Data->V1.W > 0.5f && Context.IsResourceBound(&SP->VolumeTexture))
		{
			VolumeRDG = RegisterTextureForDispatch(Context, GraphBuilder, Data->VolumeTexture, Data->TransientVolumeTexture, TEXT("DreamWind.NDIVolume"), ETextureDimension::Texture3D);
		}
		if (Data->V3.W > 0.5f && Context.IsResourceBound(&SP->VolumeFarTexture))
		{
			VolumeFarRDG = RegisterTextureForDispatch(Context, GraphBuilder, Data->VolumeFarTexture, Data->TransientVolumeFarTexture, TEXT("DreamWind.NDIVolumeFar"), ETextureDimension::Texture3D);
		}
	}
	FRDGTextureRef TerrainRDG = nullptr;
	if (Data->T1.X > 0.5f && Context.IsResourceBound(&SP->TerrainTexture))
	{
		TerrainRDG = RegisterTextureForDispatch(Context, GraphBuilder, Data->TerrainTexture, Data->TransientTerrainTexture, TEXT("DreamWind.NDITerrain"), ETextureDimension::Texture2D);
	}
	if (TerrainRDG == nullptr)
	{
		SP->T1.X = 0.0f;
		TerrainRDG = BlackTexture;
	}
	SP->TerrainTexture = TerrainRDG;
	if (VolumeRDG == nullptr)
	{
		SP->V1.W = 0.0f;   // near level off for this dispatch
		VolumeRDG = BlackVolume;
	}
	if (VolumeFarRDG == nullptr)
	{
		SP->V3.W = 0.0f;   // far level off for this dispatch
		VolumeFarRDG = BlackVolume;
	}
	SP->VolumeTexture = VolumeRDG;
	SP->VolumeFarTexture = VolumeFarRDG;
}

#undef LOCTEXT_NAMESPACE
