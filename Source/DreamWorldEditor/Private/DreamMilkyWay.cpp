// Copyright DreamMoon. All Rights Reserved.

#include "DreamMilkyWay.h"

#include "Async/ParallelFor.h"

namespace DreamMilkyWay
{
	namespace
	{
		FVector3d MilkyWayEquatorial(double RaDeg, double DecDeg)
		{
			const double Ra = FMath::DegreesToRadians(RaDeg), Dec = FMath::DegreesToRadians(DecDeg);
			return FVector3d(FMath::Cos(Dec) * FMath::Cos(Ra), FMath::Cos(Dec) * FMath::Sin(Ra), FMath::Sin(Dec));
		}

		/** Fractal noise over the sphere (a galactic direction), -1..1 roughly. */
		double MilkyWayFbm(const FVector3d& D, double Frequency, int32 Octaves, double Seed)
		{
			double Sum = 0.0, Amplitude = 0.5, Norm = 0.0;
			for (int32 Octave = 0; Octave < Octaves; ++Octave)
			{
				const FVector P = FVector(D * Frequency) + FVector(Seed * 17.31, Seed * 5.17 + Octave * 3.7, Seed * 11.9);
				Sum += Amplitude * FMath::PerlinNoise3D(P);
				Norm += Amplitude;
				Amplitude *= 0.5;
				Frequency *= 2.03;
			}
			return Sum / Norm;
		}

