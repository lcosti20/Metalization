#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameStateBase.h"
#include "MetalizationGameState.generated.h"

UCLASS()
class METALIZATION_API AMetalizationGameState : public AGameStateBase
{
	GENERATED_BODY()

public:
	AMetalizationGameState();

	UFUNCTION(BlueprintCallable, Category = "Game State")
	void RegisterTeam(int32 TeamId, float StartingEnergy = 0.f);

	UFUNCTION(BlueprintCallable, Category = "Game State")
	void AddTeamEnergy(int32 TeamId, float Amount);

	UFUNCTION(BlueprintCallable, Category = "Game State")
	void SpendTeamEnergy(int32 TeamId, float Amount);

	UFUNCTION(BlueprintPure, Category = "Game State")
	float GetTeamEnergy(int32 TeamId) const;

	UFUNCTION(BlueprintCallable, Category = "Game State")
	void SetTeamEnergy(int32 TeamId, float NewEnergy);

	UFUNCTION(BlueprintCallable, Category = "Game State")
	void SetTeamUnitCount(int32 TeamId, int32 NewCount);

	UFUNCTION(BlueprintPure, Category = "Game State")
	int32 GetTeamUnitCount(int32 TeamId) const;

	UFUNCTION(BlueprintCallable, Category = "Game State")
	void IncrementUnitCount(int32 TeamId, int32 Delta = 1);

	UFUNCTION(BlueprintPure, Category = "Game State")
	bool HasTeam(int32 TeamId) const;

	UFUNCTION(BlueprintCallable, Category = "Game State")
	void DeclareWinner(int32 TeamId);

	UFUNCTION(BlueprintPure, Category = "Game State")
	bool IsGameOver() const;

	UFUNCTION(BlueprintPure, Category = "Game State")
	int32 GetWinnerTeamId() const;

private:
	UPROPERTY()
	TMap<int32, float> TeamEnergy;

	UPROPERTY()
	TMap<int32, int32> TeamUnitCounts;

	UPROPERTY()
	int32 WinnerTeamId = -1;
};
