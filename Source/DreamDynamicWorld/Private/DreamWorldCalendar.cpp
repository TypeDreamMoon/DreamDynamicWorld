// Copyright DreamMoon. All Rights Reserved.

#include "DreamWorldCalendar.h"

#define LOCTEXT_NAMESPACE "DreamWorldCalendar"

namespace DreamWorld::CalendarRules
{
	/** The date of the Nth (1-based) given weekday of a month, or its last (N = -1). Weekday as FDateTime counts it. */
	static int32 NthWeekday(int32 Year, int32 Month, EDayOfWeek Weekday, int32 N)
	{
		if (N < 0)
		{
			const int32 Last = FDateTime::DaysInMonth(Year, Month);
			for (int32 Day = Last; Day > Last - 7; --Day)
			{
				if (FDateTime(Year, Month, Day).GetDayOfWeek() == Weekday)
				{
					return Day;
				}
			}
			return Last;
		}
		int32 Seen = 0;
		for (int32 Day = 1; Day <= FDateTime::DaysInMonth(Year, Month); ++Day)
		{
			if (FDateTime(Year, Month, Day).GetDayOfWeek() == Weekday && ++Seen == N)
			{
				return Day;
			}
		}
		return 1;
	}

	/** A local moment's place against a change at a day and an hour of a month: true from it on. */
	static bool AtOrAfter(const FDateTime& Local, int32 Month, int32 Day, int32 Hour)
	{
		return Local >= FDateTime(Local.GetYear(), Month, Day, Hour);
	}
}

const TArray<FDreamCityPreset>& UDreamWorldCalendar::GetCityPresets()
{
	static const TArray<FDreamCityPreset> Cities = []
	{
		TArray<FDreamCityPreset> Out;
		auto Add = [&Out](const TCHAR* Name, double Lat, double Lon, float Utc, EDreamDaylightSaving Rule)
		{
			FDreamCityPreset& C = Out.AddDefaulted_GetRef();
			C.Name = FName(Name);
			C.Latitude = Lat;
			C.Longitude = Lon;
			C.UtcOffsetHours = Utc;
			C.DaylightSaving = Rule;
		};
		using E = EDreamDaylightSaving;
		Add(TEXT("Tokyo"), 35.68, 139.69, 9.0f, E::None);
		Add(TEXT("Seoul"), 37.57, 126.98, 9.0f, E::None);
		Add(TEXT("Beijing"), 39.90, 116.40, 8.0f, E::None);
		Add(TEXT("Shanghai"), 31.23, 121.47, 8.0f, E::None);
		Add(TEXT("HongKong"), 22.32, 114.17, 8.0f, E::None);
		Add(TEXT("Singapore"), 1.35, 103.82, 8.0f, E::None);
		Add(TEXT("Bangkok"), 13.76, 100.50, 7.0f, E::None);
		Add(TEXT("Delhi"), 28.61, 77.21, 5.5f, E::None);
		Add(TEXT("Dubai"), 25.20, 55.27, 4.0f, E::None);
		Add(TEXT("Moscow"), 55.76, 37.62, 3.0f, E::None);
		Add(TEXT("Istanbul"), 41.01, 28.98, 3.0f, E::None);
		Add(TEXT("Cairo"), 30.04, 31.24, 2.0f, E::None);
		Add(TEXT("CapeTown"), -33.92, 18.42, 2.0f, E::None);
		Add(TEXT("Paris"), 48.86, 2.35, 1.0f, E::Europe);
		Add(TEXT("Berlin"), 52.52, 13.40, 1.0f, E::Europe);
		Add(TEXT("Rome"), 41.90, 12.50, 1.0f, E::Europe);
		Add(TEXT("Tromso"), 69.65, 18.96, 1.0f, E::Europe);
		Add(TEXT("London"), 51.51, -0.13, 0.0f, E::Europe);
		Add(TEXT("Reykjavik"), 64.15, -21.94, 0.0f, E::None);
		Add(TEXT("NewYork"), 40.71, -74.01, -5.0f, E::NorthAmerica);
		Add(TEXT("Chicago"), 41.88, -87.63, -6.0f, E::NorthAmerica);
		Add(TEXT("Denver"), 39.74, -104.99, -7.0f, E::NorthAmerica);
		Add(TEXT("LosAngeles"), 34.05, -118.24, -8.0f, E::NorthAmerica);
		Add(TEXT("Vancouver"), 49.28, -123.12, -8.0f, E::NorthAmerica);
		Add(TEXT("Anchorage"), 61.22, -149.90, -9.0f, E::NorthAmerica);
		Add(TEXT("Honolulu"), 21.31, -157.86, -10.0f, E::None);
		Add(TEXT("MexicoCity"), 19.43, -99.13, -6.0f, E::None);
		Add(TEXT("SaoPaulo"), -23.55, -46.63, -3.0f, E::None);
		Add(TEXT("BuenosAires"), -34.60, -58.38, -3.0f, E::None);
		Add(TEXT("Sydney"), -33.87, 151.21, 10.0f, E::SouthernAustralia);
		Add(TEXT("Melbourne"), -37.81, 144.96, 10.0f, E::SouthernAustralia);
		Add(TEXT("Auckland"), -36.85, 174.76, 12.0f, E::NewZealand);
		return Out;
	}();
	return Cities;
}

