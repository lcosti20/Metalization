// Enemy (and allied) robot brain: a small, readable state machine in C++.
//
// Patrol -> Investigate -> Combat (strafe at preferred range, burst fire) -> Retreat, plus
// Stunned / Disabled-weapon (melee charge) handling and squad alerts. Navigation uses the runtime
// navmesh. Kept native for now so possession can swap controllers safely; a Behavior Tree or
// StateTree can replace Tick() later without touching the character.

#pragma once

#include "CoreMinimal.h"
#include "AIController.h"
#include "MetalBotAIController.generated.h"

class AMetalBotCharacter;

/** States of the walking-robot AI. Each has a matching Tick* function in AMetalBotAIController. */
UENUM(BlueprintType)
enum class EMetalAIState : uint8
{
	/** Wander near Home at patrol speed, watching for enemies. */
	Patrol,
	/** Walk to the last known enemy position and look around before going back to patrol. */
	Investigate,
	/** Burst-fire at Target while strafing at the preferred range (or charge in for melee). */
	Combat,
	/** Run away from the target once, at low health, then rejoin the fight. */
	Retreat,
	/** The pawn died; the controller does nothing more. */
	Dead
};

/**
 * Default AI controller for walking robots (AMetalBotCharacter::AIControllerClass). Senses enemies five
 * times a second by sight cone and line-of-sight trace, runs the EMetalAIState machine every tick, and
 * drives the robot only through its public API (SetAITarget, StartFire/StopFire) plus navmesh moves.
 *
 * @see AMetalFlyerAIController for the flying equivalent.
 */
UCLASS()
class AMetalBotAIController : public AAIController
{
	GENERATED_BODY()

public:
	/** Enables ticking and keeps the controller attached to its pawn. */
	AMetalBotAIController();

	/**
	 * Called by the robot whenever it takes damage. A hostile, living attacker becomes the target and the
	 * squad is alerted, even for docile robots.
	 *
	 * @param Attacker Robot that dealt the damage, or null if unknown.
	 */
	void NotifyDamaged(AMetalBotCharacter* Attacker);

	/** Called by the robot when it dies: stops moving and enters the Dead state. */
	void NotifyPawnDied();

	/**
	 * Squad alert from an ally that has spotted Enemy. Ignored if already fighting.
	 *
	 * @param Enemy The enemy to fight.
	 */
	void Alert(AMetalBotCharacter* Enemy);

	/** @return Current AI state. */
	UFUNCTION(BlueprintPure, Category="Metalization|AI")
	EMetalAIState GetState() const { return State; }

	/** Senses on a timer, then runs the current state's Tick* function. Idle while stunned or dormant. */
	virtual void Tick(float DeltaSeconds) override;

protected:
	/** Remembers the spawn point as Home (the patrol centre) and starts patrolling. */
	virtual void OnPossess(APawn* InPawn) override;

private:
	/** @return The possessed robot, or null. */
	AMetalBotCharacter* GetBot() const;

	/** Refreshes the current target's visibility and looks for a better (closer, or active) enemy. */
	void Sense();

	/**
	 * @param Other Robot to look at.
	 * @return True if nothing blocks a Visibility trace from this robot's eyes to Other's centre.
	 */
	bool HasLineOfSight(const AMetalBotCharacter* Other) const;

	/**
	 * Targets Enemy and switches to Combat. Passive robots ignore this.
	 *
	 * @param Enemy       Robot to fight.
	 * @param bAlertSquad Also alert same-team allies within the archetype's SquadAlertRadius.
	 */
	void EnterCombat(AMetalBotCharacter* Enemy, bool bAlertSquad);

	/**
	 * Changes state, stops firing and sets the walk speed (slower while patrolling). No-op if unchanged.
	 *
	 * @param NewState State to enter.
	 */
	void SetState(EMetalAIState NewState);

	/**
	 * Picks a random nearby point to walk to every few seconds.
	 *
	 * @param Now Current world time.
	 */
	void TickPatrol(float Now);

	/**
	 * Walks to LastKnownLocation, waits, then returns to Patrol.
	 *
	 * @param Now Current world time.
	 */
	void TickInvestigate(float Now);

	/**
	 * Aims, burst-fires and strafes; chases when sight is lost; charges into melee when disarmed.
	 *
	 * @param Now Current world time.
	 */
	void TickCombat(float Now);

	/**
	 * Runs directly away for four seconds, then fights again.
	 *
	 * @param Now Current world time.
	 */
	void TickRetreat(float Now);

	/**
	 * Moves to a random navmesh point that is reachable and within Radius of Center.
	 *
	 * @param Center  Centre of the search.
	 * @param Radius  Search radius in cm.
	 * @param bStrafe Keep facing the focus (the target) while moving.
	 * @return True if a move request was started.
	 */
	bool MoveToRandomPointNear(const FVector& Center, float Radius, bool bStrafe);

	/** Enemy being fought, if any. */
	TWeakObjectPtr<AMetalBotCharacter> Target;
	EMetalAIState State = EMetalAIState::Patrol;
	/** Where the robot was when possessed; the centre of its patrol. */
	FVector Home = FVector::ZeroVector;
	/** Where the target was last seen (or where an attack came from). */
	FVector LastKnownLocation = FVector::ZeroVector;
	float NextSenseTime = 0.f;
	/** World time after which the current state may issue a new move request. */
	float NextMoveTime = 0.f;
	float LastSeenTime = -100.f;
	float StateStartTime = 0.f;
	/** World time to flip between firing and pausing in a burst. */
	float BurstToggleTime = 0.f;
	bool bBurstFiring = false;
	bool bTargetVisible = false;
	/** Robots only retreat once per life. */
	bool bHasRetreated = false;
};
