// Copyright DreamMoon. All Rights Reserved.

#include "DreamWindTerrainMap.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(DreamWindTerrainMap)

FVector4f UDreamWindTerrainMap::Sample(const FVector2D& WorldXY) const
{
	if (!IsValidMap())
	{
		return FVector4f(1, 0, 0, 0);
	}
	const FVector2D Size = Bounds.GetSize();
	const double U = (WorldXY.X - Bounds.Min.X) / FMath::Max(Size.X, 1.0);
	const double V = (WorldXY.Y - Bounds.Min.Y) / FMath::Max(Size.Y, 1.0);
	// texel centres at (i + 0.5) / N, matching a GPU bilinear fetch of the same data
	const double FX = FMath::Clamp(U * Width - 0.5, 0.0, double(Width - 1));
	const double FY = FMath::Clamp(V * Height - 0.5, 0.0, double(Height - 1));
	const int32 X0 = FMath::FloorToInt32(FX);
	const int32 Y0 = FMath::FloorToInt32(FY);
	const float TX = float(FX - X0);
	const float TY = float(FY - Y0);
	const FVector4f A = Texel(X0, Y0);
	const FVector4f B = Texel(X0 + 1, Y0);
	const FVector4f C = Texel(X0, Y0 + 1);
	const FVector4f D = Texel(X0 + 1, Y0 + 1);
	return FMath::Lerp(FMath::Lerp(A, B, TX), FMath::Lerp(C, D, TX), TY);
}
