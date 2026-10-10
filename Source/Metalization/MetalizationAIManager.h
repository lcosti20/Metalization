#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "MetalizationAIManager.generated.h"

class AMetalizationGroundUnit;
class AMetalizationStructure;
class AMetalizationEnergyVent;
class AMetalizationColony;

UCLASS()
class METALIZATION_API AMetalizationAIManager : public AActor
{
	GENERATED_BODY()

public:
	AMetalizationAIManager();

	virtual void Tick(float DeltaSeconds) override;

	UFUNCTION(BlueprintCallable, Category = "AI")
	void SetTeamId(int32 NewTeamId) { TeamId = NewTeamId; }

	UFUNCTION(BlueprintPure, Category = "AI")
	int32 GetTeamId() const { return TeamId; }

protected:
	virtual void BeginPlay() override;

private:
	void UpdateAIOrders();
	AMetalizationGroundUnit* GetNextUnitToOrder() const;
	AMetalizationEnergyVent* FindNearestOwnedVent(const AMetalizationGroundUnit* Unit) const;
	AMetalizationColony* FindNearestOwnedColony(const AMetalizationGroundUnit* Unit) const;
	AMetalizationStructure* FindNearestEnemyStructure(const AMetalizationGroundUnit* Unit) const;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AI")
	int32 TeamId = 1;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AI")
	float OrderCheckInterval = 1.5f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AI")
	float StructureRange = 5000.f;

	float TimeSinceLastOrderCheck = 0.f;
};
