#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "MetalizationGroundUnit.generated.h"

class UStaticMeshComponent;
class UPlanetSurfaceSubsystem;
class AMetalizationStructure;

UENUM(BlueprintType)
enum class EMetalizationUnitTask : uint8
{
	Idle,
	Move,
	Capture,
	Harvest,
	Attack
};

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
	void IssueCaptureOrder(AActor* TargetStructure);

	UFUNCTION(BlueprintCallable, Category = "Unit")
	void IssueHarvestOrder(AActor* SourceVent, AActor* DestinationColony);

	UFUNCTION(BlueprintCallable, Category = "Unit")
	void IssueAttackOrder(AActor* TargetUnit);

	UFUNCTION(BlueprintCallable, Category = "Unit")
	bool HasMovementPath() const { return CurrentWaypointIndex != INDEX_NONE; }

	UFUNCTION(BlueprintCallable, Category = "Unit")
	void ApplyDamage(float DamageAmount, int32 DamageSourceTeamId = -1);

	UFUNCTION(BlueprintPure, Category = "Unit")
	bool IsAlive() const { return CurrentHealth > 0.f; }

	UFUNCTION(BlueprintCallable, Category = "Unit")
	void SetMoveSpeed(float NewSpeed) { MoveSpeed = NewSpeed; }

	UFUNCTION(BlueprintCallable, Category = "Unit")
	void SetSelected(bool bSelected);

	UFUNCTION(BlueprintPure, Category = "Unit")
	bool IsSelected() const { return bIsSelected; }

	UFUNCTION(BlueprintPure, Category = "Unit")
	float GetAttackRange() const { return AttackRange; }

	UFUNCTION(BlueprintCallable, Category = "Unit")
	void SetTeamId(int32 NewTeamId) { TeamId = NewTeamId; }

	UFUNCTION(BlueprintPure, Category = "Unit")
	int32 GetTeamId() const { return TeamId; }

protected:
	virtual void BeginPlay() override;
	virtual void Destroyed() override;

private:
	void StopMovement();
	void UpdateMovement(float DeltaSeconds);
	void UpdateTaskState(float DeltaSeconds);
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
	float ReachDistance = 8.f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Unit")
	bool bIsSelected = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Unit")
	int32 TeamId = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Unit")
	float CaptureRate = 25.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Unit")
	float GatherRate = 18.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Unit")
	float CarryCapacity = 100.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Unit")
	float MaxHealth = 100.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Unit")
	float CurrentHealth = 100.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Unit")
	float AttackDamage = 18.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Unit")
	float AttackRange = 80.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Unit")
	float AttackCooldown = 0.75f;

	TWeakObjectPtr<UPlanetSurfaceSubsystem> SurfaceSubsystem;
	TArray<FVector> PathDirections;
	FVector PendingMoveTargetDirection = FVector::UpVector;
	int32 CurrentWaypointIndex = INDEX_NONE;
	EMetalizationUnitTask CurrentTask = EMetalizationUnitTask::Idle;
	TWeakObjectPtr<AMetalizationStructure> CurrentTargetStructure;
	TWeakObjectPtr<AMetalizationStructure> HarvestSource;
	TWeakObjectPtr<AMetalizationStructure> DeliveryTarget;
	TWeakObjectPtr<AMetalizationGroundUnit> AttackTarget;
	float CarriedEnergy = 0.f;
	float AttackCooldownRemaining = 0.f;
};