		double MilkyWayAngle(const FVector3d& A, const FVector3d& B)
		{
			return FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(FVector3d::DotProduct(A, B), -1.0, 1.0)));
		}

		double MilkyWayBand(double X, double Low, double High, double Soft)
		{
			return FMath::SmoothStep(Low - Soft, Low + Soft, X) * (1.0 - FMath::SmoothStep(High - Soft, High + Soft, X));
		}
	}

	FVector3d PoleAxis()
	{
		return MilkyWayEquatorial(192.85948, 27.12825);
	}

	FVector3d CentreAxis()
	{
		const FVector3d Z = PoleAxis();
		const FVector3d C = MilkyWayEquatorial(266.40500, -28.93617);
		return (C - Z * FVector3d::DotProduct(C, Z)).GetSafeNormal();
	}

	FVector3d NinetyAxis()
	{
		return FVector3d::CrossProduct(PoleAxis(), CentreAxis());
	}

	void Evaluate(double Longitude, double Latitude, FLinearColor& OutLight, float& OutDust)
	{
		// (Longitude from the centre, -180..180; the direction in the galactic frame, for the noise.)
		const double L = FMath::Fmod(Longitude + 540.0, 360.0) - 180.0;
		const double B = Latitude;
		const double Lr = FMath::DegreesToRadians(L), Br = FMath::DegreesToRadians(B);
		const FVector3d D(FMath::Cos(Br) * FMath::Cos(Lr), FMath::Cos(Br) * FMath::Sin(Lr), FMath::Sin(Br));

		// The band: thicker toward the centre, its width and its middle wandering a little along it.
		const double Inner = FMath::Exp(-FMath::Square(L / 60.0));
		const double Thickness = (4.0 + 5.0 * Inner) * (1.0 + 0.45 * MilkyWayFbm(D, 2.0, 3, 1.0));
		const double Middle = 1.0 * MilkyWayFbm(D, 1.5, 2, 2.0);
		const double Disk = FMath::Exp(-FMath::Pow(FMath::Abs(B - Middle) / Thickness, 1.35));
		// Brighter toward the centre, and in the star clouds of Cygnus (l 80) and Scutum (l 27) besides.
		const double Along = (0.7 + 0.6 * (0.5 + 0.5 * MilkyWayFbm(D, 2.5, 3, 9.0))) * (0.2 + 0.8 * FMath::Exp(-FMath::Square(L / 75.0))) + 0.22 * FMath::Exp(-FMath::Square((L - 78.0) / 14.0))
			+ 0.18 * FMath::Exp(-FMath::Square((L - 27.0) / 7.0)) + 0.06 * FMath::Exp(-FMath::Square((FMath::Abs(L) - 180.0) / 50.0));
		// The bulge round the centre, a little flattened.
		const double Bulge = FMath::Exp(-(FMath::Square(L / 11.0) + FMath::Square(B / 8.0))) + 0.3 * FMath::Exp(-FMath::Sqrt(FMath::Square(L / 18.0) + FMath::Square(B / 12.0)));
		// Clumped into star clouds, with a fine grain of the unresolved stars.
		const double Clouds = 0.3 + 1.4 * FMath::Clamp(0.5 + 1.2 * MilkyWayFbm(D, 6.0, 5, 3.0), 0.0, 1.0);
		const double Grain = 0.88 + 0.24 * (0.5 + 0.5 * MilkyWayFbm(D, 70.0, 2, 4.0));
		double Light = (Disk * Along + 0.9 * Bulge) * Clouds * Grain;
		// The Magellanic Clouds.
		const FVector3d Lmc = FVector3d(FMath::Cos(FMath::DegreesToRadians(-32.9)) * FMath::Cos(FMath::DegreesToRadians(280.5)),
			FMath::Cos(FMath::DegreesToRadians(-32.9)) * FMath::Sin(FMath::DegreesToRadians(280.5)), FMath::Sin(FMath::DegreesToRadians(-32.9)));
		const FVector3d Smc = FVector3d(FMath::Cos(FMath::DegreesToRadians(-44.3)) * FMath::Cos(FMath::DegreesToRadians(302.8)),
			FMath::Cos(FMath::DegreesToRadians(-44.3)) * FMath::Sin(FMath::DegreesToRadians(302.8)), FMath::Sin(FMath::DegreesToRadians(-44.3)));
		Light += (0.3 * FMath::Exp(-FMath::Square(MilkyWayAngle(D, Lmc) / 3.2)) + 0.13 * FMath::Exp(-FMath::Square(MilkyWayAngle(D, Smc) / 1.8)))
			* (0.7 + 0.3 * Clouds);

		// Warm white toward the centre, cooler along the arms.
		const double Warm = FMath::Clamp(1.4 * Bulge + 0.35 * Inner, 0.0, 1.0);
		const FLinearColor Arm(0.88f, 0.92f, 1.0f), Core(1.0f, 0.88f, 0.74f);
		OutLight = FMath::Lerp(Arm, Core, float(Warm)) * float(Light);

		// The dust: a thin patchy lane along the plane; the Great Rift north of it from Cygnus to the centre, widening
		// into the dark clouds of Ophiuchus; small dark clouds scattered along the band.
		const double Patchy = FMath::Clamp(0.35 + 1.1 * (0.5 + 0.6 * MilkyWayFbm(D, 9.0, 4, 5.0)), 0.0, 1.6);
		const double Lane = FMath::Exp(-FMath::Square((B - Middle - 0.8 * MilkyWayFbm(D, 4.0, 3, 6.0)) / 1.8))
			* FMath::SmoothStep(0.3, 0.8, 0.5 + 0.7 * MilkyWayFbm(D, 5.0, 4, 10.0));
		const double RiftReach = MilkyWayBand(L, -8.0, 82.0, 8.0);
		const double Ophiuchus = FMath::Exp(-FMath::Square((L - 5.0) / 14.0));
		const double RiftLat = 1.2 + 1.8 * FMath::Exp(-FMath::Square((L - 40.0) / 30.0)) + 5.0 * Ophiuchus + 1.2 * MilkyWayFbm(D, 3.0, 3, 7.0);
		const double RiftWidth = 1.0 + 0.8 * FMath::Exp(-FMath::Square((L - 50.0) / 25.0)) + 4.0 * Ophiuchus;
		const double Rift = RiftReach * FMath::Exp(-FMath::Square((B - RiftLat) / RiftWidth));
		const double Knots = FMath::SmoothStep(0.35, 0.7, MilkyWayFbm(D, 9.0, 3, 8.0)) * FMath::Exp(-FMath::Abs(B) / (1.5 * Thickness));
		OutDust = float((1.8 * Lane + 2.4 * Rift) * Patchy + 0.8 * Knots);
	}

	void Build(int32 InWidth, int32 InHeight, TArray<FColor>& OutTexels)
	{
		TArray<FLinearColor> Light;
		TArray<float> Dust;
		Light.SetNumUninitialized(InWidth * InHeight);
		Dust.SetNumUninitialized(InWidth * InHeight);
		ParallelFor(InHeight, [&](int32 Y)
		{
			const double Latitude = 90.0 - 180.0 * (Y + 0.5) / InHeight;
			for (int32 X = 0; X < InWidth; ++X)
			{
				// (The centre in the middle of the map.)
				const double Longitude = 360.0 * (X + 0.5) / InWidth - 180.0;
				Evaluate(Longitude, Latitude, Light[Y * InWidth + X], Dust[Y * InWidth + X]);
			}
		});
		float Brightest = 1.0e-6f;
		for (const FLinearColor& C : Light)
		{
			Brightest = FMath::Max(Brightest, C.GetMax());
		}
		OutTexels.SetNumUninitialized(InWidth * InHeight);
		for (int32 Index = 0; Index < OutTexels.Num(); ++Index)
		{
			FColor C = (Light[Index] * (0.98f / Brightest)).ToFColor(true);
			C.A = uint8(FMath::RoundToInt(FMath::Clamp(Dust[Index] / 4.0f, 0.0f, 1.0f) * 255.0f));
			OutTexels[Index] = C;
		}
	}
}
