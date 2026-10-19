#include "MetalFlyerAIController.h"

#include "MetalBotArchetype.h"
#include "MetalCapitalShip.h"
#include "MetalHangar.h"
#include "MetalPortal.h"
#include "MetalStalker.h"
#include "MetalWeaponData.h"
#include "Components/CapsuleComponent.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/CharacterMovementComponent.h"

namespace
{
	/** Interiors are self-contained rooms this close (cm) to their ship's InteriorLocation. */
	constexpr float InteriorRadius = 60000.f;
	constexpr float ThinkInterval = 0.4f;
	/** Portal routing: line up this far (cm) out in front of the mouth, then fly straight through. */
	constexpr float PortalApproach = 6000.f;
	constexpr float PortalLineUpRadius = 1500.f;
	/** How far ahead (seconds of travel) to sweep for obstacles. */
	constexpr float LookAheadTime = 1.2f;
}

AMetalFlyerAIController::AMetalFlyerAIController()
{
	PrimaryActorTick.bCanEverTick = true;
	bAttachToPawn = true;
}

AMetalStalker* AMetalFlyerAIController::GetStalker() const
{
	return Cast<AMetalStalker>(GetPawn());
}

void AMetalFlyerAIController::OnPossess(APawn* InPawn)
{
	Super::OnPossess(InPawn);
	OrbitPhase = FMath::FRandRange(0.f, UE_TWO_PI);
	OrbitDirection = FMath::RandBool() ? 1.f : -1.f;
	NextThinkTime = 0.f;
}

FString AMetalFlyerAIController::DescribeState() const
{
	const TCHAR* RoleName = PilotRole == EMetalPilotRole::Fighter ? TEXT("fighter") : TEXT("raider");
	if (RoutePortal.IsValid())
	{
		return FString::Printf(TEXT("%s via %s"), RoleName, *RoutePortal->GetName());
	}
	if (const AMetalBotCharacter* Flyer = TargetFlyer.Get())
	{
		return FString::Printf(TEXT("%s -> %s"), RoleName, *Flyer->GetName());
	}
	if (const UPrimitiveComponent* Part = TargetPart.Get())
	{
		return FString::Printf(TEXT("%s -> %s.%s"), RoleName, *GetNameSafe(Part->GetOwner()), *Part->GetName());
	}
	return FString::Printf(TEXT("%s idle"), RoleName);
}

void AMetalFlyerAIController::NotifyDamaged(AMetalBotCharacter* Attacker)
{
	AMetalStalker* Me = GetStalker();
	if (Me == nullptr || Attacker == nullptr || !Me->IsHostileTo(Attacker))
	{
		return;
	}
	LastAttacker = Attacker;
	// Break the attacker's aim with a roll, now and then.
	const float Now = GetWorld()->GetTimeSeconds();
	if (Now >= NextDodgeTime)
	{
		NextDodgeTime = Now + FMath::FRandRange(2.5f, 4.5f);
		Me->Dodge(FMath::RandBool() ? 1.f : -1.f);
	}
	// Fighters turn on whoever is shooting them.
	if (PilotRole == EMetalPilotRole::Fighter && Attacker->IsA<AMetalStalker>())
	{
		TargetFlyer = Attacker;
		TargetPart.Reset();
	}
}

// --------------------------------------------------------------------------------------------
// Targets
// --------------------------------------------------------------------------------------------

AMetalFlyerAIController::FSpace AMetalFlyerAIController::SpaceOf(const FVector& Location) const
{
	for (TActorIterator<AMetalCapitalShip> It(GetWorld()); It; ++It)
	{
		if (FVector::DistSquared(Location, It->InteriorLocation) < FMath::Square(InteriorRadius))
		{
			return {*It};
		}
	}
	return {};
}

AMetalBotCharacter* AMetalFlyerAIController::FindEnemyFlyer(const FVector& Near, float Range) const
{
	const AMetalStalker* Me = GetStalker();
	AMetalBotCharacter* Best = nullptr;
	float BestDistance = Range;
	for (TActorIterator<AMetalStalker> It(GetWorld()); It; ++It)
	{
		// Flying (piloted) enemy Stalkers; parked ones are just hardware.
		if (!It->IsAlive() || It->IsDormant() || It->IsParked() || It->GetController() == nullptr || !Me->IsHostileTo(*It))
		{
			continue;
		}
		const float Distance = FVector::Dist(It->GetActorLocation(), Near);
		if (Distance < BestDistance)
		{
			Best = *It;
			BestDistance = Distance;
		}
	}
	return Best;
}

