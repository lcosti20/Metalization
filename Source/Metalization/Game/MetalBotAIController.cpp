#include "MetalBotAIController.h"

#include "MetalBotArchetype.h"
#include "MetalBotCharacter.h"
#include "EngineUtils.h"
#include "NavigationSystem.h"
#include "Navigation/PathFollowingComponent.h"
#include "GameFramework/CharacterMovementComponent.h"

namespace
{
	/** Seconds between Sense() calls. */
	constexpr float SenseInterval = 0.2f;
	/** Enemies closer than this are noticed even outside the sight cone. */
	constexpr float ProximityAwareness = 500.f;
	/** Distance in cm at which a disarmed robot starts swinging. */
	constexpr float MeleeReach = 170.f;
}

AMetalBotAIController::AMetalBotAIController()
{
	PrimaryActorTick.bCanEverTick = true;
	bAttachToPawn = true;
}

AMetalBotCharacter* AMetalBotAIController::GetBot() const
{
	return Cast<AMetalBotCharacter>(GetPawn());
}

void AMetalBotAIController::OnPossess(APawn* InPawn)
{
	Super::OnPossess(InPawn);
	Home = InPawn->GetActorLocation();
	LastKnownLocation = Home;
	SetState(EMetalAIState::Patrol);
	NextMoveTime = GetWorld()->GetTimeSeconds() + FMath::FRandRange(0.5f, 2.f);
}

void AMetalBotAIController::SetState(EMetalAIState NewState)
{
	if (State != NewState)
	{
		State = NewState;
		StateStartTime = GetWorld()->GetTimeSeconds();
		NextMoveTime = 0.f;
		if (AMetalBotCharacter* Bot = GetBot())
		{
			Bot->StopFire();
			const float Speed = Bot->Archetype ? Bot->Archetype->WalkSpeed : 500.f;
			Bot->GetCharacterMovement()->MaxWalkSpeed = NewState == EMetalAIState::Patrol ? Speed * Bot->PatrolSpeedFraction : Speed;
		}
		bBurstFiring = false;
	}
}

void AMetalBotAIController::NotifyDamaged(AMetalBotCharacter* Attacker)
{
	AMetalBotCharacter* Bot = GetBot();
	if (Bot && Attacker && Bot->IsHostileTo(Attacker) && Attacker->IsAlive())
	{
		LastKnownLocation = Attacker->GetActorLocation();
		if (State != EMetalAIState::Combat || !Target.IsValid())
		{
			EnterCombat(Attacker, true);
		}
	}
}

void AMetalBotAIController::NotifyPawnDied()
{
	StopMovement();
	ClearFocus(EAIFocusPriority::Gameplay);
	SetState(EMetalAIState::Dead);
}

void AMetalBotAIController::Alert(AMetalBotCharacter* Enemy)
{
	if (State != EMetalAIState::Combat && State != EMetalAIState::Dead && Enemy)
	{
		LastKnownLocation = Enemy->GetActorLocation();
		EnterCombat(Enemy, false);
	}
}

void AMetalBotAIController::EnterCombat(AMetalBotCharacter* Enemy, bool bAlertSquad)
{
	AMetalBotCharacter* Bot = GetBot();
	if (Bot == nullptr || Bot->bPassive)
	{
		return;
	}
	Bot->bDocileUntilAttacked = false;
	Target = Enemy;
	LastSeenTime = GetWorld()->GetTimeSeconds();
	SetState(EMetalAIState::Combat);
	SetFocus(Enemy);

	if (bAlertSquad && Bot->Archetype)
	{
		const float RadiusSq = FMath::Square(Bot->Archetype->SquadAlertRadius);
		for (TActorIterator<AMetalBotCharacter> It(GetWorld()); It; ++It)
		{
			AMetalBotCharacter* Ally = *It;
			if (Ally != Bot && Ally->IsAlive() && Ally->GetTeam() == Bot->GetTeam() && FVector::DistSquared(Ally->GetActorLocation(), Bot->GetActorLocation()) < RadiusSq)
			{
				if (AMetalBotAIController* AllyAI = Cast<AMetalBotAIController>(Ally->GetController()))
				{
					AllyAI->Alert(Enemy);
				}
			}
		}
	}
}

bool AMetalBotAIController::HasLineOfSight(const AMetalBotCharacter* Other) const
{
	const AMetalBotCharacter* Bot = GetBot();
	FCollisionQueryParams Params(SCENE_QUERY_STAT(MetalAISight), false, Bot);
	Params.AddIgnoredActor(Other);
	FHitResult Hit;
	return !GetWorld()->LineTraceSingleByChannel(Hit, Bot->GetEyesLocation(), Other->GetActorLocation(), ECC_Visibility, Params);
}

