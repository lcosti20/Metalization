// Blast door that slides open when a linked console is activated.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "MetalGate.generated.h"

class UStaticMeshComponent;

UCLASS()
class AMetalGate : public AActor
{
	GENERATED_BODY()

public:
	AMetalGate();

	/** Local-space offset the door travels when opening. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Gate")
	FVector OpenOffset = FVector(0.f, 0.f, 480.f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Gate")
	float OpenTime = 2.5f;

	void Open();
	bool IsOpen() const { return bOpening; }

	virtual void Tick(float DeltaSeconds) override;

protected:
	virtual void BeginPlay() override;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Gate")
	TObjectPtr<UStaticMeshComponent> Door;

private:
	FVector ClosedLocation = FVector::ZeroVector;
	float OpenAlpha = 0.f;
	bool bOpening = false;
};
