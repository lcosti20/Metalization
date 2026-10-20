// Mission flow, checkpoints, respawning and on-screen messages for the Metalization demo.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "MetalBotCharacter.h"
#include "MetalGameMode.generated.h"

class AMetalMission;
class AMetalPlayerController;

/** One line of on-screen text shown by the HUD: dialogue, a system notice or a banner. */
struct FMetalMessage
{
	/** Who is talking; empty for system notices and banners. */
	FText Speaker;
	FText Text;
	/** Seconds the message stays up. */
	float Duration = 3.f;
};

/**
 * Game mode for the on-foot levels, and the base of AMetalSpaceBattleGameMode. It spawns the player
 * (AMetalPlayerCharacter + AMetalPlayerController + AMetalHUD), starts the level's AMetalMission, keeps
 * the message/banner state the HUD draws, and handles checkpoints, death, respawn and mission completion.
 */
UCLASS()
class AMetalGameMode : public AGameModeBase
{
	GENERATED_BODY()

public:
	/** Sets the default pawn, controller and HUD classes. */
	AMetalGameMode();

	/**
	 * Dialogue (with a speaker) is queued; system notices (no speaker) replace the current notice.
	 *
	 * @param Speaker  Name shown above dialogue, or empty for a notice.
	 * @param Text     Message text. Empty text is ignored.
	 * @param Duration Seconds to show it.
	 */
	void ShowMessage(const FText& Speaker, const FText& Text, float Duration);

	/**
	 * Big centred text ("CHECKPOINT", "MISSION COMPLETE"). Replaces any current banner.
	 *
	 * @param Text     Banner text.
	 * @param Duration Seconds to show it.
	 */
	void ShowBanner(const FText& Text, float Duration);

	/**
	 * Makes Transform the respawn point and shows the CHECKPOINT banner.
	 *
	 * @param Transform Where the player respawns.
	 */
	void SetCheckpoint(const FTransform& Transform);

	/**
	 * Called by the player controller when the player's own body dies. Saves the loadout and respawns the
	 * player three seconds later. Overridden by the space battle mode to spend tickets instead.
	 *
	 * @param Controller The player's controller.
	 * @param Body       The body that died.
	 */
	virtual void PlayerDied(AMetalPlayerController* Controller, AMetalBotCharacter* Body);

	/**
	 * Ends the mission with the stats screen; EndBanner replaces "MISSION COMPLETE" (e.g. "VICTORY").
	 *
	 * @param EndBanner Banner text, or empty for the default. Only the first call has any effect.
	 */
	void CompleteMission(const FText& EndBanner = FText::GetEmpty());

	/** Reloads the current level. */
	void RestartMission();

	/** @return True once CompleteMission has been called. */
	bool IsMissionComplete() const { return bMissionComplete; }

	/** @return The active objective's description, or empty with no mission. */
	FText GetObjectiveText() const;

	/** @return The dialogue line currently showing (front of the queue), or null. */
	const FMetalMessage* GetDialogue() const { return DialogueQueue.Num() > 0 ? &DialogueQueue[0] : nullptr; }

	/** @return The current system notice, or null if it has expired. */
	const FMetalMessage* GetNotice() const { return NoticeTimeLeft > 0.f ? &Notice : nullptr; }

	/** @return The current banner, or null if it has expired. */
	const FMetalMessage* GetBanner() const { return BannerTimeLeft > 0.f ? &Banner : nullptr; }

	/** @return Villains killed by the player this mission. */
	int32 GetKills() const { return Kills; }

	/** @return Seconds since the level started, frozen once the mission is complete. */
	float GetMissionTime() const;

	/** Counts down the notice and banner timers and advances the dialogue queue. */
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
	/** Subscribes to robot deaths, starts the first AMetalMission in the level and spawns the dev autopilot if requested. */
	virtual void BeginPlay() override;

	/** Unsubscribes from robot deaths. */
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	/**
	 * Destroys the old body (and any host still possessed), spawns a fresh player body at the checkpoint
	 * (or a player start) and restores the saved weapons with half ammo and at least two grenades.
	 *
	 * @param Controller The player's controller.
	 */
	void Respawn(AMetalPlayerController* Controller);

	/**
	 * OnAnyBotDied handler that counts the player's kills.
	 *
	 * @param Bot    Robot that died.
	 * @param Killer Controller credited with the kill.
	 */
	void OnBotDied(AMetalBotCharacter* Bot, AController* Killer);

	UPROPERTY(Transient)
	TObjectPtr<AMetalMission> Mission;

	/** Dialogue waiting to be shown; the front entry is on screen. */
	TArray<FMetalMessage> DialogueQueue;
	float DialogueTimeLeft = 0.f;
	FMetalMessage Notice;
	float NoticeTimeLeft = 0.f;
	FMetalMessage Banner;
	float BannerTimeLeft = 0.f;

	bool bHasCheckpoint = false;
	FTransform CheckpointTransform;
	/** Loadout carried over from the body that died. */
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
