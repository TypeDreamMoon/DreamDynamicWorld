// Copyright DreamMoon. All Rights Reserved.

#include "DreamCloudCarverComponent.h"
#include "DreamCloudField.h"
#include "DreamCloudLighting.h"
#include "DreamCloudSubsystem.h"
#include "DreamCloudVolume.h"
#include "DreamCloudVolumeComponent.h"

#include "Engine/Level.h"
#include "Engine/StaticMesh.h"
#include "Engine/VolumeTexture.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Misc/AutomationTest.h"
#include "UObject/Package.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace DreamCloud::Tests
{
	constexpr EAutomationTestFlags Flags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter;

	/** How many texels of a field have the wrong sign: inside the shape by more than Margin cm but positive, or outside
	 *  by more than Margin but negative (TrueDistance: the shape's exact signed distance). */
	template <typename FTrue>
	int32 CountWrongSigns(const FDreamCloudField& Field, float Margin, FTrue TrueDistance)
	{
		int32 Wrong = 0;
		for (int32 Z = 0; Z < Field.Resolution.Z; ++Z)
		{
			for (int32 Y = 0; Y < Field.Resolution.Y; ++Y)
			{
				for (int32 X = 0; X < Field.Resolution.X; ++X)
				{
					const float Truth = TrueDistance(Field.TexelCenter(X, Y, Z));
					const float Baked = Field.At(X, Y, Z);
					if ((Truth < -Margin && Baked >= 0.0f) || (Truth > Margin && Baked <= 0.0f))
					{
						++Wrong;
					}
				}
			}
		}
		return Wrong;
	}

	/** The largest error of the baked distance against the exact one, over the texels within Band cm of the surface,
	 *  and over all texels as a share of the exact distance (the far field is a chamfer estimate). */
	template <typename FTrue>
	void MeasureError(const FDreamCloudField& Field, float Band, FTrue TrueDistance, float& OutNear, float& OutFarShare)
	{
		OutNear = 0.0f;
		OutFarShare = 0.0f;
		for (int32 Z = 0; Z < Field.Resolution.Z; ++Z)
		{
			for (int32 Y = 0; Y < Field.Resolution.Y; ++Y)
			{
				for (int32 X = 0; X < Field.Resolution.X; ++X)
				{
					const float Truth = TrueDistance(Field.TexelCenter(X, Y, Z));
					const float Error = FMath::Abs(Field.At(X, Y, Z) - Truth);
					if (FMath::Abs(Truth) <= Band)
					{
						OutNear = FMath::Max(OutNear, Error);
					}
					else
					{
						OutFarShare = FMath::Max(OutFarShare, Error / FMath::Abs(Truth));
					}
				}
			}
		}
	}

	float CubeDistance(const FVector& P, double Half)
	{
		const FVector Q = P.GetAbs() - FVector(Half);
		const FVector Out(FMath::Max(Q.X, 0.0), FMath::Max(Q.Y, 0.0), FMath::Max(Q.Z, 0.0));
		return float(Out.Size() + FMath::Min(Q.GetMax(), 0.0));
	}

	struct FCloudWorld
	{
		UWorld* World = nullptr;
		FCloudWorld()
		{
			World = UWorld::CreateWorld(EWorldType::Game, false);
		}
		~FCloudWorld()
		{
			if (World)
			{
				World->DestroyWorld(false);
			}
		}
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDreamCloudBakeCubeTest, "DreamCloud.Bake.CubeMesh", DreamCloud::Tests::Flags)
bool FDreamCloudBakeCubeTest::RunTest(const FString& Parameters)
{
	using namespace DreamCloud::Tests;
	const UStaticMesh* Cube = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
	if (!TestNotNull(TEXT("engine cube"), Cube))
	{
		return false;
	}
	TArray<FVector3f> Positions;
	TArray<uint32> Indices;
	if (!TestTrue(TEXT("the cube's triangles are read"), DreamCloudBake::GatherMesh(Cube, Positions, Indices)))
	{
		return false;
	}
	DreamCloudBake::FSettings Settings;
	Settings.Resolution = 64;
	FDreamCloudField Field;
	if (!TestTrue(TEXT("baked"), DreamCloudBake::FromTriangles(Positions, Indices, Settings, Field)))
	{
		return false;
	}
	TestTrue(TEXT("field valid"), Field.IsValid());
	TestTrue(TEXT("a cube's box is a cube"), Field.Resolution == FIntVector(64, 64, 64));
	const float Texel = Field.TexelSize();
	auto Truth = [](const FVector& P) { return CubeDistance(P, 50.0); };
	TestEqual(TEXT("no texel has the wrong sign"), CountWrongSigns(Field, 0.5f * Texel, Truth), 0);
	float Near = 0.0f;
	float FarShare = 0.0f;
	MeasureError(Field, 2.0f * Texel, Truth, Near, FarShare);
	AddInfo(FString::Printf(TEXT("texel %.2f cm, near error %.3f cm, far error %.1f %%"), Texel, Near, FarShare * 100.0f));
	TestTrue(TEXT("near the surface the distance is exact (half floats)"), Near < 0.05f * Texel + 0.05f);
	TestTrue(TEXT("far off within 15 %, and never below the truth by more than a texel"), FarShare < 0.15f);
	TestNearlyEqual(TEXT("the middle is 50 cm inside"), Field.Sample(FVector::ZeroVector), -50.0f, Texel);
	TestNearlyEqual(TEXT("10 cm off a face"), Field.Sample(FVector(60.0, 0.0, 0.0)), 10.0f, 0.5f * Texel);
	TestNearlyEqual(TEXT("off an edge"), Field.Sample(FVector(55.0, 55.0, 0.0)), FMath::Sqrt(50.0f), 0.5f * Texel);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDreamCloudBakeSphereTest, "DreamCloud.Bake.SphereMesh", DreamCloud::Tests::Flags)
bool FDreamCloudBakeSphereTest::RunTest(const FString& Parameters)
{
	using namespace DreamCloud::Tests;
	const UStaticMesh* Sphere = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Sphere.Sphere"));
	if (!TestNotNull(TEXT("engine sphere"), Sphere))
	{
		return false;
	}
	TArray<FVector3f> Positions;
	TArray<uint32> Indices;
	if (!TestTrue(TEXT("the sphere's triangles are read"), DreamCloudBake::GatherMesh(Sphere, Positions, Indices)))
	{
		return false;
	}
	DreamCloudBake::FSettings Settings;
	Settings.Resolution = 80;
	FDreamCloudField Field;
	if (!TestTrue(TEXT("baked"), DreamCloudBake::FromTriangles(Positions, Indices, Settings, Field)))
	{
		return false;
	}
	const float Texel = Field.TexelSize();
	// The mesh is a faceted sphere of radius 50: its faces lie up to a little inside the true sphere.
	auto Truth = [](const FVector& P) { return float(P.Size() - 50.0); };
	TestEqual(TEXT("no texel has the wrong sign"), CountWrongSigns(Field, Texel + 1.0f, Truth), 0);
	float Near = 0.0f;
	float FarShare = 0.0f;
	MeasureError(Field, 2.0f * Texel, Truth, Near, FarShare);
	AddInfo(FString::Printf(TEXT("texel %.2f cm, near error %.3f cm, far error %.1f %%"), Texel, Near, FarShare * 100.0f));
	TestTrue(TEXT("near the surface within the facets' sag"), Near < 1.5f);
	// (Not at the very middle: the distance peaks there in a cone's apex, which the trilinear filter rounds off.)
	TestNearlyEqual(TEXT("25 cm from the middle"), Field.Sample(FVector(0.0, 0.0, 25.0)), -25.0f, 1.5f);
	TestNearlyEqual(TEXT("10 cm above the top"), Field.Sample(FVector(0.0, 0.0, 60.0)), 10.0f, 1.5f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDreamCloudBakePrimitivesTest, "DreamCloud.Bake.Primitives", DreamCloud::Tests::Flags)
bool FDreamCloudBakePrimitivesTest::RunTest(const FString& Parameters)
{
	TArray<FDreamCloudPrimitiveShape> Shapes;
	for (const double X : { -300.0, 300.0 })
	{
		FDreamCloudPrimitiveShape S;
		S.Transform.SetLocation(FVector(X, 0.0, 0.0));
		S.Size = FVector(250.0);
		Shapes.Add(S);
	}
	DreamCloudBake::FSettings Settings;
	Settings.Resolution = 96;
	FDreamCloudField Hard;
	TestTrue(TEXT("baked"), DreamCloudBake::FromPrimitives(Shapes, 0.0f, Settings, Hard));
	const float Texel = Hard.TexelSize();
	TestTrue(TEXT("the box fits the two spheres along x"), Hard.Size.X > 1100.0 && Hard.Size.Y < Hard.Size.X);
	TestNearlyEqual(TEXT("inside a sphere, 100 cm off its middle"), Hard.Sample(FVector(400.0, 0.0, 0.0)), -150.0f, 0.5f * Texel);
	TestNearlyEqual(TEXT("between them, 50 cm from each"), Hard.Sample(FVector::ZeroVector), 50.0f, 0.5f * Texel);
	FDreamCloudField Blended;
	TestTrue(TEXT("baked blended"), DreamCloudBake::FromPrimitives(Shapes, 200.0f, Settings, Blended));
	TestNearlyEqual(TEXT("blended over 200 cm the gap closes"), Blended.Sample(FVector::ZeroVector), 0.0f, 0.5f * Texel);
	TestNearlyEqual(TEXT("far from the seam a sphere stays itself"), Blended.Sample(FVector(550.0, 0.0, 0.0)), 0.0f, 0.5f * Texel);

	FDreamCloudPrimitiveShape Box;
	Box.Type = EDreamCloudPrimitive::Box;
	Box.Size = FVector(400.0, 200.0, 100.0);
	Box.Rounding = 0.0f;
	Box.Transform.SetRotation(FRotator(0.0, 90.0, 0.0).Quaternion());
	FDreamCloudField Turned;
	TestTrue(TEXT("baked box"), DreamCloudBake::FromPrimitives({ Box }, 0.0f, Settings, Turned));
	TestTrue(TEXT("a box turned 90 degrees is long along y"), Turned.Size.Y > Turned.Size.X);
	TestNearlyEqual(TEXT("its face along y"), Turned.Sample(FVector(0.0, 450.0, 0.0)), 50.0f, 0.5f * Turned.TexelSize());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDreamCloudVolumeActorTest, "DreamCloud.Volume.SpawnAndParameters", DreamCloud::Tests::Flags)
bool FDreamCloudVolumeActorTest::RunTest(const FString& Parameters)
{
	using namespace DreamCloud::Tests;
	FCloudWorld W;
	if (!TestNotNull(TEXT("world"), W.World))
	{
		return false;
	}
	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	ADreamCloudVolume* Volume = W.World->SpawnActor<ADreamCloudVolume>(FVector(1000.0, 0.0, 500.0), FRotator::ZeroRotator, Params);
	if (!TestNotNull(TEXT("spawned"), Volume))
	{
		return false;
	}
	UDreamCloudVolumeComponent* Cloud = Volume->GetCloud();
	TestTrue(TEXT("registered with a field baked from the default sphere"), Cloud->IsRegistered() && Cloud->Field.IsValid());
	TestFalse(TEXT("nothing to bake again"), Cloud->NeedsBake());
	TestNearlyEqual(TEXT("the sphere's middle is 500 cm in"), Cloud->Field.Sample(FVector::ZeroVector), -500.0f, Cloud->Field.TexelSize());
	UVolumeTexture* Texture = Cloud->GetFieldTexture();
	if (TestNotNull(TEXT("field texture"), Texture))
	{
		TestEqual(TEXT("texture size x"), Texture->GetSizeX(), Cloud->Field.Resolution.X);
		TestEqual(TEXT("texture size z"), Texture->GetSizeZ(), Cloud->Field.Resolution.Z);
		TestTrue(TEXT("half float texels"), Texture->GetPixelFormat() == PF_R16F);
	}

	UMaterialInstanceDynamic* Material = Cloud->GetCloudMaterial();
	if (TestNotNull(TEXT("cloud material"), Material))
	{
		TestTrue(TEXT("drawn with it"), Cloud->GetMaterial(0) == Material);
		UTexture* Bound = nullptr;
		Material->GetTextureParameterValue(FHashedMaterialParameterInfo(TEXT("Field")), Bound);
		TestTrue(TEXT("the field texture is bound"), Bound == Texture);
		FLinearColor Box;
		Material->GetVectorParameterValue(FHashedMaterialParameterInfo(TEXT("FieldBox")), Box);
		TestNearlyEqual(TEXT("the box's size reaches the material"), Box.R, float(Cloud->Field.Size.X), 0.01f);
		Cloud->Extinction = 0.2f;
		Cloud->Look = EDreamCloudLook::Toon;
		Cloud->UpdateCloudMaterial();
		float Value = 0.0f;
		Material->GetScalarParameterValue(FHashedMaterialParameterInfo(TEXT("Extinction")), Value);
		TestNearlyEqual(TEXT("extinction set"), Value, 0.2f, 1.0e-5f);
		Material->GetScalarParameterValue(FHashedMaterialParameterInfo(TEXT("Look")), Value);
		TestNearlyEqual(TEXT("look set"), Value, 1.0f, 1.0e-5f);
	}

	// Moving and scaling move the box only: no bake, the box follows.
	const uint32 Hash = Cloud->Field.SourceHash;
	const TArray<uint16> Before = Cloud->Field.Distances;
	Volume->SetActorLocation(FVector(-2000.0, 300.0, 100.0));
	Volume->SetActorScale3D(FVector(2.0));
	TestFalse(TEXT("moving needs no bake"), Cloud->NeedsBake());
	TestTrue(TEXT("the field is untouched"), Cloud->Field.SourceHash == Hash && Cloud->Field.Distances == Before);
	TestTrue(TEXT("the same texture"), Cloud->GetFieldTexture() == Texture);
	const FVector Origin = Cloud->GetRenderMatrix().GetOrigin();
	TestTrue(TEXT("the drawn box follows the actor"), Origin.Equals(FVector(-2000.0, 300.0, 100.0) + 2.0 * Cloud->Field.Center, 0.1));
	TestTrue(TEXT("the bounds follow and scale"), Cloud->Bounds.BoxExtent.Equals(Cloud->Field.Size, 0.5));

	// A new shape bakes again.
	Cloud->Primitives[0].Size = FVector(300.0);
	TestTrue(TEXT("a new shape needs a bake"), Cloud->NeedsBake());
	TestTrue(TEXT("baked again"), Cloud->Rebake());
	TestNearlyEqual(TEXT("the new sphere's middle"), Cloud->Field.Sample(FVector::ZeroVector), -300.0f, Cloud->Field.TexelSize());
	TestTrue(TEXT("a new texture"), Cloud->GetFieldTexture() != Texture);

	// A mesh shape.
	Cloud->ShapeSource = EDreamCloudShapeSource::Mesh;
	Cloud->ShapeMesh = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
	TestTrue(TEXT("a mesh needs a bake"), Cloud->NeedsBake());
	TestTrue(TEXT("baked from the mesh"), Cloud->Rebake());
	TestNearlyEqual(TEXT("the cube's middle"), Cloud->Field.Sample(FVector::ZeroVector), -50.0f, Cloud->Field.TexelSize());
	return true;
}


IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDreamCloudBakeCarvedAndMergedTest, "DreamCloud.Bake.SubtractedAndOverlapping", DreamCloud::Tests::Flags)
bool FDreamCloudBakeCarvedAndMergedTest::RunTest(const FString& Parameters)
{
	// A sphere with a tunnel through it along x.
	FDreamCloudPrimitiveShape Ball;
	Ball.Size = FVector(500.0);
	FDreamCloudPrimitiveShape Tunnel;
	Tunnel.Type = EDreamCloudPrimitive::Capsule;
	Tunnel.bSubtract = true;
	Tunnel.Size = FVector(150.0, 0.0, 800.0);
	Tunnel.Transform.SetRotation(FRotator(90.0, 0.0, 0.0).Quaternion());
	DreamCloudBake::FSettings Settings;
	Settings.Resolution = 96;
	FDreamCloudField Field;
	TestTrue(TEXT("baked"), DreamCloudBake::FromPrimitives({ Ball, Tunnel }, 0.0f, Settings, Field));
	const float Texel = Field.TexelSize();
	TestTrue(TEXT("the subtracted capsule does not grow the box"), Field.Size.X < 1400.0);
	TestNearlyEqual(TEXT("the tunnel's middle is clear, 150 cm from its wall"), Field.Sample(FVector::ZeroVector), 150.0f, Texel);
	TestNearlyEqual(TEXT("the tunnel runs through"), Field.Sample(FVector(400.0, 0.0, 0.0)), 150.0f, Texel);
	TestTrue(TEXT("above the tunnel is cloud"), Field.Sample(FVector(0.0, 0.0, 300.0)) < -100.0f);

	// Two overlapping cubes merged into one mesh: their overlap is inside (parity alone would call it outside).
	const UStaticMesh* Cube = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
	TArray<FVector3f> Positions;
	TArray<uint32> Indices;
	if (!TestTrue(TEXT("cube read"), DreamCloudBake::GatherMesh(Cube, Positions, Indices)))
	{
		return false;
	}
	const int32 NumPositions = Positions.Num();
	const int32 NumIndices = Indices.Num();
	for (int32 i = 0; i < NumPositions; ++i)
	{
		Positions.Add(Positions[i] + FVector3f(60.0f, 0.0f, 0.0f));
	}
	// The second cube wound the other way round, as a part of a merged mesh may be.
	for (int32 i = 0; i < NumIndices; i += 3)
	{
		Indices.Add(Indices[i] + NumPositions);
		Indices.Add(Indices[i + 2] + NumPositions);
		Indices.Add(Indices[i + 1] + NumPositions);
	}
	FDreamCloudField Merged;
	Settings.Resolution = 80;
	TestTrue(TEXT("baked merged"), DreamCloudBake::FromTriangles(Positions, Indices, Settings, Merged));
	TestTrue(TEXT("the overlap is inside"), Merged.Sample(FVector(30.0, 0.0, 0.0)) < -10.0f);
	TestTrue(TEXT("only the first cube"), Merged.Sample(FVector(-30.0, 0.0, 0.0)) < -10.0f);
	TestTrue(TEXT("only the second cube"), Merged.Sample(FVector(90.0, 0.0, 0.0)) < -10.0f);
	TestTrue(TEXT("beyond both"), Merged.Sample(FVector(30.0, 0.0, 70.0)) > 10.0f);

	// The merged sculpt of the test level: every part solid, their overlaps too.
	if (const UStaticMesh* Sculpt = LoadObject<UStaticMesh>(nullptr, TEXT("/DreamDynamicWorld/Cloud/SM_DreamCloudSculpt.SM_DreamCloudSculpt")))
	{
		FDreamCloudField Field2;
		Settings.Resolution = 96;
		TestTrue(TEXT("sculpt read"), DreamCloudBake::GatherMesh(Sculpt, Positions, Indices));
		TestTrue(TEXT("sculpt baked"), DreamCloudBake::FromTriangles(Positions, Indices, Settings, Field2));
		const FVector Inside[] = { FVector(0.0, 0.0, 0.0), FVector(55.0, 10.0, 55.0), FVector(-70.0, -10.0, 60.0), FVector(-70.0, -10.0, 140.0), FVector(-70.0, -10.0, 10.0) };
		for (const FVector& P : Inside)
		{
			TestTrue(*FString::Printf(TEXT("the sculpt is solid at %s"), *P.ToString()), Field2.Sample(P) < -5.0f);
		}
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDreamCloudLightMapTest, "DreamCloud.Light.Transmittance", DreamCloud::Tests::Flags)
bool FDreamCloudLightMapTest::RunTest(const FString& Parameters)
{
	FDreamCloudPrimitiveShape Ball;
	Ball.Size = FVector(500.0);
	DreamCloudBake::FSettings Settings;
	Settings.Resolution = 64;
	FDreamCloudField Field;
	TestTrue(TEXT("baked"), DreamCloudBake::FromPrimitives({ Ball }, 0.0f, Settings, Field));
	DreamCloudLighting::FLightMapParams Params;
	Params.LightDir = FVector3f(0.0f, 0.0f, 1.0f);
	Params.SigmaPerUnit = 0.005f;
	Params.Edge = 50.0f;
	Params.Resolution = DreamCloudLighting::ResolutionFor(Field.Size, 24);
	const TArray<float> NoHoles;
	auto At = [&](const FVector& P) { return DreamCloudLighting::TransmittanceAt(Field, NoHoles, FIntVector::ZeroValue, Params, P); };
	TestNearlyEqual(TEXT("above the cloud the light is whole"), At(FVector(0.0, 0.0, 550.0)), 1.0f, 1.0e-3f);
	TestNearlyEqual(TEXT("beside it too"), At(FVector(560.0, 0.0, 0.0)), 1.0f, 1.0e-3f);
	const float Top = At(FVector(0.0, 0.0, 450.0));
	const float Middle = At(FVector::ZeroVector);
	const float Bottom = At(FVector(0.0, 0.0, -450.0));
	AddInfo(FString::Printf(TEXT("transmittance top %.3f middle %.4f bottom %.6f"), Top, Middle, Bottom));
	// 50 cm of cloud under the top at 0.005/cm (less its soft edge): about exp(-0.25 + edge).
	TestTrue(TEXT("just under the top, most of the light"), Top > 0.7f && Top < 0.95f);
	TestNearlyEqual(TEXT("the middle: 500 cm through"), Middle, FMath::Exp(-0.005f * (500.0f - 25.0f)), 0.03f);
	TestTrue(TEXT("the bottom darker still"), Bottom < Middle);
	TArray<float> Map;
	DreamCloudLighting::ComputeTransmittance(Field, NoHoles, FIntVector::ZeroValue, Params, Map);
	TestEqual(TEXT("a value per texel"), Map.Num(), Params.Resolution.X * Params.Resolution.Y * Params.Resolution.Z);
	float Lowest = 1.0f;
	for (float T : Map)
	{
		Lowest = FMath::Min(Lowest, T);
	}
	TestTrue(TEXT("the map darkens inside"), Lowest < 0.05f);

	// A shaft carved down the middle lets the light in.
	const FIntVector HoleSize(16, 16, 16);
	TArray<float> Holes;
	Holes.SetNumZeroed(HoleSize.X * HoleSize.Y * HoleSize.Z);
	for (int32 Z = 0; Z < HoleSize.Z; ++Z)
	{
		for (int32 Y = 7; Y <= 8; ++Y)
		{
			for (int32 X = 7; X <= 8; ++X)
			{
				Holes[X + HoleSize.X * (Y + HoleSize.Y * Z)] = 1.0f;
			}
		}
	}
	const float Shaft = DreamCloudLighting::TransmittanceAt(Field, Holes, HoleSize, Params, FVector::ZeroVector);
	TestTrue(TEXT("down a carved shaft the light reaches the middle"), Shaft > 0.9f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDreamCloudCarvingTest, "DreamCloud.Volume.CarvingAndRefill", DreamCloud::Tests::Flags)
bool FDreamCloudCarvingTest::RunTest(const FString& Parameters)
{
	using namespace DreamCloud::Tests;
	FCloudWorld W;
	if (!TestNotNull(TEXT("world"), W.World))
	{
		return false;
	}
	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	ADreamCloudVolume* Volume = W.World->SpawnActor<ADreamCloudVolume>(FVector(0.0, 0.0, 1000.0), FRotator::ZeroRotator, Params);
	UDreamCloudVolumeComponent* Cloud = Volume->GetCloud();
	Cloud->HoldTime = 1.0f;
	Cloud->RefillTime = 2.0f;
	const FVector Middle(0.0, 0.0, 1000.0);
	const FVector Aside(0.0, 400.0, 1000.0);
	TestEqual(TEXT("untouched"), Cloud->GetCarving(Middle), 0.0f);

	// A carver flying through along x.
	AActor* Plane = W.World->SpawnActor<AActor>(FVector(-800.0, 0.0, 1000.0), FRotator::ZeroRotator, Params);
	USceneComponent* Root = NewObject<USceneComponent>(Plane, TEXT("Root"));
	Plane->SetRootComponent(Root);
	Root->RegisterComponent();
	Plane->SetActorLocation(FVector(-800.0, 0.0, 1000.0));
	UDreamCloudCarverComponent* Carver = NewObject<UDreamCloudCarverComponent>(Plane, TEXT("Carver"));
	Carver->Radius = 150.0f;
	Carver->SetupAttachment(Root);
	Carver->RegisterComponent();
	UDreamCloudSubsystem* Clouds = W.World->GetSubsystem<UDreamCloudSubsystem>();
	if (!TestNotNull(TEXT("subsystem"), Clouds))
	{
		return false;
	}
	Clouds->SetLightDirectionOverride(FVector(0.3, 0.2, 1.0));
	Clouds->Tick(0.0f);
	Plane->SetActorLocation(FVector(800.0, 0.0, 1000.0));
	Clouds->Tick(0.0f);
	TestNearlyEqual(TEXT("the path is cleared"), Cloud->GetCarving(Middle), 1.0f, 0.05f);
	TestNearlyEqual(TEXT("all along it"), Cloud->GetCarving(FVector(-300.0, 0.0, 1000.0)), 1.0f, 0.05f);
	TestEqual(TEXT("but not beside it"), Cloud->GetCarving(Aside), 0.0f);
	Carver->bCarving = false;

	// Held open, then growing back.
	Clouds->Tick(0.5f);
	TestNearlyEqual(TEXT("held open"), Cloud->GetCarving(Middle), 1.0f, 0.05f);
	Clouds->Tick(1.0f);
	const float Refilling = Cloud->GetCarving(Middle);
	TestTrue(TEXT("growing back after the hold"), Refilling > 0.5f && Refilling < 0.9f);
	Clouds->Tick(2.0f);
	TestEqual(TEXT("grown back"), Cloud->GetCarving(Middle), 0.0f);

	// A volume that may not be carved.
	Cloud->bCarvable = false;
	Cloud->Carve(FVector(-800.0, 0.0, 1000.0), FVector(800.0, 0.0, 1000.0), 150.0f, 1.0f);
	TestEqual(TEXT("not carvable"), Cloud->GetCarving(Middle), 0.0f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDreamCloudLightFollowsTest, "DreamCloud.Volume.LightMapFollowsTheLight", DreamCloud::Tests::Flags)
bool FDreamCloudLightFollowsTest::RunTest(const FString& Parameters)
{
	using namespace DreamCloud::Tests;
	FCloudWorld W;
	if (!TestNotNull(TEXT("world"), W.World))
	{
		return false;
	}
	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	ADreamCloudVolume* Volume = W.World->SpawnActor<ADreamCloudVolume>(FVector::ZeroVector, FRotator::ZeroRotator, Params);
	UDreamCloudVolumeComponent* Cloud = Volume->GetCloud();
	const FVector Light = FVector(1.0, 0.0, 1.0).GetSafeNormal();
	Cloud->TickCloud(0.0f, Light, nullptr);
	const int32 First = Cloud->GetLightMapUpdates();
	TestTrue(TEXT("drawn once at the start"), First >= 1);
	Cloud->TickCloud(0.1f, Light, nullptr);
	TestEqual(TEXT("not again for the same light"), Cloud->GetLightMapUpdates(), First);
	Cloud->TickCloud(0.1f, FRotator(0.0, 0.0, 0.0).RotateVector(FQuat(FVector::UpVector, FMath::DegreesToRadians(0.5)).RotateVector(Light)), nullptr);
	TestEqual(TEXT("not for half a degree"), Cloud->GetLightMapUpdates(), First);
	Cloud->TickCloud(0.1f, FQuat(FVector::UpVector, FMath::DegreesToRadians(5.0)).RotateVector(Light), nullptr);
	TestEqual(TEXT("again for five degrees"), Cloud->GetLightMapUpdates(), First + 1);
	Volume->SetActorRotation(FRotator(0.0, 90.0, 0.0));
	Cloud->TickCloud(0.1f, FQuat(FVector::UpVector, FMath::DegreesToRadians(5.0)).RotateVector(Light), nullptr);
	TestEqual(TEXT("again when the volume turns under the light"), Cloud->GetLightMapUpdates(), First + 2);
	Cloud->Extinction *= 2.0f;
	Cloud->TickCloud(0.1f, FQuat(FVector::UpVector, FMath::DegreesToRadians(5.0)).RotateVector(Light), nullptr);
	TestEqual(TEXT("again for a denser cloud"), Cloud->GetLightMapUpdates(), First + 3);
	const UDreamCloudVolumeComponent::FLightState State = Cloud->GetLightState(FVector::UpVector);
	TestTrue(TEXT("light from above stays above in the shape"), State.Direction.Equals(FVector3f(0.0f, 0.0f, 1.0f), 1.0e-4f));

	// The quality level reaches the material's step.
	IConsoleVariable* Quality = IConsoleManager::Get().FindConsoleVariable(TEXT("DreamCloud.Quality"));
	if (TestNotNull(TEXT("quality variable"), Quality) && Cloud->GetCloudMaterial())
	{
		const int32 Was = Quality->GetInt();
		Quality->Set(0, ECVF_SetByCode);
		Cloud->TickCloud(0.1f, FVector::UpVector, nullptr);
		float Step = 0.0f;
		Cloud->GetCloudMaterial()->GetScalarParameterValue(FHashedMaterialParameterInfo(TEXT("StepScale")), Step);
		TestNearlyEqual(TEXT("low quality: twice the step"), Step, 2.0f * Cloud->StepScale, 1.0e-4f);
		Quality->Set(Was, ECVF_SetByCode);
		Cloud->TickCloud(0.1f, FVector::UpVector, nullptr);
		Cloud->GetCloudMaterial()->GetScalarParameterValue(FHashedMaterialParameterInfo(TEXT("StepScale")), Step);
		TestNearlyEqual(TEXT("back"), Step, DreamCloud::GetQuality().StepScale * Cloud->StepScale, 1.0e-4f);
	}
	return true;
}


IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDreamCloudTestLevelTest, "DreamCloud.Assets.TestLevel", DreamCloud::Tests::Flags)
bool FDreamCloudTestLevelTest::RunTest(const FString& Parameters)
{
	UPackage* Package = LoadPackage(nullptr, TEXT("/DreamDynamicWorld/Cloud/Maps/Lvl_DreamCloudVolumeTest"), LOAD_None);
	UWorld* World = Package ? UWorld::FindWorldInPackage(Package) : nullptr;
	if (!TestNotNull(TEXT("the cloud volume test level loads"), World))
	{
		return false;
	}
	TSet<EDreamCloudLook> Looks;
	int32 Volumes = 0;
	int32 Meshes = 0;
	int32 Tunnels = 0;
	for (AActor* Actor : World->PersistentLevel->Actors)
	{
		const ADreamCloudVolume* Volume = Cast<ADreamCloudVolume>(Actor);
		if (!Volume)
		{
			continue;
		}
		const UDreamCloudVolumeComponent* Cloud = Volume->GetCloud();
		++Volumes;
		Looks.Add(Cloud->Look);
		Meshes += Cloud->ShapeSource == EDreamCloudShapeSource::Mesh && Cloud->ShapeMesh ? 1 : 0;
		Tunnels += Cloud->Primitives.ContainsByPredicate([](const FDreamCloudPrimitiveShape& S) { return S.bSubtract; }) ? 1 : 0;
		TestTrue(*FString::Printf(TEXT("%s is baked and saved with its field"), *Volume->GetName()), Cloud->Field.IsValid());
	}
	TestTrue(TEXT("nine cloud volumes"), Volumes >= 9);
	TestEqual(TEXT("all three looks"), Looks.Num(), 3);
	TestEqual(TEXT("three meshes"), Meshes, 3);
	TestEqual(TEXT("a tunnel"), Tunnels, 1);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
