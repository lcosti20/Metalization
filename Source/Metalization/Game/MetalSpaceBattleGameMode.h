// Game mode for the capital ship battles. Players spawn aboard their own team's ship: in the player
// starts of its streamed interior, so joining is held back until every ship's interior has loaded.
//
// Rules: each team starts with reinforcement tickets. A team loses one every time one of its
// combatants dies (a robot on foot, or a Stalker with an AI pilot aboard; a player who loses their
// Stalker just wakes up in their body), a chunk for every subsystem of its ship that's destroyed, and
// one every few seconds once its life support is gone. A team is beaten when its tickets run out or
// its ship is destroyed. No respawns after that.

#pragma once

#include "CoreMinimal.h"
#include "MetalGameMode.h"
#include "MetalSpaceBattleGameMode.generated.h"

class AMetalCapitalShip;
class UMetalShipSubsystemComponent;

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

	UPROPERTY(EditDefaultsOnly, Category="Metalization|Tickets")
	int32 StartingTickets = 150;

	/** Tickets lost when a shield generator, the engines or the comms are destroyed. */
	UPROPERTY(EditDefaultsOnly, Category="Metalization|Tickets")
	int32 SubsystemTicketPenalty = 20;

	UPROPERTY(EditDefaultsOnly, Category="Metalization|Tickets")
	int32 LifeSupportTicketPenalty = 30;

	/** With life support gone, the team loses a ticket this often (seconds). */
	UPROPERTY(EditDefaultsOnly, Category="Metalization|Tickets")
	float LifeSupportBleedInterval = 4.f;

	AMetalCapitalShip* GetShip(EMetalTeam Team) const;
	bool AreInteriorsReady() const;
	int32 GetTickets(EMetalTeam Team) const { return Tickets.FindRef(Team); }
	bool IsBattleOver() const { return bBattleOver; }
	EMetalTeam GetWinner() const { return Winner; }
	/** Removes tickets from a team (clamped at zero) and ends the battle if they run out. */
	void SpendTickets(EMetalTeam Team, int32 Amount);

	virtual void HandleStartingNewPlayer_Implementation(APlayerController* NewPlayer) override;
	virtual AActor* ChoosePlayerStart_Implementation(AController* Player) override;
	/** Never reuse the start spot picked at login: the interiors (and their player starts) weren't loaded yet. */
	virtual bool ShouldSpawnAtStartSpot(AController* Player) override { return false; }
	virtual void PlayerDied(AMetalPlayerController* Controller, AMetalBotCharacter* Body) override;
	virtual void Tick(float DeltaSeconds) override;

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	void OnInteriorReady(AMetalCapitalShip* Ship);
	void OnSubsystemDestroyed(AMetalCapitalShip* Ship, UMetalShipSubsystemComponent* Subsystem);
	void OnShipDestroyed(AMetalCapitalShip* Ship);
	void OnCombatantDied(AMetalBotCharacter* Bot, AController* Killer);
	void StartPendingPlayers();
	void EndBattle(EMetalTeam InWinner);

	TArray<TWeakObjectPtr<APlayerController>> PendingPlayers;
	TMap<EMetalTeam, int32> Tickets;
	/** Teams whose life support is down, and when they next bleed a ticket. */
	TMap<EMetalTeam, float> NextBleedTime;
	bool bBattleOver = false;
	EMetalTeam Winner = EMetalTeam::Neutral;
	FDelegateHandle InteriorReadyHandle;
	FDelegateHandle SubsystemHandle;
	FDelegateHandle ShipDestroyedHandle;
	FDelegateHandle BotDiedHandle;
};