TArray<FName> UDreamWorldCalendar::GetCityNames()
{
	TArray<FName> Names;
	Names.Add(NAME_None);
	for (const FDreamCityPreset& City : GetCityPresets())
	{
		Names.Add(City.Name);
	}
	return Names;
}

bool UDreamWorldCalendar::FindCity(FName Name, FDreamCityPreset& OutCity)
{
	for (const FDreamCityPreset& City : GetCityPresets())
	{
		if (City.Name == Name)
		{
			OutCity = City;
			return true;
		}
	}
	return false;
}

bool UDreamWorldCalendar::IsDaylightSaving(EDreamDaylightSaving Rule, FDateTime Local)
{
	using namespace DreamWorld::CalendarRules;
	const int32 Y = Local.GetYear();
	switch (Rule)
	{
	case EDreamDaylightSaving::Europe:
		// (The last Sunday of March to the last of October, at 01:00 UTC: the local hour taken as 2 and 3.)
		return AtOrAfter(Local, 3, NthWeekday(Y, 3, EDayOfWeek::Sunday, -1), 2) && !AtOrAfter(Local, 10, NthWeekday(Y, 10, EDayOfWeek::Sunday, -1), 3);
	case EDreamDaylightSaving::NorthAmerica:
		// (The second Sunday of March to the first of November, at 2:00 local.)
		return AtOrAfter(Local, 3, NthWeekday(Y, 3, EDayOfWeek::Sunday, 2), 2) && !AtOrAfter(Local, 11, NthWeekday(Y, 11, EDayOfWeek::Sunday, 1), 2);
	case EDreamDaylightSaving::SouthernAustralia:
		// (From the first Sunday of October to the first of April: off between, at 3:00 and 2:00 local.)
		return !(AtOrAfter(Local, 4, NthWeekday(Y, 4, EDayOfWeek::Sunday, 1), 3) && !AtOrAfter(Local, 10, NthWeekday(Y, 10, EDayOfWeek::Sunday, 1), 2));
	case EDreamDaylightSaving::NewZealand:
		// (From the last Sunday of September to the first of April.)
		return !(AtOrAfter(Local, 4, NthWeekday(Y, 4, EDayOfWeek::Sunday, 1), 3) && !AtOrAfter(Local, 9, NthWeekday(Y, 9, EDayOfWeek::Sunday, -1), 2));
	default:
		return false;
	}
}

void UDreamWorldCalendar::ApplyCity(FDreamCelestialSettings& Settings)
{
	FDreamCityPreset City;
	if (!Settings.City.IsNone() && FindCity(Settings.City, City))
	{
		Settings.Latitude = City.Latitude;
		Settings.Longitude = City.Longitude;
		Settings.UtcOffsetHours = City.UtcOffsetHours;
		Settings.DaylightSaving = City.DaylightSaving;
	}
}

