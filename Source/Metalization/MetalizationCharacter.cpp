#include "MetalizationCharacter.h"
#include "Engine/LocalPlayer.h"
#include "Camera/CameraComponent.h"
#include "Components/CapsuleComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/SpringArmComponent.h"
#include "GameFramework/Controller.h"
#include "GameFramework/PlayerController.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "InputActionValue.h"
#include "Metalization.h"
#include "Engine/World.h"
#include "Engine/EngineTypes.h"
#include "CollisionQueryParams.h"
#include "Kismet/GameplayStatics.h"
#include "PlanetaryGravityComponent.h"

AMetalizationCharacter::AMetalizationCharacter()
{
	PrimaryActorTick.bCanEverTick = true;

	GetCapsuleComponent()->InitCapsuleSize(42.f, 96.0f);

	// Disable controller-driven character rotation (Orient to movement will handle actor rotation)
	bUseControllerRotationPitch = false;
	bUseControllerRotationYaw = false;
	bUseControllerRotationRoll = false;

	PlanetaryGravity = CreateDefaultSubobject<UPlanetaryGravityComponent>(TEXT("PlanetaryGravity"));

	InitMovement();
	InitCameraBoom();
	InitFollowCamera();
}

void AMetalizationCharacter::InitMovement()
{
	// Movement settings
	UCharacterMovementComponent* MovementComp = GetCharacterMovement();
	MovementComp->bOrientRotationToMovement = true;
	MovementComp->RotationRate = FRotator(0.0f, 500.0f, 0.0f);

	MovementComp->JumpZVelocity = 500.f;
	MovementComp->AirControl = 0.35f;
	MovementComp->MaxWalkSpeed = 500.f;
	MovementComp->MinAnalogWalkSpeed = 20.f;
	MovementComp->BrakingDecelerationWalking = 2000.f;
	MovementComp->BrakingDecelerationFalling = 1500.0f;
	MovementComp->bOrientRotationToMovement = false;
}

void AMetalizationCharacter::InitCameraBoom()
{
	// Camera Boom (Spring Arm)
	CameraBoom = CreateDefaultSubobject<USpringArmComponent>(TEXT("CameraBoom"));
	CameraBoom->SetupAttachment(RootComponent);
	CameraBoom->TargetArmLength = 400.0f;

	// Inherit Pawn Control Rotation so mouse/stick controls orbit the player relative to the planet surface
	CameraBoom->bUsePawnControlRotation = true;
	CameraBoom->bInheritPitch = true;
	CameraBoom->bInheritYaw = true;
	CameraBoom->bInheritRoll = true;
}

void AMetalizationCharacter::InitFollowCamera()
{
	// Follow Camera
	FollowCamera = CreateDefaultSubobject<UCameraComponent>(TEXT("FollowCamera"));
	FollowCamera->SetupAttachment(CameraBoom, USpringArmComponent::SocketName);
	FollowCamera->bUsePawnControlRotation = false;
}

void AMetalizationCharacter::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)
{
	if (UEnhancedInputComponent* EnhancedInputComponent = Cast<UEnhancedInputComponent>(PlayerInputComponent))
	{
		EnhancedInputComponent->BindAction(JumpAction, ETriggerEvent::Started, this, &ACharacter::Jump);
		EnhancedInputComponent->BindAction(JumpAction, ETriggerEvent::Completed, this, &ACharacter::StopJumping);

		EnhancedInputComponent->BindAction(MoveAction, ETriggerEvent::Triggered, this, &AMetalizationCharacter::Move);
		EnhancedInputComponent->BindAction(MouseLookAction, ETriggerEvent::Triggered, this, &AMetalizationCharacter::Look);
		EnhancedInputComponent->BindAction(LookAction, ETriggerEvent::Triggered, this, &AMetalizationCharacter::Look);
	}
}

void AMetalizationCharacter::Move(const FInputActionValue& Value)
{
	FVector2D MovementVector = Value.Get<FVector2D>();
	DoMove(MovementVector.X, MovementVector.Y);
}

