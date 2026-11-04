// Copyright DreamMoon. All Rights Reserved.

#include "DreamWorldParameters.h"
#include "DreamDynamicWorldModule.h"
#include "DreamWorldSettings.h"
#include "Materials/MaterialParameterCollection.h"
#include "Misc/ScopeLock.h"
#include "Misc/SecureHash.h"

#if WITH_EDITOR
#include "AssetRegistry/AssetRegistryModule.h"
#include "Misc/PackageName.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"
#endif

namespace DreamWorld::Parameters
{
	static FCriticalSection& Lock()
	{
		static FCriticalSection Section;
		return Section;
	}

	static TArray<FDreamWorldParameterDesc>& Registry()
	{
		static TArray<FDreamWorldParameterDesc> List;
		return List;
	}
}

void FDreamWorldParameters::RegisterVector(FName Owner, FName Name, const FLinearColor& Default)
{
	using namespace DreamWorld::Parameters;
	FScopeLock ScopeLock(&Lock());
	if (Registry().ContainsByPredicate([Name](const FDreamWorldParameterDesc& D) { return D.Name == Name; }))
	{
		return;
	}
	FDreamWorldParameterDesc& Desc = Registry().AddDefaulted_GetRef();
	Desc.Owner = Owner;
	Desc.Name = Name;
	Desc.Default = Default;
	Desc.Id = MakeStableId(Name);
}

void FDreamWorldParameters::UnregisterOwner(FName Owner)
{
	using namespace DreamWorld::Parameters;
	FScopeLock ScopeLock(&Lock());
	Registry().RemoveAll([Owner](const FDreamWorldParameterDesc& D) { return D.Owner == Owner; });
}

TArray<FDreamWorldParameterDesc> FDreamWorldParameters::GetAll()
{
	using namespace DreamWorld::Parameters;
	FScopeLock ScopeLock(&Lock());
	return Registry();
}

bool FDreamWorldParameters::IsRegistered(FName Name)
{
	using namespace DreamWorld::Parameters;
	FScopeLock ScopeLock(&Lock());
	return Registry().ContainsByPredicate([Name](const FDreamWorldParameterDesc& D) { return D.Name == Name; });
}

FGuid FDreamWorldParameters::MakeStableId(FName Name)
{
	const FString Key = FString::Printf(TEXT("DreamDynamicWorld.MPC.%s"), *Name.ToString());
	const FTCHARToUTF8 Utf8(*Key);
	uint8 Digest[16];
	FMD5 Md5;
	Md5.Update(reinterpret_cast<const uint8*>(Utf8.Get()), Utf8.Length());
	Md5.Final(Digest);
	auto Word = [&Digest](int32 i) { return uint32(Digest[i]) << 24 | uint32(Digest[i + 1]) << 16 | uint32(Digest[i + 2]) << 8 | uint32(Digest[i + 3]); };
	return FGuid(Word(0), Word(4), Word(8), Word(12));
}

#if WITH_EDITOR
UMaterialParameterCollection* FDreamWorldParameters::EnsureParameterCollection(bool bSave)
{
	const FSoftObjectPath& Path = UDreamWorldSettings::Get()->ParameterCollection;
	const FString PackageName = Path.GetLongPackageName();
	if (PackageName.IsEmpty() || !FPackageName::IsValidLongPackageName(PackageName))
	{
		UE_LOG(LogDreamWorld, Error, TEXT("'%s' is not a valid asset path for the parameter collection."), *Path.ToString());
		return nullptr;
	}
	FString AssetName = Path.GetAssetName();
	if (AssetName.IsEmpty())
	{
		AssetName = FPackageName::GetLongPackageAssetName(PackageName);
	}

	UMaterialParameterCollection* Collection = Cast<UMaterialParameterCollection>(Path.TryLoad());
	bool bCreated = false;
	if (!Collection)
	{
		UPackage* Package = CreatePackage(*PackageName);
		Package->FullyLoad();
		Collection = NewObject<UMaterialParameterCollection>(Package, *AssetName, RF_Public | RF_Standalone);
		bCreated = true;
	}

	bool bChanged = false;
	Collection->PreEditChange(nullptr);
	for (const FDreamWorldParameterDesc& Desc : GetAll())
	{
		const bool bExists = Collection->VectorParameters.ContainsByPredicate([&Desc](const FCollectionVectorParameter& P) { return P.ParameterName == Desc.Name; });
		if (!bExists)
		{
			FCollectionVectorParameter& P = Collection->VectorParameters.AddDefaulted_GetRef();
			P.ParameterName = Desc.Name;
			P.Id = Desc.Id;
			P.DefaultValue = Desc.Default;
			bChanged = true;
		}
	}
	Collection->PostEditChange();

	if (bCreated)
	{
		FAssetRegistryModule::AssetCreated(Collection);
	}
	if ((bCreated || bChanged) && bSave)
	{
		UPackage* Package = Collection->GetOutermost();
		Package->MarkPackageDirty();
		const FString Filename = FPackageName::LongPackageNameToFilename(Package->GetName(), FPackageName::GetAssetPackageExtension());
		FSavePackageArgs Args;
		Args.TopLevelFlags = RF_Public | RF_Standalone;
		Args.SaveFlags = SAVE_NoError;
		if (!UPackage::SavePackage(Package, Collection, *Filename, Args))
		{
			UE_LOG(LogDreamWorld, Error, TEXT("Failed to save '%s'."), *Filename);
		}
	}
	UE_LOG(LogDreamWorld, Display, TEXT("Parameter collection %s: %s (%d vector parameters)"), *Collection->GetPathName(),
		bCreated ? TEXT("created") : (bChanged ? TEXT("parameters added") : TEXT("up to date")), Collection->VectorParameters.Num());
	return Collection;
}
#endif
