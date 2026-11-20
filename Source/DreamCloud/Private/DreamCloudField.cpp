// Copyright DreamMoon. All Rights Reserved.

#include "DreamCloudField.h"
#include "DreamCloudModule.h"

#include "Async/ParallelFor.h"
#include "Engine/StaticMesh.h"
#include "Math/Float16.h"
#include "StaticMeshResources.h"

#if WITH_EDITORONLY_DATA
#include "MeshDescription.h"
#include "StaticMeshAttributes.h"
#endif

bool FDreamCloudField::IsValid() const
{
	return Resolution.X > 0 && Resolution.Y > 0 && Resolution.Z > 0 && Distances.Num() == Resolution.X * Resolution.Y * Resolution.Z
		&& Size.X > 0.0 && Size.Y > 0.0 && Size.Z > 0.0;
}

float FDreamCloudField::TexelSize() const
{
	return Resolution.X > 0 ? float(Size.X / Resolution.X) : 0.0f;
}

float FDreamCloudField::At(int32 X, int32 Y, int32 Z) const
{
	FFloat16 Value;
	Value.Encoded = Distances[Index(X, Y, Z)];
	return Value.GetFloat();
}

FVector FDreamCloudField::TexelCenter(int32 X, int32 Y, int32 Z) const
{
	const double Texel = TexelSize();
	return Center - 0.5 * Size + FVector(X + 0.5, Y + 0.5, Z + 0.5) * Texel;
}

float FDreamCloudField::Sample(const FVector& Point) const
{
	if (!IsValid())
	{
		return UE_BIG_NUMBER;
	}
	const double Texel = TexelSize();
	const FVector U = (Point - (Center - 0.5 * Size)) / Texel - FVector(0.5);
	const FVector C(FMath::Clamp(U.X, 0.0, Resolution.X - 1.0), FMath::Clamp(U.Y, 0.0, Resolution.Y - 1.0), FMath::Clamp(U.Z, 0.0, Resolution.Z - 1.0));
	const int32 X0 = FMath::Min(int32(C.X), Resolution.X - 2 < 0 ? 0 : Resolution.X - 2);
	const int32 Y0 = FMath::Min(int32(C.Y), Resolution.Y - 2 < 0 ? 0 : Resolution.Y - 2);
	const int32 Z0 = FMath::Min(int32(C.Z), Resolution.Z - 2 < 0 ? 0 : Resolution.Z - 2);
	const int32 X1 = FMath::Min(X0 + 1, Resolution.X - 1);
	const int32 Y1 = FMath::Min(Y0 + 1, Resolution.Y - 1);
	const int32 Z1 = FMath::Min(Z0 + 1, Resolution.Z - 1);
	const float Fx = float(C.X - X0);
	const float Fy = float(C.Y - Y0);
	const float Fz = float(C.Z - Z0);
	const float C00 = FMath::Lerp(At(X0, Y0, Z0), At(X1, Y0, Z0), Fx);
	const float C10 = FMath::Lerp(At(X0, Y1, Z0), At(X1, Y1, Z0), Fx);
	const float C01 = FMath::Lerp(At(X0, Y0, Z1), At(X1, Y0, Z1), Fx);
	const float C11 = FMath::Lerp(At(X0, Y1, Z1), At(X1, Y1, Z1), Fx);
	const float D = FMath::Lerp(FMath::Lerp(C00, C10, Fy), FMath::Lerp(C01, C11, Fy), Fz);
	const FVector Out = (Point - Center).GetAbs() - 0.5 * Size;
	const double Outside = FVector(FMath::Max(Out.X, 0.0), FMath::Max(Out.Y, 0.0), FMath::Max(Out.Z, 0.0)).Size();
	return Outside > 0.0 ? FMath::Max(D, float(Outside)) : D;
}

