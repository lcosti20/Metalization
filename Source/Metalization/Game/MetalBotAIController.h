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

UENUM(BlueprintType)
enum class EMetalAIState : uint8
{
	Patrol,
	Investigate,
	Combat,
	Retreat,
	Dead
};

UCLASS()
class AMetalBotAIController : public AAIController
{
	GENERATED_BODY()

public:
	AMetalBotAIController();

	void NotifyDamaged(AMetalBotCharacter* Attacker);
	void NotifyPawnDied();
	/** Squad alert from an ally that has spotted Enemy. */
	void Alert(AMetalBotCharacter* Enemy);

	UFUNCTION(BlueprintPure, Category="Metalization|AI")
	EMetalAIState GetState() const { return State; }

	virtual void Tick(float DeltaSeconds) override;

protected:
	virtual void OnPossess(APawn* InPawn) override;

private:
	AMetalBotCharacter* GetBot() const;
	void Sense();
	bool HasLineOfSight(const AMetalBotCharacter* Other) const;
	void EnterCombat(AMetalBotCharacter* Enemy, bool bAlertSquad);
	void SetState(EMetalAIState NewState);
	void TickPatrol(float Now);
	void TickInvestigate(float Now);
	void TickCombat(float Now);
	void TickRetreat(float Now);
	bool MoveToRandomPointNear(const FVector& Center, float Radius, bool bStrafe);

	TWeakObjectPtr<AMetalBotCharacter> Target;
	EMetalAIState State = EMetalAIState::Patrol;
	FVector Home = FVector::ZeroVector;
	FVector LastKnownLocation = FVector::ZeroVector;
	float NextSenseTime = 0.f;
	float NextMoveTime = 0.f;
	float LastSeenTime = -100.f;
	float StateStartTime = 0.f;
	float BurstToggleTime = 0.f;
	bool bBurstFiring = false;
	bool bTargetVisible = false;
	bool bHasRetreated = false;
};
