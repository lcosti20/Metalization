// Every robot in the game - the player, enemies, possessed enemies - is a AMetalBotCharacter
// configured by a UMetalBotArchetype. Because possession just hands the same pawn to the player
// controller, a hijacked robot keeps its own movement, weapon, health and hit zones.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "MetalTypes.h"
#include "MetalBotCharacter.generated.h"

class UCameraComponent;
class UMaterialInstanceDynamic;
class UMetalBotAnimInstance;
class UMetalBotArchetype;
class UMetalWeaponData;
class USkeletalMeshComponent;
class USpringArmComponent;

/** A weapon in a robot's inventory: which weapon, its upgrade level and its ammo. */
USTRUCT(BlueprintType)
struct FMetalWeaponSlot
{
	GENERATED_BODY()

	/** The weapon definition. Never null for a slot in AMetalBotCharacter::Weapons. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Weapon")
	TObjectPtr<UMetalWeaponData> Data;

	/** Upgrade level, an index into UMetalWeaponData::Levels. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Weapon")
	int32 Level = 0;

	/** Rounds loaded in the clip. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Weapon")
	int32 Clip = 0;

	/** Spare rounds outside the clip. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Weapon")
	int32 Reserve = 0;
};

/**
 * Broadcast when any robot dies.
 *
 * @param Bot    The robot that died.
 * @param Killer Controller credited with the kill, or null (self-destruct, fell out of the world).
 */
DECLARE_MULTICAST_DELEGATE_TwoParams(FOnMetalBotDied, AMetalBotCharacter* /*Bot*/, AController* /*Killer*/);

/**
 * Base class for every robot: the player, enemies, allies and flyers (AMetalStalker derives from it).
 *
 * The class owns everything a robot does regardless of who drives it: health and hit zones, the weapon
 * inventory and firing, grenades, stun/dormant/hijacked states, team paint and the third-person camera.
 * Players drive it through AMetalPlayerController and AI through AMetalBotAIController (walkers) or
 * AMetalFlyerAIController (flyers); both just call StartFire/StopFire, SetAITarget, movement input, etc.
 *
 * @see UMetalBotArchetype for the data that configures it.
 * @see UMetalBotAnimInstance for the procedural animation that poses it.
 */
UCLASS()
class AMetalBotCharacter : public ACharacter
{
	GENERATED_BODY()

public:
	/**
	 * Sets up the capsule, mesh, weapon/unarmed bonemerge meshes, camera boom and default movement.
	 *
	 * @param ObjectInitializer Lets subclasses swap default subobjects (AMetalStalker uses this for its movement component).
	 */
	AMetalBotCharacter(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());

	/** Fired for every robot death; missions and the player controller listen to this. */
	static FOnMetalBotDied OnAnyBotDied;

	// --- Setup ---------------------------------------------------------------------------------

	/** The data asset defining this robot. Required: a robot without one logs an error and does nothing. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Metalization", meta=(ExposeOnSpawn=true))
	TObjectPtr<UMetalBotArchetype> Archetype;

	/** Overrides the archetype's team for this instance (e.g. friendly heroes placed in an enemy-heavy level). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Metalization")
	bool bOverrideTeam = false;

	/** Team used instead of the archetype's when bOverrideTeam is set. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Metalization", meta=(EditCondition="bOverrideTeam"))
	EMetalTeam TeamOverride = EMetalTeam::Heroes;

	/** AI wanders this far from where it was placed. 0 = holds position. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Metalization|AI")
	float PatrolRadius = 600.f;

	/** Passive robots never start fights (friendly NPCs). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Metalization|AI")
	bool bPassive = false;

	/** Ignores enemies it sees until it (or a squadmate) is shot, then fights normally. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Metalization|AI")
	bool bDocileUntilAttacked = false;

	/** Fraction of the archetype's walk speed used while patrolling. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Metalization|AI", meta=(ClampMin="0.1", ClampMax="1"))
	float PatrolSpeedFraction = 0.4f;

	/**
	 * Copies the archetype's mesh, capsule, movement, jump count and camera settings onto this robot and
	 * resets the team to GetBaseTeam(). Safe to call in the editor (OnConstruction) and again at BeginPlay.
	 * Does nothing without an Archetype.
	 */
	virtual void ApplyArchetype();

