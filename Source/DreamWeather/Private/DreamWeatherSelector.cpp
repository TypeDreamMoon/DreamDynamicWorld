// Copyright DreamMoon. All Rights Reserved.

#include "DreamWeatherSelector.h"
#include "DreamWeatherPreset.h"
#include "DreamWorldCelestial.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(DreamWeatherSelector)

namespace DreamWeather::Selector
{
	/** A 32-bit avalanche (fixed, so the same seed gives the same weather everywhere). */
	static uint32 Mix(uint32 X)
	{
		X ^= X >> 16;
		X *= 0x7feb352dU;
		X ^= X >> 15;
		X *= 0x846ca68bU;
		X ^= X >> 16;
		return X;
	}

	static int64 FloorDiv(double Value, double Divisor)
	{
		return int64(FMath::FloorToDouble(Value / Divisor));
	}
}

float UDreamWeatherSelector::Hash01(int32 Seed, int64 Slot, uint32 Salt)
{
	using namespace DreamWeather::Selector;
	const uint32 H = Mix(uint32(Seed) ^ Mix(uint32(uint64(Slot) & 0xffffffffu) ^ Mix(uint32(uint64(Slot) >> 32) ^ Mix(Salt + 0x9e3779b9U))));
	return float(H >> 8) * (1.0f / 16777216.0f);
}

int32 UDreamWeatherSelector::SeasonAt(double WorldSeconds, bool bSouthernHemisphere)
{
	// Meteorological seasons: spring Mar-May, summer Jun-Aug, autumn Sep-Nov, winter Dec-Feb.
	const int32 Month = DreamWorld::Celestial::ToDateTime(WorldSeconds).GetMonth();
	int32 Season = ((Month + 9) % 12) / 3; // Mar -> 0, Jun -> 1, Sep -> 2, Dec -> 3
	if (bSouthernHemisphere)
	{
		Season = (Season + 2) % 4;
	}
	return Season;
}

FDreamWeatherSlot UDreamWeatherSelector_Fixed::GetSlot(double WorldSeconds, int32 Seed) const
{
	FDreamWeatherSlot Slot;
	Slot.Previous = Preset;
	Slot.Current = Preset;
	Slot.Start = 0.0;
	Slot.Length = 1.0e12;
	return Slot;
}

FDreamWeatherSlot UDreamWeatherSelector_Sequence::GetSlot(double WorldSeconds, int32 Seed) const
{
	FDreamWeatherSlot Slot;
	double Total = 0.0;
	for (const FDreamWeatherSequenceEntry& E : Entries)
	{
		Total += FMath::Max(0.1, double(E.Hours)) * 3600.0;
	}
	if (Entries.Num() == 0 || Total <= 0.0)
	{
		return Slot;
	}
	const int64 Cycle = DreamWeather::Selector::FloorDiv(WorldSeconds, Total);
	double Cursor = double(Cycle) * Total;
	for (int32 i = 0; i < Entries.Num(); ++i)
	{
		const double Length = FMath::Max(0.1, double(Entries[i].Hours)) * 3600.0;
		if (WorldSeconds < Cursor + Length || i == Entries.Num() - 1)
		{
			Slot.Current = Entries[i].Preset;
			Slot.Previous = Entries[(i + Entries.Num() - 1) % Entries.Num()].Preset;
			Slot.Start = Cursor;
			Slot.Length = Length;
			Slot.Index = Cycle * Entries.Num() + i;
			return Slot;
		}
		Cursor += Length;
	}
	return Slot;
}

float UDreamWeatherSelector_Random::WeightOf(int32 EntryIndex, int64 SlotIndex) const
{
	return FMath::Max(0.0f, Entries[EntryIndex].Weight);
}

const UDreamWeatherPreset* UDreamWeatherSelector_Drawn::Choose(int64 Index, int32 Seed, int32 Depth) const
{
	const int32 Num = NumEntries();
	if (Num == 0)
	{
		return nullptr;
	}
	// Keep the weather of the slot before, now and then (bounded, so a lookup never walks far back).
	if (Depth < 24 && Hash01(Seed, Index, 1u) < Persistence)
	{
		return Choose(Index - 1, Seed, Depth + 1);
	}
	float Total = 0.0f;
	for (int32 i = 0; i < Num; ++i)
	{
		Total += WeightOf(i, Index);
	}
	if (Total <= 0.0f)
	{
		return PresetOf(0);
	}
	float Pick = Hash01(Seed, Index, 2u) * Total;
	for (int32 i = 0; i < Num; ++i)
	{
		Pick -= WeightOf(i, Index);
		if (Pick < 0.0f)
		{
			return PresetOf(i);
		}
	}
	return PresetOf(Num - 1);
}

FDreamWeatherSlot UDreamWeatherSelector_Drawn::GetSlot(double WorldSeconds, int32 Seed) const
{
	FDreamWeatherSlot Slot;
	const double Length = FMath::Max(0.5, double(SlotHours)) * 3600.0;
	Slot.Index = DreamWeather::Selector::FloorDiv(WorldSeconds, Length);
	Slot.Start = double(Slot.Index) * Length;
	Slot.Length = Length;
	Slot.Current = Choose(Slot.Index, Seed, 0);
	Slot.Previous = Choose(Slot.Index - 1, Seed, 0);
	return Slot;
}

float UDreamWeatherSelector_Seasonal::WeightOf(int32 EntryIndex, int64 SlotIndex) const
{
	const FDreamWeatherSeasonalEntry& E = SeasonalEntries[EntryIndex];
	const double SlotStart = double(SlotIndex) * FMath::Max(0.5, double(SlotHours)) * 3600.0;
	switch (SeasonAt(SlotStart, bSouthernHemisphere))
	{
	case 0: return FMath::Max(0.0f, E.Spring);
	case 1: return FMath::Max(0.0f, E.Summer);
	case 2: return FMath::Max(0.0f, E.Autumn);
	default: return FMath::Max(0.0f, E.Winter);
	}
}
