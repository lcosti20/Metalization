// Anti-gravity lift: a glowing column that carries any walking robot straight up and drops it off at the top.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "MetalGravLift.generated.h"

class AMetalBotCharacter;
class AAIController;
class UAudioComponent;
class UBoxComponent;
class UMaterialInstanceDynamic;
class UNavLinkCustomComponent;
class UStaticMeshComponent;

/**
 * Vertical lift beam. A robot that steps into the column switches to flying movement and is carried up at
 * LiftSpeed (easing in near the top), keeping some air control so the rider can drift. At the top it is
 * pushed out along ExitDirection and gets its jumps back, so it can double jump onto a ledge.
 *
 * The actor's origin is the centre of the floor pad; the column rises LiftHeight above it.
 *
 * A one-way nav link from just behind the column to the landing at the top lets AI plan paths through the lift.
 * Only AI robots whose path reached that link board (others brushing the beam, or jumping down through it, are
 * left alone). A rider's path following is paused while the lift carries it and resumed when it is let go.
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

	/**
	 * How far past the column edge, along ExitDirection, the AI nav link lands (cm). The landing height is found
	 * by tracing down there. No link is made when ExitDirection is zero.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Lift|AI", meta=(ClampMin="0"))
	float NavLandingDistance = 200.f;

	/** Glow colour of the beam. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Lift")
	FLinearColor Color = FLinearColor(0.25f, 0.85f, 1.f);

	/** @return True if Actor is currently being carried by this lift. */
	bool IsCarrying(const AActor* Actor) const;

	/** Sizes the trigger and visuals to LiftHeight / Radius, and places the AI nav link. */
	virtual void OnConstruction(const FTransform& Transform) override;

	/** Carries riders and animates the rings. */
	virtual void Tick(float DeltaSeconds) override;

protected:
	/** Makes the glow materials and starts the hum. */
	virtual void BeginPlay() override;

	/** Nav link callback: an AI's path following reached the link start, so it may board for the next few seconds. */
	void OnAIReachedLink(UNavLinkCustomComponent* Link, UObject* PathFollowing, const FVector& Destination);

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

	/** Volume covering the column; robots overlapping it are carried. */
	UPROPERTY(VisibleAnywhere, Category="Components")
	TObjectPtr<UBoxComponent> Trigger;

	/** One-way navigation link from the pad up to the landing, so AI pathfinding knows the lift goes up. */
	UPROPERTY(VisibleAnywhere, Category="Components")
	TObjectPtr<UNavLinkCustomComponent> NavLink;

	/** Glowing floor pad. */
	UPROPERTY(VisibleAnywhere, Category="Components")
	TObjectPtr<UStaticMeshComponent> Pad;

	/** Translucent-looking column (thin glow cylinder). */
	UPROPERTY(VisibleAnywhere, Category="Components")
	TObjectPtr<UStaticMeshComponent> Beam;

	/** Rings that float up the column to show which way it goes. */
	UPROPERTY(VisibleAnywhere, Category="Components")
	TArray<TObjectPtr<UStaticMeshComponent>> Rings;

	/** Glow materials: pad, beam, then one per ring. */
	UPROPERTY(Transient)
	TArray<TObjectPtr<UMaterialInstanceDynamic>> Materials;

	/** Looping hum played at the pad. */
	UPROPERTY(Transient)
	TObjectPtr<UAudioComponent> Hum;

	/** AI controllers whose path reached the nav link, and the world time they did. */
	TMap<TWeakObjectPtr<AAIController>, float> Boarding;

	/** Robots currently riding. */
	TSet<TWeakObjectPtr<AMetalBotCharacter>> Riders;

	/** 0..1 cycle position of the rising rings. */
	float RingPhase = 0.f;
};