// Used in blueprints
void AMetalizationCharacter::DoMove(float Right, float Forward)
{
	AController* LocalController = GetController();
	if (!LocalController || !PlanetaryGravity)
	{
		return;
	}

	const FRotator ControlRot = GetController()->GetControlRotation();
	CurrentSurfaceUp = PlanetaryGravity->GetCurrentSurfaceUp();

	FVector ForwardDir = FVector::VectorPlaneProject(ControlRot.Vector(), CurrentSurfaceUp);
	if (ForwardDir.SizeSquared() < KINDA_SMALL_NUMBER)
	{
		ForwardDir = FVector::VectorPlaneProject(GetActorForwardVector(), CurrentSurfaceUp);
	}
	ForwardDir = ForwardDir.GetSafeNormal();

	FVector RightDir = FVector::CrossProduct(CurrentSurfaceUp, ForwardDir).GetSafeNormal();

	AddMovementInput(ForwardDir, Forward);
	AddMovementInput(RightDir, Right);
}

void AMetalizationCharacter::Look(const FInputActionValue& Value)
{
	FVector2D LookAxisVector = Value.Get<FVector2D>();
	DoLook(LookAxisVector.X, LookAxisVector.Y);
}

// Used in blueprints
void AMetalizationCharacter::DoLook(float Yaw, float Pitch)
{
	LookYaw += Yaw * LookYawRate;
	LookPitch = FMath::Clamp(LookPitch + Pitch * LookPitchRate, -MaxLookPitch, MaxLookPitch);
}

// Used in blueprints
void AMetalizationCharacter::DoJumpStart()
{
	// signal the character to jump
	Jump();
}

// Used in blueprints
void AMetalizationCharacter::DoJumpEnd()
{
	// signal the character to stop jumping
	StopJumping();
}

void AMetalizationCharacter::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	// Update planetary frame
	// ------------------------------------------------
	AController* LocalController = GetController();
	if (!LocalController || !PlanetaryGravity)
	{
		return;
	}

	CurrentSurfaceUp = PlanetaryGravity->GetCurrentSurfaceUp();
	SurfaceOrientation = PlanetaryGravity->GetSurfaceOrientation();

	FQuat YawQuat(SurfaceOrientation.GetAxisZ(), FMath::DegreesToRadians(LookYaw));
	FQuat FrameWithYaw = YawQuat * SurfaceOrientation;
	FQuat PitchQuat(FrameWithYaw.GetAxisY(), FMath::DegreesToRadians(LookPitch));
	FQuat FinalRot = PitchQuat * FrameWithYaw;

	LocalController->SetControlRotation(FinalRot.Rotator());

	// Update actor orientation to surface
	// ------------------------------------------------
	UCharacterMovementComponent* MovementComp = GetCharacterMovement();
	if (!MovementComp)
	{
		return;
	}

	// Face direction of travel when moving with meaningful speed; otherwise keep
	// current facing, just re-projected onto the (possibly changed) tangent plane
	// so standing still doesn't cause the body to lean into the ground.
	FVector DesiredForward;
	const FVector PlanarVelocity = FVector::VectorPlaneProject(MovementComp->Velocity, CurrentSurfaceUp);
	if (PlanarVelocity.SizeSquared() > FMath::Square(10.0f))
	{
		DesiredForward = PlanarVelocity.GetSafeNormal();
	}
	else
	{
		DesiredForward = FVector::VectorPlaneProject(GetActorForwardVector(), CurrentSurfaceUp).GetSafeNormal();
		if (DesiredForward.IsNearlyZero())
		{
			DesiredForward = FVector::VectorPlaneProject(FVector::ForwardVector, CurrentSurfaceUp).GetSafeNormal();
		}
	}

	const FQuat TargetQuat = FRotationMatrix::MakeFromXZ(DesiredForward, CurrentSurfaceUp).ToQuat();
	const FQuat NewQuat = FMath::QInterpTo(GetActorQuat(), TargetQuat, DeltaTime, BodyRotationInterpSpeed);

	// Non-sweeping: we trust our own surface-normal math and don't want collision
	// deflection from this — that sweep was the source of the drift.
	SetActorRotation(NewQuat);
}
