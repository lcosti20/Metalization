// Robot dispenser: a Villain machine that welds robots together on its pad, either rebuilding the units
// assigned to it when they are destroyed or turning out new ones on a timer, until a console shuts it down.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "MetalTypes.h"
#include "MetalRobotDispenser.generated.h"

class AMetalBotCharacter;
class AMetalGate;
class UMetalBotArchetype;
class UMaterialInstanceDynamic;
class UPointLightComponent;
class UStaticMesh;
class UStaticMeshComponent;

/** What a dispenser builds. */
UENUM(BlueprintType)
enum class EMetalDispenserMode : uint8
{
	/** Rebuilds each of AssignedUnits when it is destroyed; the copy walks back to the original's post. */
	Respawn,
	/** Builds a robot from SpawnChoices every SpawnInterval seconds while fewer than MaxAlive are standing. */
	Continuous
};

/** One robot type a Continuous dispenser can pick. */
USTRUCT(BlueprintType)
struct FMetalDispenserChoice
{
	GENERATED_BODY()

	/** Robot to build. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Dispenser")
	TObjectPtr<UMetalBotArchetype> Archetype;

	/** Relative chance of being picked (Shooter 2 / Soldier 1 = two thirds Shooters). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Dispenser", meta=(ClampMin="0"))
	float Weight = 1.f;
};

/**
 * How to rebuild one assigned unit. Recorded from AssignedUnits in the editor (RecordAssignedUnits), so a
 * unit that a checkpoint restore removes before BeginPlay can still be rebuilt.
 */
USTRUCT(BlueprintType)
struct FMetalDispenserUnit
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, Category="Dispenser")
	TObjectPtr<UMetalBotArchetype> Archetype;

	/** Actor tags (mission objectives find robots by tag). */
	UPROPERTY(VisibleAnywhere, Category="Dispenser")
	TArray<FName> Tags;

	/** Where the original stood: the rebuilt robot patrols around it. */
	UPROPERTY(VisibleAnywhere, Category="Dispenser")
	FVector Post = FVector::ZeroVector;

	UPROPERTY(VisibleAnywhere, Category="Dispenser")
	float PatrolRadius = 0.f;

	UPROPERTY(VisibleAnywhere, Category="Dispenser")
	bool bOverrideTeam = false;

	UPROPERTY(VisibleAnywhere, Category="Dispenser")
	EMetalTeam TeamOverride = EMetalTeam::Villains;
};

/**
 * Builds robots on its pad. Each robot takes AssemblyTime seconds: it appears part by part (legs and
 * torso, left arm, right arm, head, then its gun) under a shower of welding sparks with drill and hammer
 * sounds, and only powers on (AI, damage, hacking) once finished. An AMetalControlConsole that lists the
 * dispenser in LinkedDispensers shuts it down for good; a robot half-built at that moment falls apart.
 * Not damageable.
 */
UCLASS()
class AMetalRobotDispenser : public AActor
{
	GENERATED_BODY()

public:
	/** Creates the dispenser mesh (SM_RobotDispenser) and its status light. */
	AMetalRobotDispenser();

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Dispenser")
	EMetalDispenserMode Mode = EMetalDispenserMode::Respawn;

	/** The machine's model, imported from Art/Graybox/Ships/Dispenser/RobotDispenser.blend. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Dispenser")
	TSoftObjectPtr<UStaticMesh> DispenserMesh;

	/** Respawn mode: placed robots this dispenser rebuilds when they are destroyed. Call RecordAssignedUnits after changing it. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Dispenser")
	TArray<TObjectPtr<AMetalBotCharacter>> AssignedUnits;

	/** Filled from AssignedUnits by RecordAssignedUnits; what each one is rebuilt as. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Dispenser")
	TArray<FMetalDispenserUnit> UnitRecords;

	/** Continuous mode: robot types to pick from at random, by weight. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Dispenser")
	TArray<FMetalDispenserChoice> SpawnChoices;

	/** Continuous mode: seconds from the start of one build to the start of the next. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Dispenser", meta=(ClampMin="0.5"))
	float SpawnInterval = 5.f;

	/** Continuous mode: no new robot while this many of its robots are still standing. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Dispenser", meta=(ClampMin="1"))
	int32 MaxAlive = 4;

	/** Continuous mode: tags given to every robot it builds. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Dispenser")
	TArray<FName> SpawnTags;

	/** Continuous mode: patrol radius of its robots, centred LaunchDistance in front of the pad. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Dispenser")
	float SpawnPatrolRadius = 900.f;

	/** Continuous mode: how far in front of the dispenser its robots patrol. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Dispenser")
	float LaunchDistance = 600.f;

	/** Seconds to build one robot. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Dispenser", meta=(ClampMin="0.5"))
	float AssemblyTime = 2.f;

	/** Respawn mode: seconds after a unit is destroyed before its replacement is started. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Dispenser")
	float RespawnDelay = 1.5f;

	/** Working from the start. Ignored when ActivationGate is set. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Dispenser")
	bool bStartsActive = true;

	/** If set, the dispenser idles until this AMetalGate starts opening. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Dispenser")
	TObjectPtr<AActor> ActivationGate;

	/** Copies the AssignedUnits' archetype, tags, post and patrol into UnitRecords (run by the level scripts after assigning). */
	UFUNCTION(BlueprintCallable, CallInEditor, Category="Dispenser")
	void RecordAssignedUnits();

	/**
	 * Shuts the dispenser down for good: no more robots, lights out, and any robot on the pad falls apart.
	 *
	 * @param bQuiet True when restoring a checkpoint: no sparks or sound.
	 */
	void Disable(bool bQuiet = false);

