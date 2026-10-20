// Player input and the Hack Tether.
//
// Possession is an explicit ownership transfer: the player's body is left powered down where it stood
// (and can still be shot), the target robot's AI controller is destroyed, and this controller
// possesses the robot. When the link ends - by choice, timeout, or the host being destroyed -
// control returns to the player's body and the burnt-out host self-destructs.
//
// Friendly robots built for it (parked Stalkers) take an open-ended link instead: no timer and no
// self-destruct, the player flies them until they disconnect. Flyers use their own input context.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "MetalPlayerController.generated.h"

class AMetalBotCharacter;
class AMetalControlConsole;
class AMetalFX;
class AMetalStalker;
class UMetalPauseMenu;
class UInputAction;
class UInputMappingContext;
struct FInputActionValue;

/**
 * The player's controller: builds the Enhanced Input actions in code, routes them to whichever robot is
 * possessed, runs the Hack Tether (hijacking enemies and linking to friendly flyers), finds nearby consoles,
 * and owns the pause menu. The HUD reads its getters for the tether timer, hit markers and prompts.
 *
 * @see AMetalBotCharacter for the robot API the input handlers call.
 * @see AMetalHUD for what is drawn from this controller's state.
 */
UCLASS()
class AMetalPlayerController : public APlayerController
{
	GENERATED_BODY()

public:
	/** Enables ticking (tether timer, hack candidate and console search run every frame). */
	AMetalPlayerController();

	/** The player's own body, even while the player drives another robot. */
	AMetalBotCharacter* GetPlayerBody() const { return PlayerBody.Get(); }

	/** The hijacked robot, if any. */
	AMetalBotCharacter* GetHost() const { return Host.Get(); }

	/** @return The robot the Hack Tether would grab right now (for the HUD's target marker), or null. */
	AMetalBotCharacter* GetHackCandidate() const { return HackCandidate.Get(); }

	/** @return The closest unused console within reach, or null. */
	AMetalControlConsole* GetNearbyConsole() const { return NearbyConsole.Get(); }

	/** @return Seconds left on the current hijack, or 0 when not linked. */
	float GetPossessionTimeRemaining() const;

	/** @return Total length of the current hijack in seconds (0 for an open-ended ally link). */
	float GetPossessionDuration() const { return PossessionDuration; }

	/** Linked to a friendly robot: no time limit, no self-destruct. */
	bool IsAllyLink() const { return bAllyLink; }

	/** Hijack the robot under the crosshair, or disconnect from the current host. */
	void ToggleTether() { TetherPressed(); }

	/** Use the nearby console, if any. */
	void Interact() { InteractPressed(); }

	/** Pauses the game and shows the pause menu (Continue / Level Select / Quit). */
	void OpenPauseMenu();

	/** Hides the pause menu and resumes. */
	void ClosePauseMenu();

	/** @return The open pause menu, or null when the game is running. */
	UMetalPauseMenu* GetPauseMenu() const { return PauseMenu; }

	/** Called by the possessed robot when one of its shots hits a robot, for the HUD hit marker. */
	void NotifyHitConfirmed();

	/** Called by the possessed robot when it takes damage, for the HUD damage flash. */
	void NotifyDamageTaken();

	/** @return World time of the last confirmed hit. */
	float GetLastHitConfirmTime() const { return LastHitConfirmTime; }

	/** @return World time the possessed robot was last damaged. */
	float GetLastDamageTime() const { return LastDamageTime; }

	/** Max distance for the Hack Tether. */
	UPROPERTY(EditAnywhere, Category="Metalization")
	float TetherRange = 2600.f;

	/** Ends timed-out hijacks, updates the tether beam and spawn pitch, and refreshes the hack candidate and nearby console. */
	virtual void Tick(float DeltaSeconds) override;

protected:
	/** Applies the input mapping context and subscribes to robot deaths. */
	virtual void BeginPlay() override;

	/** Unsubscribes from robot deaths. */
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	/** Creates the input actions and binds each to its handler. */
	virtual void SetupInputComponent() override;

	/**
	 * Switches the input context for the new pawn. Possessions that aren't part of a tether switch
	 * (level start, respawn) also record the pawn as the player's own body.
	 */
	virtual void OnPossess(APawn* InPawn) override;

private:
	/** Builds the walking and flight mapping contexts and all input actions in code (no input assets). */
	void CreateInputObjects();

	/**
	 * Creates an input action and registers it in Actions.
	 *
	 * @param Name      Action key in Actions; the object is named "IA_<Name>".
	 * @param ValueType An EInputActionValueType, as an int so the header needs no Enhanced Input includes.
	 * @return The new action.
	 */
	UInputAction* MakeAction(const TCHAR* Name, int32 ValueType);

	/** @return The possessed pawn as a robot, or null. */
	AMetalBotCharacter* GetBot() const;

