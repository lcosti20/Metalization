// Designer-configured mission: an ordered list of objectives placed in the level.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "MetalDialogue.h"
#include "MetalMission.generated.h"

class AMetalGameMode;
class UMetalWeaponData;
class UBillboardComponent;

/** What the player has to do to complete an objective. Checked by AMetalMission::IsObjectiveComplete. */
UENUM(BlueprintType)
enum class EMetalObjectiveType : uint8
{
	/** Destroy every robot carrying TargetTag. */
	EliminateTagged,
	/** Get the player's current body into TargetActor (an AMetalTriggerZone). */
	ReachZone,
	/** Activate TargetActor (an AMetalControlConsole). */
	ActivateConsole,
	/** Take control of any robot carrying TargetTag with the Hack Tether. */
	HijackTagged
};

/** One step of a mission. Which of TargetTag / TargetActor is used depends on Type. */
USTRUCT(BlueprintType)
struct FMetalObjective
{
	GENERATED_BODY()

	/** Shown on the HUD while the objective is active. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Objective")
	FText Description;

	/** What completes the objective. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Objective")
	EMetalObjectiveType Type = EMetalObjectiveType::ReachZone;

	/** Actor tag for EliminateTagged and HijackTagged (HijackTagged with no tag accepts any robot). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Objective")
	FName TargetTag;

	/** Trigger zone for ReachZone, console for ActivateConsole. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Objective")
	TObjectPtr<AActor> TargetActor;

	/** Played when the objective becomes active. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Objective")
	TArray<FMetalDialogueLine> StartDialogue;

	/** Control tip shown on the HUD while the objective is active, e.g. "Hold [Shift] to sprint". */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Objective")
	FText Hint;

	/** Point the HUD's objective marker at the target (zone, console or the nearest tagged robot). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Objective")
	bool bShowMarker = true;

	/** Played when the objective is completed (before the next objective's StartDialogue). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Objective")
	TArray<FMetalDialogueLine> CompleteDialogue;

	/**
	 * Active at the same time as the next objective. A run of objectives joined this way is a group: the
	 * player can complete them in any order (subject to Requires), and the mission only moves past the
	 * group once every objective in it is complete.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Objective")
	bool bWithNext = false;

	/** Name other objectives in the group use in their Requires. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Objective")
	FName Id;

	/**
	 * Ids of objectives that must be complete before this one unlocks. A locked objective is listed greyed
	 * out, isn't checked and its StartDialogue waits until it unlocks.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Objective")
	TArray<FName> Requires;

	/** Saves a checkpoint where the player's own body stands (and repairs it) when this objective completes. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Objective")
	bool bCheckpointOnComplete = false;
};

/** How an objective in the active group is shown on the HUD. */
enum class EMetalObjectiveState : uint8
{
	Active,
	/** Waiting on its Requires. */
	Locked,
	Done
};

/** One line of the HUD's objective list. */
struct FMetalObjectiveStatus
{
	int32 Index = INDEX_NONE;
	EMetalObjectiveState State = EMetalObjectiveState::Active;
};

/**
 * Placed once per level. Holds the ordered objectives and their dialogue; AMetalGameMode starts it at
 * BeginPlay, and it polls the active objective four times a second, advancing when it is done and
 * completing the mission after the last one. Levels without one simply have no objectives.
 */
UCLASS()
class AMetalMission : public AActor
{
	GENERATED_BODY()

public:
	/** Creates the editor billboard; the actor is hidden in game. */
	AMetalMission();

	/** Shown as a banner when the mission starts. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Mission")
	FText MissionName;

	/** Objectives in the order they must be completed. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Mission")
	TArray<FMetalObjective> Objectives;

	/** Played after the last objective. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Mission")
	TArray<FMetalDialogueLine> CompletionDialogue;

	/**
	 * Replaces the player's archetype loadout at the start of the mission (so a tutorial can hand weapons
	 * out one at a time). Empty keeps the archetype's starting weapons.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Mission")
	TArray<TObjectPtr<UMetalWeaponData>> PlayerStartingWeapons;

	/** Grenades the player starts with; negative keeps the archetype's count. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Mission")
	int32 PlayerStartingGrenades = -1;

	/** @return Control tips of the active (unlocked, unfinished) objectives, in order, without empties. */
	TArray<FText> GetCurrentHints() const;

