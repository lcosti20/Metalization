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

UENUM(BlueprintType)
enum class EMetalPilotRole : uint8
{
	Fighter,
	Raider
};

UCLASS()
class AMetalFlyerAIController : public AAIController
{
	GENERATED_BODY()

public:
	AMetalFlyerAIController();

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

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Metalization|AI")
	float FireCone = 8.f;

	void NotifyDamaged(AMetalBotCharacter* Attacker);

	/** What the pilot is doing, for logs and the autopilot trace. */
	FString DescribeState() const;

	virtual void Tick(float DeltaSeconds) override;

protected:
	virtual void OnPossess(APawn* InPawn) override;

private:
	/** A place a target or pilot can be: open space, or a ship's interior (index into the ship list). */
	struct FSpace
	{
		const AMetalCapitalShip* Interior = nullptr;
		bool operator==(const FSpace& Other) const { return Interior == Other.Interior; }
	};

	AMetalStalker* GetStalker() const;
	FSpace SpaceOf(const FVector& Location) const;
	void ChooseTarget();
	AMetalBotCharacter* FindEnemyFlyer(const FVector& Near, float Range) const;
	UPrimitiveComponent* FindRaidTarget() const;
	/** The point to fly toward this frame: the target, or the next portal on the way to it. */
	FVector RouteTo(const FVector& Goal, bool& bOutDirect);
	void Steer(const FVector& MovePoint, const FVector& LookPoint, float DeltaSeconds, bool bBoost);
	FVector AvoidObstacles(const FVector& Direction) const;
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
	float OrbitPhase = 0.f;
	float OrbitDirection = 1.f;
};
