// Copyright DreamMoon. All Rights Reserved.

#include "DreamWindSetupCommandlet.h"
#include "DreamWindEditorLibrary.h"
#include "DreamWindModule.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(DreamWindSetupCommandlet)

UDreamWindSetupCommandlet::UDreamWindSetupCommandlet()
{
	IsClient = false;
	IsServer = false;
	IsEditor = true;
	LogToConsole = true;
}

int32 UDreamWindSetupCommandlet::Main(const FString& Params)
{
	const bool bOk = UDreamWindEditorLibrary::EnsureAssets(true);
	UE_LOG(LogDreamWind, Display, TEXT("DreamWindSetup: %s"), bOk ? TEXT("OK") : TEXT("FAILED"));
	return bOk ? 0 : 1;
}
