// Copyright DreamMoon. All Rights Reserved.

#include "DreamStarMap.h"

#include "Misc/Compression.h"

namespace DreamStarMap
{
	namespace
	{
		/** The digits (and spaces, and a point) of a fixed-width field, as a number; false when it holds anything else. */
		bool StarMapField(const FString& Line, int32 First, int32 Length, double& Out, bool& bBlank)
		{
			if (Line.Len() < First + Length)
			{
				bBlank = true;
				return true;
			}
			const FString Field = Line.Mid(First, Length).TrimStartAndEnd();
			bBlank = Field.IsEmpty();
			if (bBlank)
			{
				return true;
			}
			for (int32 Index = 0; Index < Field.Len(); ++Index)
			{
				const TCHAR C = Field[Index];
				if (!FChar::IsDigit(C) && C != TEXT('.') && !(Index == 0 && (C == TEXT('-') || C == TEXT('+'))))
				{
					return false;
				}
			}
			Out = FCString::Atod(*Field);
			return true;
		}

		double StarMapSign(double X)
		{
			return X >= 0.0 ? 1.0 : -1.0;
		}
	}

	bool Unzip(const TArray<uint8>& Compressed, TArray<uint8>& OutData, FString& OutError)
	{
		// (A gzip member: the magic 1f 8b, and the inflated size, modulo 2^32, in its last four bytes.)
		if (Compressed.Num() < 18 || Compressed[0] != 0x1f || Compressed[1] != 0x8b)
		{
			OutError = TEXT("not a gzip file");
			return false;
		}
		const int32 Tail = Compressed.Num() - 4;
		const uint32 Inflated = uint32(Compressed[Tail]) | (uint32(Compressed[Tail + 1]) << 8) | (uint32(Compressed[Tail + 2]) << 16) | (uint32(Compressed[Tail + 3]) << 24);
		if (Inflated == 0 || Inflated > 16u * 1024u * 1024u)
		{
			OutError = FString::Printf(TEXT("an unlikely inflated size (%u bytes)"), Inflated);
			return false;
		}
		OutData.SetNumUninitialized(int32(Inflated));
		if (!FCompression::UncompressMemory(NAME_Gzip, OutData.GetData(), int32(Inflated), Compressed.GetData(), Compressed.Num()))
		{
			OutError = TEXT("it does not inflate");
			return false;
		}
		return true;
	}

	bool Parse(const FString& Text, TArray<FStar>& OutStars, FString& OutError)
	{
		OutStars.Reset();
		TArray<FString> Lines;
		Text.ParseIntoArrayLines(Lines, true);
		if (Lines.Num() < 9000 || Lines.Num() > 9200)
		{
			OutError = FString::Printf(TEXT("%d records, not the catalogue's 9110"), Lines.Num());
			return false;
		}
		int32 NoPlace = 0;
		for (int32 Index = 0; Index < Lines.Num(); ++Index)
		{
			const FString& Line = Lines[Index];
			double H = 0, M = 0, S = 0, Dd = 0, Dm = 0, Ds = 0, V = 0, BV = 0.6;
			bool bH, bM, bS, bDd, bDm, bDs, bV, bBV;
			// (Bytes are counted from 1 in the catalogue's description.)
			if (!StarMapField(Line, 75, 2, H, bH) || !StarMapField(Line, 77, 2, M, bM) || !StarMapField(Line, 79, 4, S, bS)
				|| !StarMapField(Line, 84, 2, Dd, bDd) || !StarMapField(Line, 86, 2, Dm, bDm) || !StarMapField(Line, 88, 2, Ds, bDs)
				|| !StarMapField(Line, 102, 5, V, bV) || !StarMapField(Line, 109, 5, BV, bBV))
			{
				OutError = FString::Printf(TEXT("record %d: a field that is not a number"), Index + 1);
				return false;
			}
			if (bH || bDd || bV)
			{
				++NoPlace;
				continue;
			}
			const TCHAR Sign = Line.Len() > 83 ? Line[83] : TEXT(' ');
			if (H > 23.0 || M > 59.0 || S >= 60.0 || Dd > 90.0 || Dm > 59.0 || Ds >= 60.0 || (Sign != TEXT('+') && Sign != TEXT('-'))
				|| V < -2.0 || V > 8.0 || (!bBV && (BV < -0.6 || BV > 6.0)))
			{
				OutError = FString::Printf(TEXT("record %d: a place or a magnitude out of its range"), Index + 1);
				return false;
			}
			FStar& Star = OutStars.AddDefaulted_GetRef();
			Star.RightAscension = FMath::DegreesToRadians(15.0 * (H + M / 60.0 + S / 3600.0));
			Star.Declination = FMath::DegreesToRadians((Sign == TEXT('-') ? -1.0 : 1.0) * (Dd + Dm / 60.0 + Ds / 3600.0));
			Star.Magnitude = float(V);
			Star.ColorIndex = bBV ? 0.6f : float(BV);
		}
		if (OutStars.Num() < 9000 || NoPlace > 50)
		{
			OutError = FString::Printf(TEXT("%d stars with a place (%d without), not the catalogue's some 9096"), OutStars.Num(), NoPlace);
			return false;
		}
		return true;
	}

