// The player's robot. All behaviour lives in AMetalBotCharacter and DA_Bot_Player; this
// class only exists so the game mode can spawn it without extra setup.

#pragma once

#include "CoreMinimal.h"
#include "MetalBotCharacter.h"
#include "MetalPlayerCharacter.generated.h"

/**
 * The player's own robot body. It differs from AMetalBotCharacter only in that it never gets an AI
 * controller and falls back to DA_Bot_Player when no archetype is assigned.
 */
UCLASS()
class AMetalPlayerCharacter : public AMetalBotCharacter
{
	GENERATED_BODY()

public:
	/** Points DefaultArchetype at DA_Bot_Player and disables AI auto-possession. */
	AMetalPlayerCharacter();

	/** Archetype used when none is assigned (created by Tools/AssetPipeline/ue_setup_gameplay.py). */
	UPROPERTY(EditDefaultsOnly, Category="Metalization")
	TSoftObjectPtr<UMetalBotArchetype> DefaultArchetype;

protected:
	/** Loads DefaultArchetype if no archetype was set, before the base class applies it. */
	virtual void PostInitializeComponents() override;
};
