#include "MetalBotAIController.h"

#include "MetalAudio.h"
#include "MetalBotArchetype.h"
#include "MetalBotCharacter.h"
#include "MetalEnergyField.h"
#include "MetalTypes.h"
#include "MetalWeaponData.h"
#include "EngineUtils.h"
#include "NavigationPath.h"
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
		if (NewState == EMetalAIState::Patrol)
		{
			// Done looking at whatever caught its attention.
			ClearFocus(EAIFocusPriority::Gameplay);
		}
		if (AMetalBotCharacter* Bot = GetBot())
		{
			Bot->StopFire();
			Bot->StopSecondaryFire();
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
	if (State != EMetalAIState::Combat && Enemy && Enemy->IsPlayerControlled())
	{
		// Caught the player: the HUD shows a "!" over this robot for a moment.
		AlertTime = GetWorld()->GetTimeSeconds();
		MetalAudio::PlayAt(this, TEXT("alert"), Bot->GetEyesLocation(), 0.8f, 0.f);
	}
	if (State != EMetalAIState::Combat && State != EMetalAIState::Retreat && Enemy && bAlertSquad)
	{
		// Only the robot that spotted the enemy calls out; the squad it alerts doesn't all shout at once.
		Bot->Speak(EMetalBark::Alert);
		NextBarkTime = GetWorld()->GetTimeSeconds() + FMath::FRandRange(3.f, 7.f);
	}
	Suspicion = 0.f;
	Target = Enemy;
	// Decide straight away whether an energy field is in the way, so the first burst isn't wasted on it
	// before the next Sense; the path check follows in Sense.
	bFieldBlocksShot = Enemy && HasLineOfSight(Enemy) && IsShotBlockedByField(Enemy);
	bTargetReachable = false;
	bReachKnown = false;
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
	AMetalEnergyField::IgnoreAll(GetWorld(), Params);
	FHitResult Hit;
	return !GetWorld()->LineTraceSingleByChannel(Hit, Bot->GetEyesLocation(), Other->GetActorLocation(), ECC_Visibility, Params);
}

bool AMetalBotAIController::IsShotBlockedByField(const AMetalBotCharacter* Other) const
{
	const AMetalBotCharacter* Bot = GetBot();
	FCollisionQueryParams Params(SCENE_QUERY_STAT(MetalAIFieldCheck), false, Bot);
	Params.AddIgnoredActor(Other);
	FHitResult Hit;
	return GetWorld()->LineTraceSingleByChannel(Hit, Bot->GetEyesLocation(), Other->GetActorLocation(), ECC_Visibility, Params)
		&& Hit.GetActor() && Hit.GetActor()->IsA<AMetalEnergyField>();
}