void AMetalBotAIController::Sense()
{
	AMetalBotCharacter* Bot = GetBot();
	if (Bot == nullptr || Bot->Archetype == nullptr || Bot->bPassive || Bot->bDocileUntilAttacked)
	{
		return;
	}

	// Keep tracking the current target.
	if (AMetalBotCharacter* Current = Target.Get())
	{
		if (!Current->IsAlive() || !Bot->IsHostileTo(Current))
		{
			Target.Reset();
			ClearFocus(EAIFocusPriority::Gameplay);
			bTargetVisible = false;
			SetState(EMetalAIState::Investigate);
		}
		else
		{
			bTargetVisible = HasLineOfSight(Current);
			if (bTargetVisible)
			{
				LastSeenTime = GetWorld()->GetTimeSeconds();
				LastKnownLocation = Current->GetActorLocation();
			}
		}
	}

	// Look for a closer or first enemy.
	const FVector Eyes = Bot->GetEyesLocation();
	const FVector Forward = Bot->GetActorForwardVector();
	const float CosHalfAngle = FMath::Cos(FMath::DegreesToRadians(Bot->Archetype->SightHalfAngle));
	AMetalBotCharacter* Best = nullptr;
	float BestDistance = Bot->Archetype->SightRange;
	for (TActorIterator<AMetalBotCharacter> It(GetWorld()); It; ++It)
	{
		AMetalBotCharacter* Other = *It;
		if (!Other->IsAlive() || !Bot->IsHostileTo(Other))
		{
			continue;
		}
		const FVector ToOther = Other->GetActorLocation() - Eyes;
		// Powered-down bodies count as further away, so active threats win.
		const float Distance = ToOther.Size() * (Other->IsDormant() ? 2.f : 1.f);
		const bool bInCone = FVector::DotProduct(ToOther / FMath::Max(Distance, 1.f), Forward) > CosHalfAngle;
		if (Distance < BestDistance && (bInCone || Distance < ProximityAwareness) && HasLineOfSight(Other))
		{
			Best = Other;
			BestDistance = Distance;
		}
	}
	// Only switch targets when not already fighting, or when the current target is a powered-down body
	// (the player's own body while they drive another robot) and a live threat is in view.
	const AMetalBotCharacter* Current = Target.Get();
	const bool bCurrentIsHarmless = Current && Current->IsDormant() && Best && !Best->IsDormant();
	if (Best && (State != EMetalAIState::Combat || Current == nullptr || bCurrentIsHarmless))
	{
		EnterCombat(Best, Current == nullptr);
	}
}

bool AMetalBotAIController::MoveToRandomPointNear(const FVector& Center, float Radius, bool bStrafe)
{
	UNavigationSystemV1* Nav = FNavigationSystem::GetCurrent<UNavigationSystemV1>(GetWorld());
	FNavLocation Point;
	if (Nav && Nav->GetRandomReachablePointInRadius(Center, Radius, Point))
	{
		return MoveToLocation(Point.Location, 50.f, true, true, false, bStrafe) != EPathFollowingRequestResult::Failed;
	}
	return false;
}

void AMetalBotAIController::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	AMetalBotCharacter* Bot = GetBot();
	if (Bot == nullptr || !Bot->IsAlive() || State == EMetalAIState::Dead)
	{
		return;
	}
	if (Bot->IsStunned() || Bot->IsDormant())
	{
		StopMovement();
		Bot->StopFire();
		return;
	}

	const float Now = GetWorld()->GetTimeSeconds();
	if (Now >= NextSenseTime)
	{
		NextSenseTime = Now + SenseInterval;
		Sense();
	}

	switch (State)
	{
	case EMetalAIState::Patrol:      TickPatrol(Now); break;
	case EMetalAIState::Investigate: TickInvestigate(Now); break;
	case EMetalAIState::Combat:      TickCombat(Now); break;
	case EMetalAIState::Retreat:     TickRetreat(Now); break;
	default: break;
	}
}

void AMetalBotAIController::TickPatrol(float Now)
{
	AMetalBotCharacter* Bot = GetBot();
	Bot->SetAITarget(FVector::ZeroVector, false);
	if (Bot->PatrolRadius <= 0.f || Now < NextMoveTime)
	{
		return;
	}
	if (GetMoveStatus() == EPathFollowingStatus::Idle)
	{
		MoveToRandomPointNear(Home, Bot->PatrolRadius, false);
		NextMoveTime = Now + FMath::FRandRange(3.f, 6.f);
	}
}

