#include "MetalizationCharacter.h"
#include "Camera/CameraComponent.h"
#include "Components/CapsuleComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/SpringArmComponent.h"
#include "GameFramework/Controller.h"
#include "GameFramework/PlayerController.h"
#include "EnhancedInputComponent.h"
#include "InputActionValue.h"
#include "Engine/World.h"
#include "PlanetaryGravityComponent.h"

AMetalizationCharacter::AMetalizationCharacter()
{
	PrimaryActorTick.bCanEverTick = true;

	GetCapsuleComponent()->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	GetMesh()->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	GetMesh()->SetSimulatePhysics(false);
	GetMesh()->SetHiddenInGame(true);
	SetActorEnableCollision(false);

	bUseControllerRotationPitch = false;
	bUseControllerRotationYaw = false;
	bUseControllerRotationRoll = false;

	PlanetaryGravity = CreateDefaultSubobject<UPlanetaryGravityComponent>(TEXT("PlanetaryGravity"));
	PlanetaryGravity->bDriveMovementComponentGravity = false;

	InitMovement();
	InitCameraBoom();
	InitFollowCamera();
}

void AMetalizationCharacter::InitMovement()
{
	// Movement settings
	UCharacterMovementComponent* MovementComp = GetCharacterMovement();
	if (MovementComp)
	{
		MovementComp->GravityScale = 0.f;
		MovementComp->DisableMovement();
		MovementComp->SetComponentTickEnabled(false);
	}
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
	CameraBoom->bDoCollisionTest = false;
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
		EnhancedInputComponent->BindAction(MoveAction, ETriggerEvent::Triggered, this, &AMetalizationCharacter::Move);
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

	const FRotator ControlRot = LocalController->GetControlRotation();
	CurrentSurfaceUp = PlanetaryGravity->GetCurrentSurfaceUp();

	FVector ForwardDir = FVector::VectorPlaneProject(ControlRot.Vector(), CurrentSurfaceUp);
	if (ForwardDir.SizeSquared() < KINDA_SMALL_NUMBER)
	{
		ForwardDir = FVector::VectorPlaneProject(GetActorForwardVector(), CurrentSurfaceUp);
	}
	ForwardDir = ForwardDir.GetSafeNormal();

	FVector RightDir = FVector::CrossProduct(CurrentSurfaceUp, ForwardDir).GetSafeNormal();
	const FVector MoveDirection = (ForwardDir * Forward + RightDir * Right).GetClampedToMaxSize(1.0f);
	if (MoveDirection.IsNearlyZero())
	{
		return;
	}

	const float Distance = CameraPanSpeed * GetWorld()->GetDeltaSeconds();
	const FVector MoveTangent = MoveDirection.GetSafeNormal() * Distance;
	AActor* Planet = PlanetaryGravity->GetCurrentPlanet();
	if (Planet)
	{
		const FVector PlanetCenter = PlanetaryGravity->GetPlanetCenter();
		const FVector RelativePosition = GetActorLocation() - PlanetCenter;
		const float Radius = RelativePosition.Size();
		if (Radius > KINDA_SMALL_NUMBER)
		{
			const FVector RotationAxis = FVector::CrossProduct(CurrentSurfaceUp, MoveDirection).GetSafeNormal();
			const float AngleRadians = Distance / Radius;
			const FQuat SurfaceStep(RotationAxis, AngleRadians);
			SetActorLocation(PlanetCenter + SurfaceStep.RotateVector(RelativePosition), false);
		}
	}
	else
	{
		SetActorLocation(GetActorLocation() + MoveTangent, false);
	}
}

// Used in blueprints
void AMetalizationCharacter::DoLook(float Yaw, float Pitch)
{
	LookYaw += Yaw * LookYawRate;
	LookPitch = FMath::Clamp(LookPitch + Pitch * LookPitchRate, -MaxLookPitch, MaxLookPitch);
}

void AMetalizationCharacter::DoZoom(float Amount)
{
	if (CameraBoom)
	{
		CameraBoom->TargetArmLength = FMath::Clamp(
			CameraBoom->TargetArmLength - Amount * CameraZoomSpeed,
			MinimumCameraDistance,
			MaximumCameraDistance);
	}
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

}
