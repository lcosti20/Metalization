// Copyright Epic Games, Inc. All Rights Reserved.

#include "MetalizationGameMode.h"

#include "MetalizationCharacter.h"
#include "MetalizationHUD.h"
#include "MetalizationPlayerController.h"

AMetalizationGameMode::AMetalizationGameMode()
{
	DefaultPawnClass = AMetalizationCharacter::StaticClass();
	PlayerControllerClass = AMetalizationPlayerController::StaticClass();
	HUDClass = AMetalizationHUD::StaticClass();
}