float FDreamCloudPrimitiveShape::Distance(const FVector& Point) const
{
	const FVector P = Transform.InverseTransformPositionNoScale(Point);
	switch (Type)
	{
	case EDreamCloudPrimitive::Box:
	{
		const FVector Half = Size.GetAbs();
		const double R = FMath::Clamp(double(Rounding), 0.0, Half.GetMin());
		const FVector Q = P.GetAbs() - (Half - FVector(R));
		const FVector Outside(FMath::Max(Q.X, 0.0), FMath::Max(Q.Y, 0.0), FMath::Max(Q.Z, 0.0));
		return float(Outside.Size() + FMath::Min(Q.GetMax(), 0.0) - R);
	}
	case EDreamCloudPrimitive::Capsule:
	{
		const double R = FMath::Abs(Size.X);
		const double H = FMath::Abs(Size.Z);
		return float((P - FVector(0.0, 0.0, FMath::Clamp(P.Z, -H, H))).Size() - R);
	}
	case EDreamCloudPrimitive::Sphere:
	default:
		return float(P.Size() - FMath::Abs(Size.X));
	}
}

FBox FDreamCloudPrimitiveShape::Bounds() const
{
	FVector Extent;
	switch (Type)
	{
	case EDreamCloudPrimitive::Box:
		Extent = Size.GetAbs();
		break;
	case EDreamCloudPrimitive::Capsule:
		Extent = FVector(FMath::Abs(Size.X), FMath::Abs(Size.X), FMath::Abs(Size.X) + FMath::Abs(Size.Z));
		break;
	case EDreamCloudPrimitive::Sphere:
	default:
		Extent = FVector(FMath::Abs(Size.X));
		break;
	}
	FTransform NoScale = Transform;
	NoScale.SetScale3D(FVector::OneVector);
	return FBox(-Extent, Extent).TransformBy(NoScale);
}

namespace DreamCloudBake
{
	namespace
	{
		/** The box around the shape, padded, cut into cubic texels with the resolution along its longest axis. */
		bool SetupGrid(const FBox& Shape, const FSettings& Settings, FDreamCloudField& Out)
		{
			if (!Shape.IsValid)
			{
				return false;
			}
			const int32 Resolution = FMath::Clamp(Settings.Resolution, 16, 256);
			const FVector Extent = Shape.GetSize();
			const double Largest = FMath::Max(Extent.GetMax(), 1.0);
			double Pad = FMath::Max(double(Settings.Padding), 0.0) * Largest;
			double Texel = (Largest + 2.0 * Pad) / Resolution;
			// At least two texels around the shape, so the field outside it is a distance and not the box's clamp.
			Pad = FMath::Max(Pad, 2.0 * Texel);
			Texel = (Largest + 2.0 * Pad) / Resolution;
			Pad = FMath::Max(Pad, 2.0 * Texel);
			for (int32 Axis = 0; Axis < 3; ++Axis)
			{
				Out.Resolution[Axis] = FMath::Clamp(FMath::CeilToInt((Extent[Axis] + 2.0 * Pad) / Texel), 4, Resolution);
			}
			Out.Size = FVector(Out.Resolution) * Texel;
			Out.Center = Shape.GetCenter();
			Out.Distances.SetNumZeroed(Out.Resolution.X * Out.Resolution.Y * Out.Resolution.Z);
			return true;
		}

		void Store(TConstArrayView<float> Signed, FDreamCloudField& Out)
		{
			check(Signed.Num() == Out.Distances.Num());
			for (int32 i = 0; i < Signed.Num(); ++i)
			{
				Out.Distances[i] = FFloat16(Signed[i]).Encoded;
			}
		}

