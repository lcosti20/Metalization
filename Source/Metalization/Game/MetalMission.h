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

	/** @return The active objective's control tip, or empty. */
	FText GetCurrentHint() const;

	/**
	 * Where the HUD's objective marker should point.
	 *
	 * @param OutLocation World position of the marker.
	 * @return False when the active objective has no marker (or there is none).
	 */
	bool GetObjectiveMarker(FVector& OutLocation) const;

	/** @return Index of the active objective (INDEX_NONE before the start, Objectives.Num() when done). */
	int32 GetCurrentObjectiveIndex() const { return CurrentObjective; }

	/**
	 * Shows the mission banner and activates the first objective (or the one given by -MetalObjective=N
	 * on the command line in non-shipping builds).
	 *
	 * @param InGameMode Game mode that receives messages and the completion call.
	 */
	void StartMission(AMetalGameMode* InGameMode);

	/** @return Description of the active objective, or empty when finished. */
	FText GetCurrentObjectiveText() const;

	/** Every 0.25 s checks the active objective and advances when it is complete. */
	virtual void Tick(float DeltaSeconds) override;

private:
	/**
	 * Makes Index the active objective and plays its start dialogue. An index past the end plays the
	 * completion dialogue and completes the mission.
	 *
	 * @param Index Objective to activate.
	 */
	void ActivateObjective(int32 Index);

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
	/** Index into Objectives; INDEX_NONE before StartMission(). */
	int32 CurrentObjective = INDEX_NONE;
	/** World time of the next objective check. */
	float NextCheckTime = 0.f;
	/** PlayerStartingWeapons has been given to the first player pawn. */
	bool bLoadoutApplied = false;
};
