// Game mode for the capital ship battles. Players spawn aboard their own team's ship: in the player
// starts of its streamed interior, so joining is held back until every ship's interior has loaded.
// Tickets, win conditions and the battle HUD build on this (see Docs/SpaceBattle_Plan.md).

#pragma once

#include "CoreMinimal.h"
#include "MetalGameMode.h"
#include "MetalSpaceBattleGameMode.generated.h"

class AMetalCapitalShip;

UCLASS()
class AMetalSpaceBattleGameMode : public AMetalGameMode
{
	GENERATED_BODY()

public:
	/** The side the player fights for; they spawn aboard this team's ship. */
	UPROPERTY(EditDefaultsOnly, Category="Metalization")
	EMetalTeam PlayerTeam = EMetalTeam::Heroes;

	/** Spawn players anyway (wherever the base game mode can) if the interiors take longer than this. */
	UPROPERTY(EditDefaultsOnly, Category="Metalization")
	float InteriorLoadTimeout = 10.f;

	AMetalCapitalShip* GetShip(EMetalTeam Team) const;
	bool AreInteriorsReady() const;

	virtual void HandleStartingNewPlayer_Implementation(APlayerController* NewPlayer) override;
	virtual AActor* ChoosePlayerStart_Implementation(AController* Player) override;
	/** Never reuse the start spot picked at login: the interiors (and their player starts) weren't loaded yet. */
	virtual bool ShouldSpawnAtStartSpot(AController* Player) override { return false; }
	virtual void Tick(float DeltaSeconds) override;

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	void OnInteriorReady(AMetalCapitalShip* Ship);
	void StartPendingPlayers();

	TArray<TWeakObjectPtr<APlayerController>> PendingPlayers;
	FDelegateHandle InteriorReadyHandle;
};