		// The squared distance from P to the triangle ABC (Ericson, Real-Time Collision Detection 5.1.5).
		float PointTriangleDistanceSquared(const FVector3f& P, const FVector3f& A, const FVector3f& B, const FVector3f& C)
		{
			const FVector3f AB = B - A;
			const FVector3f AC = C - A;
			const FVector3f AP = P - A;
			const float D1 = AB | AP;
			const float D2 = AC | AP;
			if (D1 <= 0.0f && D2 <= 0.0f)
			{
				return AP.SizeSquared();
			}
			const FVector3f BP = P - B;
			const float D3 = AB | BP;
			const float D4 = AC | BP;
			if (D3 >= 0.0f && D4 <= D3)
			{
				return BP.SizeSquared();
			}
			const float VC = D1 * D4 - D3 * D2;
			if (VC <= 0.0f && D1 >= 0.0f && D3 <= 0.0f)
			{
				const float V = D1 / (D1 - D3);
				return (P - (A + V * AB)).SizeSquared();
			}
			const FVector3f CP = P - C;
			const float D5 = AB | CP;
			const float D6 = AC | CP;
			if (D6 >= 0.0f && D5 <= D6)
			{
				return CP.SizeSquared();
			}
			const float VB = D5 * D2 - D1 * D6;
			if (VB <= 0.0f && D2 >= 0.0f && D6 <= 0.0f)
			{
				const float W = D2 / (D2 - D6);
				return (P - (A + W * AC)).SizeSquared();
			}
			const float VA = D3 * D6 - D5 * D4;
			if (VA <= 0.0f && (D4 - D3) >= 0.0f && (D5 - D6) >= 0.0f)
			{
				const float W = (D4 - D3) / ((D4 - D3) + (D5 - D6));
				return (P - (B + W * (C - B))).SizeSquared();
			}
			const float Denom = 1.0f / (VA + VB + VC);
			const float V = VB * Denom;
			const float W = VC * Denom;
			return (P - (A + AB * V + AC * W)).SizeSquared();
		}

		/** The texels whose centres lie within [Lo, Hi] along an axis (grid coordinates: texel i's centre at i + 0.5). */
		FIntPoint TexelRange(float Lo, float Hi, int32 Count)
		{
			const int32 First = FMath::Max(FMath::CeilToInt(Lo - 0.5f), 0);
			const int32 Last = FMath::Min(FMath::FloorToInt(Hi - 0.5f), Count - 1);
			return FIntPoint(First, Last);
		}

		/** Chamfer sweeps (weights 1, sqrt 2, sqrt 3 texels) spread the exact distances near the surface over the box:
		 *  an upper bound of the true distance by at most some 10 %, which the march's skip scale allows for. */
		void Sweep(TArray<float>& D, const FIntVector& N, float Texel)
		{
			struct FOffset { int32 X, Y, Z; float W; };
			TArray<FOffset, TInlineAllocator<13>> Back;
			for (int32 Dz = -1; Dz <= 0; ++Dz)
			{
				for (int32 Dy = -1; Dy <= 1; ++Dy)
				{
					for (int32 Dx = -1; Dx <= 1; ++Dx)
					{
						const bool bBefore = Dz < 0 || (Dz == 0 && (Dy < 0 || (Dy == 0 && Dx < 0)));
						if (bBefore)
						{
							Back.Add({ Dx, Dy, Dz, Texel * FMath::Sqrt(float(Dx * Dx + Dy * Dy + Dz * Dz)) });
						}
					}
				}
			}
			auto Pass = [&](int32 Sign)
			{
				const int32 Count = N.X * N.Y * N.Z;
				for (int32 Step = 0; Step < Count; ++Step)
				{
					const int32 i = Sign > 0 ? Step : Count - 1 - Step;
					const int32 X = i % N.X;
					const int32 Y = (i / N.X) % N.Y;
					const int32 Z = i / (N.X * N.Y);
					float Best = D[i];
					for (const FOffset& O : Back)
					{
						const int32 Nx = X + Sign * O.X;
						const int32 Ny = Y + Sign * O.Y;
						const int32 Nz = Z + Sign * O.Z;
						if (Nx < 0 || Ny < 0 || Nz < 0 || Nx >= N.X || Ny >= N.Y || Nz >= N.Z)
						{
							continue;
						}
						Best = FMath::Min(Best, D[Nx + N.X * (Ny + N.Y * Nz)] + O.W);
					}
					D[i] = Best;
				}
			};
			Pass(1);
			Pass(-1);
		}
	}

