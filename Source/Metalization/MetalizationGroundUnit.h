#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "MetalizationGroundUnit.generated.h"

class UStaticMeshComponent;
class UPlanetSurfaceSubsystem;

UCLASS()
class METALIZATION_API AMetalizationGroundUnit : public AActor
{
	GENERATED_BODY()

public:
	AMetalizationGroundUnit();

	virtual void Tick(float DeltaSeconds) override;

	UFUNCTION(BlueprintCallable, Category = "Unit")
	void IssueMoveToWorldLocation(const FVector& WorldLocation);

	UFUNCTION(BlueprintCallable, Category = "Unit")
	void IssueMoveToDirection(const FVector& Direction);

	UFUNCTION(BlueprintCallable, Category = "Unit")
	bool HasMovementPath() const { return CurrentWaypointIndex != INDEX_NONE; }

	UFUNCTION(BlueprintCallable, Category = "Unit")
	void SetMoveSpeed(float NewSpeed) { MoveSpeed = NewSpeed; }

protected:
	virtual void BeginPlay() override;

private:
	void StopMovement();
	void UpdateMovement(float DeltaSeconds);
	void ApplySurfaceTransform(const FVector& NewDirection, float Offset = 0.f);
	void SetPath(const TArray<FVector>& InPath);

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Unit", meta = (AllowPrivateAccess = "true"))
	USceneComponent* RootScene;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Unit", meta = (AllowPrivateAccess = "true"))
	UStaticMeshComponent* VisualMesh;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Unit")
	float MoveSpeed = 160.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Unit")
	float SurfaceOffset = 18.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Unit")
	float TurnSpeed = 9.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Unit")
	float ReachDistance = 8.f;

	TWeakObjectPtr<UPlanetSurfaceSubsystem> SurfaceSubsystem;
	TArray<FVector> PathDirections;
	int32 CurrentWaypointIndex = INDEX_NONE;
};
