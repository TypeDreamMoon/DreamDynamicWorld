// Copyright DreamMoon. All Rights Reserved.

#include "DreamWindTerrainGrid.h"
#include "DreamWindDemoActors.h"
#include "DreamWindPassThroughComponent.h"
#include "DreamWindSettings.h"
#include "DreamWindSliceActor.h"

#include "CollisionQueryParams.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Pawn.h"

namespace DreamWindTerrainGridImpl
{
	constexpr int32 SweepsPerFrame = 256;

	int32 FloorDiv(double Value, double Step)
	{
		return int32(FMath::FloorToDouble(Value / Step));
	}

	int32 PositiveMod(int32 A, int32 B)
	{
		const int32 R = A % B;
		return R < 0 ? R + B : R;
	}
}

// ---- The snapshot ------------------------------------------------------------------------------------------------

float FDreamWindTerrainGridData::Weight(double WorldX, double WorldY) const
{
	if (!IsValid())
	{
		return 0.0f;
	}
	const double Extent = double(Size) * CellSize;
	const double CenterX = (double(WindowMin.X) + Size * 0.5) * CellSize;
	const double CenterY = (double(WindowMin.Y) + Size * 0.5) * CellSize;
	const double Edge = FMath::Max(FMath::Abs(WorldX - CenterX), FMath::Abs(WorldY - CenterY)) / Extent * 2.0;
	// (A cell not swept yet is not covered: whatever stands in for the grid there keeps it.)
	if (HeightAt(WorldX, WorldY) < NotSwept * 0.75f)
	{
		return 0.0f;
	}
	return FMath::Clamp(float((1.0 - Edge) * 8.0), 0.0f, 1.0f);
}

FVector4f FDreamWindTerrainGridData::SampleDerived(double WorldX, double WorldY) const
{
	if (!IsValid())
	{
		return FVector4f(1.0f, 0.0f, 0.0f, Unknown);
	}
	// Cell centres at (i + 0.5) * CellSize; clamped to the window.
	const double FX = FMath::Clamp(WorldX / CellSize - 0.5 - WindowMin.X, 0.0, double(Size - 1));
	const double FY = FMath::Clamp(WorldY / CellSize - 0.5 - WindowMin.Y, 0.0, double(Size - 1));
	const int32 X0 = FMath::Min(int32(FX), Size - 2);
	const int32 Y0 = FMath::Min(int32(FY), Size - 2);
	const float TX = float(FX - X0);
	const float TY = float(FY - Y0);
	const FVector4f& A = Derived[Y0 * Size + X0];
	const FVector4f& B = Derived[Y0 * Size + X0 + 1];
	const FVector4f& C = Derived[(Y0 + 1) * Size + X0];
	const FVector4f& D = Derived[(Y0 + 1) * Size + X0 + 1];
	FVector4f Out = FMath::Lerp(FMath::Lerp(A, B, TX), FMath::Lerp(C, D, TX), TY);
	// (The shadow height as the GPU's sweep keeps it: the highest of the four, so a one-cell wall keeps its height.)
	Out.W = FMath::Max(FMath::Max(A.W, B.W), FMath::Max(C.W, D.W));
	return Out;
}

float FDreamWindTerrainGridData::HeightAt(double WorldX, double WorldY) const
{
	if (!IsValid())
	{
		return Unknown;
	}
	const int32 X = DreamWindTerrainGridImpl::FloorDiv(WorldX, CellSize) - WindowMin.X;
	const int32 Y = DreamWindTerrainGridImpl::FloorDiv(WorldY, CellSize) - WindowMin.Y;
	return X >= 0 && Y >= 0 && X < Size && Y < Size ? Heights[Y * Size + X] : Unknown;
}

DreamWind::Hlsl::FDreamWindSample FDreamWindTerrainGridData::Apply(DreamWind::Hlsl::FDreamWindSample S, const DreamWind::Hlsl::float3& Pos) const
{
	using namespace DreamWind::Hlsl;
	const float W = Weight(Pos.x, Pos.y);
	if (W <= 0.0f)
	{
		return S;
	}
	const FVector4f T = SampleDerived(Pos.x, Pos.y);
	const float4 Texel(T.X, T.Y, T.Z, T.W);
	// The same function the GPU runs, the one grid standing for both levels.
	return DW_ApplyL1(S, Pos, Texel, W, Texel, W, Params);
}