	bool FromTriangles(TConstArrayView<FVector3f> Positions, TConstArrayView<uint32> Indices, const FSettings& Settings, FDreamCloudField& Out)
	{
		const int32 NumTriangles = Indices.Num() / 3;
		if (NumTriangles == 0 || Positions.Num() == 0)
		{
			return false;
		}
		FBox Shape(ForceInit);
		for (uint32 Index : Indices)
		{
			if (!Positions.IsValidIndex(int32(Index)))
			{
				return false;
			}
			Shape += FVector(Positions[Index]);
		}
		FDreamCloudField Field;
		if (!SetupGrid(Shape, Settings, Field))
		{
			return false;
		}
		const FIntVector N = Field.Resolution;
		const float Texel = Field.TexelSize();
		// Grid space: texel i's centre at i + 0.5 along each axis.
		const FVector3f Origin = FVector3f(Field.Center - 0.5 * Field.Size);
		TArray<FVector3f> G;
		G.SetNumUninitialized(Positions.Num());
		for (int32 i = 0; i < Positions.Num(); ++i)
		{
			G[i] = (Positions[i] - Origin) / Texel;
		}

		// Exact distances (in texels) within a band around each triangle, slice by slice.
		const float Band = 2.5f;
		TArray<TArray<int32>> SliceTriangles;
		SliceTriangles.SetNum(N.Z);
		for (int32 t = 0; t < NumTriangles; ++t)
		{
			const FVector3f& A = G[Indices[3 * t]];
			const FVector3f& B = G[Indices[3 * t + 1]];
			const FVector3f& C = G[Indices[3 * t + 2]];
			const FIntPoint Zs = TexelRange(FMath::Min3(A.Z, B.Z, C.Z) - Band, FMath::Max3(A.Z, B.Z, C.Z) + Band, N.Z);
			for (int32 Z = Zs.X; Z <= Zs.Y; ++Z)
			{
				SliceTriangles[Z].Add(t);
			}
		}
		TArray<float> D;
		D.Init(UE_BIG_NUMBER, N.X * N.Y * N.Z);
		ParallelFor(N.Z, [&](int32 Z)
		{
			for (int32 t : SliceTriangles[Z])
			{
				const FVector3f& A = G[Indices[3 * t]];
				const FVector3f& B = G[Indices[3 * t + 1]];
				const FVector3f& C = G[Indices[3 * t + 2]];
				const FIntPoint Xs = TexelRange(FMath::Min3(A.X, B.X, C.X) - Band, FMath::Max3(A.X, B.X, C.X) + Band, N.X);
				const FIntPoint Ys = TexelRange(FMath::Min3(A.Y, B.Y, C.Y) - Band, FMath::Max3(A.Y, B.Y, C.Y) + Band, N.Y);
				for (int32 Y = Ys.X; Y <= Ys.Y; ++Y)
				{
					for (int32 X = Xs.X; X <= Xs.Y; ++X)
					{
						const FVector3f P(X + 0.5f, Y + 0.5f, Z + 0.5f);
						const float Dist = FMath::Sqrt(PointTriangleDistanceSquared(P, A, B, C));
						float& Slot = D[X + N.X * (Y + N.Y * Z)];
						Slot = FMath::Min(Slot, Dist);
					}
				}
			}
		});
		Sweep(D, N, 1.0f);

		// Inside or out: rays along each axis through the texel centres (nudged off them, so a ray does not run along an
		// edge of an axis-aligned mesh) add up the surfaces they cross, +1 or -1 by which way each faces: a texel is inside
		// where that winding is not 0 (inside overlapping parts too, which plain parity would call outside), and the texel
		// is inside when two of three rays say so.
		TArray<uint8> Votes;
		Votes.SetNumZeroed(N.X * N.Y * N.Z);
		for (int32 Axis = 0; Axis < 3; ++Axis)
		{
			const int32 AxU = (Axis + 1) % 3;
			const int32 AxV = (Axis + 2) % 3;
			const int32 NU = N[AxU];
			const int32 NV = N[AxV];
			const int32 NA = N[Axis];
			const float NudgeU = 0.0131f;
			const float NudgeV = 0.0247f;
			TArray<TArray<int32>> Rows;
			Rows.SetNum(NU * NV);
			for (int32 t = 0; t < NumTriangles; ++t)
			{
				const FVector3f& A = G[Indices[3 * t]];
				const FVector3f& B = G[Indices[3 * t + 1]];
				const FVector3f& C = G[Indices[3 * t + 2]];
				const FIntPoint Us = TexelRange(FMath::Min3(A[AxU], B[AxU], C[AxU]) - NudgeU, FMath::Max3(A[AxU], B[AxU], C[AxU]) - NudgeU, NU);
				const FIntPoint Vs = TexelRange(FMath::Min3(A[AxV], B[AxV], C[AxV]) - NudgeV, FMath::Max3(A[AxV], B[AxV], C[AxV]) - NudgeV, NV);
				for (int32 V = Vs.X; V <= Vs.Y; ++V)
				{
					for (int32 U = Us.X; U <= Us.Y; ++U)
					{
						Rows[U + NU * V].Add(t);
					}
				}
			}
			ParallelFor(NU * NV, [&](int32 Row)
			{
				const TArray<int32>& Candidates = Rows[Row];
				if (Candidates.Num() == 0)
				{
					return;
				}
				const int32 U = Row % NU;
				const int32 V = Row / NU;
				const float Pu = U + 0.5f + NudgeU;
				const float Pv = V + 0.5f + NudgeV;
				TArray<TPair<float, int32>, TInlineAllocator<16>> Hits;
				for (int32 t : Candidates)
				{
					const FVector3f& A = G[Indices[3 * t]];
					const FVector3f& B = G[Indices[3 * t + 1]];
					const FVector3f& C = G[Indices[3 * t + 2]];
					const float Area = (B[AxU] - A[AxU]) * (C[AxV] - A[AxV]) - (C[AxU] - A[AxU]) * (B[AxV] - A[AxV]);
					if (FMath::Abs(Area) < 1.0e-9f)
					{
						continue;
					}
					const float W0 = ((B[AxU] - Pu) * (C[AxV] - Pv) - (C[AxU] - Pu) * (B[AxV] - Pv)) / Area;
					const float W1 = ((C[AxU] - Pu) * (A[AxV] - Pv) - (A[AxU] - Pu) * (C[AxV] - Pv)) / Area;
					const float W2 = 1.0f - W0 - W1;
					if (W0 < 0.0f || W1 < 0.0f || W2 < 0.0f)
					{
						continue;
					}
					Hits.Add({ W0 * A[Axis] + W1 * B[Axis] + W2 * C[Axis], Area > 0.0f ? 1 : -1 });
				}
				if (Hits.Num() == 0)
				{
					return;
				}
				Hits.Sort([](const TPair<float, int32>& L, const TPair<float, int32>& R) { return L.Key < R.Key; });
				int32 Crossed = 0;
				int32 Winding = 0;
				for (int32 Along = 0; Along < NA; ++Along)
				{
					const float Pa = Along + 0.5f;
					while (Crossed < Hits.Num() && Hits[Crossed].Key < Pa)
					{
						Winding += Hits[Crossed].Value;
						++Crossed;
					}
					if (Winding != 0)
					{
						FIntVector Cell;
						Cell[Axis] = Along;
						Cell[AxU] = U;
						Cell[AxV] = V;
						++Votes[Cell.X + N.X * (Cell.Y + N.Y * Cell.Z)];
					}
				}
			});
		}

		TArray<float> Signed;
		Signed.SetNumUninitialized(D.Num());
		for (int32 i = 0; i < D.Num(); ++i)
		{
			Signed[i] = (Votes[i] >= 2 ? -D[i] : D[i]) * Texel;
		}
		Store(Signed, Field);
		Field.SourceHash = Out.SourceHash;
		Out = MoveTemp(Field);
		return true;
	}

