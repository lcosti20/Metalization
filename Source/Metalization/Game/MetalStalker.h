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
	/** Steering friction used while flying (copied from FMetalFlightTuning::Grip). */
	float FlightGrip = 3.f;

	/**
	 * While flying, substitutes FlightGrip for the friction and turns off fluid handling, then defers to
	 * the base implementation; other movement modes are unchanged.
	 */
	virtual void CalcVelocity(float DeltaTime, float Friction, bool bFluid, float BrakingDeceleration) override;
};

/**
 * The flying robot used in the space battles, piloted by the player (via the Hack Tether link) or by an
 * AMetalFlyerAIController. Flight is a CharacterMovementComponent in MOVE_Flying with an upright capsule;
 * the nose pitch and banking are purely visual (MeshTilt). Flight numbers come from the archetype's
 * FMetalFlightTuning.
 */
UCLASS()
class AMetalStalker : public AMetalBotCharacter
{
	GENERATED_BODY()

public:
	/**
	 * Swaps in UMetalFlightMovementComponent, removes the default AI brain, and makes the mesh's physics
	 * bodies take shots so the long arms can be hit.
	 */
	AMetalStalker(const FObjectInitializer& ObjectInitializer);

	/** Starts on the floor with its arms folded (a hangar pad). Otherwise it starts hovering in place. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Metalization|Flight")
	bool bStartParked = true;

	/**
	 * @param bHeld True while the boost button is held. Boost only engages while there is meter left.
	 */
	void SetBoostHeld(bool bHeld) { bBoostHeld = bHeld; }

	/**
	 * Climb (+1) or descend (-1) along world up.
	 *
	 * @param Value Input scale, -1..1.
	 */
	void AddVerticalInput(float Value);

	/**
	 * Sideways burst with a barrel roll: -1 left, +1 right. Costs boost.
	 *
	 * @param Direction Sign picks the side; zero is ignored. Ignored mid-dodge or without enough meter.
	 */
	void Dodge(float Direction);

	/** @return True while boost is engaged this frame. */
	bool IsBoosting() const { return bBoosting; }

	/** @return True while resting on its pad, before anyone has linked to it. */
	bool IsParked() const { return bParked; }

	/** 0..1 boost meter. */
	float GetBoostFraction() const { return BoostMeter; }

	/** Shots leave from the tip of the current gun arm rather than from the gun bone. */
	virtual FVector GetMuzzleLocation() const override;

	/** Uses the visual body pitch, so the guns fire where the nose points. */
	virtual FVector GetAimForward() const override;

	/** Unlike the base class, does not stop movement: a released Stalker coasts to a hover. */
	virtual void SetDormant(bool bNewDormant) override;

	/** Flyers always use the strafing camera. */
	virtual bool UsesActionCamera() const override { return false; }

	/** Also applies the archetype's flight speeds, acceleration, braking and grip. */
	virtual void ApplyArchetype() override;

	/** Forces flying mode on every possession, and lifts off the pad if it was parked. */
	virtual void PossessedBy(AController* NewController) override;

	/** Updates boost and tilt before the base Tick applies MeshTilt. */
	virtual void Tick(float DeltaSeconds) override;

protected:
	/** Parks on the floor (dormant) if bStartParked and unpossessed; otherwise starts flying. */
	virtual void BeginPlay() override;

	/** The body yaws toward the controller's aim at the archetype's turn rate; pitch and bank stay visual. */
	virtual void ConfigureRotationForController() override;

private:
	/** @return The archetype's flight tuning, or defaults without an archetype. */
	const FMetalFlightTuning& GetFlight() const;

	/**
	 * Drains or refills the boost meter and switches the speed caps between cruise and boost.
	 *
	 * @param DeltaSeconds Frame time.
	 * @param Now          Current world time.
	 */
	void TickBoost(float DeltaSeconds, float Now);

	/**
	 * Computes the visual body pitch (follows the aim), bank (from strafe speed and turn rate) and dodge
	 * roll, and stores them in MeshTilt.
	 *
	 * @param DeltaSeconds Frame time.
	 * @param Now          Current world time.
	 */
	void TickTilt(float DeltaSeconds, float Now);

	bool bParked = false;
	bool bBoostHeld = false;
	bool bBoosting = false;
	float BoostMeter = 1.f;
	float LastBoostTime = -100.f;
	/** Visual body pitch (the capsule stays upright) and bank, degrees. */
	float BodyPitch = 0.f;
	float Bank = 0.f;
	/** Yaw last frame, to measure the turn rate for banking. */
	float LastYaw = 0.f;
	/** World time the current dodge roll started, or -1. */
	float DodgeStartTime = -1.f;
	float DodgeDirection = 0.f;
};
