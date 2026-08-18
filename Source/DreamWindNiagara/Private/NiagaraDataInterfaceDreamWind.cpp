// Copyright DreamMoon. All Rights Reserved.

#include "NiagaraDataInterfaceDreamWind.h"

#include "DreamWindSubsystem.h"
#include "DreamWindHlslShim.h"

#include "NiagaraCompileHashVisitor.h"
#include "NiagaraGpuComputeDispatchInterface.h"
#include "NiagaraShaderParametersBuilder.h"
#include "NiagaraSystemInstance.h"
#include "NiagaraTypes.h"
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
	TArray<FVector4f> Sources;                 // three float4 per source
	TWeakObjectPtr<UTexture> VolumeTexture;    // read on the render thread only through TextureReferenceRHI

	// per graph-builder transient state
	FRDGBufferSRVRef TransientSourcesSRV = nullptr;
	FRDGTextureRef TransientVolumeTexture = nullptr;
};

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
	RT->Sources.Reserve(GT->Frame.Sources.Num() * 3);
	for (const FDreamWindSourceData& S : GT->Frame.Sources)
	{
		RT->Sources.Add(ToVector4f(S.PositionRadius));
		RT->Sources.Add(ToVector4f(S.DirectionStrength));
		RT->Sources.Add(ToVector4f(S.Misc));
	}
	RT->VolumeTexture = GT->Frame.VolumeTexture.Get();
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
		const FDreamWindSample S = bFull ? DW_SampleAnalytic(P, Pos) : DW_SampleAnalyticCheap(P, Pos);
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

	if (!Data)
	{
		SP->P0 = FVector4f(1, 0, 0, 0);
		SP->P1 = SP->P2 = SP->V0 = SP->V1 = FVector4f::Zero();
		SP->NumSources = 0;
		static const TArray<FVector4f> Empty;
		SP->Sources = GraphBuilder.CreateSRV(CreateStructuredBuffer(GraphBuilder, TEXT("DreamWind.NDISources.Dummy"), Empty));
		SP->VolumeTexture = Context.GetComputeDispatchInterface().GetBlackTexture(GraphBuilder, ETextureDimension::Texture3D);
		return;
	}

	SP->P0 = Data->P0;
	SP->P1 = Data->P1;
	SP->P2 = Data->P2;
	SP->V0 = Data->V0;
	SP->V1 = Data->V1;
	SP->NumSources = uint32(Data->Sources.Num() / 3);

	if (Data->TransientSourcesSRV == nullptr)
	{
		// Copies the data into the graph, so the instance array may change under it later in the frame.
		FRDGBufferRef Buffer = CreateStructuredBuffer(GraphBuilder, TEXT("DreamWind.NDISources"), Data->Sources);
		Data->TransientSourcesSRV = GraphBuilder.CreateSRV(Buffer);
	}
	SP->Sources = Data->TransientSourcesSRV;

	FRDGTextureRef VolumeRDG = nullptr;
	if (bSampleVolume && Data->V1.W > 0.5f && Context.IsResourceBound(&SP->VolumeTexture))
	{
		if (Data->TransientVolumeTexture == nullptr)
		{
			// The weak pointer is only dereferenced for its RHI reference, which is safe on the render thread.
			if (UTexture* Texture = Data->VolumeTexture.GetEvenIfUnreachable())
			{
				FTextureReferenceRHIRef TextureRef = Texture->TextureReference.TextureReferenceRHI;
				if (TextureRef.IsValid())
				{
					FTextureRHIRef Resolved = TextureRef->GetReferencedTexture();
					if (Resolved.IsValid() && Resolved->GetDesc().Dimension == ETextureDimension::Texture3D)
					{
						Data->TransientVolumeTexture = GraphBuilder.FindExternalTexture(Resolved);
						if (Data->TransientVolumeTexture == nullptr)
						{
							Data->TransientVolumeTexture = GraphBuilder.RegisterExternalTexture(CreateRenderTarget(Resolved, TEXT("DreamWind.NDIVolume")));
							GraphBuilder.UseInternalAccessMode(Data->TransientVolumeTexture);
							Context.GetRDGExternalAccessQueue().Add(Data->TransientVolumeTexture);
						}
					}
				}
			}
		}
		VolumeRDG = Data->TransientVolumeTexture;
	}
	if (VolumeRDG == nullptr)
	{
		SP->V1.W = 0.0f;   // volume off for this dispatch
		VolumeRDG = Context.GetComputeDispatchInterface().GetBlackTexture(GraphBuilder, ETextureDimension::Texture3D);
	}
	SP->VolumeTexture = VolumeRDG;
}

#undef LOCTEXT_NAMESPACE