	bool FromPrimitives(TConstArrayView<FDreamCloudPrimitiveShape> Shapes, float Blend, const FSettings& Settings, FDreamCloudField& Out)
	{
		if (Shapes.Num() == 0)
		{
			return false;
		}
		FBox Shape(ForceInit);
		for (const FDreamCloudPrimitiveShape& S : Shapes)
		{
			if (!S.bSubtract)
			{
				Shape += S.Bounds();
			}
		}
		FDreamCloudField Field;
		if (!SetupGrid(Shape, Settings, Field))
		{
			return false;
		}
		const FIntVector N = Field.Resolution;
		const float K = FMath::Max(Blend, 0.0f);
		TArray<float> Signed;
		Signed.SetNumUninitialized(N.X * N.Y * N.Z);
		ParallelFor(N.Z, [&](int32 Z)
		{
			for (int32 Y = 0; Y < N.Y; ++Y)
			{
				for (int32 X = 0; X < N.X; ++X)
				{
					const FVector P = Field.TexelCenter(X, Y, Z);
					// Polynomial smooth minimum and maximum (Quilez): the seams between primitives rounded over K cm. The added
					// ones first, then each subtracted one carved out of their union.
					float D = UE_BIG_NUMBER;
					for (int32 Pass = 0; Pass < 2; ++Pass)
					{
						for (const FDreamCloudPrimitiveShape& S : Shapes)
						{
							if (S.bSubtract != (Pass == 1))
							{
								continue;
							}
							const float E = Pass == 0 ? S.Distance(P) : -S.Distance(P);
							if (D >= UE_BIG_NUMBER)
							{
								D = E;
								continue;
							}
							const float H = K > 0.0f ? FMath::Max(K - FMath::Abs(D - E), 0.0f) / K : 0.0f;
							D = Pass == 0 ? FMath::Min(D, E) - H * H * K * 0.25f : FMath::Max(D, E) + H * H * K * 0.25f;
						}
					}
					Signed[X + N.X * (Y + N.Y * Z)] = D;
				}
			}
		});
		Store(Signed, Field);
		Field.SourceHash = Out.SourceHash;
		Out = MoveTemp(Field);
		return true;
	}