FDreamCelestialSettings UDreamWorldCalendar::Resolve(const FDreamCelestialSettings& Settings, const FDateTime& Local)
{
	FDreamCelestialSettings Out = Settings;
	ApplyCity(Out);
	if (IsDaylightSaving(Out.DaylightSaving, Local))
	{
		Out.UtcOffsetHours += 1.0f;
	}
	return Out;
}

FDreamCalendarDate UDreamWorldCalendar::GregorianDate(double WorldSeconds)
{
	const FDateTime Date = FDateTime(int64(WorldSeconds * double(ETimespan::TicksPerSecond)));
	FDreamCalendarDate Out;
	Out.Year = Date.GetYear();
	Out.Month = Date.GetMonth();
	Out.Day = Date.GetDay();
	Out.DayOfYear = Date.GetDayOfYear();
	Out.WeekDay = int32(Date.GetDayOfWeek());
	Out.Hour = Date.GetHour();
	Out.Minute = Date.GetMinute();
	Out.MonthName = FText::AsCultureInvariant(FString::Printf(TEXT("%d"), Out.Month));
	static const FText Days[7] = { LOCTEXT("Monday", "Monday"), LOCTEXT("Tuesday", "Tuesday"), LOCTEXT("Wednesday", "Wednesday"),
		LOCTEXT("Thursday", "Thursday"), LOCTEXT("Friday", "Friday"), LOCTEXT("Saturday", "Saturday"), LOCTEXT("Sunday", "Sunday") };
	static const FText Months[12] = { LOCTEXT("January", "January"), LOCTEXT("February", "February"), LOCTEXT("March", "March"),
		LOCTEXT("April", "April"), LOCTEXT("May", "May"), LOCTEXT("June", "June"), LOCTEXT("July", "July"), LOCTEXT("August", "August"),
		LOCTEXT("September", "September"), LOCTEXT("October", "October"), LOCTEXT("November", "November"), LOCTEXT("December", "December") };
	Out.WeekDayName = Days[FMath::Clamp(Out.WeekDay, 0, 6)];
	Out.MonthName = Months[FMath::Clamp(Out.Month - 1, 0, 11)];
	return Out;
}

int32 UDreamCalendarAsset::GetDaysInYear() const
{
	int32 Days = 0;
	for (const FDreamCalendarMonth& M : Months)
	{
		Days += FMath::Max(M.Days, 1);
	}
	return Days;
}

FDreamCalendarDate UDreamCalendarAsset::ToDate(double WorldSeconds) const
{
	const int32 YearDays = GetDaysInYear();
	if (YearDays <= 0)
	{
		return UDreamWorldCalendar::GregorianDate(WorldSeconds);
	}
	FDreamCalendarDate Out;
	const int64 DayIndex = int64(FMath::FloorToDouble(WorldSeconds / 86400.0));
	const double Into = WorldSeconds - double(DayIndex) * 86400.0;
	Out.Hour = int32(Into / 3600.0);
	Out.Minute = int32((Into - Out.Hour * 3600.0) / 60.0);
	Out.Year = FirstYear + int32(DayIndex / YearDays);
	int32 Left = int32(DayIndex % YearDays);
	Out.DayOfYear = Left + 1;
	for (int32 M = 0; M < Months.Num(); ++M)
	{
		const int32 Days = FMath::Max(Months[M].Days, 1);
		if (Left < Days)
		{
			Out.Month = M + 1;
			Out.Day = Left + 1;
			Out.MonthName = Months[M].Name;
			break;
		}
		Left -= Days;
	}
	if (WeekDays.Num() > 0)
	{
		Out.WeekDay = int32(DayIndex % WeekDays.Num());
		Out.WeekDayName = WeekDays[Out.WeekDay];
	}
	Out.EraName = EraName;
	return Out;
}

#undef LOCTEXT_NAMESPACE
