// Title screen (MZ_Title): a slow camera orbit over a Blue-vs-Red standoff with the menu on top.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "GameFramework/PlayerController.h"
#include "MetalTitle.generated.h"

class ACameraActor;
class UMetalPauseMenu;

/** Game mode of the title map: no pawn, the title controller and no HUD. */
UCLASS()
class AMetalTitleGameMode : public AGameModeBase
{
	GENERATED_BODY()

public:
	AMetalTitleGameMode();
};

/**
 * Shows the menu (UMetalPauseMenu in title mode) and flies the view around the camera actor tagged
 * "TitleCamera" (orbiting its starting position's look-at point), or the world origin if there is none.
 */
UCLASS()
class AMetalTitlePlayerController : public APlayerController
{
	GENERATED_BODY()

public:
	AMetalTitlePlayerController();

	/** Orbits the camera. */
	virtual void Tick(float DeltaSeconds) override;

	/** @return The title menu, for the dev autopilot. */
	UMetalPauseMenu* GetMenu() const { return Menu; }

protected:
	/** Creates the menu, gives it the input and picks the camera. */
	virtual void BeginPlay() override;

private:
	UPROPERTY(Transient)
	TObjectPtr<UMetalPauseMenu> Menu;

	UPROPERTY(Transient)
	TObjectPtr<ACameraActor> TitleCamera;

	/** Point the camera circles and looks at. */
	FVector Focus = FVector::ZeroVector;
	float OrbitRadius = 1500.f;
	float OrbitHeight = 400.f;
	float OrbitAngle = 0.f;
};
