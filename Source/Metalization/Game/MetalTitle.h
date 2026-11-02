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
	/** Sets the title controller, no default pawn and a plain HUD. */
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
	/** Ticks even while paused so the orbit keeps moving. */
	AMetalTitlePlayerController();

	/** Orbits the camera. */
	virtual void Tick(float DeltaSeconds) override;

	/** @return The title menu, for the dev autopilot. */
	UMetalPauseMenu* GetMenu() const { return Menu; }

protected:
	/** Creates the menu, gives it the input and picks the camera. */
	virtual void BeginPlay() override;

private:
	/** The menu, in title mode. */
	UPROPERTY(Transient)
	TObjectPtr<UMetalPauseMenu> Menu;

	/** Placed camera tagged "TitleCamera", or null. */
	UPROPERTY(Transient)
	TObjectPtr<ACameraActor> TitleCamera;

	/** Point the camera circles and looks at. */
	FVector Focus = FVector::ZeroVector;
	/** Horizontal distance of the camera from Focus, cm. */
	float OrbitRadius = 1500.f;
	/** Height of the camera above Focus, cm. */
	float OrbitHeight = 400.f;
	/** Current angle around Focus, degrees. */
	float OrbitAngle = 0.f;
};
