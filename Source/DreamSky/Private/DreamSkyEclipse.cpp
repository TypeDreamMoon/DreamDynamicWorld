// Copyright DreamMoon. All Rights Reserved.

#include "DreamSkyEclipse.h"

namespace DreamSky::Eclipse
{
	float DiskOverlap(float D, float R)
	{
		D = FMath::Abs(D);
		R = FMath::Max(R, 1.0e-6f);
		if (D >= 1.0f + R)
		{
			return 0.0f;
		}
		if (D <= FMath::Abs(R - 1.0f))
		{
			// One inside the other: the smaller's area over the first's.
			return FMath::Min(1.0f, R * R);
		}
		// The lens of two circles (radii 1 and R, centres D apart), over pi.
		const double d = D, r = R;
		const double A1 = FMath::Acos(FMath::Clamp((d * d + 1.0 - r * r) / (2.0 * d), -1.0, 1.0));
		const double A2 = FMath::Acos(FMath::Clamp((d * d + r * r - 1.0) / (2.0 * d * r), -1.0, 1.0));
		const double K = 0.5 * FMath::Sqrt(FMath::Max(0.0, (-d + 1.0 + r) * (d + 1.0 - r) * (d - 1.0 + r) * (d + 1.0 + r)));
		return float(FMath::Clamp((A1 + r * r * A2 - K) / UE_DOUBLE_PI, 0.0, 1.0));
	}

	float DistanceForOverlap(float Overlap, float R)
	{
		const float Full = DiskOverlap(0.0f, R);
		Overlap = FMath::Clamp(Overlap, 0.0f, Full);
		if (Overlap >= Full)
		{
			return 0.0f;
		}
		float Lo = 0.0f;
		float Hi = 1.0f + R;
		for (int32 Step = 0; Step < 40; ++Step)
		{
			const float Mid = 0.5f * (Lo + Hi);
			(DiskOverlap(Mid, R) > Overlap ? Lo : Hi) = Mid;
		}
		return 0.5f * (Lo + Hi);
	}

	void DiskFrame(const FVector& Direction, FVector& OutX, FVector& OutY)
	{
		const FVector Dir = Direction.GetSafeNormal();
		const FVector Up = FMath::Abs(Dir.Z) < 0.99 ? FVector::UpVector : FVector::ForwardVector;
		OutX = FVector::CrossProduct(Up, Dir).GetSafeNormal();
		OutY = FVector::CrossProduct(Dir, OutX);
	}

	FVector2D OnDisk(const FVector& Direction, const FVector& Point, double Radius)
	{
		FVector X, Y;
		DiskFrame(Direction, X, Y);
		const FVector D = Point.GetSafeNormal() - Direction.GetSafeNormal();
		return FVector2D(FVector::DotProduct(D, X), FVector::DotProduct(D, Y)) / FMath::Max(Radius, 1.0e-9);
	}

	float Solar(const FVector& Sun, const FVector& Moon, FVector2D& OutMoonOnSun)
	{
		OutMoonOnSun = OnDisk(Sun, Moon, SunRadius);
		// (Only toward the sun: the moon behind the view is not on the sun's disk however its projection falls.)
		if (FVector::DotProduct(Sun.GetSafeNormal(), Moon.GetSafeNormal()) <= 0.0)
		{
			OutMoonOnSun = FVector2D(1.0e3, 0.0);
			return 0.0f;
		}
		return DiskOverlap(float(OutMoonOnSun.Size()), float(MoonToSun));
	}

	float Lunar(const FVector& Sun, const FVector& Moon, FVector2D& OutUmbraOnMoon)
	{
		const FVector AntiSun = -Sun.GetSafeNormal();
		OutUmbraOnMoon = OnDisk(Moon, AntiSun, MoonRadius);
		if (FVector::DotProduct(AntiSun, Moon.GetSafeNormal()) <= 0.0)
		{
			OutUmbraOnMoon = FVector2D(1.0e3, 0.0);
			return 0.0f;
		}
		return DiskOverlap(float(OutUmbraOnMoon.Size()), float(UmbraRadius / MoonRadius));
	}
}