// ---- The builder -------------------------------------------------------------------------------------------------

int32 FDreamWindTerrainGrid::RingIndex(const FIntPoint& Cell) const
{
	using namespace DreamWindTerrainGridImpl;
	return PositiveMod(Cell.Y, Size) * Size + PositiveMod(Cell.X, Size);
}

void FDreamWindTerrainGrid::Follow(const FVector& ViewLocation)
{
	using namespace DreamWindTerrainGridImpl;
	const int32 Step = FMath::Max(Size / 8, 1);
	const FIntPoint Cell(FloorDiv(ViewLocation.X, CellSize), FloorDiv(ViewLocation.Y, CellSize));
	const FIntPoint Min(FMath::RoundToInt(float(Cell.X - Size / 2) / float(Step)) * Step, FMath::RoundToInt(float(Cell.Y - Size / 2) / float(Step)) * Step);
	if (bValid && Min == WindowMin)
	{
		return;
	}
	const FIntRect Old(WindowMin, WindowMin + FIntPoint(Size));
	const bool bKeep = bValid;
	WindowMin = Min;
	bValid = true;
	// The cells that came into the window are unknown until swept, nearest the view first.
	TArray<FIntPoint> New;
	for (int32 Y = Min.Y; Y < Min.Y + Size; ++Y)
	{
		for (int32 X = Min.X; X < Min.X + Size; ++X)
		{
			const FIntPoint C(X, Y);
			if (bKeep && Old.Contains(C))
			{
				continue;
			}
			const int32 I = RingIndex(C);
			Heights[I] = FDreamWindTerrainGridData::NotSwept;
			SlotCell[I] = C;
			New.Add(C);
		}
	}
	New.Sort([Cell](const FIntPoint& A, const FIntPoint& B) { return (A - Cell).SizeSquared() < (B - Cell).SizeSquared(); });
	Pending.Insert(New, 0);
	bChanged = true;
}

void FDreamWindTerrainGrid::RefreshIgnored(UWorld& World)
{
	Ignored.Reset();
	for (TActorIterator<AActor> It(&World); It; ++It)
	{
		if (UDreamWindPassThroughComponent::IsPassThrough(*It) || It->IsA<ADreamWindGrassField>() || It->IsA<ADreamWindSliceActor>() || It->IsA<APawn>())
		{
			Ignored.Add(*It);
		}
	}
}

void FDreamWindTerrainGrid::IssueSweeps(UWorld& World, double ViewZ)
{
	using namespace DreamWindTerrainGridImpl;
	const double Now = FPlatformTime::Seconds();
	if (Now - LastIgnoredScan > 2.0)
	{
		LastIgnoredScan = Now;
		RefreshIgnored(World);
	}
	// Nothing new to sweep: one row of the window again.
	if (Pending.Num() == 0)
	{
		const int32 Y = WindowMin.Y + RefreshRow;
		for (int32 X = WindowMin.X; X < WindowMin.X + Size; ++X)
		{
			Pending.Add(FIntPoint(X, Y));
		}
		RefreshRow = (RefreshRow + 1) % Size;
	}
	FCollisionQueryParams Params(SCENE_QUERY_STAT(DreamWindTerrainGrid), true);
	for (const TWeakObjectPtr<AActor>& Actor : Ignored)
	{
		if (Actor.IsValid())
		{
			Params.AddIgnoredActor(Actor.Get());
		}
	}
	const UDreamWindSettings* S = UDreamWindSettings::Get();
	const FCollisionShape Box = FCollisionShape::MakeBox(FVector(CellSize * 0.5, CellSize * 0.5, 1.0));
	const double Top = ViewZ + S->RuntimeCaptureAltitude;
	const double Bottom = ViewZ - S->RuntimeCaptureAltitude;
	FTraceDelegate Delegate = FTraceDelegate::CreateThreadSafeSP(AsShared(), &FDreamWindTerrainGrid::OnSweep);
	const FIntRect Window(WindowMin, WindowMin + FIntPoint(Size));
	int32 Issued = 0;
	int32 Taken = 0;
	while (Taken < Pending.Num() && Issued < SweepsPerFrame)
	{
		const FIntPoint C = Pending[Taken++];
		if (!Window.Contains(C))
		{
			continue;
		}
		const FVector2D Centre((C.X + 0.5) * CellSize, (C.Y + 0.5) * CellSize);
		World.AsyncSweepByChannel(EAsyncTraceType::Single, FVector(Centre.X, Centre.Y, Top), FVector(Centre.X, Centre.Y, Bottom), FQuat::Identity,
			ECC_WorldStatic, Box, Params, FCollisionResponseParams::DefaultResponseParam, &Delegate);
		++Issued;
	}
	Pending.RemoveAt(0, Taken, EAllowShrinking::No);
	SweepCount += Issued;
}

