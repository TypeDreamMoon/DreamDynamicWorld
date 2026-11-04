// Copyright DreamMoon. All Rights Reserved.

#include "DreamWorldCelestial.h"

namespace DreamWorld::Celestial
{
	namespace
	{
		constexpr double SynodicMonth = 29.530588853;
		/** A new moon: 2000-01-06 18:14 UTC. */
		constexpr double ReferenceNewMoonJD = 2451550.26;

		double Wrap360(double Deg)
		{
			Deg = FMath::Fmod(Deg, 360.0);
			return Deg < 0.0 ? Deg + 360.0 : Deg;
		}

		double Frac(double X) { return X - FMath::FloorToDouble(X); }

		struct FBasis
		{
			FVector North, East, Up;
		};

		FBasis MakeBasis(double NorthYawDeg)
		{
			const double Yaw = FMath::DegreesToRadians(NorthYawDeg);
			FBasis B;
			B.North = FVector(FMath::Cos(Yaw), FMath::Sin(Yaw), 0.0);
			B.East = FVector(-FMath::Sin(Yaw), FMath::Cos(Yaw), 0.0);
			B.Up = FVector::UpVector;
			return B;
		}

		double AzimuthOf(const FVector& Dir, const FBasis& B)
		{
			return Wrap360(FMath::RadiansToDegrees(FMath::Atan2(FVector::DotProduct(Dir, B.East), FVector::DotProduct(Dir, B.North))));
		}

		/** Artistic orbit: east at Theta 0, Elevation degrees high in the south at Theta pi/2, west at pi, below the horizon after. */
		FVector OrbitDirection(double Theta, double NoonElevationDeg, const FBasis& B)
		{
			const double N = FMath::DegreesToRadians(NoonElevationDeg);
			const FVector Arc = -B.North * FMath::Cos(N) + B.Up * FMath::Sin(N);
			return (B.East * FMath::Cos(Theta) + Arc * FMath::Sin(Theta)).GetSafeNormal();
		}

		/** Angle along the artistic orbit at an hour of the day. */
		double OrbitAngle(double Hours, double Rise, double Set)
		{
			Set = FMath::Max(Set, Rise + 0.1);
			const double DayLength = FMath::Min(Set - Rise, 23.9);
			const double NightLength = 24.0 - DayLength;
			if (Hours >= Rise && Hours <= Rise + DayLength)
			{
				return UE_DOUBLE_PI * (Hours - Rise) / DayLength;
			}
			double SinceSet = Hours - (Rise + DayLength);
			if (SinceSet < 0.0)
			{
				SinceSet += 24.0;
			}
			return UE_DOUBLE_PI + UE_DOUBLE_PI * FMath::Clamp(SinceSet / NightLength, 0.0, 1.0);
		}

		struct FSunPosition
		{
			double Elevation = 0.0;
			double Azimuth = 0.0;
			/** Apparent ecliptic longitude, degrees. */
			double Longitude = 0.0;
		};

		/** The NOAA solar position algorithm (Meeus-based), accurate to a fraction of a degree. */
		FSunPosition SolarPosition(double JulianDayUtc, double LatitudeDeg, double LongitudeDeg)
		{
			const double JC = (JulianDayUtc - 2451545.0) / 36525.0;
			const double L0 = Wrap360(280.46646 + JC * (36000.76983 + JC * 0.0003032));
			const double M = 357.52911 + JC * (35999.05029 - 0.0001537 * JC);
			const double E = 0.016708634 - JC * (0.000042037 + 0.0000001267 * JC);
			const double Mr = FMath::DegreesToRadians(M);
			const double C = FMath::Sin(Mr) * (1.914602 - JC * (0.004817 + 0.000014 * JC)) + FMath::Sin(2.0 * Mr) * (0.019993 - 0.000101 * JC) + FMath::Sin(3.0 * Mr) * 0.000289;
			const double TrueLong = L0 + C;
			const double Omega = FMath::DegreesToRadians(125.04 - 1934.136 * JC);
			const double AppLong = TrueLong - 0.00569 - 0.00478 * FMath::Sin(Omega);
			const double MeanObliq = 23.0 + (26.0 + (21.448 - JC * (46.815 + JC * (0.00059 - JC * 0.001813))) / 60.0) / 60.0;
			const double Obliq = FMath::DegreesToRadians(MeanObliq + 0.00256 * FMath::Cos(Omega));
			const double Lambda = FMath::DegreesToRadians(AppLong);
			const double Decl = FMath::Asin(FMath::Sin(Obliq) * FMath::Sin(Lambda));

			const double Y = FMath::Square(FMath::Tan(Obliq * 0.5));
			const double L0r = FMath::DegreesToRadians(L0);
			const double EqTimeMinutes = 4.0 * FMath::RadiansToDegrees(Y * FMath::Sin(2.0 * L0r) - 2.0 * E * FMath::Sin(Mr) + 4.0 * E * Y * FMath::Sin(Mr) * FMath::Cos(2.0 * L0r)
				- 0.5 * Y * Y * FMath::Sin(4.0 * L0r) - 1.25 * E * E * FMath::Sin(2.0 * Mr));

			const double UtcMinutes = Frac(JulianDayUtc + 0.5) * 1440.0;
			double TrueSolarMinutes = FMath::Fmod(UtcMinutes + EqTimeMinutes + 4.0 * LongitudeDeg, 1440.0);
			if (TrueSolarMinutes < 0.0)
			{
				TrueSolarMinutes += 1440.0;
			}
			double HourAngle = TrueSolarMinutes / 4.0 - 180.0;
			if (HourAngle < -180.0)
			{
				HourAngle += 360.0;
			}

			const double Lat = FMath::DegreesToRadians(LatitudeDeg);
			const double Ha = FMath::DegreesToRadians(HourAngle);
			const double CosZenith = FMath::Clamp(FMath::Sin(Lat) * FMath::Sin(Decl) + FMath::Cos(Lat) * FMath::Cos(Decl) * FMath::Cos(Ha), -1.0, 1.0);
			const double Zenith = FMath::Acos(CosZenith);

			FSunPosition Out;
			Out.Elevation = 90.0 - FMath::RadiansToDegrees(Zenith);
			Out.Longitude = AppLong;
			const double Denominator = FMath::Cos(Lat) * FMath::Sin(Zenith);
			if (FMath::Abs(Denominator) > 1e-9)
			{
				const double CosAz = FMath::Clamp((FMath::Sin(Lat) * FMath::Cos(Zenith) - FMath::Sin(Decl)) / Denominator, -1.0, 1.0);
				const double Az = FMath::RadiansToDegrees(FMath::Acos(CosAz));
				Out.Azimuth = HourAngle > 0.0 ? Wrap360(Az + 180.0) : Wrap360(540.0 - Az);
			}
			else
			{
				Out.Azimuth = LatitudeDeg > 0.0 ? 180.0 : 0.0;
			}
			return Out;
		}

