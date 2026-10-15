#include "MetalWeaponData.h"

const FMetalWeaponLevel& UMetalWeaponData::GetLevel(int32 LevelIndex) const
{
	static const FMetalWeaponLevel Fallback;
	if (Levels.Num() == 0)
	{
		return Fallback;
	}
	return Levels[FMath::Clamp(LevelIndex, 0, Levels.Num() - 1)];
}
