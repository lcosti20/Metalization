#include "MetalStalker.h"

#include "MetalBotArchetype.h"
#include "MetalWeaponData.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/World.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/SpringArmComponent.h"

namespace
{
	/** Seconds a dodge barrel roll takes. */
	constexpr float DodgeDuration = 0.45f;
	/** Seconds after the last boost before the meter starts refilling. */
	constexpr float BoostRechargeDelay = 0.6f;
}

void UMetalFlightMovementComponent::CalcVelocity(float DeltaTime, float Friction, bool bFluid, float BrakingDeceleration)
{
	// Flying normally counts as fluid movement, which would also turn the grip into drag and cap the speed.
	const bool bFlying = MovementMode == MOVE_Flying;
	Super::CalcVelocity(DeltaTime, bFlying ? FlightGrip : Friction, bFlying ? false : bFluid, BrakingDeceleration);
}

AMetalStalker::AMetalStalker(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer.SetDefaultSubobjectClass<UMetalFlightMovementComponent>(ACharacter::CharacterMovementComponentName))
{
	// No ground brain: parked Stalkers sit idle until a pilot (the player, later AI pilots) links in.
	AIControllerClass = nullptr;
	AutoPossessAI = EAutoPossessAI::Disabled;

	// The arms reach far outside the capsule, so the mesh takes shots (Visibility traces, projectile
	// sweeps) through its physics bodies. It never blocks movement.
	USkeletalMeshComponent* Body = GetMesh();
	Body->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	Body->SetCollisionObjectType(ECC_Pawn);
	Body->SetCollisionResponseToAllChannels(ECR_Ignore);
	Body->SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);
	Body->SetCollisionResponseToChannel(ECC_WorldDynamic, ECR_Block);
	Body->SetGenerateOverlapEvents(false);

	UCharacterMovementComponent* Move = GetCharacterMovement();
	Move->NavAgentProps.bCanFly = true;
	// Grip steers; letting go brakes at a steady rate (BrakingDecelerationFlying) rather than by grip.
	Move->bUseSeparateBrakingFriction = true;
	Move->BrakingFriction = 0.f;

	// Let the camera trail a little at speed so boosting reads as fast.
	CameraBoom->CameraLagSpeed = 8.f;
	CameraBoom->CameraLagMaxDistance = 700.f;
}

const FMetalFlightTuning& AMetalStalker::GetFlight() const
{
	static const FMetalFlightTuning Default;
	return Archetype ? Archetype->Flight : Default;
}

void AMetalStalker::ApplyArchetype()
{
	Super::ApplyArchetype();
	const FMetalFlightTuning& Flight = GetFlight();
	UCharacterMovementComponent* Move = GetCharacterMovement();
	Move->MaxFlySpeed = Flight.MaxSpeed;
	Move->MaxAcceleration = Flight.Acceleration;
	Move->BrakingDecelerationFlying = Flight.Braking;
	if (UMetalFlightMovementComponent* FlightMove = Cast<UMetalFlightMovementComponent>(Move))
	{
		FlightMove->FlightGrip = Flight.Grip;
	}
}

void AMetalStalker::BeginPlay()
{
	Super::BeginPlay();
	LastYaw = GetActorRotation().Yaw;
	if (bStartParked && GetController() == nullptr)
	{
		bParked = true;
		SetDormant(true);
	}
	else
	{
		GetCharacterMovement()->DefaultLandMovementMode = MOVE_Flying;
		GetCharacterMovement()->SetMovementMode(MOVE_Flying);
	}
}

void AMetalStalker::PossessedBy(AController* NewController)
{
	Super::PossessedBy(NewController);
	UCharacterMovementComponent* Move = GetCharacterMovement();
	// Possession restarts the pawn, which puts it back in its default movement mode: make that flying.
	Move->DefaultLandMovementMode = MOVE_Flying;
	if (bParked)
	{
		// Lift off the pad.
		bParked = false;
		Move->SetMovementMode(MOVE_Flying);
		Move->Velocity = FVector(0.f, 0.f, 700.f);
	}
	else if (Move->MovementMode != MOVE_Flying)
	{
		Move->SetMovementMode(MOVE_Flying);
	}
}

void AMetalStalker::ConfigureRotationForController()
{
	// The body yaws toward the aim at a limited rate. Pitch and bank are visual only (TickTilt): a
	// character capsule has to stay upright, and RotationRate.Pitch = 0 keeps the movement component
	// from pitching it while flying.
	UCharacterMovementComponent* Move = GetCharacterMovement();
	bUseControllerRotationYaw = false;
	Move->bOrientRotationToMovement = false;
	Move->bUseControllerDesiredRotation = GetController() != nullptr;
	Move->RotationRate = FRotator(0.f, GetFlight().TurnRate, 0.f);
}

void AMetalStalker::SetDormant(bool bNewDormant)
{
	// Unlike walkers, a released Stalker coasts to a hover instead of stopping dead.
	bDormant = bNewDormant;
	StopFire();
	bBoostHeld = false;
}

void AMetalStalker::AddVerticalInput(float Value)
{
	AddMovementInput(FVector::UpVector, Value);
}

void AMetalStalker::Dodge(float Direction)
{
	const UWorld* World = GetWorld();
	const FMetalFlightTuning& Flight = GetFlight();
	if (!IsAlive() || IsDormant() || bParked || IsStunned() || DodgeStartTime >= 0.f || BoostMeter < Flight.DodgeCost || FMath::IsNearlyZero(Direction))
	{
		return;
	}
	BoostMeter -= Flight.DodgeCost;
	LastBoostTime = World->GetTimeSeconds();
	DodgeStartTime = LastBoostTime;
	DodgeDirection = FMath::Sign(Direction);
	// Over the speed limit the movement component brakes back down, so the burst fades on its own.
	const FVector Right = FRotationMatrix(FRotator(0.f, GetActorRotation().Yaw, 0.f)).GetUnitAxis(EAxis::Y);
	GetCharacterMovement()->Velocity += Right * DodgeDirection * Flight.DodgeSpeed;
}

