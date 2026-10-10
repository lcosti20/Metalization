#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "MetalizationStructure.generated.h"

class UStaticMeshComponent;

UCLASS()
class METALIZATION_API AMetalizationStructure : public AActor
{
	GENERATED_BODY()

public:
	AMetalizationStructure();

	virtual void Tick(float DeltaSeconds) override;

	UFUNCTION(BlueprintCallable, Category = "Structure")
	void SetSurfaceDetails(AActor* InPlanet, const FVector& InDirection, float InRadius);

	UFUNCTION(BlueprintCallable, Category = "Structure")
	void SetTeamId(int32 NewTeamId) { TeamId = NewTeamId; }

	UFUNCTION(BlueprintPure, Category = "Structure")
	int32 GetTeamId() const { return TeamId; }

	UFUNCTION(BlueprintCallable, Category = "Structure")
	bool IsOwnedByTeam(int32 InTeamId) const { return TeamId == InTeamId; }

	UFUNCTION(BlueprintCallable, Category = "Structure")
	float TakeDamage(float Amount, int32 DamageSourceTeamId = -1);

	UFUNCTION(BlueprintPure, Category = "Structure")
	bool IsAlive() const { return CurrentHealth > 0.f; }

	UFUNCTION(BlueprintCallable, Category = "Structure")
	void ApplyCaptureProgress(float Amount, int32 CapturingTeamId);

	UFUNCTION(BlueprintPure, Category = "Structure")
	float GetCaptureProgress() const { return CaptureProgress; }

	UFUNCTION(BlueprintCallable, Category = "Structure")
	void AddStoredEnergy(float Amount) { StoredEnergy += Amount; }

	UFUNCTION(BlueprintPure, Category = "Structure")
	float GetStoredEnergy() const { return StoredEnergy; }

	UFUNCTION(BlueprintPure, Category = "Structure")
	FVector GetSurfaceDirection() const { return SurfaceDirection; }

	UFUNCTION(BlueprintCallable, Category = "Structure")
	bool IsStructureNear(const FVector& WorldLocation, float DistanceThreshold) const;

	UFUNCTION(BlueprintCallable, Category = "Structure")
	void SetHealth(float NewHealth) { CurrentHealth = NewHealth; }

protected:
	virtual void BeginPlay() override;

	void UpdateSurfaceTransform();
	void HandleStructureDefense(float DeltaSeconds);
	void HandleStructureDestruction();

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Structure", meta = (AllowPrivateAccess = "true"))
	USceneComponent* RootScene;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Structure", meta = (AllowPrivateAccess = "true"))
	UStaticMeshComponent* VisualMesh;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Structure")
	float CaptureRadius = 80.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Structure")
	float CaptureProgressRequired = 100.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Structure")
	float CaptureRate = 30.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Structure")
	float MaxHealth = 180.f;

	UPROPERTY(VisibleAnywhere, BlueprintReadWrite, Category = "Structure")
	float CurrentHealth = 180.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Structure")
	float DefenseDamage = 16.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Structure")
	float DefenseRange = 150.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Structure")
	float DefenseCooldown = 1.0f;

	UPROPERTY(VisibleAnywhere, BlueprintReadWrite, Category = "Structure")
	int32 TeamId = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadWrite, Category = "Structure")
	float CaptureProgress = 0.f;

	UPROPERTY(VisibleAnywhere, BlueprintReadWrite, Category = "Structure")
	float StoredEnergy = 0.f;

	float DefenseCooldownRemaining = 0.f;

	TWeakObjectPtr<AActor> PlanetActor;
	FVector SurfaceDirection = FVector::UpVector;
	float PlanetRadius = 1000.f;
};

UCLASS()
class METALIZATION_API AMetalizationEnergyVent : public AMetalizationStructure
{
	GENERATED_BODY()

public:
	AMetalizationEnergyVent();

	UFUNCTION(BlueprintCallable, Category = "Structure")
	float HarvestEnergy(float Amount);

	UFUNCTION(BlueprintPure, Category = "Structure")
	bool HasEnergy() const { return StoredEnergy > 0.f; }

protected:
	virtual void BeginPlay() override;

private:
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vent")
	float MaxEnergy = 2200.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vent")
	float HarvestRate = 35.f;
};

UCLASS()
class METALIZATION_API AMetalizationColony : public AMetalizationStructure
{
	GENERATED_BODY()

public:
	AMetalizationColony();

	virtual void Tick(float DeltaSeconds) override;

	UFUNCTION(BlueprintCallable, Category = "Structure")
	void AddProduction(float Amount) { StoredEnergy += Amount; }

	UFUNCTION(BlueprintCallable, Category = "Structure")
	float DepositEnergy(float Amount);

protected:
	virtual void BeginPlay() override;

private:
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Colony")
	float ProductionPerSecond = 10.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Colony")
	float MaxStoredEnergy = 6000.f;
};