	/** @return The archetype's rig description, or a default rig when no archetype is set. */
	const FMetalBotRig& GetRig() const;

	/** @return The archetype id ("player", "shooter"...) used to pick held weapon meshes, or NAME_None. */
	FName GetArchetypeId() const;

	/** @return The archetype's display name, or the actor name when no archetype is set. */
	FText GetDisplayName() const;

	// --- State ---------------------------------------------------------------------------------

	/** @return The team the robot currently fights for (Heroes while hijacked). */
	UFUNCTION(BlueprintPure, Category="Metalization")
	EMetalTeam GetTeam() const { return Team; }

	/**
	 * Switches the robot's side and repaints it with that team's colours.
	 *
	 * @param NewTeam Team to fight for from now on.
	 */
	virtual void SetTeam(EMetalTeam NewTeam);

	/** Team from the archetype or the per-instance override, ignoring any hijack. */
	EMetalTeam GetBaseTeam() const;

	/**
	 * @param Other Robot to test against.
	 * @return True if Other is a different robot on an opposing (non-neutral) team.
	 */
	bool IsHostileTo(const AMetalBotCharacter* Other) const;

	/** @return False once the robot has died. */
	UFUNCTION(BlueprintPure, Category="Metalization")
	bool IsAlive() const { return !bDead; }

	/** @return Current hit points. */
	UFUNCTION(BlueprintPure, Category="Metalization")
	float GetHealth() const { return Health; }

	/** @return Hit points at full health (from the archetype). */
	UFUNCTION(BlueprintPure, Category="Metalization")
	float GetMaxHealth() const { return MaxHealth; }

	/** @return True while an EMP stun is active: the robot can't move or shoot. */
	bool IsStunned() const;

	/** Powered down: body left behind during possession, or a robot with no ragdoll that died. */
	bool IsSlumped() const { return bDormant || (bDead && !bRagdoll); }

	/** @return True while the robot is switched off (its controller is driving another robot). */
	bool IsDormant() const { return bDormant; }

	/** @return True once the weapon arm (or every built-in gun) has been shot off; the robot then fights in melee. */
	bool IsWeaponDisabled() const { return bWeaponDisabled; }

	/**
	 * Powers the robot down or back up. Dormant robots stop firing and moving and are drawn slumped.
	 *
	 * @param bNewDormant True to power down.
	 */
	virtual void SetDormant(bool bNewDormant);

	/**
	 * Disables movement and firing for a while (EMP grenades). Overlapping stuns extend to the later end time.
	 *
	 * @param Duration Seconds to stay stunned.
	 */
	void Stun(float Duration);

	/**
	 * Restores health, capped at GetMaxHealth(). Ignored once dead.
	 *
	 * @param Amount Hit points to add.
	 */
	void Heal(float Amount);

	/**
	 * Enemy robots can be hijacked (a timed link, then they self-destruct). Friendly robots built for it
	 * (bAllyHackable, e.g. parked Stalkers) accept an open-ended link instead.
	 *
	 * @param Hacker The robot firing the Hack Tether.
	 * @return True if Hacker may take control of this robot now.
	 */
	bool CanBeHackedBy(const AMetalBotCharacter* Hacker) const;

	/**
	 * Marks the robot as under the player's control: it joins the Heroes and its hack glow pulses.
	 *
	 * @param bNewHijacked True when the link starts, false when it ends.
	 */
	void SetHijacked(bool bNewHijacked);

	/** @return True while the player is controlling this robot through the Hack Tether. */
	bool IsHijacked() const { return bHijacked; }

	/** @return Seconds a hijack link to this robot lasts (from the archetype). */
	float GetPossessionDuration() const;

	/**
	 * Powers the robot down, then after Delay it explodes, damaging nearby enemies of its current team.
	 *
	 * @param Delay Seconds before the explosion.
	 */
	void SelfDestruct(float Delay);

	// --- Combat --------------------------------------------------------------------------------

	/** Holds the trigger. Tick() fires the equipped weapon whenever it is ready. */
	void StartFire();

	/** Releases the trigger. */
	void StopFire();

	/** Starts a reload if the clip isn't full and there is reserve ammo. Tick() finishes it after ReloadTime. */
	void Reload();

	/**
	 * Equips the next or previous weapon, wrapping around the inventory.
	 *
	 * @param Direction +1 for next, -1 for previous.
	 */
	void CycleWeapon(int32 Direction);

