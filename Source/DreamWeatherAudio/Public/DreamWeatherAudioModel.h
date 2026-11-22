// Copyright DreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "DreamWeatherAudioTypes.h"

struct FDreamLightningStrike;

/**
 * The weather's sound as pure functions of what is around the listener: the subsystem gathers the inputs every
 * frame and hands the results to the voices; the tests call the same functions.
 */
namespace DreamWeatherAudio
{
	/**
	 * Walls and roof from the view probe. Each side's openness (1 - closed) is a way in for outdoor sound: the
	 * sound comes through the open sides (Balance, by the listener's right) and is quieter and duller the more of
	 * the room is closed (Volume, Muffle), by how much the probe calls it an interior.
	 */
	DREAMWEATHERAUDIO_API FDreamOutdoorOcclusion Occlude(const FDreamViewOcclusion& Occlusion, const FVector& ListenerRight, float InteriorMinVolume, float MinCutoffHz);

	/** The low-pass cutoff for a muffle (0 = 20 kHz .. 1 = MinCutoffHz), log-spaced exactly as the voice graph maps it. */
	DREAMWEATHERAUDIO_API float MuffleToCutoff(float Muffle, float MinCutoffHz);

	/** Left and right gains for a loudness at a balance (-1 left .. 1 right): the far side fades, the near side keeps full loudness. */
	DREAMWEATHERAUDIO_API void Place(float Loudness, float Balance, float& OutLeft, float& OutRight);

	/** The wind speed as 0..1 of a loud wind. */
	DREAMWEATHERAUDIO_API float WindAmount(float WindSpeed, float WindLoudSpeed);

	/**
	 * Gain of the wind and the rain Seconds after a near thunder clap: down by DuckDb at once, back to 1 over
	 * DuckSeconds (held for the first third, then easing back). 1 before the clap and after.
	 */
	DREAMWEATHERAUDIO_API float ThunderDuck(float Seconds, float DuckDb, float DuckSeconds);

	/** Every looping voice for these inputs. */
	DREAMWEATHERAUDIO_API void Mix(const FDreamWeatherAudioInputs& In, const FDreamWeatherAudioTuning& Tuning, FDreamWeatherVoiceMix OutVoices[(int32)EDreamWeatherVoice::Num]);

	/**
	 * The thunder of a strike as heard at the listener: near or far, how loud, how dull (distance and walls), from
	 * which side, and after how long. False when the strike is beyond hearing.
	 */
	DREAMWEATHERAUDIO_API bool Thunder(const FDreamLightningStrike& Strike, const FVector& ListenerLocation, const FVector& ListenerRight,
		const FDreamOutdoorOcclusion& Outdoor, const FDreamWeatherAudioTuning& Tuning, FDreamThunder& Out);
}
