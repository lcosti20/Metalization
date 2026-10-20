// Enemy-only control console. The player can't use it directly - they have to hijack an enemy robot
// with the Hack Tether and operate it with that body. Activating it opens the linked gates.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "MetalTypes.h"
#include "MetalControlConsole.generated.h"

class AMetalBotCharacter;
class UStaticMeshComponent;
class UPointLightComponent;
class UMaterialInstanceDynamic;

/**
 * Faction-locked console that opens AMetalGate actors. The player interacts with it through
 * AMetalPlayerController (which finds the nearest console within InteractRadius); only a robot whose
 * archetype belongs to RequiredFaction succeeds, which is what forces the player to hijack an enemy.
 */
UCLASS()
class AMetalControlConsole : public AActor
{
	GENERATED_BODY()

public:
	/** Builds the placeholder base, screen and light from engine cubes. */
	AMetalControlConsole();

	/** Robots on this team can operate the console. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Console")
	EMetalTeam RequiredFaction = EMetalTeam::Villains;

	/** Gates (AMetalGate) opened on activation. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Console")
	TArray<TObjectPtr<AActor>> LinkedGates;

	/** Distance in cm within which the player is offered the prompt. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Console")
	float InteractRadius = 220.f;

	/** HUD prompt shown when in reach. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Console")
	FText PromptText = NSLOCTEXT("Metalization", "ConsolePrompt", "[E] Operate gate console");

	/** @return True once the console has been used successfully. */
	bool IsActivated() const { return bActivated; }

	/**
	 * Tries to operate the console. The check uses the archetype's team, not the robot's current team, so
	 * a hijacked enemy (which fights for the Heroes) still has the enemy's access.
	 *
	 * @param User The robot pressing the button.
	 * @return The message to show the player (granted, denied or already open).
	 */
	FText TryActivate(AMetalBotCharacter* User);

protected:
	/** Creates the glowing screen material, starting red (locked). */
	virtual void BeginPlay() override;

	/**
	 * Tints the screen and its light.
	 *
	 * @param Color New colour.
	 */
	void SetScreenColor(const FLinearColor& Color);

	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UStaticMeshComponent> Base;

	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UStaticMeshComponent> Screen;

	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UPointLightComponent> Light;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> ScreenMaterial;

private:
	bool bActivated = false;
};
