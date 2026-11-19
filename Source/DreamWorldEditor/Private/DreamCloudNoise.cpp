// Copyright DreamMoon. All Rights Reserved.

#include "DreamCloudNoise.h"

#include "Async/ParallelFor.h"

namespace DreamCloudNoise
{
	namespace
	{
		uint32 Hash(int32 X, int32 Y, int32 Z, uint32 Seed)
		{
			uint32 H = uint32(X) * 73856093u ^ uint32(Y) * 19349663u ^ uint32(Z) * 83492791u ^ Seed * 2654435761u;
			H ^= H >> 16;
			H *= 0x7feb352du;
			H ^= H >> 15;
			H *= 0x846ca68bu;
			H ^= H >> 16;
			return H;
		}

		float Unit(uint32 H)
		{
			return float(H & 0xffffffu) * (1.0f / 16777215.0f);
		}

		int32 Wrap(int32 V, int32 Period)
		{
			const int32 M = V % Period;
			return M < 0 ? M + Period : M;
		}

		float Fade(float T)
		{
			return T * T * T * (T * (T * 6.0f - 15.0f) + 10.0f);
		}

		/** Inverted Worley F1 of P (0..1 across the tile), Period cells across: 1 at a feature point, 0 a cell away. */
		float Worley(const FVector3f& P, int32 Period, uint32 Seed)
		{
			const FVector3f Q = P * float(Period);
			const int32 X0 = FMath::FloorToInt(Q.X);
			const int32 Y0 = FMath::FloorToInt(Q.Y);
			const int32 Z0 = FMath::FloorToInt(Q.Z);
			float Best = 1.0e9f;
			for (int32 Z = Z0 - 1; Z <= Z0 + 1; ++Z)
			{
				for (int32 Y = Y0 - 1; Y <= Y0 + 1; ++Y)
				{
					for (int32 X = X0 - 1; X <= X0 + 1; ++X)
					{
						// The feature point of a cell is the one of its wrapped twin, so the field tiles.
						const int32 WX = Wrap(X, Period), WY = Wrap(Y, Period), WZ = Wrap(Z, Period);
						const FVector3f Feature(float(X) + Unit(Hash(WX, WY, WZ, Seed)), float(Y) + Unit(Hash(WX, WY, WZ, Seed + 1u)),
							float(Z) + Unit(Hash(WX, WY, WZ, Seed + 2u)));
						Best = FMath::Min(Best, FVector3f::DistSquared(Q, Feature));
					}
				}
			}
			return 1.0f - FMath::Clamp(FMath::Sqrt(Best), 0.0f, 1.0f);
		}

		float WorleyFbm(const FVector3f& P, int32 Period, uint32 Seed)
		{
			return Worley(P, Period, Seed) * 0.625f + Worley(P, Period * 2, Seed + 7u) * 0.25f + Worley(P, Period * 4, Seed + 13u) * 0.125f;
		}

		/** Gradient noise of P, Period cells across, about -1..1. */
		float Perlin(const FVector3f& P, int32 Period, uint32 Seed)
		{
			const FVector3f Q = P * float(Period);
			const int32 X0 = FMath::FloorToInt(Q.X);
			const int32 Y0 = FMath::FloorToInt(Q.Y);
			const int32 Z0 = FMath::FloorToInt(Q.Z);
			const FVector3f F(Q.X - float(X0), Q.Y - float(Y0), Q.Z - float(Z0));
			auto Corner = [&](int32 DX, int32 DY, int32 DZ)
			{
				const int32 WX = Wrap(X0 + DX, Period), WY = Wrap(Y0 + DY, Period), WZ = Wrap(Z0 + DZ, Period);
				FVector3f G(Unit(Hash(WX, WY, WZ, Seed)) * 2.0f - 1.0f, Unit(Hash(WX, WY, WZ, Seed + 1u)) * 2.0f - 1.0f,
					Unit(Hash(WX, WY, WZ, Seed + 2u)) * 2.0f - 1.0f);
				G = G.GetSafeNormal(1.0e-6f, FVector3f(1.0f, 0.0f, 0.0f));
				return FVector3f::DotProduct(G, F - FVector3f(float(DX), float(DY), float(DZ)));
			};
			const float U = Fade(F.X), V = Fade(F.Y), W = Fade(F.Z);
			const float X00 = FMath::Lerp(Corner(0, 0, 0), Corner(1, 0, 0), U);
			const float X10 = FMath::Lerp(Corner(0, 1, 0), Corner(1, 1, 0), U);
			const float X01 = FMath::Lerp(Corner(0, 0, 1), Corner(1, 0, 1), U);
			const float X11 = FMath::Lerp(Corner(0, 1, 1), Corner(1, 1, 1), U);
			return FMath::Lerp(FMath::Lerp(X00, X10, V), FMath::Lerp(X01, X11, V), W);
		}

		/** Two octaves of gradient noise, 0..1. */
		float PerlinFbm(const FVector3f& P, int32 Period, uint32 Seed)
		{
			const float Sum = Perlin(P, Period, Seed) * 0.7f + Perlin(P, Period * 2, Seed + 31u) * 0.3f;
			return FMath::Clamp(Sum * 0.9f + 0.5f, 0.0f, 1.0f);
		}

		/** Two octaves of Worley, for the smooth base shapes. */
		float WorleySmooth(const FVector3f& P, int32 Period, uint32 Seed)
		{
			return Worley(P, Period, Seed) * 0.7f + Worley(P, Period * 2, Seed + 7u) * 0.3f;
		}

		uint8 Byte(float V)
		{
			return uint8(FMath::Clamp(FMath::RoundToInt(V * 255.0f), 0, 255));
		}
	}

	void Build(int32 Size, TArray<FColor>& OutVoxels)
	{
		Size = FMath::Max(8, Size);
		OutVoxels.SetNumUninitialized(Size * Size * Size);
		ParallelFor(Size, [Size, &OutVoxels](int32 Z)
		{
			for (int32 Y = 0; Y < Size; ++Y)
			{
				for (int32 X = 0; X < Size; ++X)
				{
					const FVector3f P((float(X) + 0.5f) / float(Size), (float(Y) + 0.5f) / float(Size), (float(Z) + 0.5f) / float(Size));
					// Perlin-Worley: the gradient noise remapped against the Worley cells, so its blobs come out
					// round instead of stringy; two octaves of each, so they stay smooth.
					const float Cells = WorleySmooth(P, 4, 101u);
					const float Gradient = PerlinFbm(P, 4, 202u);
					const float Low = Cells - 1.0f;
					// It comes out between about 0.45 and 0.87 (median 0.67): stretched to fill 0..1, so a cover
					// threshold clears about as much sky as it says.
					const float PerlinWorley = FMath::Clamp(((Gradient - Low) / (1.0f - Low) - 0.45f) / 0.42f, 0.0f, 1.0f);
					FColor& Out = OutVoxels[(Z * Size + Y) * Size + X];
					Out.R = Byte(PerlinWorley);
					Out.G = Byte(WorleyFbm(P, 8, 303u));
					Out.B = Byte(WorleyFbm(P, 16, 404u));
					Out.A = Byte(Worley(P, 32, 505u) * 0.75f + Worley(P, 64, 506u) * 0.25f);
				}
			}
		});
	}
}
