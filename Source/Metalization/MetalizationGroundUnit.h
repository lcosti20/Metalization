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
	void IssueStopOrder();

	UFUNCTION(BlueprintCallable, Category = "Unit")
	void IssueHarvestOrder(AActor* SourceVent, AActor* DestinationColony);

	UFUNCTION(BlueprintCallable, Category = "Unit")
	void IssueAttackOrder(AActor* TargetActor);

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

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Unit", meta = (AllowPrivateAccess = "true"))
	float MoveSpeed = 180.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Unit", meta = (AllowPrivateAccess = "true"))
	float SurfaceOffset = 18.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Unit", meta = (AllowPrivateAccess = "true"))
	float ReachDistance = 8.f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Unit", meta = (AllowPrivateAccess = "true"))
	bool bIsSelected = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Unit", meta = (AllowPrivateAccess = "true"))
	int32 TeamId = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Unit", meta = (AllowPrivateAccess = "true"))
	float CaptureRate = 30.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Unit", meta = (AllowPrivateAccess = "true"))
	float GatherRate = 20.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Unit", meta = (AllowPrivateAccess = "true"))
	float CarryCapacity = 100.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Unit", meta = (AllowPrivateAccess = "true"))
	float MaxHealth = 120.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Unit", meta = (AllowPrivateAccess = "true"))
	float CurrentHealth = 120.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Unit", meta = (AllowPrivateAccess = "true"))
	float AttackDamage = 22.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Unit", meta = (AllowPrivateAccess = "true"))
	float AttackRange = 95.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Unit", meta = (AllowPrivateAccess = "true"))
	float AttackCooldown = 0.65f;

	TWeakObjectPtr<UPlanetSurfaceSubsystem> SurfaceSubsystem;
	TArray<FVector> PathDirections;
	FVector PendingMoveTargetDirection = FVector::UpVector;
	int32 CurrentWaypointIndex = INDEX_NONE;
	EMetalizationUnitTask CurrentTask = EMetalizationUnitTask::Idle;
	TWeakObjectPtr<AMetalizationStructure> CurrentTargetStructure;
	TWeakObjectPtr<AMetalizationStructure> HarvestSource;
	TWeakObjectPtr<AMetalizationStructure> DeliveryTarget;
	TWeakObjectPtr<AActor> AttackTarget;
	float CarriedEnergy = 0.f;
	float AttackCooldownRemaining = 0.f;
};
