// Copyright DreamMoon. All Rights Reserved.

#include "DreamWindSettings.h"
#include "DreamWindSubsystem.h"
#include "Engine/Engine.h"
#include "Engine/World.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(DreamWindSettings)

#define LOCTEXT_NAMESPACE "DreamWind"

UDreamWindSettings::UDreamWindSettings()
{
	CategoryName = TEXT("DreamPlugin");
	SectionName = TEXT("Dream Wind");
}

#if WITH_EDITOR
FText UDreamWindSettings::GetSectionText() const
{
	return LOCTEXT("SettingsSection", "Dream Wind");
}

void UDreamWindSettings::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);

	// Live-apply the default wind to editor worlds that are not overridden by a level actor, so
	// tweaking the settings shows in the viewport immediately.
	const FName MemberName = PropertyChangedEvent.MemberProperty ? PropertyChangedEvent.MemberProperty->GetFName() : NAME_None;
	if (MemberName == GET_MEMBER_NAME_CHECKED(UDreamWindSettings, DefaultWind) && GEngine)
	{
		for (const FWorldContext& Context : GEngine->GetWorldContexts())
		{
			if (UWorld* World = Context.World())
			{
				if (UDreamWindSubsystem* Wind = World->GetSubsystem<UDreamWindSubsystem>())
				{
					Wind->SetGlobalWind(DefaultWind, 0.0f);
				}
			}
		}
	}
}
#endif

#undef LOCTEXT_NAMESPACE
