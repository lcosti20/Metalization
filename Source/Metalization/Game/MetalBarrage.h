// Artillery barrage zone: while the player is inside, shells keep landing a little behind them.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "MetalBarrage.generated.h"

class UBoxComponent;

/**
 * Incoming fire that chases the player through an area (the Production Alpha's trench). Every Interval
 * seconds a shell whistles in and lands TrailDistance behind the player, inside the box: standing still or
 * walking gets you clipped, sprinting outruns it. Shells hurt only the player's side.
 */
UCLASS()
class AMetalBarrage : public AActor
{
	GENERATED_BODY()

public:
	/** Creates the box area the barrage covers (sized in the level). */
	AMetalBarrage();

	/** Seconds between shells. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Barrage")
	float Interval = 1.1f;

	/** How far behind the player (along their movement, or the box's X axis when still) shells land, cm. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Barrage")
	float TrailDistance = 450.f;

	/** Random scatter around the aim point, cm. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Barrage")
	float Scatter = 200.f;

	/** Damage at the centre of a blast. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Barrage")
	float Damage = 22.f;

	/** Radius of a blast, cm. Damage falls off towards the edge. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Barrage")
	float BlastRadius = 320.f;

	/** Seconds between the incoming whistle and the impact. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Barrage")
	float FlightTime = 0.6f;

	/** Fires the next shell when the player is inside Area and Interval has passed. */
	virtual void Tick(float DeltaSeconds) override;

protected:
	/** Shells only fall while the player is inside this box, and always land inside it. */
	UPROPERTY(VisibleAnywhere, Category="Components")
	TObjectPtr<UBoxComponent> Area;

private:
	/** Lands a shell at Location (marker flash first, then the blast). */
	void Fire(const FVector& Location);

	/** World time the next shell may be fired. */
	float NextShotTime = 0.f;
};
