// Anti-gravity lift: a glowing column that carries any walking robot straight up and drops it off at the top.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "MetalGravLift.generated.h"

class AMetalBotCharacter;
class UAudioComponent;
class UBoxComponent;
class UMaterialInstanceDynamic;
class UStaticMeshComponent;

/**
 * Vertical lift beam. A robot that steps into the column switches to flying movement and is carried up at
 * LiftSpeed (easing in near the top), keeping some air control so the rider can drift. At the top it is
 * pushed out along ExitDirection and gets its jumps back, so it can double jump onto a ledge.
 *
 * The actor's origin is the centre of the floor pad; the column rises LiftHeight above it.
 */
UCLASS()
class AMetalGravLift : public AActor
{
	GENERATED_BODY()

public:
	/** Creates the trigger, base pad, beam, rings and light. */
	AMetalGravLift();

	/** Height of the column above the pad, cm. Riders are released at the top. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Lift", meta=(ClampMin="100"))
	float LiftHeight = 900.f;

	/** Radius of the column, cm. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Lift", meta=(ClampMin="50"))
	float Radius = 140.f;

	/** Rising speed, cm/s. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Lift")
	float LiftSpeed = 650.f;

	/** Horizontal direction riders are pushed at the top (actor space). Zero = straight up and drop back down. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Lift")
	FVector ExitDirection = FVector::ForwardVector;

	/** Speed of the push at the top, cm/s. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Lift")
	float ExitSpeed = 550.f;

	/** Glow colour of the beam. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Lift")
	FLinearColor Color = FLinearColor(0.25f, 0.85f, 1.f);

	/** @return True if Actor is currently being carried by this lift. */
	bool IsCarrying(const AActor* Actor) const;

	/** Sizes the trigger and visuals to LiftHeight / Radius. */
	virtual void OnConstruction(const FTransform& Transform) override;

	/** Carries riders and animates the rings. */
	virtual void Tick(float DeltaSeconds) override;

protected:
	/** Makes the glow materials and starts the hum. */
	virtual void BeginPlay() override;

	/** Hands a rider back to normal walking/falling if it leaves the column sideways. */
	UFUNCTION()
	void OnEndOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor, UPrimitiveComponent* OtherComp, int32 OtherBodyIndex);

	/**
	 * Lets go of a rider.
	 *
	 * @param Bot    The rider.
	 * @param bAtTop Reached the top: push it out along ExitDirection and give its jumps back.
	 */
	void Release(AMetalBotCharacter* Bot, bool bAtTop);

	UPROPERTY(VisibleAnywhere, Category="Components")
	TObjectPtr<UBoxComponent> Trigger;

	/** Glowing floor pad. */
	UPROPERTY(VisibleAnywhere, Category="Components")
	TObjectPtr<UStaticMeshComponent> Pad;

	/** Translucent-looking column (thin glow cylinder). */
	UPROPERTY(VisibleAnywhere, Category="Components")
	TObjectPtr<UStaticMeshComponent> Beam;

	/** Rings that float up the column to show which way it goes. */
	UPROPERTY(VisibleAnywhere, Category="Components")
	TArray<TObjectPtr<UStaticMeshComponent>> Rings;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UMaterialInstanceDynamic>> Materials;

	UPROPERTY(Transient)
	TObjectPtr<UAudioComponent> Hum;

	/** Robots currently riding. */
	TSet<TWeakObjectPtr<AMetalBotCharacter>> Riders;

	float RingPhase = 0.f;
};
