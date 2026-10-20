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

/**
 * Game mode for space-battle maps (two AMetalCapitalShip actors). Holds players until the ship interiors
 * stream in, spawns them aboard their team's ship, keeps each team's reinforcement tickets and declares
 * victory or defeat through AMetalGameMode::CompleteMission.
 */
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

	/** Tickets each team starts with. */
	UPROPERTY(EditDefaultsOnly, Category="Metalization|Tickets")
	int32 StartingTickets = 150;

	/** Tickets lost when a shield generator, the engines or the comms are destroyed. */
	UPROPERTY(EditDefaultsOnly, Category="Metalization|Tickets")
	int32 SubsystemTicketPenalty = 20;

	/** Tickets lost when life support is destroyed (before the bleed starts). */
	UPROPERTY(EditDefaultsOnly, Category="Metalization|Tickets")
	int32 LifeSupportTicketPenalty = 30;

	/** With life support gone, the team loses a ticket this often (seconds). */
	UPROPERTY(EditDefaultsOnly, Category="Metalization|Tickets")
	float LifeSupportBleedInterval = 4.f;

	/**
	 * @param Team Team to look up.
	 * @return That team's capital ship, or null.
	 */
	AMetalCapitalShip* GetShip(EMetalTeam Team) const;

	/** @return True when every ship that has an interior has finished streaming it in. */
	bool AreInteriorsReady() const;

	/** @return Remaining tickets for Team. */
	int32 GetTickets(EMetalTeam Team) const { return Tickets.FindRef(Team); }

	/** @return True once a winner has been decided. */
	bool IsBattleOver() const { return bBattleOver; }

	/** @return The winning team, or Neutral while the battle is on. */
	EMetalTeam GetWinner() const { return Winner; }

	/**
	 * Removes tickets from a team (clamped at zero) and ends the battle if they run out.
	 *
	 * @param Team   Team that pays.
	 * @param Amount Tickets to remove; ignored if not positive or the battle is over.
	 */
	void SpendTickets(EMetalTeam Team, int32 Amount);

	/** Queues the player in PendingPlayers until the interiors are ready, then starts them normally. */
	virtual void HandleStartingNewPlayer_Implementation(APlayerController* NewPlayer) override;

	/** Picks a random unoccupied player start inside PlayerTeam's ship interior. */
	virtual AActor* ChoosePlayerStart_Implementation(AController* Player) override;
	/** Never reuse the start spot picked at login: the interiors (and their player starts) weren't loaded yet. */
	virtual bool ShouldSpawnAtStartSpot(AController* Player) override { return false; }
	/** Respawns the player as usual unless the battle is already over. */
	virtual void PlayerDied(AMetalPlayerController* Controller, AMetalBotCharacter* Body) override;

	/** Bleeds tickets from teams without life support and enforces InteriorLoadTimeout. */
	virtual void Tick(float DeltaSeconds) override;

protected:
	/** Sets the starting tickets and subscribes to ship and robot events. */
	virtual void BeginPlay() override;

	/** Unsubscribes from ship and robot events. */
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	/** @param Ship The ship whose interior just loaded. Starts waiting players once all are ready. */
	void OnInteriorReady(AMetalCapitalShip* Ship);

	/**
	 * Announces the loss, charges the ticket penalty and, for life support, starts the ticket bleed.
	 *
	 * @param Ship      Ship that lost the part.
	 * @param Subsystem The destroyed part.
	 */
	void OnSubsystemDestroyed(AMetalCapitalShip* Ship, UMetalShipSubsystemComponent* Subsystem);

	/** @param Ship The destroyed ship; its team loses all remaining tickets. */
	void OnShipDestroyed(AMetalCapitalShip* Ship);

	/**
	 * Charges one ticket to the robot's base team if it was a combatant: any controlled walker, or a
	 * Stalker flown by an AI pilot. Player-flown Stalkers and empty parked ones are free.
	 *
	 * @param Bot    Robot that died.
	 * @param Killer Controller credited with the kill.
	 */
	void OnCombatantDied(AMetalBotCharacter* Bot, AController* Killer);

	/** Runs the normal player start for everyone in PendingPlayers. */
	void StartPendingPlayers();

	/**
	 * Ends the battle once and shows VICTORY or DEFEAT.
	 *
	 * @param InWinner The winning team.
	 */
	void EndBattle(EMetalTeam InWinner);

	/** Players who joined before the interiors were ready. */
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