	/**
	 * Switches to a weapon slot, cancelling any reload and adding a short draw delay.
	 *
	 * @param SlotIndex Index into GetWeapons(). Invalid or already-equipped indices are ignored.
	 */
	void EquipWeapon(int32 SlotIndex);

	/**
	 * Adds a new weapon (half reserve ammo), or upgrades it one level and refills it if already owned.
	 *
	 * @param Weapon Weapon to give.
	 * @param bEquip Switch to the weapon straight away.
	 * @return True if anything changed (new weapon, upgrade, or ammo refilled), so pickups know to vanish.
	 */
	bool GiveWeapon(UMetalWeaponData* Weapon, bool bEquip);

	/**
	 * Tops up the reserve ammo of every finite-ammo weapon.
	 *
	 * @param Fraction Share of each weapon's MaxReserveAmmo to add (0-1).
	 * @return True if any weapon gained ammo.
	 */
	bool AddAmmoFraction(float Fraction);

	/** Lobs one grenade (GrenadeWeapon) towards the aim point, if any are left. */
	void ThrowGrenade();

	/**
	 * @param bNewAiming True while the aim button is held (zooms the action camera).
	 */
	void SetAiming(bool bNewAiming) { bAiming = bNewAiming; }

	/** @return True while the aim button is held. */
	bool IsAiming() const { return bAiming; }

	/** Player robot in a level whose game mode uses the action camera (see AMetalGameMode::bActionCamera). */
	virtual bool UsesActionCamera() const;

	/** Gun up and facing the aim (aiming, shooting, or an AI with a target), as opposed to running freely. */
	bool IsInCombatStance() const;

	/** Mesh rotation relative to the capsule, without the double-jump flip. The animator works in this space. */
	FQuat GetMeshBaseRotation() const { return MeshBaseRotation; }

	/** 0..1 tuck amount while flipping. */
	float GetFlipTuck() const { return FlipTuck; }

	/** @return The weapon inventory. */
	const TArray<FMetalWeaponSlot>& GetWeapons() const { return Weapons; }

	/**
	 * Replaces the whole inventory. Used to carry the player's loadout across a respawn.
	 *
	 * @param NewWeapons Inventory to copy.
	 * @param EquipIndex Slot to equip afterwards.
	 */
	void SetWeapons(const TArray<FMetalWeaponSlot>& NewWeapons, int32 EquipIndex);

	/** @return The equipped weapon slot, or null when unarmed. */
	const FMetalWeaponSlot* GetCurrentWeapon() const;

	/** @return Index of the equipped slot, or INDEX_NONE. */
	int32 GetCurrentWeaponIndex() const { return CurrentWeapon; }

	/** @return True while a reload is in progress. */
	bool IsReloading() const { return ReloadEndTime > 0.f; }

	/** Weapon thrown by ThrowGrenade(). Filled from the archetype at BeginPlay if left empty. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Metalization|Combat")
	TObjectPtr<UMetalWeaponData> GrenadeWeapon;

	/** Grenades carried. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Metalization|Combat")
	int32 Grenades = 0;

	/** Where the robot is aiming in world space (camera for players, target for AI). */
	FVector GetAimPoint() const;

	/** Aim pitch in degrees for the animator. */
	float GetAimPitch() const;

	/**
	 * Where the next shot leaves from: the next working built-in gun bone, else a point MuzzleOffset in
	 * front of the weapon bone, else just in front of the capsule.
	 *
	 * @return World-space muzzle position.
	 */
	virtual FVector GetMuzzleLocation() const;

	/** Direction the robot's guns face: its yaw, pitched with the aim. */
	virtual FVector GetAimForward() const;

	/** Built-in guns (MuzzleBones), and how many of them haven't been shot off. */
	int32 GetMuzzleCount() const;

	/** @return Built-in guns still able to fire (0 once the weapon is disabled). */
	int32 GetWorkingMuzzleCount() const;

	/** @return World position of the head bone, or the actor location plus BaseEyeHeight if there is none. */
	FVector GetEyesLocation() const;

	/**
	 * AI sets this to the point it wants to shoot at.
	 *
	 * @param WorldPoint Point to aim at.
	 * @param bHasTarget False to clear the target (the robot then aims straight ahead).
	 */
	void SetAITarget(const FVector& WorldPoint, bool bHasTarget) { AITargetPoint = WorldPoint; bHasAITarget = bHasTarget; }

