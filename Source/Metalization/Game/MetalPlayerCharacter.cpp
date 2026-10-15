#include "MetalPlayerCharacter.h"

#include "MetalBotArchetype.h"

AMetalPlayerCharacter::AMetalPlayerCharacter()
{
	DefaultArchetype = TSoftObjectPtr<UMetalBotArchetype>(FSoftObjectPath(TEXT("/Game/Metalization/Data/DA_Bot_Player.DA_Bot_Player")));
	AutoPossessAI = EAutoPossessAI::Disabled;
}

void AMetalPlayerCharacter::PostInitializeComponents()
{
	if (Archetype == nullptr)
	{
		Archetype = DefaultArchetype.LoadSynchronous();
	}
	Super::PostInitializeComponents();
}