UPrimitiveComponent* AMetalFlyerAIController::FindRaidTarget() const
{
	const AMetalStalker* Me = GetStalker();
	for (TActorIterator<AMetalCapitalShip> It(GetWorld()); It; ++It)
	{
		if (It->GetTeam() == Me->GetTeam() || It->IsShipDestroyed())
		{
			continue;
		}
		// Shields first (they protect the hull), then engines and comms, then life support, then the hull.
		const EMetalShipSubsystem Order[] = {EMetalShipSubsystem::ShieldGenerator, EMetalShipSubsystem::Engines, EMetalShipSubsystem::Comms, EMetalShipSubsystem::LifeSupport};
		for (EMetalShipSubsystem Type : Order)
		{
			UMetalShipSubsystemComponent* Closest = nullptr;
			for (UMetalShipSubsystemComponent* Part : It->GetSubsystems())
			{
				if (Part->Type == Type && !Part->IsDestroyed() && (Closest == nullptr
					|| FVector::DistSquared(Part->Bounds.Origin, Me->GetActorLocation()) < FVector::DistSquared(Closest->Bounds.Origin, Me->GetActorLocation())))
				{
					Closest = Part;
				}
			}
			if (Closest)
			{
				return Closest;
			}
		}
		return Cast<UPrimitiveComponent>(It->GetRootComponent());
	}
	return nullptr;
}

void AMetalFlyerAIController::ChooseTarget()
{
	const AMetalStalker* Me = GetStalker();
	const AMetalCapitalShip* Home = nullptr;
	for (TActorIterator<AMetalCapitalShip> It(GetWorld()); It; ++It)
	{
		Home = It->GetTeam() == Me->GetTeam() ? *It : Home;
	}

	// Keep a live flyer target while it's in range; otherwise re-pick by role.
	if (const AMetalBotCharacter* Current = TargetFlyer.Get(); Current && Current->IsAlive() && !Current->IsDormant()
		&& FVector::Dist(Current->GetActorLocation(), Me->GetActorLocation()) < EngageRange)
	{
		return;
	}
	TargetFlyer.Reset();
	if (const UPrimitiveComponent* Part = TargetPart.Get(); Part && PilotRole == EMetalPilotRole::Raider)
	{
		const UMetalShipSubsystemComponent* Subsystem = Cast<UMetalShipSubsystemComponent>(Part);
		const AMetalCapitalShip* Ship = Cast<AMetalCapitalShip>(Part->GetOwner());
		if ((Subsystem && !Subsystem->IsDestroyed()) || (!Subsystem && Ship && !Ship->IsShipDestroyed()))
		{
			return;
		}
	}
	TargetPart.Reset();

	if (PilotRole == EMetalPilotRole::Fighter)
	{
		// Defend the ship first, then hunt anywhere in range.
		AMetalBotCharacter* Raider = Home ? FindEnemyFlyer(Home->GetActorLocation(), DefendRadius) : nullptr;
		TargetFlyer = Raider ? Raider : FindEnemyFlyer(Me->GetActorLocation(), EngageRange);
		if (!TargetFlyer.IsValid())
		{
			TargetPart = FindRaidTarget();
		}
	}
	else
	{
		TargetPart = FindRaidTarget();
		if (!TargetPart.IsValid())
		{
			TargetFlyer = FindEnemyFlyer(Me->GetActorLocation(), EngageRange);
		}
	}
}

// --------------------------------------------------------------------------------------------
// Movement
// --------------------------------------------------------------------------------------------

FVector AMetalFlyerAIController::RouteTo(const FVector& Goal, bool& bOutDirect)
{
	const AMetalStalker* Me = GetStalker();
	const FVector Here = Me->GetActorLocation();
	const FSpace MySpace = SpaceOf(Here);
	const FSpace GoalSpace = SpaceOf(Goal);
	bOutDirect = MySpace == GoalSpace;
	if (bOutDirect)
	{
		RoutePortal.Reset();
		bRouteLinedUp = false;
		return Goal;
	}
	// Inside an interior: leave by its hangar mouth. Out in space: enter the goal's interior by its ship's hull mouth.
	AMetalPortal* Portal = MySpace.Interior ? MySpace.Interior->GetInnerPortal() : (GoalSpace.Interior ? GoalSpace.Interior->GetOuterPortal() : nullptr);
	if (Portal == nullptr)
	{
		bOutDirect = true;
		return Goal;
	}
	if (RoutePortal.Get() != Portal)
	{
		RoutePortal = Portal;
		bRouteLinedUp = false;
	}
	// We're on the mouth's own side (the hangar, or open space): line up out in front of it, then fly
	// straight through. GetSideDistance is positive on that side, which tells us which way it faces.
	const FVector Forward = Portal->GetActorForwardVector();
	const FVector OwnSide = Forward * FMath::Sign(Portal->GetSideDistance(Portal->GetActorLocation() + Forward));
	const FVector LineUp = Portal->GetActorLocation() + OwnSide * PortalApproach;
	if (!bRouteLinedUp && FVector::Dist(Here, LineUp) < PortalLineUpRadius)
	{
		bRouteLinedUp = true;
	}
	return bRouteLinedUp ? Portal->GetActorLocation() - OwnSide * 3000.f : LineUp;
}

