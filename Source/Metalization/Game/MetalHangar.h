// The hangar inside a capital ship's interior level. It owns the hangar shell mesh and the parking
// pads (one per pad_N socket on the shell), parks a robot (a Stalker) on each pad in its ship's team
// colours, and replaces destroyed ones after a delay. Robots that are still out flying keep their pad.
// It also holds the ship's life-support core: a subsystem that enemies reach by flying in through the
// hangar mouth, so its damage goes through the owning ship like any other subsystem.
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
class UMaterialInterface;
class UMetalShipSubsystemComponent;
class UInstancedStaticMeshComponent;
class UMetalBotArchetype;
class UStaticMesh;

/**
 * Placed in a ship interior map. Builds pads from the shell's pad_N sockets, keeps a parked Stalker on
 * each (for players to link to), launches and replaces the ship's AI pilots, and owns the life-support
 * subsystem component (whose damage is forwarded to the owning ship).
 */
UCLASS()
class AMetalHangar : public AActor
{
	GENERATED_BODY()

public:
	/** Creates the shell, pad instances and the life-support cylinder. Parks Stalkers by default. */
	AMetalHangar();

	/** Used when there is no owning ship (the interior map opened directly). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Metalization")
	EMetalTeam Team = EMetalTeam::Heroes;

	/** Archetype of parked robots and AI pilots. Replaced by the ship data's HangarRobot when set. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Metalization")
	TObjectPtr<UMetalBotArchetype> RobotArchetype;

	/** Seconds before a destroyed pad robot or AI pilot is replaced (doubled when the ship loses its engines). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Metalization")
	float RespawnDelay = 20.f;

	/** Robot class parked on the pads (spawned parked, with RobotArchetype). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Metalization")
	TSubclassOf<AMetalBotCharacter> RobotClass;

	/** Room mesh: needs pad_N sockets on the floor (facing the mouth) and a hangar_portal socket at the mouth. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Metalization")
	TObjectPtr<UStaticMesh> ShellMesh;

	/** Mesh instanced on every pad socket; robots spawn standing on its top. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Metalization")
	TObjectPtr<UStaticMesh> PadMesh;

	/** Life-support core: a glowing column against the hangar's back wall. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Metalization")
	TObjectPtr<UMaterialInterface> LifeSupportMaterial;

	/** Base of the core, relative to the hangar (cm). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Metalization")
	FVector LifeSupportLocation = FVector(-4300.f, -9300.f, 0.f);

	/** Core diameter and height (cm). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Metalization")
	FVector2D LifeSupportSize = FVector2D(1200.f, 2600.f);

	/** The interior stops drawing beyond this (cm) so it never shows up as a speck in the sky over the battle. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Metalization")
	float InteriorDrawDistance = 500000.f;

	/**
	 * Activates the hangar: takes the ship's team and settings, registers life support with the ship,
	 * parks a robot on every pad and schedules the AI pilots' staggered launches.
	 *
	 * @param Ship Owning ship, or null when the interior map is played on its own.
	 */
	void InitForShip(AMetalCapitalShip* Ship);

	/** @return The owning ship, or null. */
	AMetalCapitalShip* GetShip() const { return OwningShip.Get(); }

	/** @return The hangar's team (the ship's, once initialised). */
	EMetalTeam GetTeam() const { return Team; }

	/** The hangar mouth (hangar_portal socket): forward points out into space. */
	FTransform GetMouthTransform() const;

	/** @return Number of pads (pad_N sockets found on the shell). */
	int32 GetPadCount() const { return Pads.Num(); }

	/**
	 * @param PadIndex Pad to query.
	 * @return The robot assigned to that pad (it may have flown off), or null.
	 */
	AMetalBotCharacter* GetParkedRobot(int32 PadIndex) const;

	/** AI-piloted robots currently out (alive) from this hangar. */
	int32 GetActivePilots() const;

	/** @return The ship's life-support core. */
	UMetalShipSubsystemComponent* GetLifeSupport() const { return LifeSupport; }

	/**
	 * Forwards point damage on the life-support core to the owning ship; all other damage is ignored.
	 *
	 * @return Damage dealt to the core.
	 */
	virtual float TakeDamage(float DamageAmount, struct FDamageEvent const& DamageEvent, AController* EventInstigator, AActor* DamageCauser) override;

	/** Rebuilds the pads in the editor. */
	virtual void OnConstruction(const FTransform& Transform) override;

	/** Twice a second, schedules and performs replacements for dead AI pilots and pad robots. */
	virtual void Tick(float DeltaSeconds) override;

protected:
	/** Builds the pads; self-initialises when this interior is the persistent level (opened directly). */
	virtual void BeginPlay() override;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Components")
	TObjectPtr<UStaticMeshComponent> Shell;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Components")
	TObjectPtr<UInstancedStaticMeshComponent> PadMeshes;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Components")
	TObjectPtr<UMetalShipSubsystemComponent> LifeSupport;

private:
	/** One parking pad. */
	struct FPad
	{
		/** Pad socket transform, relative to the hangar. */
		FTransform Transform;
		TWeakObjectPtr<AMetalBotCharacter> Robot;
		/** World time to spawn a replacement, or -1 when none is scheduled. */
		float RespawnAt = -1.f;
	};

	/** Assigns the meshes and recreates one pad per pad_N socket on the shell (pad_1, pad_2, ...). */
	void BuildPads();

	/**
	 * Spawns a parked robot standing on the pad, on the hangar's team.
	 *
	 * @param Pad Pad to fill.
	 */
	void SpawnRobot(FPad& Pad);

	/**
	 * Spawns a flying robot above a pad and gives it an AMetalFlyerAIController (odd slots raid, even slots fight).
	 *
	 * @param Slot Index into PilotSlots.
	 */
	void LaunchPilot(int32 Slot);

	/** The AI squadron: one entry per pilot slot, respawned like the pads. */
	struct FPilotSlot
	{
		TWeakObjectPtr<AMetalBotCharacter> Robot;
		/** World time to launch a replacement, or -1 when none is scheduled. */
		float RespawnAt = 0.f;
	};
	TArray<FPilotSlot> PilotSlots;

	TArray<FPad> Pads;
	TWeakObjectPtr<AMetalCapitalShip> OwningShip;
	/** Set by InitForShip; until then nothing spawns. */
	bool bActive = false;
	float NextCheckTime = 0.f;
};
