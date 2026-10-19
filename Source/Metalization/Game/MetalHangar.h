// The hangar inside a capital ship's interior level. It owns the hangar shell mesh and the parking
// pads (one per pad_N socket on the shell), parks a robot (a Stalker) on each pad in its ship's team
// colours, and replaces destroyed ones after a delay. Robots that are still out flying keep their pad.
//
// The owning AMetalCapitalShip finds the hangar when its interior streams in and calls InitForShip.
// Opened on its own (the interior map as the persistent level) the hangar uses its own settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "MetalTypes.h"
#include "MetalHangar.generated.h"

class AMetalBotCharacter;
class AMetalCapitalShip;
class UInstancedStaticMeshComponent;
class UMetalBotArchetype;
class UStaticMesh;

UCLASS()
class AMetalHangar : public AActor
{
	GENERATED_BODY()

public:
	AMetalHangar();

	/** Used when there is no owning ship (the interior map opened directly). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Metalization")
	EMetalTeam Team = EMetalTeam::Heroes;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Metalization")
	TObjectPtr<UMetalBotArchetype> RobotArchetype;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Metalization")
	float RespawnDelay = 20.f;

	/** Robot class parked on the pads (spawned parked, with RobotArchetype). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Metalization")
	TSubclassOf<AMetalBotCharacter> RobotClass;

	/** Room mesh: needs pad_N sockets on the floor (facing the mouth) and a hangar_portal socket at the mouth. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Metalization")
	TObjectPtr<UStaticMesh> ShellMesh;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Metalization")
	TObjectPtr<UStaticMesh> PadMesh;

	/** The interior stops drawing beyond this (cm) so it never shows up as a speck in the sky over the battle. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Metalization")
	float InteriorDrawDistance = 500000.f;

	void InitForShip(AMetalCapitalShip* Ship);
	AMetalCapitalShip* GetShip() const { return OwningShip.Get(); }
	EMetalTeam GetTeam() const { return Team; }

	/** The hangar mouth (hangar_portal socket): forward points out into space. */
	FTransform GetMouthTransform() const;
	int32 GetPadCount() const { return Pads.Num(); }
	AMetalBotCharacter* GetParkedRobot(int32 PadIndex) const;

	virtual void OnConstruction(const FTransform& Transform) override;
	virtual void Tick(float DeltaSeconds) override;

protected:
	virtual void BeginPlay() override;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Components")
	TObjectPtr<UStaticMeshComponent> Shell;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Components")
	TObjectPtr<UInstancedStaticMeshComponent> PadMeshes;

private:
	struct FPad
	{
		FTransform Transform;
		TWeakObjectPtr<AMetalBotCharacter> Robot;
		float RespawnAt = -1.f;
	};

	void BuildPads();
	void SpawnRobot(FPad& Pad);

	TArray<FPad> Pads;
	TWeakObjectPtr<AMetalCapitalShip> OwningShip;
	bool bActive = false;
	float NextCheckTime = 0.f;
};