FVector AMetalFlyerAIController::AvoidObstacles(const FVector& Direction) const
{
	const AMetalStalker* Me = GetStalker();
	const float Speed = FMath::Max(1500.f, static_cast<float>(Me->GetVelocity().Size()));
	const FVector Start = Me->GetActorLocation();
	const FVector End = Start + Direction * Speed * LookAheadTime;
	FCollisionQueryParams Params(SCENE_QUERY_STAT(MetalFlyerAvoid), false, Me);
	if (const AActor* Flyer = TargetFlyer.Get())
	{
		Params.AddIgnoredActor(Flyer);
	}
	FHitResult Hit;
	const float Radius = Me->GetCapsuleComponent()->GetScaledCapsuleRadius() * 1.5f;
	if (!GetWorld()->SweepSingleByChannel(Hit, Start, End, FQuat::Identity, ECC_WorldStatic, FCollisionShape::MakeSphere(Radius), Params))
	{
		return Direction;
	}
	// Slide off the surface, biased upward so pilots climb over hulls instead of hugging them.
	const FVector Slide = FVector::VectorPlaneProject(Direction, Hit.ImpactNormal).GetSafeNormal();
	const float Urgency = 1.f - Hit.Time;
	return (Direction * (1.f - Urgency) + (Slide + Hit.ImpactNormal * 0.8f + FVector::UpVector * 0.4f) * Urgency).GetSafeNormal();
}

void AMetalFlyerAIController::Steer(const FVector& MovePoint, const FVector& LookPoint, float DeltaSeconds, bool bBoost)
{
	AMetalStalker* Me = GetStalker();
	const FVector ToMove = MovePoint - Me->GetActorLocation();
	const float Distance = ToMove.Size();
	FVector Direction = AvoidObstacles(ToMove.GetSafeNormal());
	// Ease off on arrival so pilots settle into an orbit instead of overshooting it.
	const float Throttle = FMath::Clamp(Distance / 3000.f, 0.15f, 1.f);
	Me->AddMovementInput(Direction, Throttle);
	Me->SetBoostHeld(bBoost && Distance > 40000.f && FVector::DotProduct(Me->GetAimForward(), ToMove / FMath::Max(Distance, 1.f)) > 0.8f);

	// Turn the nose (control rotation drives the body's yaw and pitch) toward what we want to look at.
	const FRotator Desired = (LookPoint - Me->GetActorLocation()).Rotation();
	SetControlRotation(FMath::RInterpTo(GetControlRotation(), Desired, DeltaSeconds, 5.f));
}

void AMetalFlyerAIController::UpdateFiring(const FVector& AimPoint, float Now)
{
	AMetalStalker* Me = GetStalker();
	const UMetalBotArchetype* Archetype = Me->Archetype;
	const FVector ToAim = AimPoint - Me->GetActorLocation();
	bool bLinedUp = ToAim.Size() < FireRange
		&& FVector::DotProduct(Me->GetAimForward(), ToAim.GetSafeNormal()) > FMath::Cos(FMath::DegreesToRadians(FireCone));
	if (bLinedUp)
	{
		// Hold fire if something else (a hull, a friend) is in the way: anything but the target's actor.
		const AActor* TargetActor = TargetFlyer.IsValid() ? static_cast<const AActor*>(TargetFlyer.Get()) : (TargetPart.IsValid() ? TargetPart->GetOwner() : nullptr);
		FCollisionQueryParams Params(SCENE_QUERY_STAT(MetalFlyerFire), false, Me);
		FHitResult Hit;
		if (GetWorld()->LineTraceSingleByChannel(Hit, Me->GetActorLocation(), AimPoint, ECC_Visibility, Params) && Hit.GetActor() != TargetActor
			&& !(TargetPart.IsValid() && Hit.GetComponent() == TargetPart.Get()))
		{
			bLinedUp = false;
		}
	}
	if (!bLinedUp)
	{
		Me->StopFire();
		bBurstFiring = false;
		return;
	}
	// Bursts and pauses, like the ground troops.
	if (Now >= BurstToggleTime)
	{
		bBurstFiring = !bBurstFiring;
		const float Burst = Archetype ? Archetype->BurstDuration : 1.2f;
		const float Pause = Archetype ? Archetype->BurstPause : 1.f;
		BurstToggleTime = Now + (bBurstFiring ? Burst : Pause) * FMath::FRandRange(0.8f, 1.2f);
	}
	if (bBurstFiring)
	{
		Me->StartFire();
	}
	else
	{
		Me->StopFire();
	}
}

