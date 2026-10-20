// Brain for AI-piloted Stalkers in the space battles.
//
// Each pilot is a Fighter (hunts enemy flyers, and peels off to defend its own ship) or a Raider (circle-
// strafes the enemy ship's subsystems: shields, then engines and comms, then flies in through the enemy
// hangar mouth for the life support, then the hull). Both fall back on the other job when theirs has no
// target. Movement is steering, not a navmesh: fly at a point, sweep ahead for obstacles, boost over
// long distances, dodge-roll when hit. Hangar portals are navigation links: a pilot whose target is in
// another "space" (open space or a ship's interior) first flies to the portal and through it.

#pragma once

#include "CoreMinimal.h"
#include "AIController.h"
#include "MetalFlyerAIController.generated.h"

class AMetalBotCharacter;
class AMetalCapitalShip;
class AMetalPortal;
class AMetalStalker;
class UPrimitiveComponent;

/** The job an AI pilot does in a space battle. */
UENUM(BlueprintType)
enum class EMetalPilotRole : uint8
{
	/** Defends its own ship, then hunts enemy flyers anywhere in range. */
	Fighter,
	/** Attacks the enemy ship's subsystems in order, then its hull. */
	Raider
};

/**
 * AI pilot for AMetalStalker. Spawned and assigned a role by AMetalHangar for each AI flyer it launches.
 * Re-thinks its target about every 0.4 s; every tick it routes (through portals if needed), steers into an
 * orbit around the target with obstacle avoidance, leads moving targets and fires in bursts when lined up.
 */
UCLASS()
class AMetalFlyerAIController : public AAIController
{
	GENERATED_BODY()

public:
	/** Enables ticking and keeps the controller attached to its pawn. */
	AMetalFlyerAIController();

	/** Fighter or Raider; set by whoever spawns the pilot. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Metalization|AI")
	EMetalPilotRole PilotRole = EMetalPilotRole::Fighter;

	/** Enemy flyers this close (cm) are engaged. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Metalization|AI")
	float EngageRange = 300000.f;

	/** Fighters break off to defend their ship from enemies this close to it (cm). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Metalization|AI")
	float DefendRadius = 80000.f;

	/** Raiders circle a subsystem at this distance (cm). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Metalization|AI")
	float StrafeRange = 22000.f;

	/**
	 * Only shoot inside this range (cm) and when the guns are within FireCone degrees of the lead point.
	 * Keep it well past StrafeRange: orbiting pilots settle a little outside their standoff.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Metalization|AI")
	float FireRange = 45000.f;

	/** Half-angle in degrees between the guns and the aim point inside which the pilot fires. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Metalization|AI")
	float FireCone = 8.f;

	/**
	 * Called by the Stalker when it takes damage: occasionally dodge-rolls, and fighters retarget the attacker.
	 *
	 * @param Attacker Robot that dealt the damage, or null.
	 */
	void NotifyDamaged(AMetalBotCharacter* Attacker);

	/** What the pilot is doing, for logs and the autopilot trace. */
	FString DescribeState() const;

	/** Thinks on a timer, then routes, steers and fires at the current target. */
	virtual void Tick(float DeltaSeconds) override;

protected:
	/** Randomises the orbit phase and direction so pilots don't fly in formation. */
	virtual void OnPossess(APawn* InPawn) override;

private:
	/** A place a target or pilot can be: open space, or a ship's interior (index into the ship list). */
	struct FSpace
	{
		/** Ship whose interior this is, or null for open space. */
		const AMetalCapitalShip* Interior = nullptr;
		bool operator==(const FSpace& Other) const { return Interior == Other.Interior; }
	};

	/** @return The possessed Stalker, or null. */
	AMetalStalker* GetStalker() const;

	/**
	 * @param Location World point.
	 * @return The interior containing it (within InteriorRadius of a ship's InteriorLocation), or open space.
	 */
	FSpace SpaceOf(const FVector& Location) const;

	/** Keeps a valid target or picks a new one by role (see EMetalPilotRole). */
	void ChooseTarget();

	/**
	 * @param Near  Point to measure from.
	 * @param Range Maximum distance in cm.
	 * @return The closest piloted, hostile, flying Stalker within Range of Near, or null.
	 */
	AMetalBotCharacter* FindEnemyFlyer(const FVector& Near, float Range) const;

	/**
	 * @return The nearest surviving subsystem of the enemy ship in priority order (shields, engines, comms,
	 *         life support), the ship's hull once they're all gone, or null if there's no enemy ship left.
	 */
	UPrimitiveComponent* FindRaidTarget() const;

	/**
	 * The point to fly toward this frame: the target, or the next portal on the way to it.
	 *
	 * @param Goal       Where the pilot ultimately wants to be.
	 * @param bOutDirect Set to true when Goal is in the same space, so the pilot can fight; false while routing.
	 * @return The waypoint to steer at.
	 */
	FVector RouteTo(const FVector& Goal, bool& bOutDirect);

	/**
	 * Applies movement input towards MovePoint (avoiding obstacles, easing off on arrival) and turns the
	 * nose towards LookPoint.
	 *
	 * @param MovePoint    Where to fly.
	 * @param LookPoint    Where to point the guns.
	 * @param DeltaSeconds Frame time.
	 * @param bBoost       Allow boosting on long, straight legs.
	 */
	void Steer(const FVector& MovePoint, const FVector& LookPoint, float DeltaSeconds, bool bBoost);

	/**
	 * Sweeps ahead along Direction and bends it away from anything solid.
	 *
	 * @param Direction Desired unit direction of travel.
	 * @return Adjusted unit direction (unchanged if the way is clear).
	 */
	FVector AvoidObstacles(const FVector& Direction) const;

	/**
	 * Fires in bursts while the target is in range, within FireCone and not blocked by something else.
	 *
	 * @param AimPoint The (lead) point being aimed at.
	 * @param Now      Current world time.
	 */
	void UpdateFiring(const FVector& AimPoint, float Now);

	TWeakObjectPtr<AMetalBotCharacter> TargetFlyer;
	TWeakObjectPtr<UPrimitiveComponent> TargetPart;
	TWeakObjectPtr<AMetalBotCharacter> LastAttacker;
	/** Portal currently being flown through, and whether we've lined up in front of it. */
	TWeakObjectPtr<AMetalPortal> RoutePortal;
	bool bRouteLinedUp = false;
	float NextThinkTime = 0.f;
	float NextDodgeTime = 0.f;
	float BurstToggleTime = 0.f;
	bool bBurstFiring = false;
	/** Angle driving the orbit's sway around the target. */
	float OrbitPhase = 0.f;
	/** +1 or -1: which way this pilot circles. */
	float OrbitDirection = 1.f;
};