	/** @return World time of the last damage taken, for the HUD's damage flash. */
	float GetLastDamageTakenTime() const { return LastDamageTakenTime; }

	/** @return The third-person camera's spring arm. */
	USpringArmComponent* GetCameraBoom() const { return CameraBoom; }

	/**
	 * Applies damage after the friendly-fire check, scaling it by the hit zone that was struck and breaking
	 * zones whose durability runs out. Notifies the AI/player controller and kills the robot at 0 health.
	 *
	 * @param DamageAmount     Raw damage before hit-zone multipliers.
	 * @param DamageEvent      Point damage events carry the hit used to find the hit zone.
	 * @param EventInstigator  Controller responsible; credited with the kill.
	 * @param DamageCauser     The shooter, or a projectile whose instigator is the shooter.
	 * @return Damage actually applied (0 for friendly fire or a dead robot).
	 */
	virtual float TakeDamage(float DamageAmount, struct FDamageEvent const& DamageEvent, AController* EventInstigator, AActor* DamageCauser) override;

	/** Reconfigures the rotation rules for the new controller type (player strafing vs AI focus). */
	virtual void PossessedBy(AController* NewController) override;

	/** Applies the archetype in the editor so designers see the right robot and starting gun when placing it. */
	virtual void OnConstruction(const FTransform& Transform) override;

	/** Drives material flashes, camera blending, the flip, reloads and automatic fire. */
	virtual void Tick(float DeltaSeconds) override;

	/** Kills and explodes the robot instead of silently destroying it, so deaths are still reported. */
	virtual void FellOutOfWorld(const UDamageType& DamageType) override;

	/** Ends any double-jump flip and plays a landing dip scaled by the fall speed. */
	virtual void Landed(const FHitResult& Hit) override;

protected:
	/** Applies the archetype, sets health, spawns extra body parts and grants the starting loadout. */
	virtual void BeginPlay() override;

	/** Starts the forward flip on a mid-air (second) jump. */
	virtual void OnJumped_Implementation() override;

	/**
	 * Blends the camera between exploring (centred, pulled back), combat (over the shoulder) and aiming
	 * (zoomed), and switches the body between facing its movement and facing the camera.
	 *
	 * @param DeltaSeconds Frame time.
	 */
	void TickActionCamera(float DeltaSeconds);

	/**
	 * Advances the double-jump flip and applies it, together with MeshTilt, to the mesh transform.
	 *
	 * @param Now Current world time in seconds.
	 */
	void TickFlip(float Now);

	/**
	 * Kills the robot: disables movement and collision, ragdolls it if it has a physics asset, broadcasts
	 * OnAnyBotDied and schedules Explode(). Safe to call more than once.
	 *
	 * @param Killer Controller credited with the kill, or null.
	 */
	virtual void Die(AController* Killer);

	/** Plays the death explosion and destroys the robot (or only hides it when the player is still controlling it). */
	void Explode();

	/**
	 * Fires the equipped weapon once: hitscan traces or projectiles per pellet, muzzle FX, recoil, ammo use.
	 * Falls back to the archetype's melee weapon when the gun is disabled. Called by Tick() while the trigger is held.
	 */
	virtual void FireOnce();

	/**
	 * Damages every hostile robot in a sphere in front of this one.
	 *
	 * @param Slot Melee weapon to swing; its Range sets the reach.
	 */
	void FireMelee(const FMetalWeaponSlot& Slot);

	/** Moves rounds from the reserve into the clip at the end of a reload. */
	void FinishReload();

	/** Shows the equipped weapon's mesh for this archetype (or the unarmed part) and rebuilds the materials. */
	void RefreshWeaponVisual();

	/** Recreates dynamic material instances on every mesh and reapplies hack glow and team colours. */
	void RefreshMaterials();

	/**
	 * Sets a scalar parameter on every dynamic material of the robot.
	 *
	 * @param Parameter Material parameter name ("HitFlash", "HackGlow").
	 * @param Value     Value to set.
	 */
	void SetMaterialScalar(FName Parameter, float Value);

	/** Players turn with the controller (strafe); AI turns smoothly towards its focus point. */
	virtual void ConfigureRotationForController();