void AMetalFlyerAIController::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	AMetalStalker* Me = GetStalker();
	if (Me == nullptr || !Me->IsAlive() || Me->IsDormant() || Me->IsStunned())
	{
		if (Me)
		{
			Me->StopFire();
			Me->SetBoostHeld(false);
		}
		return;
	}
	const float Now = GetWorld()->GetTimeSeconds();
	if (Now >= NextThinkTime)
	{
		NextThinkTime = Now + ThinkInterval * FMath::FRandRange(0.8f, 1.2f);
		ChooseTarget();
	}

	const FVector Here = Me->GetActorLocation();
	FVector Goal = Here;
	FVector AimPoint = Here + Me->GetAimForward() * 10000.f;
	float Standoff = 0.f;
	if (const AMetalBotCharacter* Flyer = TargetFlyer.Get())
	{
		// Lead the target by the slug's flight time.
		const FMetalWeaponSlot* Slot = Me->GetCurrentWeapon();
		const float ShotSpeed = Slot && Slot->Data ? FMath::Max(1000.f, Slot->Data->ProjectileSpeed) : 30000.f;
		const FVector TargetLocation = Flyer->GetActorLocation();
		AimPoint = TargetLocation + Flyer->GetVelocity() * (FVector::Dist(Here, TargetLocation) / ShotSpeed);
		Goal = TargetLocation;
		Standoff = Me->Archetype ? FMath::Max(3000.f, Me->Archetype->PreferredRange) : 4000.f;
	}
	else if (const UPrimitiveComponent* Part = TargetPart.Get())
	{
		AimPoint = Part->Bounds.Origin;
		Goal = AimPoint;
		Standoff = Cast<UMetalShipSubsystemComponent>(Part) ? StrafeRange : StrafeRange * 2.f;
	}
	else
	{
		Me->StopFire();
		Me->SetAITarget(FVector::ZeroVector, false);
		Steer(Here, AimPoint, DeltaSeconds, false);
		return;
	}

	bool bDirect = true;
	const FVector Waypoint = RouteTo(Goal, bDirect);
	if (!bDirect)
	{
		// On the way through a portal: no shooting, nose on the path.
		Me->StopFire();
		Me->SetAITarget(FVector::ZeroVector, false);
		Steer(Waypoint, Waypoint, DeltaSeconds, true);
		return;
	}

	// Circle the target at the standoff range (the hover-and-circle attack), guns on it. The orbit point
	// stays on our side of the target (never through a hull to the far side) at a shallow 25-35 degree
	// elevation, so the guns can always bear (the body pitches at most 80 degrees); the sway left and
	// right and up and down keeps the pilot a moving target.
	OrbitPhase += DeltaSeconds * 0.35f * OrbitDirection;
	FVector Away = (Here - Goal).GetSafeNormal2D();
	if (Away.IsNearlyZero())
	{
		Away = -Me->GetActorForwardVector().GetSafeNormal2D();
	}
	const FVector Sideways = Away.RotateAngleAxis(35.f * FMath::Sin(OrbitPhase), FVector::UpVector);
	const float Elevation = FMath::DegreesToRadians(30.f + 5.f * FMath::Cos(OrbitPhase * 0.7f));
	const FVector Orbit = Sideways * FMath::Cos(Elevation) + FVector::UpVector * FMath::Sin(Elevation);
	const FVector MovePoint = FVector::Dist(Here, Goal) > Standoff * 2.5f ? Goal : Goal + Orbit * Standoff;
	Me->SetAITarget(AimPoint, true);
	Steer(MovePoint, AimPoint, DeltaSeconds, true);
	UpdateFiring(AimPoint, Now);
}