bool AMetalBotAIController::CanReach(const AMetalBotCharacter* Other) const
{
	const AMetalBotCharacter* Bot = GetBot();
	const UNavigationPath* Path = UNavigationSystemV1::FindPathToActorSynchronously(GetWorld(), Bot->GetNavAgentLocation(), const_cast<AMetalBotCharacter*>(Other), 0.f, const_cast<AMetalBotCharacter*>(Bot));
	return Path && Path->IsValid() && !Path->IsPartial();
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
			bFieldBlocksShot = false;
			bReachKnown = false;
			SetState(EMetalAIState::Investigate);
		}
		else
		{
			bTargetVisible = HasLineOfSight(Current);
			bFieldBlocksShot = bTargetVisible && IsShotBlockedByField(Current);
			if (bTargetVisible)
			{
				LastSeenTime = GetWorld()->GetTimeSeconds();
				LastKnownLocation = Current->GetActorLocation();
			}
			if (!bFieldBlocksShot)
			{
				bReachKnown = false;
			}
			else if (!bReachKnown || GetWorld()->GetTimeSeconds() >= NextReachCheckTime)
			{
				NextReachCheckTime = GetWorld()->GetTimeSeconds() + 1.f;
				const bool bWasReachable = bTargetReachable;
				bTargetReachable = CanReach(Current);
				if (!bReachKnown || bWasReachable != bTargetReachable)
				{
					UE_LOG(LogMetal, Log, TEXT("[AI] %s sees %s through an energy field: holding fire, %s"), *GetBot()->GetActorNameOrLabel(),
						*Current->GetActorNameOrLabel(), bTargetReachable ? TEXT("going round") : TEXT("no path, holding position"));
				}
				bReachKnown = true;
			}
		}
	}

	// Look for a closer or first enemy.
	const FVector Eyes = Bot->GetEyesLocation();
	const FVector Forward = Bot->GetActorForwardVector();
	const float CosHalfAngle = FMath::Cos(FMath::DegreesToRadians(Bot->Archetype->SightHalfAngle));
	AMetalBotCharacter* Best = nullptr;
	float BestDistance = Bot->Archetype->SightRange;
	float BestRealDistance = 0.f;
	for (TActorIterator<AMetalBotCharacter> It(GetWorld()); It; ++It)
	{
		AMetalBotCharacter* Other = *It;
		if (!Other->IsAlive() || !Bot->IsHostileTo(Other))
		{
			continue;
		}
		const FVector ToOther = Other->GetActorLocation() - Eyes;
		const float RealDistance = ToOther.Size();
		// Powered-down bodies count as further away, so active threats win. A sneaking robot is a small,
		// still shape: it is only seen at about 60% of the usual range and isn't felt behind the back.
		const float Distance = RealDistance * (Other->IsDormant() ? 2.f : 1.f) * (Other->IsSneaking() ? 1.7f : 1.f);
		const float Proximity = Other->IsSneaking() ? ProximityAwareness * 0.35f : ProximityAwareness;
		const bool bInCone = FVector::DotProduct(ToOther / FMath::Max(RealDistance, 1.f), Forward) > CosHalfAngle;
		if (Distance < BestDistance && (bInCone || RealDistance < Proximity) && HasLineOfSight(Other))
		{
			Best = Other;
			BestDistance = Distance;
			BestRealDistance = RealDistance;
		}
	}

	// The player isn't spotted at a glance: suspicion builds while they stay in view (see BuildSuspicion).
	// Other robots - including the player's allies - are fought on sight.
	const bool bSuspect = Best && Best->IsPlayerControlled() && State != EMetalAIState::Combat;
	bSuspectInView = bSuspect;
	if (bSuspect)
	{
		BuildSuspicion(Best, BestRealDistance);
		return;
	}
	if (Suspicion > 0.f)
	{
		Suspicion = FMath::Max(0.f, Suspicion - SenseInterval * 0.25f);
		if (Suspicion <= 0.f && State == EMetalAIState::Patrol)
		{
			ClearFocus(EAIFocusPriority::Gameplay);
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

void AMetalBotAIController::BuildSuspicion(AMetalBotCharacter* Suspect, float Distance)
{
	AMetalBotCharacter* Bot = GetBot();
	const float Now = GetWorld()->GetTimeSeconds();
	// Seconds of continuous sight before attacking: under half a second up close, ~3.5 s at the edge of
	// sight. Sneaking stretches that, sprinting and shooting shorten it, and a robot of our own side
	// (hijacked by the player) passes at a glance unless it does something stupid.
	float Seconds = FMath::GetMappedRangeValueClamped(FVector2D(300.f, Bot->Archetype->SightRange), FVector2D(0.4f, 3.5f), Distance);
	if (Suspect->IsSneaking())
	{
		Seconds *= 2.5f;
	}
	if (Suspect->IsSprinting())
	{
		Seconds *= 0.6f;
	}
	if (Now - Suspect->GetLastFireTime() < 1.5f)
	{
		Seconds *= 0.25f;
	}
	if (Suspect->IsHijacked() && Suspect->GetBaseTeam() == Bot->GetTeam())
	{
		Seconds *= 4.f;
	}
	const float Before = Suspicion;
	Suspicion = FMath::Min(1.f, Suspicion + SenseInterval / FMath::Max(Seconds, 0.05f));
	LastKnownLocation = Suspect->GetActorLocation();
	if (Suspicion >= 1.f)
	{
		EnterCombat(Suspect, true);
		return;
	}
	if (Before <= 0.f)
	{
		MetalAudio::PlayAt(this, TEXT("suspicion"), Bot->GetEyesLocation(), 0.6f, 0.f);
	}
	// Something's there: stop and stare, and past halfway walk over to get a better look.
	SetFocus(Suspect);
	if (State == EMetalAIState::Patrol)
	{
		StopMovement();
		if (Suspicion > 0.5f)
		{
			SetState(EMetalAIState::Investigate);
		}
	}
}

void AMetalBotAIController::HearNoise(const FVector& Location)
{
	AMetalBotCharacter* Bot = GetBot();
	if (Bot == nullptr || !Bot->IsAlive() || Bot->bPassive || Bot->bDocileUntilAttacked || Bot->IsDormant()
		|| State == EMetalAIState::Combat || State == EMetalAIState::Retreat || State == EMetalAIState::Dead)
	{
		return;
	}
	LastKnownLocation = Location;
	Suspicion = FMath::Max(Suspicion, 0.35f);
	SetFocalPoint(Location);
	if (State == EMetalAIState::Investigate)
	{
		// Already checking something out: head for the new noise instead and keep looking a while longer.
		StateStartTime = GetWorld()->GetTimeSeconds();
		MoveToLocation(Location, 100.f);
	}
	else
	{
		SetState(EMetalAIState::Investigate);
	}
}

void AMetalBotAIController::ReportNoise(AMetalBotCharacter* Instigator, const FVector& Location, float Radius)
{
	UWorld* World = Instigator ? Instigator->GetWorld() : nullptr;
	if (World == nullptr || Radius <= 0.f)
	{
		return;
	}
	const float RadiusSq = FMath::Square(Radius);
	for (TActorIterator<AMetalBotAIController> It(World); It; ++It)
	{
		const AMetalBotCharacter* Listener = It->GetBot();
		if (Listener && Listener->IsHostileTo(Instigator) && FVector::DistSquared(Listener->GetActorLocation(), Location) < RadiusSq)
		{
			It->HearNoise(Location);
		}
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
	// EMP-stunned or switched-off robots stand still and hold fire, keeping their state for when they wake.
	const float Now = GetWorld()->GetTimeSeconds();
	if (NextBarkTime <= 0.f)
	{
		// Stagger the first lines, so a squad that spawned together doesn't speak in chorus.
		NextBarkTime = Now + FMath::FRandRange(2.f, 14.f);
	}
	if (Bot->IsStunned() || Bot->IsDormant())
	{
		StopMovement();
		Bot->StopFire();
		Bot->StopSecondaryFire();
		// A long stun: keep sounding dazed.
		if (Bot->IsStunned() && Now >= NextBarkTime)
		{
			Bot->Speak(EMetalBark::Confused);
			NextBarkTime = Now + FMath::FRandRange(2.5f, 4.f);
		}
		return;
	}

	// Unprompted lines: muttering on patrol (not while something has its attention), orders in a fight
	// while the enemy is in view.
	if (Now >= NextBarkTime)
	{
		if (State == EMetalAIState::Patrol && Suspicion <= 0.f)
		{
			Bot->Speak(EMetalBark::Chatter);
			NextBarkTime = Now + FMath::FRandRange(7.f, 16.f);
		}
		else if (State == EMetalAIState::Combat && bTargetVisible)
		{
			Bot->Speak(EMetalBark::Orders);
			NextBarkTime = Now + FMath::FRandRange(4.f, 9.f);
		}
		else
		{
			NextBarkTime = Now + 1.f;
		}
	}
	// Look and listen a few times a second rather than every frame; the state ticks below act on the result.
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
		SetFocalPoint(LastKnownLocation);
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
			Bot->StopSecondaryFire();
		}
		return;
	}

	// Lost sight: hold fire, give it a moment, then go to where the enemy was seen. After 12 s, give up.
	if (!bTargetVisible)
	{
		Bot->StopFire();
		Bot->StopSecondaryFire();
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

	// In sight but behind an energy field: shooting would only ripple it. Walk round to the target if the
	// navmesh has a way (e.g. through an open gate), otherwise stand and watch until something changes.
	if (bFieldBlocksShot)
	{
		Bot->StopFire();
		Bot->StopSecondaryFire();
		bBurstFiring = false;
		if (Now >= NextMoveTime)
		{
			NextMoveTime = Now + 1.f;
			if (bTargetReachable)
			{
				MoveToActor(Enemy, Archetype->PreferredRange * 0.5f, true, true, true);
			}
			else
			{
				StopMovement();
			}
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

	// Built-in secondary weapon (Brute rockets): one shot whenever it's ready and the target is far enough away
	// that the blast won't catch the shooter, with a random extra wait so volleys don't land like clockwork.
	if (Bot->HasSecondaryWeapon() && Now >= NextSecondaryTime && Bot->IsSecondaryReady()
		&& Distance > Archetype->SecondaryWeapon->ExplosionRadius * 1.3f + Bot->GetSimpleCollisionRadius())
	{
		Bot->StartSecondaryFire();
		NextSecondaryTime = Now + FMath::FRandRange(0.5f, 2.f);
	}
	else
	{
		Bot->StopSecondaryFire();
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
	Bot->StopSecondaryFire();
	// On entering the state, run to a random spot about 9 m directly away from the enemy.
	if (Now - StateStartTime < 0.1f)
	{
		const AMetalBotCharacter* Enemy = Target.Get();
		const FVector From = Enemy ? Enemy->GetActorLocation() : LastKnownLocation;
		const FVector Away = (Bot->GetActorLocation() - From).GetSafeNormal2D();
		MoveToRandomPointNear(Bot->GetActorLocation() + Away * 900.f, 300.f, false);
	}
	// After 4 s of falling back, turn and fight again (or go looking if the enemy is gone).
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
