// Copyright DreamMoon. All Rights Reserved.

#include "DreamWorldCalendar.h"
#include "DreamWorldCelestial.h"
#include "DreamWorldSubsystem.h"
#include "DreamWorldTestListener.h"

#include "Engine/World.h"
#include "Misc/AutomationTest.h"
#include "UObject/Package.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDreamWorldCalendarTest, "DreamWorld.Calendar", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FDreamWorldCalendarTest::RunTest(const FString& Parameters)
{
	using E = EDreamDaylightSaving;
	// Daylight saving by its rules (2026: Europe March 29 to October 25, North America March 8 to November 1, Sydney
	// till April 5 and from October 4, Auckland till April 5 and from September 27).
	TestFalse(TEXT("Europe: not on March 28"), UDreamWorldCalendar::IsDaylightSaving(E::Europe, FDateTime(2026, 3, 28, 12, 0, 0)));
	TestTrue(TEXT("Europe: on from March 29"), UDreamWorldCalendar::IsDaylightSaving(E::Europe, FDateTime(2026, 3, 29, 12, 0, 0)));
	TestTrue(TEXT("Europe: on in July"), UDreamWorldCalendar::IsDaylightSaving(E::Europe, FDateTime(2026, 7, 1)));
	TestFalse(TEXT("Europe: off after October 25"), UDreamWorldCalendar::IsDaylightSaving(E::Europe, FDateTime(2026, 10, 26)));
	TestFalse(TEXT("North America: not on March 7"), UDreamWorldCalendar::IsDaylightSaving(E::NorthAmerica, FDateTime(2026, 3, 7, 12, 0, 0)));
	TestTrue(TEXT("North America: on from March 8"), UDreamWorldCalendar::IsDaylightSaving(E::NorthAmerica, FDateTime(2026, 3, 8, 12, 0, 0)));
	TestFalse(TEXT("North America: off from November 1"), UDreamWorldCalendar::IsDaylightSaving(E::NorthAmerica, FDateTime(2026, 11, 1, 12, 0, 0)));
	TestTrue(TEXT("Sydney: on in January"), UDreamWorldCalendar::IsDaylightSaving(E::SouthernAustralia, FDateTime(2026, 1, 15)));
	TestFalse(TEXT("Sydney: off in July"), UDreamWorldCalendar::IsDaylightSaving(E::SouthernAustralia, FDateTime(2026, 7, 1)));
	TestTrue(TEXT("Sydney: on from October 4"), UDreamWorldCalendar::IsDaylightSaving(E::SouthernAustralia, FDateTime(2026, 10, 4, 12, 0, 0)));
	TestTrue(TEXT("Auckland: on from September 27"), UDreamWorldCalendar::IsDaylightSaving(E::NewZealand, FDateTime(2026, 9, 27, 12, 0, 0)));
	TestFalse(TEXT("none: never"), UDreamWorldCalendar::IsDaylightSaving(E::None, FDateTime(2026, 7, 1)));

	// The cities: London picked, its place and rule; the sun at its highest some 13:00 on the summer clock.
	FDreamCityPreset London;
	TestTrue(TEXT("London is a city"), UDreamWorldCalendar::FindCity(TEXT("London"), London) && FMath::IsNearlyEqual(London.Latitude, 51.51, 0.01));
	TestTrue(TEXT("the pick list leads with none"), UDreamWorldCalendar::GetCityNames()[0].IsNone() && UDreamWorldCalendar::GetCityNames().Num() > 20);
	FDreamCelestialSettings Settings;
	Settings.Mode = EDreamCelestialMode::Realistic;
	Settings.City = TEXT("London");
	auto SunAt = [&Settings](int32 Hour, int32 Minute)
	{
		return DreamWorld::Celestial::Evaluate(Settings, DreamWorld::Celestial::FromDateTime(FDateTime(2026, 6, 21, Hour, Minute, 0))).SunElevation;
	};
	TestTrue(FString::Printf(TEXT("London midsummer: higher at 13:00 than at 12:00 on the summer clock (%.2f, %.2f)"), SunAt(13, 0), SunAt(12, 0)), SunAt(13, 0) > SunAt(12, 0));
	TestTrue(FString::Printf(TEXT("as high as London's sun gets (%.1f)"), SunAt(13, 3)), FMath::IsNearlyEqual(SunAt(13, 3), 90.0f - 51.51f + 23.44f, 1.0f));

	// A custom calendar: ten months of 36 days, a six-day week, an era.
	UDreamCalendarAsset* Calendar = NewObject<UDreamCalendarAsset>(GetTransientPackage());
	for (int32 M = 0; M < 10; ++M)
	{
		FDreamCalendarMonth& Month = Calendar->Months.AddDefaulted_GetRef();
		Month.Name = FText::AsCultureInvariant(FString::Printf(TEXT("M%d"), M + 1));
		Month.Days = 36;
	}
	for (int32 D = 0; D < 6; ++D)
	{
		Calendar->WeekDays.Add(FText::AsCultureInvariant(FString::Printf(TEXT("D%d"), D + 1)));
	}
	Calendar->FirstYear = 100;
	TestEqual(TEXT("a year of 360 days"), Calendar->GetDaysInYear(), 360);
	const FDreamCalendarDate Date = Calendar->ToDate((360.0 * 2.0 + 36.0 * 3.0 + 4.0) * 86400.0 + 3600.0 * 7.5);
	TestEqual(TEXT("its year"), Date.Year, 102);
	TestEqual(TEXT("its month"), Date.Month, 4);
	TestEqual(TEXT("its day"), Date.Day, 5);
	TestEqual(TEXT("the hour"), Date.Hour, 7);
	TestEqual(TEXT("the minute"), Date.Minute, 30);
	TestEqual(TEXT("the month's name"), Date.MonthName.ToString(), FString(TEXT("M4")));

	// The clock's events: each minute, and the times registered.
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
	UDreamWorldSubsystem* Clock = World ? World->GetSubsystem<UDreamWorldSubsystem>() : nullptr;
	if (TestNotNull(TEXT("clock"), Clock))
	{
		FDreamWorldClockConfig Config;
		Config.StartTime = FDateTime(2026, 6, 21, 6, 58, 0);
		Config.DayLengthMinutes = 1.0f;   // (a game minute a twenty-fourth of a real second)
		Config.bAnimate = true;
		Config.Celestial.City = TEXT("Sydney");
		Config.Calendar = Calendar;
		Clock->ApplyClockConfig(Config, true);
		TestTrue(TEXT("the city written into the clock's settings"), FMath::IsNearlyEqual(Clock->GetCelestialSettings().Latitude, -33.87, 0.01));
		TestEqual(TEXT("the clock tells the custom calendar's date"), Clock->GetCalendarDate().EraName.ToString(), Calendar->EraName.ToString());
		Clock->RegisterTimeEvent(TEXT("Breakfast"), 7.0f);
		Clock->RegisterTimeEvent(TEXT("Dinner"), 19.0f);
		UDreamWorldTestListener* Listener = NewObject<UDreamWorldTestListener>();
		Listener->Bind(Clock);
		Clock->Tick(0.016f);   // (primes the events)
		Listener->Minutes = 0;
		for (int32 i = 0; i < 10; ++i)
		{
			Clock->Tick(0.05f);   // (some 1.2 game minutes a frame)
		}
		TestTrue(FString::Printf(TEXT("a minute event a frame (%d)"), Listener->Minutes), Listener->Minutes >= 8 && Listener->Minutes <= 10);
		TestTrue(TEXT("breakfast at seven, once"), Listener->Times.Num() == 1 && Listener->Times[0] == FName(TEXT("Breakfast")));
		Clock->UnregisterTimeEvent(TEXT("Breakfast"));
		World->DestroyWorld(false);
	}
	return true;
}

#endif
