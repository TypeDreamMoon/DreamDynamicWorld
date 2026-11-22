// Copyright DreamMoon. All Rights Reserved.

#include "DreamAtmosphereHlslShim.h"
#include "DreamAtmosphereSettings.h"
#include "DreamAtmosphereSubsystem.h"
#include "DreamAtmosphereTables.h"
#include "DreamAtmosphereTypes.h"

#include "Engine/TextureRenderTarget2D.h"
#include "Engine/TextureRenderTargetVolume.h"
#include "Engine/World.h"
#include "Misc/App.h"
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "RenderingThread.h"
#include "TextureResource.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace DreamAtmosphere::GpuTests
{
	constexpr EAutomationTestFlags Flags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter;

	DreamWind::Hlsl::FDSA_Atmosphere Unpacked(const FDreamAtmosphereParameters& P)
	{
		FVector4f V[5];
		P.Pack(V);
		using DreamWind::Hlsl::ToHlsl;
		return DreamWind::Hlsl::DSA_Unpack(ToHlsl(V[0]), ToHlsl(V[1]), ToHlsl(V[2]), ToHlsl(V[3]), ToHlsl(V[4]));
	}

	/** Within Relative of each other per channel, or both under Floor (half floats and the GPU's interpolated reads). */
	bool Near(const FLinearColor& Gpu, const DreamWind::Hlsl::float3& Cpu, float Relative, float Floor)
	{
		auto One = [&](float G, float C) { return FMath::Abs(G - C) <= FMath::Max(Relative * FMath::Max(FMath::Abs(G), FMath::Abs(C)), Floor); };
		return One(Gpu.R, Cpu.x) && One(Gpu.G, Cpu.y) && One(Gpu.B, Cpu.z);
	}

	bool CanRender()
	{
		return FApp::CanEverRender() && !GUsingNullRHI;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDreamAtmosphereGpuTablesTest, "DreamAtmosphere.Gpu.TablesMatchCpu", DreamAtmosphere::GpuTests::Flags)
bool FDreamAtmosphereGpuTablesTest::RunTest(const FString& Parameters)
{
	using namespace DreamWind::Hlsl;
	using namespace DreamAtmosphere::GpuTests;
	if (!CanRender())
	{
		AddInfo(TEXT("No renderer (-nullrhi): the tables are not built. Run with a renderer (-RenderOffscreen)."));
		return true;
	}
	const FDreamAtmosphereParameters Air;
	const FDSA_Atmosphere A = Unpacked(Air);
	UDreamAtmosphereTables* Tables = NewObject<UDreamAtmosphereTables>();
	Tables->Build(Air);
	FlushRenderingCommands();
	if (!TestTrue(TEXT("built"), Tables->IsBuiltFor(Air) && Tables->GetTransmittance() && Tables->GetMultiScattering() && Tables->GetSky()))
	{
		return false;
	}

	// The transmittance table: the direct integral at its texels (the same code; half floats).
	{
		UTextureRenderTarget2D* T = Tables->GetTransmittance();
		TArray<FLinearColor> Pixels;
		TestTrue(TEXT("transmittance read back"), T->GameThread_GetRenderTargetResource()->ReadLinearColorPixels(Pixels));
		const int32 W = T->SizeX, H = T->SizeY;
		int32 Off = 0, Count = 0;
		for (int32 y = 0; y < H && Pixels.Num() == W * H; y += H / 8)
		{
			for (int32 x = 0; x < W; x += W / 8)
			{
				const float2 RMu = DSA_TransmittanceRMu(A, float2(DSA_TableParam(float(x), float(W)), DSA_TableParam(float(y), float(H))));
				Off += Near(Pixels[y * W + x], DSA_TransmittanceToTopDirect(A, RMu.x, RMu.y), 0.004f, 1.0e-4f) ? 0 : 1;
				++Count;
			}
		}
		TestTrue(FString::Printf(TEXT("the transmittance table is the model's (%d of %d off)"), Off, Count), Count > 0 && Off == 0);
	}

	// The multiple scattering table: the GPU reads the transmittance from its table, the CPU integrates it.
	{
		UTextureRenderTarget2D* M = Tables->GetMultiScattering();
		TArray<FLinearColor> Pixels;
		TestTrue(TEXT("multiple scattering read back"), M->GameThread_GetRenderTargetResource()->ReadLinearColorPixels(Pixels));
		const int32 W = M->SizeX, H = M->SizeY;
		int32 Off = 0, Count = 0;
		FString Worst;
		for (const FIntPoint& Texel : { FIntPoint(W - 1, 0), FIntPoint(W * 3 / 4, 0), FIntPoint(W / 2 + 1, 0), FIntPoint(W - 1, H / 2), FIntPoint(W * 3 / 4, H - 1) })
		{
			if (Pixels.Num() != W * H)
			{
				break;
			}
			const float2 RMu = DSA_MultiScatteringRMu(A, float2(DSA_TableParam(float(Texel.X), float(W)), DSA_TableParam(float(Texel.Y), float(H))));
			const float3 Cpu = DSA_MultiScatteringDirect(A, RMu.x, RMu.y);
			const FLinearColor& Gpu = Pixels[Texel.Y * W + Texel.X];
			if (!Near(Gpu, Cpu, 0.02f, 1.0e-5f))
			{
				++Off;
				Worst = FString::Printf(TEXT(" e.g. (%d, %d): GPU (%.5f, %.5f, %.5f), CPU (%.5f, %.5f, %.5f)"), Texel.X, Texel.Y, Gpu.R, Gpu.G, Gpu.B, Cpu.x, Cpu.y, Cpu.z);
			}
			++Count;
		}
		TestTrue(FString::Printf(TEXT("the multiple scattering table is the model's within 2%% (%d of %d off)%s"), Off, Count, *Worst), Count > 0 && Off == 0);
	}

	// The sky table, a slice at a time: noon, a low sun, sunset, twilight.
	{
		const UTextureRenderTargetVolume* Sky = Tables->GetSky();
		const int32 W = Sky->SizeX, H = Sky->SizeY, D = Sky->SizeZ;
		const int32 Steps = FMath::Clamp(UDreamAtmosphereSettings::Get()->SkySteps, 8, 128);
		int32 Off = 0, Count = 0;
		FString Worst;
		for (const int32 Z : { D - 1, D * 3 / 4, D / 2, D / 4 })
		{
			TArray<FLinearColor> Pixels;
			if (!TestTrue(FString::Printf(TEXT("sky slice %d read back"), Z), Tables->ReadSkySlice(Z, Pixels) && Pixels.Num() == W * H))
			{
				continue;
			}
			for (const FIntPoint& Texel : { FIntPoint(W - 1, 0), FIntPoint(W * 3 / 4, H / 2), FIntPoint(W / 2 + 2, 0), FIntPoint(W / 2 + 2, H - 1), FIntPoint(W / 4, H / 3) })
			{
				const float3 Directions = DSA_SkyDirections(float3(DSA_TableParam(float(Texel.X), float(W)), DSA_TableParam(float(Texel.Y), float(H)),
					DSA_TableParam(float(Z), float(D))));
				const float3 Cpu = DSA_SkyLuminance(A, 0.0f, DSA_SkyViewVector(Directions), DSA_SkyLightVector(Directions), Steps);
				const FLinearColor& Gpu = Pixels[Texel.Y * W + Texel.X];
				// (With the light under the horizon the earth's shadow edge runs through the air, and the GPU reads it from
				// tables of finite resolution: a few percent more there.)
				if (!Near(Gpu, Cpu, Directions.z < 0.0f ? 0.08f : 0.04f, 2.0e-5f))
				{
					++Off;
					Worst = FString::Printf(TEXT(" e.g. slice %d (%d, %d): GPU (%.5f, %.5f, %.5f), CPU (%.5f, %.5f, %.5f)"), Z, Texel.X, Texel.Y, Gpu.R, Gpu.G, Gpu.B, Cpu.x, Cpu.y, Cpu.z);
				}
				++Count;
			}
		}
		TestTrue(FString::Printf(TEXT("the sky table is the model's within 4%%, 8%% in twilight (%d of %d off)%s"), Off, Count, *Worst), Count > 0 && Off == 0);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDreamAtmosphereGpuRebuildTest, "DreamAtmosphere.Gpu.RebuiltOnlyOnChange", DreamAtmosphere::GpuTests::Flags)
bool FDreamAtmosphereGpuRebuildTest::RunTest(const FString& Parameters)
{
	using namespace DreamAtmosphere::GpuTests;
	if (!CanRender())
	{
		AddInfo(TEXT("No renderer (-nullrhi): no tables to build."));
		return true;
	}
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
	if (!TestNotNull(TEXT("world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(false); };
	UDreamAtmosphereSubsystem* Atmosphere = UDreamAtmosphereSubsystem::Get(World);
	if (!TestNotNull(TEXT("the atmosphere"), Atmosphere) || !TestNotNull(TEXT("its tables"), Atmosphere->GetTables()))
	{
		return false;
	}
	Atmosphere->Tick(0.016f);
	TestEqual(TEXT("built once"), Atmosphere->GetBuildCount(), 1);
	for (int32 i = 0; i < 5; ++i)
	{
		Atmosphere->Tick(0.016f);
	}
	TestEqual(TEXT("and not again while the air holds"), Atmosphere->GetBuildCount(), 1);
	FDreamAtmosphereParameters Hazy = Atmosphere->GetParameters();
	Hazy.MieScattering *= 4.0f;
	Atmosphere->SetParameters(Hazy);
	Atmosphere->Tick(0.016f);
	TestEqual(TEXT("again when it turns hazy"), Atmosphere->GetBuildCount(), 2);
	TestTrue(TEXT("for the hazy air"), Atmosphere->GetTables()->IsBuiltFor(Hazy));
	Atmosphere->ResetParameters();
	Atmosphere->Tick(0.016f);
	TestEqual(TEXT("and back"), Atmosphere->GetBuildCount(), 3);
	FlushRenderingCommands();
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
