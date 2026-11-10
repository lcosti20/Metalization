// Checkpoint snapshots: the state of a level when the player reached a checkpoint, kept across the level
// reload that follows a death so the level can be put back exactly as it was.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "MetalBotCharacter.h"
#include "MetalCheckpoint.generated.h"

class AMetalGameMode;

/** A robot that was still standing when the snapshot was taken. */
USTRUCT()
struct FMetalBotSnapshot
{
	GENERATED_BODY()

	/** Where it stood. */
	UPROPERTY()
	FTransform Transform;

	/** Hit points it had. */
	UPROPERTY()
	float Health = 0.f;

	/** It was powered down (an ally the player had linked to and let go). */
	UPROPERTY()
	bool bDormant = false;
};

/**
 * Everything about a level that a checkpoint restores. Actors are identified by their path inside the
 * world ("PersistentLevel.Bot_Shooter3"), which is the same every time the map loads, so only actors
 * placed in the map (not ones spawned while playing) can be recorded.
 */
USTRUCT()
struct FMetalLevelSnapshot
{
	GENERATED_BODY()

	/** World->GetMapName() of the level the snapshot belongs to. */
	UPROPERTY()
	FString MapName;

	/** Where the player respawns (the checkpoint zone's centre, facing the way they entered). */
	UPROPERTY()
	FTransform PlayerTransform;

	/** The player's weapons when they reached the checkpoint. */
	UPROPERTY()
	TArray<FMetalWeaponSlot> Weapons;

	/** Equipped slot of Weapons. */
	UPROPERTY()
	int32 WeaponIndex = 0;

	/** The player's grenades. */
	UPROPERTY()
	int32 Grenades = 0;

	/** AMetalMission's active objective. */
	UPROPERTY()
	int32 ObjectiveIndex = INDEX_NONE;

	/** Villains the player had destroyed. */
	UPROPERTY()
	int32 Kills = 0;

	/** Placed actors that still existed (living robots, pickups not yet taken, ...). Any other placed actor is removed on restore. */
	UPROPERTY()
	TSet<FString> LiveActors;

	/** Living robots, by actor path. */
	UPROPERTY()
	TMap<FString, FMetalBotSnapshot> Bots;

	/** Consoles that had been activated (their gates open). */
	UPROPERTY()
	TSet<FString> ActivatedConsoles;

	/** Checkpoint zones already used. */
	UPROPERTY()
	TSet<FString> TriggeredZones;

	/** Zones whose dialogue had already played. */
	UPROPERTY()
	TSet<FString> PlayedDialogueZones;
};

/**
 * Holds the last checkpoint snapshot for the game instance, so it survives the level reload. On a death
 * AMetalGameMode marks a retry and reloads the map; the new game mode then takes the snapshot back
 * (TakeRetry) and puts the level into that state in two passes around BeginPlay (ApplyBeforeBeginPlay,
 * ApplyAfterBeginPlay). Any other level load (restart, level select, title) clears it.
 */
UCLASS()
class UMetalCheckpointSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	/**
	 * Records the level as it is now as the new checkpoint.
	 *
	 * @param GameMode        Game mode of the level (mission progress, kills).
	 * @param PlayerBody      The player's own body (its loadout is saved).
	 * @param PlayerTransform Where the player respawns.
	 */
	void Capture(const AMetalGameMode* GameMode, const AMetalBotCharacter* PlayerBody, const FTransform& PlayerTransform);

	/**
	 * Marks that the next load of this level is a retry after a death. Called just before the reload.
	 *
	 * @param Deaths  Deaths so far, carried over to the reloaded level.
	 * @param Elapsed Mission time so far, carried over so the results clock keeps counting.
	 */
	void PrepareRetry(int32 Deaths, float Elapsed);

	/**
	 * Called by the new game mode as the level loads. Clears everything unless a retry of this map is pending.
	 *
	 * @param World       The world being loaded.
	 * @param OutDeaths   Deaths carried over (unchanged without a retry).
	 * @param OutElapsed  Mission time carried over (unchanged without a retry).
	 * @return The snapshot to restore, or null (no retry, or the player died before any checkpoint).
	 */
	const FMetalLevelSnapshot* TakeRetry(const UWorld* World, int32& OutDeaths, float& OutElapsed);

	/**
	 * First restore pass, before any actor's BeginPlay: removes placed actors that were gone, moves the
	 * robots back and marks used zones (so standing in them at spawn does not trigger them again).
	 *
	 * @param World    The reloaded world.
	 * @param Snapshot What to restore.
	 */
	static void ApplyBeforeBeginPlay(UWorld* World, const FMetalLevelSnapshot& Snapshot);

	/**
	 * Second pass, after every BeginPlay (which resets health, screens and loadouts): restores robot health,
	 * consoles and their gates, and the player's loadout.
	 *
	 * @param World    The reloaded world.
	 * @param Snapshot What to restore.
	 */
	static void ApplyAfterBeginPlay(UWorld* World, const FMetalLevelSnapshot& Snapshot);

	/**
	 * @param Actor A placed actor.
	 * @return Its stable key in snapshots.
	 */
	static FString KeyOf(const AActor* Actor);

private:
	/** The last checkpoint reached, valid when bHasSnapshot. */
	UPROPERTY()
	FMetalLevelSnapshot Snapshot;

	/** A checkpoint has been captured in the current level. */
	bool bHasSnapshot = false;

	/** The next load is a retry (set by PrepareRetry). */
	bool bRetryPending = false;

	/** Map the retry is for. */
	FString RetryMap;

	/** Deaths to carry into the retry. */
	int32 RetryDeaths = 0;

	/** Mission time to carry into the retry. */
	float RetryElapsed = 0.f;
};
