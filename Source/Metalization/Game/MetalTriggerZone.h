// Box volume used for checkpoints and "reach this area" objectives, with an optional glowing floor marker.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "MetalTriggerZone.generated.h"

class UBoxComponent;
class UStaticMeshComponent;

/**
 * Box trigger used as a checkpoint (bIsCheckpoint) and/or as the target of a ReachZone mission objective.
 *
 * @see AMetalMission
 */
UCLASS()
class AMetalTriggerZone : public AActor
{
	GENERATED_BODY()

public:
	/** Creates the trigger box and the floor-marker cylinder. */
	AMetalTriggerZone();

	/** Saves the player's respawn point (and repairs the player) the first time they enter. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Zone")
	bool bIsCheckpoint = false;

	/** Colour of the glowing floor marker. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Zone")
	FLinearColor MarkerColor = FLinearColor(0.1f, 0.8f, 1.f);

	/** Draw a glowing disc on the floor of the zone. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Zone")
	bool bShowMarker = true;

	/**
	 * @param Actor Actor to test.
	 * @return True if Actor currently overlaps the zone's box.
	 */
	bool ContainsActor(const AActor* Actor) const;

	/** Fits the floor marker to the box extent. */
	virtual void OnConstruction(const FTransform& Transform) override;

protected:
	/** Binds the overlap event and gives the marker the glow material. */
	virtual void BeginPlay() override;

	/** Checkpoints trigger once, on the player's own body: saves the respawn point and fully repairs the player. */
	UFUNCTION()
	void OnOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor, UPrimitiveComponent* OtherComp, int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult);

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Zone")
	TObjectPtr<UBoxComponent> Box;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Zone")
	TObjectPtr<UStaticMeshComponent> Marker;

private:
	bool bTriggered = false;
};
