// Copyright DreamMoon. All Rights Reserved.

#include "DreamCloudField.h"
#include "DreamCloudVolume.h"
#include "DreamCloudVolumeComponent.h"

#include "Engine/StaticMesh.h"
#include "Engine/VolumeTexture.h"
#include "Engine/World.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Misc/AutomationTest.h"

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

#endif // WITH_DEV_AUTOMATION_TESTS
