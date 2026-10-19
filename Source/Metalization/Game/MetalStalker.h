// A flying robot with four gun arms in an X. Handles like a hover gunship rather than a plane: it can
// stop dead and hover, strafes on every axis, banks into its turns and levels itself out when the
// controls are released. Boost is the flight version of sprinting, and a dodge roll spends boost.
//
// The capsule only wraps the body; the long arms are hit through the mesh's physics bodies, and each
// arm is a hit zone that loses its gun when shot off. Parked Stalkers rest on the floor with their
// arms folded until a pilot links to them with the Hack Tether (friendly ones have no time limit).

#pragma once

#include "CoreMinimal.h"
#include "MetalBotCharacter.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "MetalStalker.generated.h"

struct FMetalFlightTuning;

/**
 * Character movement for flyers. In flight the velocity swings toward the stick direction at a tunable
 * grip instead of the physics volume's (very low) fluid friction, so the craft corners and stops like a
 * hover gunship rather than sliding on its momentum.
 */
UCLASS()
class UMetalFlightMovementComponent : public UCharacterMovementComponent
{
	GENERATED_BODY()

public:
	float FlightGrip = 3.f;

	virtual void CalcVelocity(float DeltaTime, float Friction, bool bFluid, float BrakingDeceleration) override;
};

UCLASS()
class AMetalStalker : public AMetalBotCharacter
{
	GENERATED_BODY()

public:
	AMetalStalker(const FObjectInitializer& ObjectInitializer);

	/** Starts on the floor with its arms folded (a hangar pad). Otherwise it starts hovering in place. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Metalization|Flight")
	bool bStartParked = true;

	void SetBoostHeld(bool bHeld) { bBoostHeld = bHeld; }
	/** Climb (+1) or descend (-1) along world up. */
	void AddVerticalInput(float Value);
	/** Sideways burst with a barrel roll: -1 left, +1 right. Costs boost. */
	void Dodge(float Direction);

	bool IsBoosting() const { return bBoosting; }
	bool IsParked() const { return bParked; }
	/** 0..1 boost meter. */
	float GetBoostFraction() const { return BoostMeter; }

	virtual FVector GetMuzzleLocation() const override;
	virtual FVector GetAimForward() const override;
	virtual void SetDormant(bool bNewDormant) override;
	virtual bool UsesActionCamera() const override { return false; }
	virtual void ApplyArchetype() override;
	virtual void PossessedBy(AController* NewController) override;
	virtual void Tick(float DeltaSeconds) override;

protected:
	virtual void BeginPlay() override;
	virtual void ConfigureRotationForController() override;

private:
	const FMetalFlightTuning& GetFlight() const;
	void TickBoost(float DeltaSeconds, float Now);
	void TickTilt(float DeltaSeconds, float Now);

	bool bParked = false;
	bool bBoostHeld = false;
	bool bBoosting = false;
	float BoostMeter = 1.f;
	float LastBoostTime = -100.f;
	/** Visual body pitch (the capsule stays upright) and bank, degrees. */
	float BodyPitch = 0.f;
	float Bank = 0.f;
	float LastYaw = 0.f;
	float DodgeStartTime = -1.f;
	float DodgeDirection = 0.f;
};