	bool GatherMesh(const UStaticMesh* Mesh, TArray<FVector3f>& OutPositions, TArray<uint32>& OutIndices)
	{
		OutPositions.Reset();
		OutIndices.Reset();
		if (!Mesh)
		{
			return false;
		}
#if WITH_EDITORONLY_DATA
		if (const FMeshDescription* Description = Mesh->GetMeshDescription(0))
		{
			FStaticMeshConstAttributes Attributes(*Description);
			const TVertexAttributesConstRef<FVector3f> Positions = Attributes.GetVertexPositions();
			OutPositions.SetNumZeroed(Description->Vertices().GetArraySize());
			for (const FVertexID Vertex : Description->Vertices().GetElementIDs())
			{
				OutPositions[Vertex.GetValue()] = Positions[Vertex];
			}
			for (const FTriangleID Triangle : Description->Triangles().GetElementIDs())
			{
				const TArrayView<const FVertexID> Corners = Description->GetTriangleVertices(Triangle);
				for (const FVertexID Corner : Corners)
				{
					OutIndices.Add(uint32(Corner.GetValue()));
				}
			}
			if (OutIndices.Num() > 0)
			{
				return true;
			}
		}
#endif
		const FStaticMeshRenderData* RenderData = Mesh->GetRenderData();
		if (Mesh->bAllowCPUAccess && RenderData && RenderData->LODResources.Num() > 0)
		{
			const FStaticMeshLODResources& LOD = RenderData->LODResources[0];
			const FPositionVertexBuffer& Buffer = LOD.VertexBuffers.PositionVertexBuffer;
			if (Buffer.GetNumVertices() > 0 && Buffer.GetVertexData())
			{
				OutPositions.SetNumUninitialized(Buffer.GetNumVertices());
				for (uint32 i = 0; i < Buffer.GetNumVertices(); ++i)
				{
					OutPositions[i] = Buffer.VertexPosition(i);
				}
				LOD.IndexBuffer.GetCopy(OutIndices);
			}
		}
		if (OutIndices.Num() == 0)
		{
			UE_LOG(LogDreamCloud, Warning, TEXT("%s: no triangles to bake a cloud from (in a game the mesh must allow CPU access)."), *Mesh->GetPathName());
			OutPositions.Reset();
			return false;
		}
		return true;
	}
}
