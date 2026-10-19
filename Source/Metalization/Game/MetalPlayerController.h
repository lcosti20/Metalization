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
class UInputAction;
class UInputMappingContext;
struct FInputActionValue;

UCLASS()
class AMetalPlayerController : public APlayerController
{
	GENERATED_BODY()

public:
	AMetalPlayerController();

	/** The player's own body, even while the player drives another robot. */
	AMetalBotCharacter* GetPlayerBody() const { return PlayerBody.Get(); }
	/** The hijacked robot, if any. */
	AMetalBotCharacter* GetHost() const { return Host.Get(); }
	AMetalBotCharacter* GetHackCandidate() const { return HackCandidate.Get(); }
	AMetalControlConsole* GetNearbyConsole() const { return NearbyConsole.Get(); }
	float GetPossessionTimeRemaining() const;
	float GetPossessionDuration() const { return PossessionDuration; }
	/** Linked to a friendly robot: no time limit, no self-destruct. */
	bool IsAllyLink() const { return bAllyLink; }

	/** Hijack the robot under the crosshair, or disconnect from the current host. */
	void ToggleTether() { TetherPressed(); }

	/** Use the nearby console, if any. */
	void Interact() { InteractPressed(); }

	void NotifyHitConfirmed();
	void NotifyDamageTaken();
	float GetLastHitConfirmTime() const { return LastHitConfirmTime; }
	float GetLastDamageTime() const { return LastDamageTime; }

	/** Max distance for the Hack Tether. */
	UPROPERTY(EditAnywhere, Category="Metalization")
	float TetherRange = 2600.f;

	virtual void Tick(float DeltaSeconds) override;

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void SetupInputComponent() override;
	virtual void OnPossess(APawn* InPawn) override;

private:
	void CreateInputObjects();
	UInputAction* MakeAction(const TCHAR* Name, int32 ValueType);

	AMetalBotCharacter* GetBot() const;
	AMetalStalker* GetFlyer() const;
	bool CanAct() const;
	/** Walking or flying controls, depending on the pawn. */
	void UpdateMappingContext();

	void Move(const FInputActionValue& Value);
	void Look(const FInputActionValue& Value);
	void JumpPressed();
	void JumpReleased();
	void FirePressed();
	void FireReleased();
	void AimPressed();
	void AimReleased();
	void ReloadPressed();
	void GrenadePressed();
	void CycleWeaponAxis(const FInputActionValue& Value);
	void NextWeapon();
	void SelectWeapon(int32 Index);
	void TetherPressed();
	void InteractPressed();
	void ConfirmPressed();
	void AscendAxis(const FInputActionValue& Value);
	void BoostPressed();
	void BoostReleased();
	void DodgePressed(float Direction);

	AMetalBotCharacter* FindHackCandidate() const;
	void BeginPossession(AMetalBotCharacter* Target);
	void EndPossession();
	void OnBotDied(AMetalBotCharacter* Bot, AController* Killer);

	UPROPERTY(Transient)
	TObjectPtr<UInputMappingContext> MappingContext;

	UPROPERTY(Transient)
	TObjectPtr<UInputMappingContext> FlightContext;

	UPROPERTY(Transient)
	TMap<FName, TObjectPtr<UInputAction>> Actions;

	UPROPERTY(Transient)
	TObjectPtr<AMetalFX> TetherBeam;

	TWeakObjectPtr<AMetalBotCharacter> PlayerBody;
	TWeakObjectPtr<AMetalBotCharacter> Host;
	TWeakObjectPtr<AMetalBotCharacter> HackCandidate;
	TWeakObjectPtr<AMetalControlConsole> NearbyConsole;
	float PossessionEndTime = 0.f;
	float PossessionDuration = 0.f;
	float LastHitConfirmTime = -100.f;
	float LastDamageTime = -100.f;
	bool bSwitchingBodies = false;
	bool bAllyLink = false;
	bool bApplySpawnPitch = false;
	FDelegateHandle BotDiedHandle;
};
