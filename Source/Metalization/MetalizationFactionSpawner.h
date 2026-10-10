#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "MetalizationFactionSpawner.generated.h"

class AMetalizationGroundUnit;
class AMetalizationEnergyVent;
class AMetalizationColony;
class AMetalizationAIManager;

UCLASS()
class METALIZATION_API AMetalizationFactionSpawner : public AActor
{
	GENERATED_BODY()

public:
	AMetalizationFactionSpawner();

	UFUNCTION(BlueprintCallable, Category = "Faction")
	void SetPlanetReference(AActor* InPlanet, float InRadius = 1000.f);

	UFUNCTION(BlueprintCallable, Category = "Faction")
	void SetTeamId(int32 NewTeamId) { TeamId = NewTeamId; }

	UFUNCTION(BlueprintCallable, Category = "Faction")
	AMetalizationGroundUnit* SpawnUnitAtLocation(const FVector& WorldLocation);

	UFUNCTION(BlueprintCallable, Category = "Faction")
	AMetalizationEnergyVent* SpawnVentAtLocation(const FVector& WorldLocation);

	UFUNCTION(BlueprintCallable, Category = "Faction")
	AMetalizationColony* SpawnColonyAtLocation(const FVector& WorldLocation);

	UFUNCTION(BlueprintCallable, Category = "Faction")
	void SpawnStartSetup();

protected:
	virtual void BeginPlay() override;

private:
	FVector SurfaceDirectionForWorld(const FVector& WorldLocation) const;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Faction")
	int32 TeamId = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Faction")
	TSubclassOf<AMetalizationGroundUnit> UnitClass;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Faction")
	TSubclassOf<AMetalizationEnergyVent> VentClass;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Faction")
	TSubclassOf<AMetalizationColony> ColonyClass;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Faction")
	TSubclassOf<AMetalizationAIManager> AIManagerClass;

	TWeakObjectPtr<AActor> PlanetActor;
	float PlanetRadius = 1000.f;
};