void AMetalBotAIController::TickInvestigate(float Now)
{
	if (Now - StateStartTime < 0.1f)
	{
		MoveToLocation(LastKnownLocation, 100.f);
	}
	// Look around at the last known position for a few seconds, then go back to patrolling.
	if (GetMoveStatus() == EPathFollowingStatus::Idle && Now - StateStartTime > 6.f)
	{
		SetState(EMetalAIState::Patrol);
	}
}

void AMetalBotAIController::TickCombat(float Now)
{
	AMetalBotCharacter* Bot = GetBot();
	AMetalBotCharacter* Enemy = Target.Get();
	if (Enemy == nullptr)
	{
		SetState(EMetalAIState::Investigate);
		return;
	}
	const UMetalBotArchetype* Archetype = Bot->Archetype;
	const float Distance = FVector::Dist(Bot->GetActorLocation(), Enemy->GetActorLocation());

	if (Archetype->RetreatHealthFraction > 0.f && !bHasRetreated && Bot->GetHealth() < Bot->GetMaxHealth() * Archetype->RetreatHealthFraction)
	{
		bHasRetreated = true;
		SetState(EMetalAIState::Retreat);
		return;
	}

	// Aim at the enemy's chest; the character applies the archetype's aim error.
	Bot->SetAITarget(Enemy->GetActorLocation() + FVector(0.f, 0.f, Enemy->BaseEyeHeight * 0.3f), true);

	if (Bot->IsWeaponDisabled())
	{
		// Lost the gun arm: charge and swing.
		if (Now >= NextMoveTime)
		{
			MoveToActor(Enemy, MeleeReach * 0.5f, true, true, true);
			NextMoveTime = Now + 0.5f;
		}
		if (Distance < MeleeReach + Bot->GetSimpleCollisionRadius())
		{
			Bot->StartFire();
		}
		else
		{
			Bot->StopFire();
		}
		return;
	}

	// Lost sight: hold fire, give it a moment, then go to where the enemy was seen. After 12 s, give up.
	if (!bTargetVisible)
	{
		Bot->StopFire();
		if (Now - LastSeenTime > 2.5f && Now >= NextMoveTime)
		{
			MoveToLocation(LastKnownLocation, 150.f, true, true, true);
			NextMoveTime = Now + 2.f;
		}
		if (Now - LastSeenTime > 12.f)
		{
			Target.Reset();
			ClearFocus(EAIFocusPriority::Gameplay);
			SetState(EMetalAIState::Investigate);
		}
		return;
	}

	// Burst fire.
	if (Now >= BurstToggleTime)
	{
		bBurstFiring = !bBurstFiring;
		BurstToggleTime = Now + (bBurstFiring ? Archetype->BurstDuration : Archetype->BurstPause) * FMath::FRandRange(0.7f, 1.3f);
	}
	if (bBurstFiring)
	{
		Bot->StartFire();
	}
	else
	{
		Bot->StopFire();
	}

	// Reposition: strafe around the target at the preferred range.
	if (Now >= NextMoveTime)
	{
		NextMoveTime = Now + FMath::FRandRange(1.8f, 3.5f);
		const FVector Away = (Bot->GetActorLocation() - Enemy->GetActorLocation()).GetSafeNormal2D();
		const FVector Side = FVector::CrossProduct(Away, FVector::UpVector) * (FMath::RandBool() ? 1.f : -1.f);
		// Aim for a point at the preferred range, offset sideways by half that range so the robot circles
		// the target instead of backing straight off; the clamp keeps it from drifting beyond the range.
		const FVector Desired = Enemy->GetActorLocation() + (Away * Archetype->PreferredRange + Side * Archetype->PreferredRange * 0.5f).GetClampedToMaxSize(Archetype->PreferredRange);
		MoveToRandomPointNear(Desired, 250.f, true);
	}
}

void AMetalBotAIController::TickRetreat(float Now)
{
	AMetalBotCharacter* Bot = GetBot();
	Bot->StopFire();
	if (Now - StateStartTime < 0.1f)
	{
		const AMetalBotCharacter* Enemy = Target.Get();
		const FVector From = Enemy ? Enemy->GetActorLocation() : LastKnownLocation;
		const FVector Away = (Bot->GetActorLocation() - From).GetSafeNormal2D();
		MoveToRandomPointNear(Bot->GetActorLocation() + Away * 900.f, 300.f, false);
	}
	if (Now - StateStartTime > 4.f)
	{
		if (AMetalBotCharacter* Enemy = Target.Get())
		{
			EnterCombat(Enemy, false);
		}
		else
		{
			SetState(EMetalAIState::Investigate);
		}
	}
}