	/**
	 * Works out which hit zone a shot struck: the bone that a physics-body hit reports, or else the zone bone
	 * closest to the shot's ray within its radius.
	 *
	 * @param DamageEvent The damage event; only point damage can hit a zone.
	 * @return Index into the archetype's HitZones, or INDEX_NONE.
	 */
	int32 FindHitZone(const FDamageEvent& DamageEvent) const;

	/** Breaks the gun: the held weapon disappears and FireOnce() switches to melee. */
	void DisableWeapon();

	/**
	 * Applies the consequences of a zone running out of durability (lost weapon arm or built-in gun).
	 *
	 * @param Zone The zone that broke.
	 */
	void BreakZone(const FMetalHitZone& Zone);

	/** Repaints the Body/Accent material slots with the archetype's colours for the current team, if it has any. */
	void ApplyTeamColors();

	/** Spring arm holding the third-person camera. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Components")
	TObjectPtr<USpringArmComponent> CameraBoom;

	/** Third-person camera, used when a player controls this robot. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Components")
	TObjectPtr<UCameraComponent> FollowCamera;

	/** Held weapon, skinned to this robot's skeleton and driven by the body (Source-style bonemerge). */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Components")
	TObjectPtr<USkeletalMeshComponent> WeaponMesh;

	/** Archetype's UnarmedPart, shown when no weapon mesh is held. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Components")
	TObjectPtr<USkeletalMeshComponent> UnarmedMesh;

	/** Components created at BeginPlay for the archetype's extra Parts, all following the body's pose. */
	UPROPERTY(Transient)
	TArray<TObjectPtr<USkeletalMeshComponent>> PartMeshes;

	/** Every dynamic material on the robot, for the HitFlash/HackGlow parameters. */
	UPROPERTY(Transient)
	TArray<TObjectPtr<UMaterialInstanceDynamic>> DynamicMaterials;

	/** The weapon inventory. */
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category="Metalization|Combat")
	TArray<FMetalWeaponSlot> Weapons;

	/** Current hit points. */
	UPROPERTY(VisibleInstanceOnly, Category="Metalization")
	float Health = 100.f;

	float MaxHealth = 100.f;
	EMetalTeam Team = EMetalTeam::Villains;
	/** Index of the equipped slot in Weapons, or INDEX_NONE. */
	int32 CurrentWeapon = INDEX_NONE;
	/** Damage absorbed so far by each archetype hit zone (same indices as HitZones). */
	TArray<float> ZoneDamage;
	bool bDead = false;
	/** Died with a physics asset and is now simulating; the mesh transform is then left alone. */
	bool bRagdoll = false;
	bool bDormant = false;
	bool bHijacked = false;
	bool bWeaponDisabled = false;
	bool bFireHeld = false;
	bool bAiming = false;
	bool bHasAITarget = false;
	FVector AITargetPoint = FVector::ZeroVector;
	/** World time when the weapon may fire again. */
	float NextFireTime = 0.f;
	/** World time a reload finishes, or 0 when not reloading. */
	float ReloadEndTime = 0.f;
	float StunEndTime = 0.f;
	/** 1 right after a hit, fading to 0; drives the HitFlash material parameter. */
	float HitFlash = 0.f;
	float LastDamageTakenTime = -100.f;
	/** Camera distance and offset from the archetype; the action camera blends around these. */
	float DefaultArmLength = 300.f;
	FVector DefaultCameraOffset = FVector(0.f, 55.f, 55.f);
	float LastFireTime = -100.f;
	/** World time the double-jump flip started, or -1 when not flipping. */
	float FlipStartTime = -1.f;
	float FlipTuck = 0.f;
	/** Most negative Z velocity during the current fall, to scale the landing. */
	float LowestFallSpeed = 0.f;
	FQuat MeshBaseRotation = FRotator(0.f, -90.f, 0.f).Quaternion();
	FVector MeshBaseLocation = FVector(0.f, 0.f, -68.f);
	/** Round-robin counter selecting which built-in gun (MuzzleBones) fires next. */
	int32 MuzzleCycle = 0;
	FTimerHandle ExplodeTimer;
	/** Built-in guns whose arm was shot off. */
	TSet<FName> BrokenMuzzles;
	/** Extra mesh rotation around the capsule centre (flyers pitch and bank the body with it). */
	FQuat MeshTilt = FQuat::Identity;
};
