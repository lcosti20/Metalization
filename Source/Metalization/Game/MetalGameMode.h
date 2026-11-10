// Mission flow, checkpoints, respawning and on-screen messages for the Metalization demo.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "MetalBotCharacter.h"
#include "MetalCheckpoint.h"
#include "MetalGameMode.generated.h"

class AMetalMission;
class AMetalPlayerController;

/** One line of on-screen text shown by the HUD: a system notice or a banner. Dialogue lives in UMetalDialogueSubsystem. */
struct FMetalMessage
{
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
	 * Dialogue (with a speaker) is queued on UMetalDialogueSubsystem; system notices (no speaker) replace
	 * the current notice.
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
	 * Makes Transform the respawn point, shows the CHECKPOINT banner and, with bResetLevelOnDeath, takes a
	 * snapshot of the level (UMetalCheckpointSubsystem) to go back to when the player dies.
	 *
	 * @param Transform  Where the player respawns.
	 * @param PlayerBody The player's own body (its loadout is part of the snapshot).
	 */
	void SetCheckpoint(const FTransform& Transform, AMetalBotCharacter* PlayerBody);

	/**
	 * Called by the player controller when the player's own body dies. Three seconds later it either
	 * reloads the level back to the last checkpoint snapshot (bResetLevelOnDeath; the level start if no
	 * checkpoint was reached) or respawns the player in the running level with the loadout they died with.
	 * Overridden by the space battle mode to spend tickets.
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

	/** @return The current system notice, or null if it has expired. */
	const FMetalMessage* GetNotice() const { return NoticeTimeLeft > 0.f ? &Notice : nullptr; }

	/** @return The current banner, or null if it has expired. */
	const FMetalMessage* GetBanner() const { return BannerTimeLeft > 0.f ? &Banner : nullptr; }

	/** @return Villains killed by the player this mission. */
	int32 GetKills() const { return Kills; }

	/** @return Times the player was scrapped this mission. */
	int32 GetDeaths() const { return Deaths; }

	/** @return The level's mission, or null. */
	AMetalMission* GetMission() const { return Mission; }

	/** Opens the title screen map (MZ_Title), or reloads this level if there is none. */
	void ReturnToTitle();

	/** @return Seconds since the level started, frozen once the mission is complete. */
	float GetMissionTime() const;

	/** Counts down the notice and banner timers. */
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

	/**
	 * Dying puts the whole level back the way it was at the last checkpoint (robots, pickups, consoles,
	 * objective, loadout) by reloading it. Off for free-roam levels and the space battle, which respawn
	 * the player into the running level instead.
	 */
	UPROPERTY(EditDefaultsOnly, Category="Metalization")
	bool bResetLevelOnDeath = true;

	/** @return True when this level was loaded as a retry from a checkpoint snapshot. */
	bool IsRestoredFromCheckpoint() const { return bRestoredFromCheckpoint; }

protected:
	/** Takes back a pending checkpoint retry (UMetalCheckpointSubsystem::TakeRetry) before anything spawns. */
	virtual void InitGame(const FString& MapName, const FString& Options, FString& ErrorMessage) override;

	/** On a checkpoint retry, restores the snapshot in two passes around every actor's BeginPlay. */
	virtual void StartPlay() override;

	/** Once a checkpoint is known (including on a retry), spawns the player there instead of at a player start. */
	virtual void RestartPlayer(AController* NewPlayer) override;

	/** Subscribes to robot deaths, starts (or, on a retry, resumes) the level's AMetalMission and spawns the dev autopilot if requested. */
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

	/** Reloads the level for a retry from the last checkpoint snapshot, carrying over deaths and mission time. */
	void ReloadFromCheckpoint();

	/**
	 * OnAnyBotDied handler that counts the player's kills.
	 *
	 * @param Bot    Robot that died.
	 * @param Killer Controller credited with the kill.
	 */
	void OnBotDied(AMetalBotCharacter* Bot, AController* Killer);

	/** The mission being played, or null in free-roam levels. */
	UPROPERTY(Transient)
	TObjectPtr<AMetalMission> Mission;

	/** Small message under the crosshair (pickups, console results). */
	FMetalMessage Notice;
	/** Seconds Notice stays on screen. */
	float NoticeTimeLeft = 0.f;
	/** Large centred message (objective and mission complete). */
	FMetalMessage Banner;
	/** Seconds Banner stays on screen. */
	float BannerTimeLeft = 0.f;

	/** A checkpoint has been reached; respawns use CheckpointTransform. */
	bool bHasCheckpoint = false;
	/** Where the player respawns after dying. */
	FTransform CheckpointTransform;
	/** Loadout carried over from the body that died. */
	TArray<FMetalWeaponSlot> SavedWeapons;
	/** Equipped slot of the saved loadout. */
	int32 SavedWeaponIndex = 0;
	/** Grenades of the saved loadout. */
	int32 SavedGrenades = 0;
	/** The last objective is done and the results are showing. */
	bool bMissionComplete = false;
	/** World time the mission started, for the results time. */
	float MissionStartTime = 0.f;
	/** World time the mission was completed. */
	float MissionEndTime = 0.f;
	/** Enemies the player has destroyed. */
	int32 Kills = 0;
	/** Times the player has died. */
	int32 Deaths = 0;
	/** Subscription to AMetalBotCharacter::OnAnyBotDied. */
	FDelegateHandle BotDiedHandle;
	/** This load is a retry and RestoreSnapshot is being put back. */
	bool bRestoredFromCheckpoint = false;
	/** Copy of the snapshot being restored (valid when bRestoredFromCheckpoint). */
	UPROPERTY(Transient)
	FMetalLevelSnapshot RestoreSnapshot;
	/** Mission time carried over from before the retry. */
	float CarriedMissionTime = 0.f;
};

/**
 * The Production Alpha mission (MZ_ProductionAlpha): the full HUD with the action camera, so the robot
 * faces where it runs and sprinting reads well.
 */
UCLASS()
class AMetalAlphaGameMode : public AMetalGameMode
{
	GENERATED_BODY()

public:
	/** Turns on the action camera. */
	AMetalAlphaGameMode()
	{
		bActionCamera = true;
	}
};

/** Free-roam prototype arena (MZ_Arena): action camera, no mission, clean HUD. */
UCLASS()
class AMetalArenaGameMode : public AMetalGameMode
{
	GENERATED_BODY()

public:
	/** Turns on the action camera and the minimal HUD. */
	AMetalArenaGameMode()
	{
		bActionCamera = true;
		bMinimalHUD = true;
		bResetLevelOnDeath = false;
	}
};