	/** @return The possessed pawn as a flyer, or null when walking. */
	AMetalStalker* GetFlyer() const;

	/** @return True if the possessed robot is alive, awake and not stunned, so gameplay input applies. */
	bool CanAct() const;

	/** Walking or flying controls, depending on the pawn. */
	void UpdateMappingContext();

	// Input handlers, one per action. Each checks CanAct() where needed and forwards to the robot.

	/** @param Value 2D stick/WASD value: X strafes, Y moves forward. */
	void Move(const FInputActionValue& Value);

	/** @param Value 2D mouse/stick delta: X yaws, Y pitches. */
	void Look(const FInputActionValue& Value);

	/** Jumps (a second press in mid-air is the double jump). */
	void JumpPressed();

	/** Ends the jump hold. */
	void JumpReleased();

	/** Holds the trigger on the possessed robot. */
	void FirePressed();

	/** Releases the trigger. */
	void FireReleased();

	/** Starts aiming (zoomed camera). */
	void AimPressed();

	/** Stops aiming. */
	void AimReleased();

	/** Reloads the equipped weapon. */
	void ReloadPressed();

	/** Throws a grenade. */
	void GrenadePressed();

	/** @param Value Mouse wheel axis; its sign picks the next or previous weapon. */
	void CycleWeaponAxis(const FInputActionValue& Value);

	/** Equips the next weapon (gamepad). */
	void NextWeapon();

	/** @param Index Weapon slot bound to number keys 1-6. */
	void SelectWeapon(int32 Index);

	/** Disconnects if linked, otherwise hijacks the hack candidate (or says there is none). */
	void TetherPressed();

	/** Activates the nearby console and shows its result message. */
	void InteractPressed();

	/** Restarts the mission once it is complete. */
	void ConfirmPressed();

	/** Opens the pause menu (closing is handled by the menu itself). */
	void PausePressed();

	/** @param Value Flight climb (+) / dive (-) axis. */
	void AscendAxis(const FInputActionValue& Value);

	/** Engages flight boost. */
	void BoostPressed();

	/** Releases flight boost. */
	void BoostReleased();

	/** @param Direction -1 to roll left, +1 to roll right. */
	void DodgePressed(float Direction);

	/**
	 * Finds what the Hack Tether would grab: the hackable robot within TetherRange closest to the centre
	 * of a 7-degree cone around the view direction, with a clear line from the player's eyes.
	 * Only works from the player's own body.
	 *
	 * @return The best candidate, or null.
	 */
	AMetalBotCharacter* FindHackCandidate() const;

	/**
	 * Hands the player's control over to Target: destroys its AI controller, powers the body down,
	 * possesses Target and starts the hijack timer (or an open-ended link for an ally).
	 *
	 * @param Target Robot to take over; must satisfy CanBeHackedBy.
	 */
	void BeginPossession(AMetalBotCharacter* Target);

	/** Returns control to the player's body. A hijacked enemy self-destructs; an ally link goes dormant. */
	void EndPossession();

	/**
	 * OnAnyBotDied handler: ends the link if the host died, and reports the player's death to the game mode.
	 *
	 * @param Bot    Robot that died.
	 * @param Killer Controller credited with the kill.
	 */
	void OnBotDied(AMetalBotCharacter* Bot, AController* Killer);

	/** Walking controls. */
	UPROPERTY(Transient)
	TObjectPtr<UInputMappingContext> MappingContext;

	/** Flying controls (Stalkers). */
	UPROPERTY(Transient)
	TObjectPtr<UInputMappingContext> FlightContext;

	/** Every input action by name ("Move", "Fire", "Weapon1"...). Shared by both contexts. */
	UPROPERTY(Transient)
	TMap<FName, TObjectPtr<UInputAction>> Actions;

	/** Beam drawn from the player's body to the hijacked robot. */
	UPROPERTY(Transient)
	TObjectPtr<AMetalFX> TetherBeam;

	UPROPERTY(Transient)
	TObjectPtr<UMetalPauseMenu> PauseMenu;

	TWeakObjectPtr<AMetalBotCharacter> PlayerBody;
	TWeakObjectPtr<AMetalBotCharacter> Host;
	TWeakObjectPtr<AMetalBotCharacter> HackCandidate;
	TWeakObjectPtr<AMetalControlConsole> NearbyConsole;
	float PossessionEndTime = 0.f;
	float PossessionDuration = 0.f;
	float LastHitConfirmTime = -100.f;
	float LastDamageTime = -100.f;
	/** Set around Possess() calls made by the tether, so OnPossess doesn't mistake the host for the body. */
	bool bSwitchingBodies = false;
	bool bAllyLink = false;
	/** Tilt the camera down on the next tick after a fresh spawn. */
	bool bApplySpawnPitch = false;
	FDelegateHandle BotDiedHandle;
};
