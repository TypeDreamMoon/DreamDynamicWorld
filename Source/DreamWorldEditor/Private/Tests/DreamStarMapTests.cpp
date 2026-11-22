// Copyright DreamMoon. All Rights Reserved.
//
// The star map Setup builds from the Yale Bright Star Catalogue (DreamStarMap.h): reading the catalogue's records (and
// refusing what does not look like it), the octahedral map's coordinates, how stars share its cells; and, when the
// catalogue's file is at <project>/Saved/StarData/bsc5.dat.gz, the real thing.

#include "CoreMinimal.h"
#include "DreamStarMap.h"

#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace DreamStarMapTests
{
	static constexpr EAutomationTestFlags Flags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter;

	/** A record laid out as the catalogue's: right ascension bytes 76-83, declination 84-90, V 103-107, B-V 110-114. */
	FString Record(int32 Number, int32 H, int32 M, double S, TCHAR Sign, int32 Dd, int32 Dm, int32 Ds, double V, double BV)
	{
		FString Line = FString::ChrN(120, TEXT(' '));
		auto Put = [&Line](int32 First, const FString& Field)
		{
			for (int32 Index = 0; Index < Field.Len(); ++Index)
			{
				Line[First + Index] = Field[Index];
			}
		};
		Put(0, FString::Printf(TEXT("%4d"), Number));
		Put(75, FString::Printf(TEXT("%02d%02d%04.1f"), H, M, S));
		Put(83, FString::Printf(TEXT("%c%02d%02d%02d"), Sign, Dd, Dm, Ds));
		Put(102, FString::Printf(TEXT("%5.2f"), V));
		Put(109, FString::Printf(TEXT("%+5.2f"), BV));
		return Line;
	}

	/** Some 9110 records spread over the sky. */
	FString Catalogue(int32 Count)
	{
		TArray<FString> Lines;
		for (int32 Index = 0; Index < Count; ++Index)
		{
			const int32 Dec = (Index * 37) % 89;
			Lines.Add(Record(Index + 1, Index % 24, (Index * 7) % 60, double((Index * 13) % 600) / 10.0, Index % 3 == 0 ? TEXT('-') : TEXT('+'), Dec,
				(Index * 11) % 60, (Index * 17) % 60, 1.0 + double(Index % 55) / 10.0, -0.3 + double(Index % 20) / 10.0));
		}
		return FString::Join(Lines, TEXT("\n")) + TEXT("\n");
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDreamStarMapParseTest, "DreamWorld.StarMap.Parse", DreamStarMapTests::Flags)
bool FDreamStarMapParseTest::RunTest(const FString& Parameters)
{
	using namespace DreamStarMapTests;
	TArray<DreamStarMap::FStar> Stars;
	FString Error;
	TestTrue(TEXT("a catalogue of 9110 records reads"), DreamStarMap::Parse(Catalogue(9110), Stars, Error));
	TestEqual(TEXT("each a star"), Stars.Num(), 9110);
	// (Sirius, HR 2491: 6h 45m 08.9s, -16 42' 58", V -1.46.)
	const FString Sirius = Record(2491, 6, 45, 8.9, TEXT('-'), 16, 42, 58, -1.46, 0.0);
	FString Text = Catalogue(9109) + Sirius + TEXT("\n");
	if (TestTrue(TEXT("with Sirius in it"), DreamStarMap::Parse(Text, Stars, Error)))
	{
		const DreamStarMap::FStar& S = Stars.Last();
		TestNearlyEqual(TEXT("its right ascension"), FMath::RadiansToDegrees(S.RightAscension), 15.0 * (6.0 + 45.0 / 60.0 + 8.9 / 3600.0), 1.0e-6);
		TestNearlyEqual(TEXT("its declination, south"), FMath::RadiansToDegrees(S.Declination), -(16.0 + 42.0 / 60.0 + 58.0 / 3600.0), 1.0e-6);
		TestNearlyEqual(TEXT("its magnitude"), S.Magnitude, -1.46f, 1.0e-4f);
	}
	// (A record with no place, as the catalogue's novae, is left out.)
	Text = Catalogue(9109) + FString::Printf(TEXT("%4d"), 92) + FString::ChrN(110, TEXT(' ')) + TEXT("\n");
	TestTrue(TEXT("a record with no place is left out"), DreamStarMap::Parse(Text, Stars, Error) && Stars.Num() == 9109);

	TestFalse(TEXT("too few records: not the catalogue"), DreamStarMap::Parse(Catalogue(500), Stars, Error));
	Text = Catalogue(9109) + Record(9110, 25, 0, 0.0, TEXT('+'), 10, 0, 0, 3.0, 0.5) + TEXT("\n");
	TestFalse(TEXT("an hour of 25: not the catalogue"), DreamStarMap::Parse(Text, Stars, Error));
	Text = Catalogue(9109) + Record(9110, 5, 0, 0.0, TEXT('+'), 10, 0, 0, 12.0, 0.5) + TEXT("\n");
	TestFalse(TEXT("magnitude 12: not the catalogue"), DreamStarMap::Parse(Text, Stars, Error));
	Text = Catalogue(9109) + FString(TEXT("<html><body>Moved</body></html>")) + FString::ChrN(100, TEXT('x')) + TEXT("\n");
	TestFalse(TEXT("a page in place of a record: not the catalogue"), DreamStarMap::Parse(Text, Stars, Error));

	TArray<uint8> Junk = { 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20 };
	TArray<uint8> Raw;
	TestFalse(TEXT("not a gzip file"), DreamStarMap::Unzip(Junk, Raw, Error));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDreamStarMapCellsTest, "DreamWorld.StarMap.Cells", DreamStarMapTests::Flags)
bool FDreamStarMapCellsTest::RunTest(const FString& Parameters)
{
	// (The octahedral map's coordinates go there and back, both hemispheres.)
	double Worst = 0.0;
	for (int32 Index = 0; Index < 500; ++Index)
	{
		const double Ra = 2.0 * UE_DOUBLE_PI * double(Index) / 500.0 * 7.0;
		const double Dec = FMath::Asin(2.0 * double((Index * 37) % 500) / 500.0 - 1.0);
		const FVector3d D(FMath::Cos(Dec) * FMath::Cos(Ra), FMath::Cos(Dec) * FMath::Sin(Ra), FMath::Sin(Dec));
		Worst = FMath::Max(Worst, (DreamStarMap::Decode(DreamStarMap::Encode(D)) - D).Length());
	}
	TestTrue(FString::Printf(TEXT("a direction comes back as it went (%.2e)"), Worst), Worst < 1.0e-9);

	// (Two stars in one cell: the brighter keeps it, the dimmer goes next door; where each is, to a fraction of a cell.)
	TArray<DreamStarMap::FStar> Stars;
	DreamStarMap::FStar A;
	A.RightAscension = 1.0;
	A.Declination = 0.3;
	A.Magnitude = 4.0f;
	DreamStarMap::FStar B = A;
	B.RightAscension += 1.0e-4;
	B.Magnitude = 1.0f;
	Stars = { A, B };
	TArray<FColor> Texels;
	const int32 Size = 64;
	TestEqual(TEXT("both find a cell"), DreamStarMap::Build(Stars, Size, Texels), 0);
	int32 Filled = 0;
	uint8 Brightest = 0;
	for (const FColor& Texel : Texels)
	{
		Filled += Texel.B != 0 ? 1 : 0;
		Brightest = FMath::Max(Brightest, Texel.B);
	}
	TestEqual(TEXT("two cells hold them"), Filled, 2);
	const FVector2d G = DreamStarMap::Encode(DreamStarMap::Direction(B)) * double(Size);
	const FColor& Own = Texels[FMath::FloorToInt(G.Y) * Size + FMath::FloorToInt(G.X)];
	TestEqual(TEXT("the brighter keeps its own cell"), Own.B, Brightest);
	const FVector2d Placed(FMath::FloorToInt(G.X) + 0.5 + (Own.R / 255.0 - 0.5) * 3.0, FMath::FloorToInt(G.Y) + 0.5 + (Own.G / 255.0 - 0.5) * 3.0);
	TestTrue(TEXT("where it is, to a fiftieth of a cell"), FVector2d::Distance(Placed, G) < 0.02);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDreamStarMapCatalogueTest, "DreamWorld.StarMap.Catalogue", DreamStarMapTests::Flags)
bool FDreamStarMapCatalogueTest::RunTest(const FString& Parameters)
{
	const FString Path = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("StarData"), TEXT("bsc5.dat.gz"));
	TArray<uint8> Compressed;
	if (!FFileHelper::LoadFileToArray(Compressed, *Path, FILEREAD_Silent))
	{
		AddInfo(FString::Printf(TEXT("no catalogue at %s: the real one is not checked"), *Path));
		return true;
	}
	TArray<uint8> Raw;
	FString Text, Error;
	TArray<DreamStarMap::FStar> Stars;
	if (!TestTrue(TEXT("the catalogue inflates"), DreamStarMap::Unzip(Compressed, Raw, Error)))
	{
		return false;
	}
	FFileHelper::BufferToString(Text, Raw.GetData(), Raw.Num());
	if (!TestTrue(FString::Printf(TEXT("and reads (%s)"), *Error), DreamStarMap::Parse(Text, Stars, Error)))
	{
		return false;
	}
	TestTrue(FString::Printf(TEXT("some 9096 stars (%d)"), Stars.Num()), Stars.Num() >= 9090 && Stars.Num() <= 9110);
	// (Polaris, HR 424, within a degree of the pole; Sirius the brightest.)
	const DreamStarMap::FStar* Brightest = nullptr;
	double Nearest = 90.0;
	for (const DreamStarMap::FStar& Star : Stars)
	{
		Brightest = !Brightest || Star.Magnitude < Brightest->Magnitude ? &Star : Brightest;
		Nearest = FMath::Min(Nearest, 90.0 - FMath::RadiansToDegrees(Star.Declination));
	}
	TestTrue(FString::Printf(TEXT("a star within a degree of the pole (%.2f)"), Nearest), Nearest < 1.0);
	TestTrue(TEXT("the brightest is Sirius"), Brightest && FMath::IsNearlyEqual(Brightest->Magnitude, -1.46f, 0.01f));
	TArray<FColor> Texels;
	const int32 Lost = DreamStarMap::Build(Stars, DreamStarMap::Size, Texels);
	TestTrue(FString::Printf(TEXT("hardly any left without a cell (%d)"), Lost), Lost < 20);
	return true;
}

#endif