	/**
	 * Where the HUD's objective markers should point: one per active objective that has a marker.
	 *
	 * @param OutLocations World positions of the markers.
	 */
	void GetObjectiveMarkers(TArray<FVector>& OutLocations) const;

	/** @return Every objective of the current group with its state, in order (empty when finished). */
	TArray<FMetalObjectiveStatus> GetObjectiveList() const;

	/** @return Index of the first objective of the current group (INDEX_NONE before the start, Objectives.Num() when done). */
	int32 GetCurrentObjectiveIndex() const { return CurrentObjective; }

	/** @return Indices of the current group's objectives that are already complete. */
	TArray<int32> GetCompletedInGroup() const;

	/**
	 * Shows the mission banner and activates the first objective (or the one given by -MetalObjective=N
	 * on the command line in non-shipping builds).
	 *
	 * @param InGameMode Game mode that receives messages and the completion call.
	 */
	void StartMission(AMetalGameMode* InGameMode);

	/**
	 * Resumes the mission at a checkpoint snapshot's objective instead of StartMission: no banner, no
	 * start dialogue, and the starting loadout is not handed out (the snapshot restores the player's own).
	 * If that objective is already met when it is first checked (the player respawns inside the zone that
	 * completes it), it advances without the "Objective complete" notice.
	 *
	 * @param InGameMode Game mode that receives messages and the completion call.
	 * @param Index      First objective of the group to resume at.
	 * @param Completed  Objectives of that group that were already complete.
	 */
	void RestoreObjective(AMetalGameMode* InGameMode, int32 Index, const TArray<int32>& Completed);

	/** @return Description of the first active objective, or empty when finished. */
	FText GetCurrentObjectiveText() const;

	/** Every 0.25 s checks the active objectives and advances past the group when all are complete. */
	virtual void Tick(float DeltaSeconds) override;

private:
	/**
	 * Makes the group starting at Index current and plays the start dialogue of its unlocked objectives.
	 * An index past the end plays the completion dialogue and completes the mission.
	 *
	 * @param Index First objective of the group.
	 */
	void ActivateObjective(int32 Index);

	/** @return Last index of the group that starts at First (follows bWithNext). */
	int32 GroupEnd(int32 First) const;

	/** @return True if Index is in the current group, not done and its Requires are all done. */
	bool IsActive(int32 Index) const;

	/** @return True if every objective named in Requires is complete (or comes before the current group). */
	bool RequirementsMet(const FMetalObjective& Objective) const;

	/** Plays the start dialogue of current-group objectives that have just unlocked (once each). */
	void AnnounceUnlocked();

	/**
	 * @param Objective Objective to test.
	 * @return True if its condition currently holds.
	 */
	bool IsObjectiveComplete(const FMetalObjective& Objective) const;

	/**
	 * Queues the lines on the world's UMetalDialogueSubsystem.
	 *
	 * @param Lines Dialogue to play, in order.
	 */
	void PlayDialogue(const TArray<FMetalDialogueLine>& Lines) const;

	/** Editor icon so the mission actor can be found in the level. */
	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UBillboardComponent> Sprite;

	/** Game mode that started the mission; receives its messages. */
	TWeakObjectPtr<AMetalGameMode> GameMode;
	/** First objective of the current group; INDEX_NONE before StartMission(). */
	int32 CurrentObjective = INDEX_NONE;
	/** Per objective: complete (only meaningful for the current group; earlier ones count as done). */
	TArray<bool> Done;
	/** Per objective: its StartDialogue has played (or was skipped by a checkpoint restore). */
	TArray<bool> Announced;
	/** World time of the next objective check. */
	float NextCheckTime = 0.f;
	/** PlayerStartingWeapons has been given to the first player pawn. */
	bool bLoadoutApplied = false;
	/** World time until which completions are silent: the first checks after RestoreObjective. */
	float QuietUntil = -1.f;
};
