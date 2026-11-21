// Copyright DreamMoon. All Rights Reserved.

#include "DreamSkyOverrides.h"
#include "DreamSkyStyle.h"


#include UE_INLINE_GENERATED_CPP_BY_NAME(DreamSkyOverrides)

void FDreamSkyCloudOverrides::ApplyTo(UDreamSkyStyle& Style) const
{
	if (bOverride_Coverage) { Style.CloudCoverage = Coverage; }
	if (bOverride_Scale) { Style.CloudScale = Scale; }
	if (bOverride_BaseAltitude) { Style.CloudLayerHeight = BaseAltitude; }
	if (bOverride_Speed) { Style.CloudSpeed = Speed; }
	if (bOverride_FormationSpeed) { Style.CloudFormationSpeed = FormationSpeed; }
	if (bOverride_Direction) { Style.CloudDirection = Direction; }
	if (bOverride_Diurnal) { Style.CloudDiurnal = Diurnal; }
	if (bOverride_Erosion) { Style.CloudErosion = Erosion; }
	if (bOverride_Detail) { Style.CloudDetail = Detail; }
	if (bOverride_HighCover) { Style.CloudHighCover = HighCover; }
	if (bOverride_StormDarkening) { Style.StormCloudDarkening = StormDarkening; }
	if (bOverride_Visibility) { Style.CloudVisibility = Visibility; }
}

bool FDreamSkyCloudOverrides::TouchesStyle() const
{
	return bOverride_Coverage || bOverride_Scale || bOverride_BaseAltitude || bOverride_Speed || bOverride_FormationSpeed || bOverride_Direction || bOverride_Diurnal
		|| bOverride_Erosion || bOverride_Detail || bOverride_HighCover || bOverride_StormDarkening || bOverride_Visibility;
}

void FDreamSkyLightOverrides::ApplyTo(UDreamSkyStyle& Style) const
{
	if (bOverride_MoonCastsShadows) { Style.bMoonCastsShadows = bMoonCastsShadows; }
	if (bOverride_SunDiskSize) { Style.SunDiskSize = SunDiskSize; }
	if (bOverride_MoonDiskSize) { Style.MoonDiskSize = MoonDiskSize; }
}

bool FDreamSkyLightOverrides::TouchesStyle() const
{
	return bOverride_MoonCastsShadows || bOverride_SunDiskSize || bOverride_MoonDiskSize;
}

void FDreamSkyAmbientOverrides::ApplyTo(UDreamSkyStyle& Style) const
{
	if (bOverride_FogHeightFalloff) { Style.FogHeightFalloff = FogHeightFalloff; }
	if (bOverride_FogMaxOpacity) { Style.FogMaxOpacity = FogMaxOpacity; }
}

bool FDreamSkyAmbientOverrides::TouchesStyle() const
{
	return bOverride_FogHeightFalloff || bOverride_FogMaxOpacity;
}

void FDreamSkyExposureOverrides::ApplyTo(UDreamSkyStyle& Style) const
{
	if (bOverride_Mode) { Style.ExposureMode = Mode; }
	if (bOverride_AutoRange) { Style.ExposureAutoRange = AutoRange; }
	if (bOverride_AutoSpeedUp) { Style.ExposureSpeedUp = AutoSpeedUp; }
	if (bOverride_AutoSpeedDown) { Style.ExposureSpeedDown = AutoSpeedDown; }
}

bool FDreamSkyExposureOverrides::TouchesStyle() const
{
	return bOverride_Mode || bOverride_AutoRange || bOverride_AutoSpeedUp || bOverride_AutoSpeedDown;
}