		struct FMoonPosition
		{
			double Elevation = 0.0;
			double Azimuth = 0.0;
			double Longitude = 0.0;
		};

		/** A low-precision lunar position (main periodic terms only, about a degree), topocentric parallax ignored. */
		FMoonPosition LunarPosition(double JulianDayUtc, double LatitudeDeg, double LongitudeDeg)
		{
			const double D = JulianDayUtc - 2451545.0;
			const double L = 218.316 + 13.176396 * D;
			const double M = FMath::DegreesToRadians(134.963 + 13.064993 * D);
			const double F = FMath::DegreesToRadians(93.272 + 13.229350 * D);
			const double Elong = FMath::DegreesToRadians(297.850 + 12.190749 * D);
			const double Ms = FMath::DegreesToRadians(357.529 + 0.98560028 * D);
			// longitude: equation of the centre, evection, variation, annual equation
			const double LambdaDeg = L + 6.289 * FMath::Sin(M) + 1.274 * FMath::Sin(2.0 * Elong - M) + 0.658 * FMath::Sin(2.0 * Elong) - 0.186 * FMath::Sin(Ms);
			const double Beta = FMath::DegreesToRadians(5.128 * FMath::Sin(F));
			const double Lambda = FMath::DegreesToRadians(LambdaDeg);
			const double Eps = FMath::DegreesToRadians(23.439 - 0.0000004 * D);

			const double RA = FMath::Atan2(FMath::Sin(Lambda) * FMath::Cos(Eps) - FMath::Tan(Beta) * FMath::Sin(Eps), FMath::Cos(Lambda));
			const double Decl = FMath::Asin(FMath::Sin(Beta) * FMath::Cos(Eps) + FMath::Cos(Beta) * FMath::Sin(Eps) * FMath::Sin(Lambda));

			const double Gmst = 280.46061837 + 360.98564736629 * D;
			const double Lst = FMath::DegreesToRadians(Wrap360(Gmst + LongitudeDeg));
			const double H = Lst - RA;
			const double Lat = FMath::DegreesToRadians(LatitudeDeg);

			FMoonPosition Out;
			Out.Elevation = FMath::RadiansToDegrees(FMath::Asin(FMath::Clamp(FMath::Sin(Lat) * FMath::Sin(Decl) + FMath::Cos(Lat) * FMath::Cos(Decl) * FMath::Cos(H), -1.0, 1.0)));
			// atan2 gives the azimuth from south, westward; +180 turns it into clockwise from north.
			Out.Azimuth = Wrap360(FMath::RadiansToDegrees(FMath::Atan2(FMath::Sin(H), FMath::Cos(H) * FMath::Sin(Lat) - FMath::Tan(Decl) * FMath::Cos(Lat))) + 180.0);
			Out.Longitude = Wrap360(LambdaDeg);
			return Out;
		}

		double SiderealDegrees(double JulianDayUtc, double LongitudeDeg)
		{
			return Wrap360(280.46061837 + 360.98564736629 * (JulianDayUtc - 2451545.0) + LongitudeDeg);
		}
	}

	double FromDateTime(const FDateTime& Time)
	{
		return double(Time.GetTicks()) / double(ETimespan::TicksPerSecond);
	}

