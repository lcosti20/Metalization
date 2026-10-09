#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "InputActionValue.h"
#include "MetalizationCharacter.generated.h"

class USpringArmComponent;
class UCameraComponent;
class UInputAction;
class UPlanetaryGravityComponent;

UCLASS(config = Game)
class AMetalizationCharacter : public ACharacter
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = Camera, meta = (AllowPrivateAccess = "true"))
	USpringArmComponent* CameraBoom;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = Camera, meta = (AllowPrivateAccess = "true"))
	UCameraComponent* FollowCamera;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = Input, meta = (AllowPrivateAccess = "true"))
	UInputAction* JumpAction;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = Input, meta = (AllowPrivateAccess = "true"))
	UInputAction* MoveAction;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = Input, meta = (AllowPrivateAccess = "true"))
	UInputAction* LookAction;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = Input, meta = (AllowPrivateAccess = "true"))
	UInputAction* MouseLookAction;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Planetary Gravity", meta = (AllowPrivateAccess = "true"))
	UPlanetaryGravityComponent* PlanetaryGravity;

	void InitMovement();
	void InitCameraBoom();
	void InitFollowCamera();

public:
	AMetalizationCharacter();

	virtual void Tick(float DeltaTime) override;

protected:
	// Accumulated look input, independent of engine ControlRotation
	float LookYaw = 0.0f;
	float LookPitch = 0.0f;

	UPROPERTY(EditAnywhere, Category = "Planetary Gravity")
	float LookYawRate = 1.0f;

	UPROPERTY(EditAnywhere, Category = "Planetary Gravity")
	float LookPitchRate = 1.0f;

	UPROPERTY(EditAnywhere, Category = "Planetary Gravity")
	float MaxLookPitch = 85.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Planetary Gravity")
	float BodyRotationInterpSpeed = 10.0f;

	// Persistent, incrementally-updated surface-aligned frame (no yaw/pitch baked in)
	FQuat SurfaceOrientation = FQuat::Identity;

	// Cached each tick so DoMove and camera code agree on the same "up"
	FVector CurrentSurfaceUp = FVector::UpVector;

	bool bSurfaceFrameInitialized = false;

	virtual void SetupPlayerInputComponent(class UInputComponent* PlayerInputComponent) override;

	void Move(const FInputActionValue& Value);
	void Look(const FInputActionValue& Value);

	UFUNCTION(BlueprintCallable, Category = "Movement")
	void DoMove(float Right, float Forward);
	UFUNCTION(BlueprintCallable, Category = "Movement")
	void DoLook(float Yaw, float Pitch);
	UFUNCTION(BlueprintCallable, Category = "Movement")
	void DoJumpStart();
	UFUNCTION(BlueprintCallable, Category = "Movement")
	void DoJumpEnd();
};