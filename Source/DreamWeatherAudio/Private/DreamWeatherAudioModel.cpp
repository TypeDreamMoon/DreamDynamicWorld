// Copyright DreamMoon. All Rights Reserved.

#include "DreamWeatherAudioModel.h"
#include "DreamWeatherTypes.h"

#include "DSP/Dsp.h"

namespace DreamWeatherAudio
{
	static float Smooth(float Edge0, float Edge1, float X)
	{
		return FMath::SmoothStep(Edge0, Edge1, X);
	}

	FDreamOutdoorOcclusion Occlude(const FDreamViewOcclusion& Occlusion, const FVector& ListenerRight, float InteriorMinVolume, float MinCutoffHz)
	{
		FDreamOutdoorOcclusion Out;
		Out.CutoffHz = MuffleToCutoff(0.0f, MinCutoffHz);
		if (!Occlusion.bValid)
		{
			return Out;
		}
		const float Closed[4] = { Occlusion.PlusX, Occlusion.MinusX, Occlusion.PlusY, Occlusion.MinusY };
		static const FVector2D Sides[4] = { FVector2D(1.0, 0.0), FVector2D(-1.0, 0.0), FVector2D(0.0, 1.0), FVector2D(0.0, -1.0) };
		FVector2D Right(ListenerRight.X, ListenerRight.Y);
		if (!Right.Normalize())
		{
			Right = FVector2D(0.0, 1.0);
		}

		float SumOpen = 0.0f;
		float SumPan = 0.0f;
		for (int32 i = 0; i < 4; ++i)
		{
			const float Open = 1.0f - FMath::Clamp(Closed[i], 0.0f, 1.0f);
			SumOpen += Open;
			SumPan += Open * float(FVector2D::DotProduct(Sides[i], Right));
		}
		const float Openness = SumOpen * 0.25f;
		const float Enclosed = FMath::Clamp(Occlusion.Interior, 0.0f, 1.0f);
		const float MinVolume = FMath::Clamp(InteriorMinVolume, 0.0f, 1.0f);

		Out.Volume = FMath::Lerp(1.0f, MinVolume + (1.0f - MinVolume) * Openness, Enclosed);
		Out.Muffle = Enclosed * (1.0f - Openness);
		Out.Balance = SumOpen > 1.0e-3f ? FMath::Clamp(SumPan / SumOpen, -1.0f, 1.0f) * Enclosed : 0.0f;
		Out.CutoffHz = MuffleToCutoff(Out.Muffle, MinCutoffHz);
		return Out;
	}

	float MuffleToCutoff(float Muffle, float MinCutoffHz)
	{
		const float Low = FMath::Clamp(MinCutoffHz, 20.0f, 20000.0f);
		return Audio::GetLogFrequencyClamped(1.0f - FMath::Clamp(Muffle, 0.0f, 1.0f), FVector2D(0.0, 1.0), FVector2D(Low, 20000.0));
	}

	void Place(float Loudness, float Balance, float& OutLeft, float& OutRight)
	{
		const float B = FMath::Clamp(Balance, -1.0f, 1.0f);
		OutLeft = Loudness * (1.0f - 0.7f * FMath::Max(B, 0.0f));
		OutRight = Loudness * (1.0f - 0.7f * FMath::Max(-B, 0.0f));
	}

	float WindAmount(float WindSpeed, float WindLoudSpeed)
	{
		return FMath::Clamp(WindSpeed / FMath::Max(WindLoudSpeed, 1.0f), 0.0f, 1.0f);
	}

