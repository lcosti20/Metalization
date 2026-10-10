// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "MetalizationGameMode.generated.h"

class AMetalizationFactionSpawner;

UCLASS()
class METALIZATION_API AMetalizationGameMode : public AGameModeBase
{
	GENERATED_BODY()

public:
	AMetalizationGameMode();

	virtual void BeginPlay() override;

protected:
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Setup")
	FName PlanetTag = TEXT("Planet");

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Setup")
	float PlanetRadius = 1000.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Setup")
	int32 PlayerTeamId = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Setup")
	int32 EnemyTeamId = 1;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Setup")
	TSubclassOf<AMetalizationFactionSpawner> FactionSpawnerClass;

private:
	void SetupPlanetAndSpawners();
};


