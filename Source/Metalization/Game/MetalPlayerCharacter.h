// The player's robot. All behaviour lives in AMetalBotCharacter and DA_Bot_Player; this
// class only exists so the game mode can spawn it without extra setup.

#pragma once

#include "CoreMinimal.h"
#include "MetalBotCharacter.h"
#include "MetalPlayerCharacter.generated.h"

UCLASS()
class AMetalPlayerCharacter : public AMetalBotCharacter
{
	GENERATED_BODY()

public:
	AMetalPlayerCharacter();

	/** Archetype used when none is assigned (created by Tools/AssetPipeline/ue_setup_gameplay.py). */
	UPROPERTY(EditDefaultsOnly, Category="Metalization")
	TSoftObjectPtr<UMetalBotArchetype> DefaultArchetype;

protected:
	virtual void PostInitializeComponents() override;
};