	void Mix(const FDreamWeatherAudioInputs& In, const FDreamWeatherAudioTuning& Tuning, FDreamWeatherVoiceMix OutVoices[(int32)EDreamWeatherVoice::Num])
	{
		const FDreamOutdoorOcclusion Outdoor = Occlude(In.Occlusion, In.ListenerRight, Tuning.InteriorMinVolume, Tuning.MinCutoffHz);

		float Intensity[(int32)EDreamWeatherVoice::Num] = {};
		const float Wind = WindAmount(In.WindSpeed, Tuning.WindLoudSpeed);
		const float Gust = FMath::Clamp((In.WindGust - 1.0f) / 0.8f, 0.0f, 1.0f);
		const float Rain = FMath::Clamp(In.Rain, 0.0f, 1.0f);
		const float Roof = FMath::Clamp(In.Surfaces[2], 0.0f, 1.0f);
		const float Leaves = FMath::Clamp(In.Surfaces[3], 0.0f, 1.0f);
		// The probe cannot tell a roof from a canopy: cover overhead counts as a roof only as far as the rain nearby
		// does not land on leaves.
		const float Overhead = In.Occlusion.bValid ? FMath::Clamp(In.Occlusion.Overhead, 0.0f, 1.0f) * (1.0f - Leaves) : 0.0f;

		Intensity[(int32)EDreamWeatherVoice::Wind] = FMath::Pow(Wind, 0.8f);
		Intensity[(int32)EDreamWeatherVoice::WindGust] = Smooth(0.35f, 1.0f, Wind) * (0.3f + 0.7f * Gust);
		Intensity[(int32)EDreamWeatherVoice::RainLight] = Smooth(0.0f, 0.12f, Rain) * (1.0f - 0.7f * Smooth(0.35f, 0.85f, Rain));
		Intensity[(int32)EDreamWeatherVoice::RainHeavy] = Smooth(0.25f, 0.95f, Rain);
		Intensity[(int32)EDreamWeatherVoice::RainOnRoof] = Rain * FMath::Max(Overhead, Roof);
		Intensity[(int32)EDreamWeatherVoice::RainOnLeaves] = Rain * Smooth(0.0f, 0.5f, Leaves);
		Intensity[(int32)EDreamWeatherVoice::Snow] = Smooth(0.0f, 0.5f, FMath::Clamp(In.Snow, 0.0f, 1.0f));
		Intensity[(int32)EDreamWeatherVoice::Dust] = FMath::Clamp(In.Dust, 0.0f, 1.0f) * (0.35f + 0.65f * Wind);

		for (int32 v = 0; v < (int32)EDreamWeatherVoice::Num; ++v)
		{
			FDreamWeatherVoiceMix& Voice = OutVoices[v];
			Voice.Intensity = Intensity[v] * Tuning.SlotVolume[v];
			float Volume = Outdoor.Volume;
			float Muffle = Outdoor.Muffle;
			float Balance = Outdoor.Balance;
			if (v == (int32)EDreamWeatherVoice::RainOnRoof && Intensity[v] > 0.0f)
			{
				// The roof over the listener is not outside: heard in full, a little dull through the ceiling. The
				// roofs around are outdoor sound like the rest.
				const float OwnRoof = Overhead / FMath::Max(FMath::Max(Overhead, Roof), 1.0e-4f);
				Volume = FMath::Lerp(Outdoor.Volume, 1.0f, OwnRoof);
				Muffle = FMath::Lerp(Outdoor.Muffle, 0.25f * FMath::Clamp(In.Occlusion.Interior, 0.0f, 1.0f), OwnRoof);
				Balance = FMath::Lerp(Outdoor.Balance, 0.0f, OwnRoof);
			}
			Place(Voice.Intensity * Volume, Balance, Voice.Left, Voice.Right);
			Voice.Muffle = Muffle;
		}
	}

	bool Thunder(const FDreamLightningStrike& Strike, const FVector& ListenerLocation, const FVector& ListenerRight,
		const FDreamOutdoorOcclusion& Outdoor, const FDreamWeatherAudioTuning& Tuning, FDreamThunder& Out)
	{
		FVector2D ToStrike(Strike.Location.X - ListenerLocation.X, Strike.Location.Y - ListenerLocation.Y);
		const float Distance = float(ToStrike.Size());
		const float MaxDistance = FMath::Max(Tuning.ThunderMaxDistance, 1.0f);
		if (Distance > MaxDistance || Strike.Intensity <= 0.0f)
		{
			return false;
		}
		Out = FDreamThunder();
		Out.Delay = FMath::Max(Strike.ThunderDelay, 0.0f);
		Out.bNear = Distance < Tuning.ThunderNearDistance;
		Out.Distance = Distance / MaxDistance;
		Out.Variant = GetTypeHash(Strike.Location) ^ GetTypeHash(Strike.WorldSeconds);

		// Loud close by, a far rumble at the edge of hearing; the low end carries through walls, so walls take
		// less of it than of rain, but more of its brightness.
		const float SlotVolume = Out.bNear ? Tuning.ThunderNearVolume : Tuning.ThunderFarVolume;
		const float Loudness = FMath::Clamp(Strike.Intensity, 0.0f, 1.0f) * (1.0f - 0.85f * Smooth(0.0f, 1.0f, Out.Distance)) * SlotVolume;
		Out.Mix.Intensity = Loudness;
		const float Volume = Loudness * FMath::Lerp(1.0f, Outdoor.Volume, 0.6f);
		Out.Mix.Muffle = FMath::Max(0.8f * Smooth(0.1f, 0.8f, Out.Distance), Outdoor.Muffle * Tuning.ThunderInteriorMuffle);

		FVector2D Right(ListenerRight.X, ListenerRight.Y);
		float Pan = 0.0f;
		if (Right.Normalize() && ToStrike.Normalize())
		{
			Pan = float(FVector2D::DotProduct(ToStrike, Right));
		}
		const float Balance = FMath::Clamp(0.6f * Pan * (1.0f - Outdoor.Muffle) + Outdoor.Balance, -1.0f, 1.0f);
		Place(Volume, Balance, Out.Mix.Left, Out.Mix.Right);
		return true;
	}
}
