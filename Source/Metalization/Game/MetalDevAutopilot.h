// Development-only helper for automated smoke tests and screenshots (never spawned in Shipping).
//
//   -MetalTeleport=X,Y,Z,Yaw     move the player on start
//   -MetalCam=X,Y,Z,Pitch,Yaw    view from a fixed camera instead of the player
//                                (separate several with ";" to use one camera per screenshot)
//   -MetalShots=2,5.5,9          take screenshots at these times (Saved/Screenshots)
//   -MetalAutopilot[=combat|gate|boss|arena|flight|hangar|ships]  run a scripted smoke test (see Scenarios() in the .cpp)
//   -MetalObjective=N            start the mission at objective N
//   -MetalQuitAt=12              exit the game

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "MetalDevAutopilot.generated.h"

class AMetalBotCharacter;

UCLASS(NotPlaceable, Transient)
class AMetalDevAutopilot : public AActor
{
	GENERATED_BODY()

public:
	AMetalDevAutopilot();
	static void SpawnIfRequested(UWorld* World);
	virtual void Tick(float DeltaSeconds) override;

protected:
	virtual void BeginPlay() override;

private:
	AMetalBotCharacter* FindNearest(bool bHackableOnly, FName Tag) const;
	/** Closest friendly robot the player can link to (a parked Stalker). */
	AMetalBotCharacter* FindNearestLinkable() const;
	void AimAt(const AActor* Target) const;
	void RunStep(int32 Step);
	void ViewFromCamera(int32 Index);
	void Start();

	TArray<float> ShotTimes;
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
	FString Scenario;
	int32 NextStep = 0;
	int32 ShotIndex = 0;
	TWeakObjectPtr<AMetalBotCharacter> AimTarget;
	/** Aim at this bone of AimTarget instead of its centre. */
	FName AimBone;
	/** Aim at a component instead (a capital ship's subsystem). */
	TWeakObjectPtr<USceneComponent> AimComponent;
};