	/** @return True once Disable has been called. */
	bool IsDisabled() const { return bDisabled; }

	/** @return True while it is building or ready to build (started and not disabled). */
	bool IsWorking() const { return bActive && !bDisabled; }

	/** @return True while a robot is being built on the pad. */
	bool IsAssembling() const { return Building.IsValid(); }

	/**
	 * Calls Visit for every finished robot this dispenser built that is still standing (for checkpoint
	 * snapshots, which only track placed actors otherwise). Placed originals in AssignedUnits are skipped.
	 *
	 * @param Visit Called with the robot and its Respawn slot (INDEX_NONE for a Continuous build).
	 */
	void ForEachBuiltRobot(TFunctionRef<void(AMetalBotCharacter* Robot, int32 Slot)> Visit) const;

	/**
	 * Puts back a robot it had built when a checkpoint was saved: spawned finished (no assembly), in its
	 * Respawn slot (so it isn't rebuilt) or counted against MaxAlive.
	 *
	 * @param Archetype What it was.
	 * @param Slot      Respawn slot, or INDEX_NONE.
	 * @param Transform Where it stood.
	 * @return The robot, or null if it couldn't be spawned.
	 */
	AMetalBotCharacter* RestoreBuiltRobot(UMetalBotArchetype* Archetype, int32 Slot, const FTransform& Transform);

	/** Runs the build in progress and, a few times a second, decides what to build next. */
	virtual void Tick(float DeltaSeconds) override;

protected:
	/** Loads DispenserMesh so the machine shows in the editor. */
	virtual void OnConstruction(const FTransform& Transform) override;

	/** Reads the assigned units back (rebuilding any that are already gone) and sets the lights. */
	virtual void BeginPlay() override;

	/** The machine (static mesh with UCX collision and assembly/nozzle/torch sockets). */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Dispenser")
	TObjectPtr<UStaticMeshComponent> Body;

	/** Red status light over the pad; bright while working, off once disabled. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Dispenser")
	TObjectPtr<UPointLightComponent> Light;

private:
	/** One robot being built. */
	struct FBuildPart
	{
		/** Bones hidden until this part is welded on (they hide their children too). */
		TArray<FName> Bones;
		/** Weld here (a bone of the part), or the pad centre. */
		FName WeldBone;
		/** Last part: shows the gun and extra parts and powers the robot on. */
		bool bFinal = false;
	};

	/** Respawn-mode slot: the robot currently filling an assigned unit's place. */
	struct FSlot
	{
		TWeakObjectPtr<AMetalBotCharacter> Robot;
		/** World time to start the replacement, or < 0 when none is due. */
		float RebuildAt = -1.f;
	};

	/** Starts building a robot. Slot is the Respawn slot it replaces, or INDEX_NONE (Continuous). @return True if started. */
	bool StartBuild(UMetalBotArchetype* Archetype, int32 Slot);

	/**
	 * Spawns one of its robots with the slot's (or SpawnTags') tags, patrol and team, and registers it in
	 * the slot or in Spawned.
	 *
	 * @param bAssembling Spawn it powered off for a build on the pad.
	 */
	AMetalBotCharacter* SpawnRobot(UMetalBotArchetype* Archetype, int32 Slot, const FTransform& Transform, bool bAssembling);

	/** @return Where a robot from Slot patrols: the original's post, or a spot in front of the machine. */
	FVector PostFor(int32 Slot) const;

	/** Advances the robot on the pad: welds on the parts in turn and finishes it. */
	void TickBuild(float Now);

	/** Powers the finished robot on and sends it to its post. */
	void FinishBuild();

	/** Spawns a few welding sparks and arcs from the nozzle and the torches to At. */
	void Weld(const FVector& At, int32 Sparks);

	/** @return True if no standing robot is in the way on the pad. */
	bool IsPadClear() const;

	/** @return A weighted random pick from SpawnChoices, or null. */
	UMetalBotArchetype* PickChoice() const;

	/** Sets the status light and the glowing trims for the current state. */
	void UpdateGlow();

	/** @return World transform of a mesh socket, or the actor's transform offset by Fallback. */
	FVector SocketLocation(FName Socket, const FVector& Fallback) const;

	/** Dynamic copies of the mesh's glowing materials, dimmed when the dispenser is idle or disabled. */
	UPROPERTY(Transient)
	TArray<TObjectPtr<UMaterialInstanceDynamic>> GlowMaterials;

	TArray<FSlot> Slots;
	/** Continuous mode: robots it built that may still be standing. */
	TArray<TWeakObjectPtr<AMetalBotCharacter>> Spawned;

	/** The robot on the pad. */
	TWeakObjectPtr<AMetalBotCharacter> Building;
	/** Respawn slot of Building, or INDEX_NONE. */
	int32 BuildingSlot = INDEX_NONE;
	float BuildStartTime = 0.f;
	/** Parts of Building in weld order; the next to weld is NextPart. */
	TArray<FBuildPart> Parts;
	int32 NextPart = 0;
	/** Index of the part currently being drilled (its drill sound has played). */
	int32 DrilledPart = INDEX_NONE;
	/** Components of Building hidden until the final part. */
	TArray<TWeakObjectPtr<UPrimitiveComponent>> HiddenComponents;
	float NextSparkTime = 0.f;

	float NextCheckTime = 0.f;
	float NextSpawnTime = 0.f;
	bool bActive = false;
	bool bDisabled = false;
};