void FDreamWindTerrainGrid::OnSweep(const FTraceHandle& Handle, FTraceDatum& Datum)
{
	using namespace DreamWindTerrainGridImpl;
	++ResultCount;
	const FIntPoint C(FloorDiv(Datum.Start.X, CellSize), FloorDiv(Datum.Start.Y, CellSize));
	const int32 I = RingIndex(C);
	if (!Heights.IsValidIndex(I) || SlotCell[I] != C)
	{
		return;   // (the window moved on: the slot holds another cell now)
	}
	// The box stops on the top surface: its centre then sits a centimetre above it.
	const float H = Datum.OutHits.Num() > 0 && Datum.OutHits[0].bBlockingHit ? float(Datum.OutHits[0].Location.Z - 1.0) : FDreamWindTerrainGridData::Unknown;
	if (Heights[I] != H)
	{
		Heights[I] = H;
		bChanged = true;
	}
}

void FDreamWindTerrainGrid::MarkDirty(const FBox& Bounds)
{
	using namespace DreamWindTerrainGridImpl;
	for (int32 Y = FloorDiv(Bounds.Min.Y, CellSize); Y <= FloorDiv(Bounds.Max.Y, CellSize); ++Y)
	{
		for (int32 X = FloorDiv(Bounds.Min.X, CellSize); X <= FloorDiv(Bounds.Max.X, CellSize); ++X)
		{
			Pending.Insert(FIntPoint(X, Y), 0);
		}
	}
}

