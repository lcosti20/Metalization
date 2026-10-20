// Development-only helper for automated smoke tests and screenshots (never spawned in Shipping).
//
//   -MetalTeleport=X,Y,Z,Yaw     move the player on start
//   -MetalCam=X,Y,Z,Pitch,Yaw    view from a fixed camera instead of the player
//                                (separate several with ";" to use one camera per screenshot)
//   -MetalShots=2,5.5,9          take screenshots at these times (Saved/Screenshots)
//   -MetalAutopilot[=combat|gate|boss|arena|flight|hangar|battle|dogfight|menu|ships]  run a scripted smoke test (see Scenarios() in the .cpp)
//   -MetalMenuLevel=Arena        Level Select entry the "menu" scenario picks (it runs again after the switch)
//   -MetalObjective=N            start the mission at objective N
//   -MetalQuitAt=12              exit the game

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "MetalDevAutopilot.generated.h"

class AMetalBotCharacter;

/**
 * Spawned at level start when any of the options above is given. It drives the player through the
 * chosen scenario by calling the same controller and robot functions as real input, and logs the state
 * of every robot and ship once a second for the smoke test to check.
 */
UCLASS(NotPlaceable, Transient)
class AMetalDevAutopilot : public AActor
{
	GENERATED_BODY()

public:
	/** Ticks even while paused, so scenarios can drive the pause menu. */
	AMetalDevAutopilot();

	/**
	 * Spawns the autopilot if any of the -Metal* options above is on the command line.
	 * Called from AMetalGameMode::BeginPlay in non-shipping builds.
	 *
	 * @param World World to spawn into.
	 */
	static void SpawnIfRequested(UWorld* World);

	/** Runs due scenario steps, keeps aiming and moving, takes screenshots, logs state each second and quits on time. */
	virtual void Tick(float DeltaSeconds) override;

protected:
	/** Parses the command line options (scenario, shot times, cameras, quit time). */
	virtual void BeginPlay() override;

private:
	/**
	 * @param bHackableOnly Only robots the player could hijack.
	 * @param Tag           Required actor tag, or NAME_None.
	 * @return The nearest living robot hostile to the player's pawn that matches, or null.
	 */
	AMetalBotCharacter* FindNearest(bool bHackableOnly, FName Tag) const;

	/** Closest friendly robot the player can link to (a parked Stalker). */
	AMetalBotCharacter* FindNearestLinkable() const;

	/**
	 * Points the player's control rotation from the camera at Target's centre.
	 *
	 * @param Target Actor to aim at.
	 */
	void AimAt(const AActor* Target) const;

	/**
	 * Performs one step of the current scenario.
	 *
	 * @param Step Index into the scenario's step list.
	 */
	void RunStep(int32 Step);

	/**
	 * Switches the view to a fixed camera from -MetalCam.
	 *
	 * @param Index Camera to use; out-of-range indices are ignored.
	 */
	void ViewFromCamera(int32 Index);

	/** Starts the scenario clock once the player has a pawn, applying -MetalTeleport. */
	void Start();

	/** Seconds after Start() at which to take screenshots. */
	TArray<float> ShotTimes;
	/** Fixed cameras from -MetalCam, one per screenshot. */
	TArray<FTransform> Cameras;
	float CameraSwitchTime = -1.f;
	float QuitAt = -1.f;
	float StartTime = 0.f;
	float NextLogTime = 0.f;
	bool bAutopilot = false;
	bool bRunning = false;
	/** The clock starts once the player has a pawn. */
	bool bStarted = false;
	bool bClimbing = false;
	/** Name of the scenario being run (a key of Scenarios() in the .cpp). */
	FString Scenario;
	/** Index of the next scenario step to run. */
	int32 NextStep = 0;
	int32 ShotIndex = 0;
	TWeakObjectPtr<AMetalBotCharacter> AimTarget;
	/** Aim at this bone of AimTarget instead of its centre. */
	FName AimBone;
	/** Aim at a component instead (a capital ship's subsystem). */
	TWeakObjectPtr<USceneComponent> AimComponent;
};