FVector AMetalStalker::GetAimForward() const
{
	return FRotator(BodyPitch, GetActorRotation().Yaw, 0.f).Vector();
}

FVector AMetalStalker::GetMuzzleLocation() const
{
	// The guns point outward along the arms, so shots leave from the tip: the gun bone pushed out
	// along the body-to-gun direction by the weapon's muzzle offset.
	if (Archetype && Archetype->MuzzleBones.Num() > 0)
	{
		const FName Bone = Archetype->MuzzleBones[MuzzleCycle % Archetype->MuzzleBones.Num()];
		if (GetMesh()->GetBoneIndex(Bone) != INDEX_NONE)
		{
			const FVector GunBase = GetMesh()->GetBoneLocation(Bone);
			const FVector Center = GetMesh()->GetBoneLocation(GetRig().Pelvis);
			const FMetalWeaponSlot* Slot = GetCurrentWeapon();
			const float Offset = (Slot && Slot->Data ? Slot->Data->MuzzleOffset : 0.f) * GetMesh()->GetRelativeScale3D().X;
			return GunBase + (GunBase - Center).GetSafeNormal() * Offset;
		}
	}
	return Super::GetMuzzleLocation();
}

void AMetalStalker::TickBoost(float DeltaSeconds, float Now)
{
	const FMetalFlightTuning& Flight = GetFlight();
	UCharacterMovementComponent* Move = GetCharacterMovement();
	bBoosting = bBoostHeld && BoostMeter > 0.f && IsAlive() && !IsDormant() && !IsStunned() && Move->MovementMode == MOVE_Flying;
	if (bBoosting)
	{
		BoostMeter = FMath::Max(0.f, BoostMeter - DeltaSeconds / FMath::Max(0.1f, Flight.BoostDuration));
		LastBoostTime = Now;
		// Like sprinting: boost always drives the craft forward along its aim.
		AddMovementInput(GetAimForward(), 1.f);
	}
	else if (Now - LastBoostTime > BoostRechargeDelay)
	{
		BoostMeter = FMath::Min(1.f, BoostMeter + DeltaSeconds / FMath::Max(0.1f, Flight.BoostRechargeTime));
	}
	Move->MaxFlySpeed = bBoosting ? Flight.BoostSpeed : Flight.MaxSpeed;
	Move->MaxAcceleration = bBoosting ? Flight.BoostAcceleration : Flight.Acceleration;
}

void AMetalStalker::TickTilt(float DeltaSeconds, float Now)
{
	if (!IsAlive())
	{
		return;
	}
	const FMetalFlightTuning& Flight = GetFlight();
	const bool bPiloted = GetController() != nullptr && !IsDormant();

	// Nose follows the aim pitch, rate-limited like the yaw.
	const float TargetPitch = bPiloted ? FMath::Clamp(FRotator::NormalizeAxis(GetControlRotation().Pitch), -80.f, 80.f) : 0.f;
	const float PitchStep = FMath::Clamp(TargetPitch - BodyPitch, -Flight.TurnRate * DeltaSeconds, Flight.TurnRate * DeltaSeconds);
	BodyPitch = FMath::FInterpTo(BodyPitch, BodyPitch + PitchStep, DeltaSeconds, 12.f);

	// Bank into strafes and turns; with no input it eases back to level. Sideways speed and yaw rate each
	// contribute up to 60% of MaxBank (normalised by the top speed and turn rate), clamped to MaxBank.
	const float Yaw = GetActorRotation().Yaw;
	const float YawRate = DeltaSeconds > 0.f ? FRotator::NormalizeAxis(Yaw - LastYaw) / DeltaSeconds : 0.f;
	LastYaw = Yaw;
	const float LateralSpeed = GetActorTransform().InverseTransformVectorNoScale(GetVelocity()).Y;
	const float TargetBank = FMath::Clamp(
		(FMath::Clamp(LateralSpeed / FMath::Max(1.f, Flight.MaxSpeed), -1.f, 1.f) + FMath::Clamp(YawRate / FMath::Max(1.f, Flight.TurnRate), -1.f, 1.f)) * 0.6f * Flight.MaxBank,
		-Flight.MaxBank, Flight.MaxBank);
	Bank = FMath::FInterpTo(Bank, TargetBank, DeltaSeconds, 4.f);

	float Roll = Bank;
	if (DodgeStartTime >= 0.f)
	{
		const float T = (Now - DodgeStartTime) / DodgeDuration;
		if (T >= 1.f)
		{
			DodgeStartTime = -1.f;
		}
		else
		{
			Roll += 360.f * DodgeDirection * FMath::InterpEaseInOut(0.f, 1.f, T, 2.f);
		}
	}
	// Positive roll dips the right side, which is the way a rightward strafe or turn should lean.
	MeshTilt = FRotator(BodyPitch, 0.f, Roll).Quaternion();
}

void AMetalStalker::Tick(float DeltaSeconds)
{
	const float Now = GetWorld()->GetTimeSeconds();
	TickBoost(DeltaSeconds, Now);
	// Before Super::Tick, which applies MeshTilt to the mesh.
	TickTilt(DeltaSeconds, Now);
	Super::Tick(DeltaSeconds);
}