void FDreamWindTerrainGrid::Derive(const FVector2D& WindDir2)
{
	const UDreamWindSettings* S = UDreamWindSettings::Get();
	const int32 N = Size;
	TSharedRef<FDreamWindTerrainGridData, ESPMode::ThreadSafe> Out = MakeShared<FDreamWindTerrainGridData, ESPMode::ThreadSafe>();
	Out->WindowMin = WindowMin;
	Out->Size = N;
	Out->CellSize = CellSize;
	Out->Heights.SetNumUninitialized(N * N);
	for (int32 Y = 0; Y < N; ++Y)
	{
		for (int32 X = 0; X < N; ++X)
		{
			Out->Heights[Y * N + X] = Heights[RingIndex(WindowMin + FIntPoint(X, Y))];
		}
	}
	const TArray<float>& H = Out->Heights;
	auto At = [N](const TArray<float>& A, int32 X, int32 Y) { return A[FMath::Clamp(Y, 0, N - 1) * N + FMath::Clamp(X, 0, N - 1)]; };

	// The wind shadow, as the GPU sweeps it (each step twice as far upwind; outside the window nothing shades).
	TArray<float> Shadow = H;
	TArray<float> Next;
	Next.SetNumUninitialized(N * N);
	const float Slope = FMath::Max(S->OcclusionSlope, 0.01f);
	const int32 Steps = FMath::Clamp(FMath::CeilToInt(FMath::Log2(FMath::Max(S->OcclusionRange, 0.0f) / CellSize + 1.0f)), 1, 10);
	const FVector2D Upwind = -WindDir2;
	for (int32 k = 0; k < Steps; ++k)
	{
		const float Reach = float(1 << k);
		for (int32 Y = 0; Y < N; ++Y)
		{
			for (int32 X = 0; X < N; ++X)
			{
				float Here = Shadow[Y * N + X];
				const double PX = X + Upwind.X * Reach;
				const double PY = Y + Upwind.Y * Reach;
				const int32 X0 = int32(FMath::FloorToDouble(PX));
				const int32 Y0 = int32(FMath::FloorToDouble(PY));
				if (X0 >= 0 && Y0 >= 0 && X0 + 1 < N && Y0 + 1 < N)
				{
					const float Up = FMath::Max(FMath::Max(Shadow[Y0 * N + X0], Shadow[Y0 * N + X0 + 1]), FMath::Max(Shadow[(Y0 + 1) * N + X0], Shadow[(Y0 + 1) * N + X0 + 1]));
					Here = DreamWind::Hlsl::DW_L1ShadowStep(Here, Up, Reach * CellSize, Slope);
				}
				Next[Y * N + X] = Here;
			}
		}
		Swap(Shadow, Next);
	}

	// The terrain's large shapes: the heights blurred (unknown cells as the window's mean), then speed-up and steering.
	double Sum = 0.0;
	int32 Known = 0;
	for (float V : H)
	{
		if (V > FDreamWindTerrainGridData::Unknown * 0.5f)
		{
			Sum += V;
			++Known;
		}
	}
	const float Mean = Known > 0 ? float(Sum / Known) : 0.0f;
	TArray<float> Filled = H;
	for (float& V : Filled)
	{
		V = V > FDreamWindTerrainGridData::Unknown * 0.5f ? V : Mean;
	}
	const FDreamWindTerrainBakeSettings& Shape = S->RuntimeShape;
	const int32 R = FMath::Clamp(FMath::RoundToInt(Shape.BlurRadius / CellSize / 2.0f), 1, N / 2);
	TArray<float> Temp, Blur;
	Temp.SetNumUninitialized(N * N);
	Blur.SetNumUninitialized(N * N);
	for (int32 Pass = 0; Pass < 2; ++Pass)
	{
		const TArray<float>& In = Pass == 0 ? Filled : Temp;
		TArray<float>& Dest = Pass == 0 ? Temp : Blur;
		for (int32 Y = 0; Y < N; ++Y)
		{
			for (int32 X = 0; X < N; ++X)
			{
				float Acc = 0.0f;
				for (int32 j = -R; j <= R; ++j)
				{
					Acc += Pass == 0 ? At(In, X + j, Y) : At(In, X, Y + j);
				}
				Dest[Y * N + X] = Acc / float(2 * R + 1);
			}
		}
	}
	Out->Derived.SetNumUninitialized(N * N);
	for (int32 Y = 0; Y < N; ++Y)
	{
		for (int32 X = 0; X < N; ++X)
		{
			const int32 I = Y * N + X;
			FVector4f D(1.0f, 0.0f, 0.0f, Shadow[I]);
			if (H[I] > FDreamWindTerrainGridData::Unknown * 0.5f)
			{
				D.X = 1.0f + Shape.ExposureGain * FMath::Clamp((H[I] - Blur[I]) / FMath::Max(Shape.ExposureScale, 1.0f), -1.0f, 1.0f);
				FVector2f Steer(-(At(Blur, X + 1, Y) - At(Blur, X - 1, Y)), -(At(Blur, X, Y + 1) - At(Blur, X, Y - 1)));
				Steer *= Shape.SteerGain / (2.0f * CellSize);
				if (Steer.Size() > Shape.MaxSteer)
				{
					Steer *= Shape.MaxSteer / Steer.Size();
				}
				D.Y = Steer.X;
				D.Z = Steer.Y;
			}
			Out->Derived[I] = D;
		}
	}
	Out->Params.ShadowStrength = S->OcclusionStrength;
	Out->Params.FadeBand = S->RuntimeShadowFadeBand;
	Out->Params.SteerScale = S->TerrainSteerScale;
	Out->Params.SpeedScale = S->TerrainSpeedScale;
	Snapshot = Out;
}

void FDreamWindTerrainGrid::Update(UWorld& World, const FVector& ViewLocation, const FVector& WindDir)
{
	if (Heights.Num() != Size * Size)
	{
		Heights.Init(FDreamWindTerrainGridData::NotSwept, Size * Size);
		SlotCell.Init(FIntPoint(TNumericLimits<int32>::Max()), Size * Size);
		bValid = false;
	}
	Follow(ViewLocation);
	IssueSweeps(World, ViewLocation.Z);
	const FVector2D Dir2 = FVector2D(WindDir.X, WindDir.Y).GetSafeNormal();
	const bool bTurned = (Dir2 | DerivedWindDir) < FMath::Cos(FMath::DegreesToRadians(5.0f));
	if (bChanged || bTurned || !Snapshot.IsValid())
	{
		Derive(Dir2.IsNearlyZero() ? FVector2D(1.0, 0.0) : Dir2);
		DerivedWindDir = Dir2;
		bChanged = false;
	}
}
