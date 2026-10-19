// Mission flow, checkpoints, respawning and on-screen messages for the Metalization demo.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "MetalBotCharacter.h"
#include "MetalGameMode.generated.h"

class AMetalMission;
class AMetalPlayerController;

struct FMetalMessage
{
	FText Speaker;
	FText Text;
	float Duration = 3.f;
};

UCLASS()
class AMetalGameMode : public AGameModeBase
{
	GENERATED_BODY()

public:
	AMetalGameMode();

	/** Dialogue (with a speaker) is queued; system notices (no speaker) replace the current notice. */
	void ShowMessage(const FText& Speaker, const FText& Text, float Duration);
	/** Big centred text ("CHECKPOINT", "MISSION COMPLETE"). */
	void ShowBanner(const FText& Text, float Duration);

	void SetCheckpoint(const FTransform& Transform);
	virtual void PlayerDied(AMetalPlayerController* Controller, AMetalBotCharacter* Body);
	/** Ends the mission with the stats screen; EndBanner replaces "MISSION COMPLETE" (e.g. "VICTORY"). */
	void CompleteMission(const FText& EndBanner = FText::GetEmpty());
	void RestartMission();

	bool IsMissionComplete() const { return bMissionComplete; }
	FText GetObjectiveText() const;
	const FMetalMessage* GetDialogue() const { return DialogueQueue.Num() > 0 ? &DialogueQueue[0] : nullptr; }
	const FMetalMessage* GetNotice() const { return NoticeTimeLeft > 0.f ? &Notice : nullptr; }
	const FMetalMessage* GetBanner() const { return BannerTimeLeft > 0.f ? &Banner : nullptr; }
	int32 GetKills() const { return Kills; }
	float GetMissionTime() const;

	virtual void Tick(float DeltaSeconds) override;

	/**
	 * Action-adventure controls: while not shooting, the player's robot turns to face where it runs
	 * and the camera orbits freely behind it; aiming or firing switches to the over-the-shoulder strafe.
	 */
	UPROPERTY(EditDefaultsOnly, Category="Metalization")
	bool bActionCamera = false;

	/** Hide the health/ammo panels until the player is in combat (clean exploration view). */
	UPROPERTY(EditDefaultsOnly, Category="Metalization")
	bool bMinimalHUD = false;

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	void Respawn(AMetalPlayerController* Controller);
	void OnBotDied(AMetalBotCharacter* Bot, AController* Killer);

	UPROPERTY(Transient)
	TObjectPtr<AMetalMission> Mission;

	TArray<FMetalMessage> DialogueQueue;
	float DialogueTimeLeft = 0.f;
	FMetalMessage Notice;
	float NoticeTimeLeft = 0.f;
	FMetalMessage Banner;
	float BannerTimeLeft = 0.f;

	bool bHasCheckpoint = false;
	FTransform CheckpointTransform;
	TArray<FMetalWeaponSlot> SavedWeapons;
	int32 SavedWeaponIndex = 0;
	int32 SavedGrenades = 0;
	bool bMissionComplete = false;
	float MissionStartTime = 0.f;
	float MissionEndTime = 0.f;
	int32 Kills = 0;
	FDelegateHandle BotDiedHandle;
};

/** Free-roam prototype arena (MZ_Arena): action camera, no mission, clean HUD. */
UCLASS()
class AMetalArenaGameMode : public AMetalGameMode
{
	GENERATED_BODY()

public:
	AMetalArenaGameMode()
	{
		bActionCamera = true;
		bMinimalHUD = true;
	}
};
