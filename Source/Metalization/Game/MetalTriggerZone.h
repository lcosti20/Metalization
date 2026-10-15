// Box volume used for checkpoints and "reach this area" objectives, with an optional glowing floor marker.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "MetalTriggerZone.generated.h"

class UBoxComponent;
class UStaticMeshComponent;

UCLASS()
class AMetalTriggerZone : public AActor
{
	GENERATED_BODY()

public:
	AMetalTriggerZone();

	/** Saves the player's respawn point (and repairs the player) the first time they enter. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Zone")
	bool bIsCheckpoint = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Zone")
	FLinearColor MarkerColor = FLinearColor(0.1f, 0.8f, 1.f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Zone")
	bool bShowMarker = true;

	bool ContainsActor(const AActor* Actor) const;

	virtual void OnConstruction(const FTransform& Transform) override;

protected:
	virtual void BeginPlay() override;

	UFUNCTION()
	void OnOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor, UPrimitiveComponent* OtherComp, int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult);

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Zone")
	TObjectPtr<UBoxComponent> Box;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Zone")
	TObjectPtr<UStaticMeshComponent> Marker;

private:
	bool bTriggered = false;
};
