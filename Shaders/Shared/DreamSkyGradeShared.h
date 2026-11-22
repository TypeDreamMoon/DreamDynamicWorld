// Copyright DreamMoon. All Rights Reserved.
//
// DreamSkyGradeShared.h - the sky style's grade over the physical sky: one function for every colour that comes from the air
// (the dome, the fog, the clouds' sky, the ambient light), so a style tints all of them alike. Compiled BOTH as HLSL and as
// C++ (DreamSkyGradeShim.h -> DreamWind::Hlsl::DSG_*). The rules of DreamWindShared.h apply.
//
// The grade, four float4s (FDreamSkyGradeValues::Pack), for this moment's sun and weather:
//   G0 = (exposure scale, saturation, hue shift in radians, contrast)
//   G1 = (tint rgb, how the zenith tint gives way to the horizon's: the exponent on the view's elevation sine)
//   G2 = (zenith tint rgb, -)
//   G3 = (horizon tint rgb, -)
// In order: exposure and the tints, the hue turned about the grey axis, the saturation about the luminance, the contrast
// about a middle grey. The identity grade is (1, 1, 0, 1), (1, 1, 1, *), (1, 1, 1), (1, 1, 1).

#ifndef DREAMSKY_GRADE_SHARED_H
#define DREAMSKY_GRADE_SHARED_H

#ifndef DSG_FN
#define DSG_FN
#endif

DSG_FN float DSG_Luma(float3 C)
{
	return 0.2126f * C.x + 0.7152f * C.y + 0.0722f * C.z;
}

DSG_FN float3 DSG_Grade(float3 Color, float ViewZ, float4 G0, float4 G1, float4 G2, float4 G3)
{
	float Up = pow(saturate(ViewZ), max(G1.w, 0.01f));
	float3 Ramp = lerp(float3(G3.x, G3.y, G3.z), float3(G2.x, G2.y, G2.z), Up);
	float3 X = Color * G0.x * float3(G1.x, G1.y, G1.z) * Ramp;

	// The hue, turned about the grey axis (Rodrigues).
	float C = cos(G0.z);
	float S = sin(G0.z);
	float3 K = float3(0.57735027f, 0.57735027f, 0.57735027f);
	X = X * C + cross(K, X) * S + K * (dot(K, X) * (1.0f - C));

	// The saturation, about the luminance.
	float L = DSG_Luma(X);
	X = max(float3(L, L, L) + (X - float3(L, L, L)) * G0.y, float3(0.0f, 0.0f, 0.0f));

	// The contrast, about a middle grey (in the log: a power).
	float Pivot = 0.18f;
	if (abs(G0.w - 1.0f) > 1.0e-4f)
	{
		X = float3(Pivot * pow(max(X.x, 1.0e-7f) / Pivot, G0.w), Pivot * pow(max(X.y, 1.0e-7f) / Pivot, G0.w), Pivot * pow(max(X.z, 1.0e-7f) / Pivot, G0.w));
	}
	return X;
}

#endif // DREAMSKY_GRADE_SHARED_H