	FDateTime ToDateTime(double WorldSeconds)
	{
		const double Clamped = FMath::Clamp(WorldSeconds, 0.0, double(FDateTime::MaxValue().GetTicks()) / double(ETimespan::TicksPerSecond));
		return FDateTime(int64(Clamped * double(ETimespan::TicksPerSecond)));
	}

	double HoursOfDay(double WorldSeconds)
	{
		return FMath::Fmod(FMath::Max(WorldSeconds, 0.0), 86400.0) / 3600.0;
	}

	int64 DayIndex(double WorldSeconds)
	{
		return int64(FMath::FloorToDouble(FMath::Max(WorldSeconds, 0.0) / 86400.0));
	}

	FVector DirectionFromAngles(double ElevationDeg, double AzimuthDeg, double NorthYawDeg)
	{
		const FBasis B = MakeBasis(NorthYawDeg);
		const double El = FMath::DegreesToRadians(ElevationDeg);
		const double Az = FMath::DegreesToRadians(AzimuthDeg);
		return (FMath::Cos(El) * (FMath::Cos(Az) * B.North + FMath::Sin(Az) * B.East) + FMath::Sin(El) * B.Up).GetSafeNormal();
	}

	double ElevationOf(const FVector& Direction)
	{
		return FMath::RadiansToDegrees(FMath::Asin(FMath::Clamp(Direction.GetSafeNormal().Z, -1.0, 1.0)));
	}

	double MeanMoonPhase(double JulianDayUtc)
	{
		return Frac((JulianDayUtc - ReferenceNewMoonJD) / SynodicMonth);
	}

	double IlluminationFromPhase(double Phase)
	{
		return 0.5 * (1.0 - FMath::Cos(2.0 * UE_DOUBLE_PI * Phase));
	}

	FDreamCelestialState Evaluate(const FDreamCelestialSettings& S, double WorldSeconds)
	{
		const FBasis B = MakeBasis(S.NorthYaw);
		const double Hours = HoursOfDay(WorldSeconds);
		// FDateTime counts days from 0001-01-01; its Julian day treats the clock as UTC.
		const double JulianLocal = 1721425.5 + WorldSeconds / 86400.0;

		FDreamCelestialState Out;
		if (S.Mode == EDreamCelestialMode::Realistic)
		{
			const double JulianUtc = JulianLocal - double(S.UtcOffsetHours) / 24.0;
			const FSunPosition Sun = SolarPosition(JulianUtc, S.Latitude, S.Longitude);
			const FMoonPosition Moon = LunarPosition(JulianUtc, S.Latitude, S.Longitude);
			Out.SunDirection = DirectionFromAngles(Sun.Elevation, Sun.Azimuth, S.NorthYaw);
			Out.MoonDirection = DirectionFromAngles(Moon.Elevation, Moon.Azimuth, S.NorthYaw);
			Out.MoonPhase = float(Frac((Moon.Longitude - Sun.Longitude) / 360.0));
			Out.StarRotation = float(SiderealDegrees(JulianUtc, S.Longitude));
		}
		else
		{
			const double Theta = OrbitAngle(Hours, S.SunriseHour, S.SunsetHour);
			const double Phase = MeanMoonPhase(JulianLocal);
			// A waxing moon trails the sun across the sky by its phase; the full moon is opposite.
			const double MoonTheta = S.bMoonOppositeSun ? Theta + UE_DOUBLE_PI : Theta - 2.0 * UE_DOUBLE_PI * Phase;
			Out.SunDirection = OrbitDirection(Theta, S.NoonElevation, B);
			Out.MoonDirection = OrbitDirection(MoonTheta, S.NoonElevation, B);
			Out.MoonPhase = float(Phase);
			// Sidereal: one turn a day plus one extra turn a year.
			Out.StarRotation = float(Wrap360(360.0 * (Hours / 24.0) + 360.0 * Frac(JulianLocal / 365.2422)));
		}
		Out.SunElevation = float(ElevationOf(Out.SunDirection));
		Out.SunAzimuth = float(AzimuthOf(Out.SunDirection, B));
		Out.MoonElevation = float(ElevationOf(Out.MoonDirection));
		Out.MoonAzimuth = float(AzimuthOf(Out.MoonDirection, B));
		Out.MoonIllumination = float(IlluminationFromPhase(Out.MoonPhase));
		return Out;
	}

	FDreamDayPhase DayPhase(float SunElevation, float DayBottom, float TwilightTop, float NightTop, float NightBottom)
	{
		FDreamDayPhase P;
		P.Day = FMath::SmoothStep(TwilightTop, FMath::Max(DayBottom, TwilightTop + 0.01f), SunElevation);
		P.Night = 1.0f - FMath::SmoothStep(FMath::Min(NightBottom, NightTop - 0.01f), NightTop, SunElevation);
		const float Sum = P.Day + P.Night;
		if (Sum > 1.0f)
		{
			P.Day /= Sum;
			P.Night /= Sum;
		}
		P.Twilight = FMath::Max(0.0f, 1.0f - P.Day - P.Night);
		return P;
	}
}
