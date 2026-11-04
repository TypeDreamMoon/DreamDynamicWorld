// Copyright DreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

class UMaterialParameterCollection;

/** One vector parameter of the shared Material Parameter Collection. */
struct FDreamWorldParameterDesc
{
	/** Module (or subsystem) that writes it. */
	FName Owner;
	FName Name;
	FLinearColor Default = FLinearColor::Transparent;
	/** The parameter's id in the collection. Derived from Name, so it never changes between regenerations. */
	FGuid Id;
};

/**
 * The parameters every subsystem of the plugin writes into the ONE Material Parameter Collection they share.
 *
 * A material may reference at most two collections, so the plugin uses one: a material that reads the wind
 * and the sky still leaves a slot for the project's own collection. Each module registers its parameters
 * when it starts; the Setup step creates the collection from this list, and materials read the parameters
 * by name (through the collection) or by the generated material functions.
 */
class DREAMDYNAMICWORLD_API FDreamWorldParameters
{
public:
	/** Registers a vector parameter. Registering the same name twice keeps the first registration. */
	static void RegisterVector(FName Owner, FName Name, const FLinearColor& Default);

	/** Removes everything Owner registered (module shutdown). */
	static void UnregisterOwner(FName Owner);

	/** Every registered parameter, in registration order. */
	static TArray<FDreamWorldParameterDesc> GetAll();

	/** True when Name is registered. */
	static bool IsRegistered(FName Name);

	/** The id a parameter called Name gets in the collection: an MD5 of the name, so stable across machines and runs. */
	static FGuid MakeStableId(FName Name);

#if WITH_EDITOR
	/**
	 * Creates the collection at UDreamWorldSettings::ParameterCollection when missing and adds every registered
	 * parameter it lacks (existing parameters, their ids and values are left alone). Saves the asset when it
	 * changed and bSave is set. Returns the collection, or null when the path is invalid.
	 */
	static UMaterialParameterCollection* EnsureParameterCollection(bool bSave);
#endif
};