	FVector3d Direction(const FStar& Star)
	{
		const double C = FMath::Cos(Star.Declination);
		return FVector3d(C * FMath::Cos(Star.RightAscension), C * FMath::Sin(Star.RightAscension), FMath::Sin(Star.Declination));
	}

	FVector2d Encode(const FVector3d& Direction)
	{
		const FVector3d D = Direction / FMath::Max(FMath::Abs(Direction.X) + FMath::Abs(Direction.Y) + FMath::Abs(Direction.Z), 1.0e-12);
		FVector2d P(D.X, D.Y);
		if (D.Z < 0.0)
		{
			P = FVector2d((1.0 - FMath::Abs(D.Y)) * StarMapSign(D.X), (1.0 - FMath::Abs(D.X)) * StarMapSign(D.Y));
		}
		return P * 0.5 + FVector2d(0.5, 0.5);
	}

	FVector3d Decode(const FVector2d& UV)
	{
		const FVector2d P = UV * 2.0 - FVector2d(1.0, 1.0);
		FVector3d D(P.X, P.Y, 1.0 - FMath::Abs(P.X) - FMath::Abs(P.Y));
		const double T = FMath::Max(-D.Z, 0.0);
		D.X += D.X >= 0.0 ? -T : T;
		D.Y += D.Y >= 0.0 ? -T : T;
		return D.GetSafeNormal();
	}

	int32 Build(const TArray<FStar>& Stars, int32 InSize, TArray<FColor>& OutTexels)
	{
		OutTexels.Init(FColor(0, 0, 0, 0), InSize * InSize);
		// (The brightest first: where two fall in one cell the dimmer moves to a neighbour, or is left out.)
		TArray<int32> Order;
		Order.Reserve(Stars.Num());
		for (int32 Index = 0; Index < Stars.Num(); ++Index)
		{
			Order.Add(Index);
		}
		Order.Sort([&Stars](int32 A, int32 B) { return Stars[A].Magnitude < Stars[B].Magnitude; });
		auto Byte = [](double X) { return uint8(FMath::RoundToInt(FMath::Clamp(X, 0.0, 1.0) * 255.0)); };
		int32 Lost = 0;
		for (const int32 Index : Order)
		{
			const FStar& Star = Stars[Index];
			const FVector2d G = Encode(Direction(Star)) * double(InSize);
			const FIntPoint Own(FMath::Clamp(FMath::FloorToInt(G.X), 0, InSize - 1), FMath::Clamp(FMath::FloorToInt(G.Y), 0, InSize - 1));
			TArray<FIntPoint, TInlineAllocator<9>> Cells;
			for (int32 Dy = -1; Dy <= 1; ++Dy)
			{
				for (int32 Dx = -1; Dx <= 1; ++Dx)
				{
					Cells.Add(Own + FIntPoint(Dx, Dy));
				}
			}
			Cells.Sort([&G](const FIntPoint& A, const FIntPoint& B)
			{
				return FVector2d::DistSquared(G, FVector2d(A.X + 0.5, A.Y + 0.5)) < FVector2d::DistSquared(G, FVector2d(B.X + 0.5, B.Y + 0.5));
			});
			bool bPlaced = false;
			for (const FIntPoint& Cell : Cells)
			{
				if (Cell.X < 0 || Cell.Y < 0 || Cell.X >= InSize || Cell.Y >= InSize)
				{
					continue;
				}
				FColor& Texel = OutTexels[Cell.Y * InSize + Cell.X];
				const FVector2d Offset = G - FVector2d(Cell.X + 0.5, Cell.Y + 0.5);
				if (Texel.B != 0 || FMath::Abs(Offset.X) > 1.5 || FMath::Abs(Offset.Y) > 1.5)
				{
					continue;
				}
				Texel.R = Byte(Offset.X / 3.0 + 0.5);
				Texel.G = Byte(Offset.Y / 3.0 + 0.5);
				Texel.B = uint8(1 + FMath::RoundToInt(FMath::Clamp((8.0 - Star.Magnitude) / 10.0, 0.0, 1.0) * 254.0));
				Texel.A = Byte((Star.ColorIndex + 0.4) / 2.4);
				bPlaced = true;
				break;
			}
			Lost += bPlaced ? 0 : 1;
		}
		return Lost;
	}
}
