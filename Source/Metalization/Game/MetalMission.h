// Designer-configured mission: an ordered list of objectives placed in the level.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "MetalMission.generated.h"

class AMetalGameMode;
class UBillboardComponent;

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

USTRUCT(BlueprintType)
struct FMetalDialogueLine
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Dialogue")
	FText Speaker;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Dialogue", meta=(MultiLine=true))
	FText Line;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Dialogue")
	float Duration = 4.f;
};

USTRUCT(BlueprintType)
struct FMetalObjective
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Objective")
	FText Description;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Objective")
	EMetalObjectiveType Type = EMetalObjectiveType::ReachZone;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Objective")
	FName TargetTag;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Objective")
	TObjectPtr<AActor> TargetActor;

	/** Played when the objective becomes active. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Objective")
	TArray<FMetalDialogueLine> StartDialogue;
};

UCLASS()
class AMetalMission : public AActor
{
	GENERATED_BODY()

public:
	AMetalMission();

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Mission")
	FText MissionName;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Mission")
	TArray<FMetalObjective> Objectives;

	/** Played after the last objective. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Mission")
	TArray<FMetalDialogueLine> CompletionDialogue;

	void StartMission(AMetalGameMode* InGameMode);
	FText GetCurrentObjectiveText() const;

	virtual void Tick(float DeltaSeconds) override;

private:
	void ActivateObjective(int32 Index);
	bool IsObjectiveComplete(const FMetalObjective& Objective) const;
	void PlayDialogue(const TArray<FMetalDialogueLine>& Lines) const;

	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UBillboardComponent> Sprite;

	TWeakObjectPtr<AMetalGameMode> GameMode;
	int32 CurrentObjective = INDEX_NONE;
	float NextCheckTime = 0.f;
};
