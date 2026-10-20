// Blast door that slides open when a linked console is activated.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "MetalGate.generated.h"

class UStaticMeshComponent;

/**
 * A sliding blast door. Placeholder cube geometry; opened by an AMetalControlConsole that lists it in
 * LinkedGates. Once opened it stays open.
 */
UCLASS()
class AMetalGate : public AActor
{
	GENERATED_BODY()

public:
	/** Builds the 6 m x 4.5 m door slab from the engine cube. */
	AMetalGate();

	/** Local-space offset the door travels when opening. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Gate")
	FVector OpenOffset = FVector(0.f, 0.f, 480.f);

	/** Seconds the door takes to slide fully open. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Gate")
	float OpenTime = 2.5f;

	/** Starts opening the door. */
	void Open();

	/** @return True once Open() has been called (the door may still be moving). */
	bool IsOpen() const { return bOpening; }

	/** Slides the door towards OpenOffset with ease-in/out. */
	virtual void Tick(float DeltaSeconds) override;

protected:
	/** Records the closed position. */
	virtual void BeginPlay() override;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Gate")
	TObjectPtr<UStaticMeshComponent> Door;

private:
	FVector ClosedLocation = FVector::ZeroVector;
	float OpenAlpha = 0.f;
	bool bOpening = false;
};
