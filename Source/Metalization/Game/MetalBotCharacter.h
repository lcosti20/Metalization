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

/** A weapon in a robot's inventory. */
USTRUCT(BlueprintType)
struct FMetalWeaponSlot
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Weapon")
	TObjectPtr<UMetalWeaponData> Data;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Weapon")
	int32 Level = 0;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Weapon")
	int32 Clip = 0;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Weapon")
	int32 Reserve = 0;
};

DECLARE_MULTICAST_DELEGATE_TwoParams(FOnMetalBotDied, AMetalBotCharacter* /*Bot*/, AController* /*Killer*/);

UCLASS()
class AMetalBotCharacter : public ACharacter
{
	GENERATED_BODY()

public:
	AMetalBotCharacter(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());

	/** Fired for every robot death; missions and the player controller listen to this. */
	static FOnMetalBotDied OnAnyBotDied;

	// --- Setup ---------------------------------------------------------------------------------

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Metalization", meta=(ExposeOnSpawn=true))
	TObjectPtr<UMetalBotArchetype> Archetype;

	/** Overrides the archetype's team for this instance (e.g. friendly heroes placed in an enemy-heavy level). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Metalization")
	bool bOverrideTeam = false;

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

	virtual void ApplyArchetype();
	const FMetalBotRig& GetRig() const;
	FName GetArchetypeId() const;
	FText GetDisplayName() const;

	// --- State ---------------------------------------------------------------------------------

	UFUNCTION(BlueprintPure, Category="Metalization")
	EMetalTeam GetTeam() const { return Team; }

	virtual void SetTeam(EMetalTeam NewTeam);
	/** Team from the archetype or the per-instance override, ignoring any hijack. */
	EMetalTeam GetBaseTeam() const;
	bool IsHostileTo(const AMetalBotCharacter* Other) const;

	UFUNCTION(BlueprintPure, Category="Metalization")
	bool IsAlive() const { return !bDead; }

	UFUNCTION(BlueprintPure, Category="Metalization")
	float GetHealth() const { return Health; }

	UFUNCTION(BlueprintPure, Category="Metalization")
	float GetMaxHealth() const { return MaxHealth; }

	bool IsStunned() const;
	/** Powered down: body left behind during possession, or a robot with no ragdoll that died. */
	bool IsSlumped() const { return bDormant || (bDead && !bRagdoll); }
	bool IsDormant() const { return bDormant; }
	bool IsWeaponDisabled() const { return bWeaponDisabled; }

	virtual void SetDormant(bool bNewDormant);
	void Stun(float Duration);
	void Heal(float Amount);

	/**
	 * Enemy robots can be hijacked (a timed link, then they self-destruct). Friendly robots built for it
	 * (bAllyHackable, e.g. parked Stalkers) accept an open-ended link instead.
	 */
	bool CanBeHackedBy(const AMetalBotCharacter* Hacker) const;
	void SetHijacked(bool bNewHijacked);
	bool IsHijacked() const { return bHijacked; }
	float GetPossessionDuration() const;
	void SelfDestruct(float Delay);

	// --- Combat --------------------------------------------------------------------------------

	void StartFire();
	void StopFire();
	void Reload();
	void CycleWeapon(int32 Direction);
	void EquipWeapon(int32 SlotIndex);
	/** Adds a new weapon, or upgrades it one level if already owned. Returns true if anything changed. */
	bool GiveWeapon(UMetalWeaponData* Weapon, bool bEquip);
	bool AddAmmoFraction(float Fraction);
	void ThrowGrenade();

	void SetAiming(bool bNewAiming) { bAiming = bNewAiming; }
	bool IsAiming() const { return bAiming; }

	/** Player robot in a level whose game mode uses the action camera (see AMetalGameMode::bActionCamera). */
	virtual bool UsesActionCamera() const;
	/** Gun up and facing the aim (aiming, shooting, or an AI with a target), as opposed to running freely. */
	bool IsInCombatStance() const;

	/** Mesh rotation relative to the capsule, without the double-jump flip. The animator works in this space. */
	FQuat GetMeshBaseRotation() const { return MeshBaseRotation; }
	/** 0..1 tuck amount while flipping. */
	float GetFlipTuck() const { return FlipTuck; }

	const TArray<FMetalWeaponSlot>& GetWeapons() const { return Weapons; }
	void SetWeapons(const TArray<FMetalWeaponSlot>& NewWeapons, int32 EquipIndex);
	const FMetalWeaponSlot* GetCurrentWeapon() const;
	int32 GetCurrentWeaponIndex() const { return CurrentWeapon; }
	bool IsReloading() const { return ReloadEndTime > 0.f; }

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Metalization|Combat")
	TObjectPtr<UMetalWeaponData> GrenadeWeapon;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Metalization|Combat")
	int32 Grenades = 0;

	/** Where the robot is aiming in world space (camera for players, target for AI). */
	FVector GetAimPoint() const;
	/** Aim pitch in degrees for the animator. */
	float GetAimPitch() const;
	virtual FVector GetMuzzleLocation() const;
	/** Direction the robot's guns face: its yaw, pitched with the aim. */
	virtual FVector GetAimForward() const;
	/** Built-in guns (MuzzleBones), and how many of them haven't been shot off. */
	int32 GetMuzzleCount() const;
	int32 GetWorkingMuzzleCount() const;
	FVector GetEyesLocation() const;

	/** AI sets this to the point it wants to shoot at. */
	void SetAITarget(const FVector& WorldPoint, bool bHasTarget) { AITargetPoint = WorldPoint; bHasAITarget = bHasTarget; }

	/** Called by the player controller when a shot lands, for hit markers. */
	float GetLastDamageTakenTime() const { return LastDamageTakenTime; }

	USpringArmComponent* GetCameraBoom() const { return CameraBoom; }

	virtual float TakeDamage(float DamageAmount, struct FDamageEvent const& DamageEvent, AController* EventInstigator, AActor* DamageCauser) override;
	virtual void PossessedBy(AController* NewController) override;
	virtual void OnConstruction(const FTransform& Transform) override;
	virtual void Tick(float DeltaSeconds) override;
	virtual void FellOutOfWorld(const UDamageType& DamageType) override;
	virtual void Landed(const FHitResult& Hit) override;

protected:
	virtual void BeginPlay() override;
	virtual void OnJumped_Implementation() override;
	void TickActionCamera(float DeltaSeconds);
	void TickFlip(float Now);

	virtual void Die(AController* Killer);
	void Explode();
	virtual void FireOnce();
	void FireMelee(const FMetalWeaponSlot& Slot);
	void FinishReload();
	void RefreshWeaponVisual();
	void RefreshMaterials();
	void SetMaterialScalar(FName Parameter, float Value);
	virtual void ConfigureRotationForController();
	int32 FindHitZone(const FDamageEvent& DamageEvent) const;
	void DisableWeapon();
	void BreakZone(const FMetalHitZone& Zone);
	/** Repaints the Body/Accent material slots with the archetype's colours for the current team, if it has any. */
	void ApplyTeamColors();

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Components")
	TObjectPtr<USpringArmComponent> CameraBoom;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Components")
	TObjectPtr<UCameraComponent> FollowCamera;

	/** Held weapon, skinned to this robot's skeleton and driven by the body (Source-style bonemerge). */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Components")
	TObjectPtr<USkeletalMeshComponent> WeaponMesh;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Components")
	TObjectPtr<USkeletalMeshComponent> UnarmedMesh;

	UPROPERTY(Transient)
	TArray<TObjectPtr<USkeletalMeshComponent>> PartMeshes;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UMaterialInstanceDynamic>> DynamicMaterials;

	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category="Metalization|Combat")
	TArray<FMetalWeaponSlot> Weapons;

	UPROPERTY(VisibleInstanceOnly, Category="Metalization")
	float Health = 100.f;

	float MaxHealth = 100.f;
	EMetalTeam Team = EMetalTeam::Villains;
	int32 CurrentWeapon = INDEX_NONE;
	TArray<float> ZoneDamage;
	bool bDead = false;
	bool bRagdoll = false;
	bool bDormant = false;
	bool bHijacked = false;
	bool bWeaponDisabled = false;
	bool bFireHeld = false;
	bool bAiming = false;
	bool bHasAITarget = false;
	FVector AITargetPoint = FVector::ZeroVector;
	float NextFireTime = 0.f;
	float ReloadEndTime = 0.f;
	float StunEndTime = 0.f;
	float HitFlash = 0.f;
	float LastDamageTakenTime = -100.f;
	float DefaultArmLength = 300.f;
	FVector DefaultCameraOffset = FVector(0.f, 55.f, 55.f);
	float LastFireTime = -100.f;
	float FlipStartTime = -1.f;
	float FlipTuck = 0.f;
	float LowestFallSpeed = 0.f;
	FQuat MeshBaseRotation = FRotator(0.f, -90.f, 0.f).Quaternion();
	FVector MeshBaseLocation = FVector(0.f, 0.f, -68.f);
	int32 MuzzleCycle = 0;
	FTimerHandle ExplodeTimer;
	/** Built-in guns whose arm was shot off. */
	TSet<FName> BrokenMuzzles;
	/** Extra mesh rotation around the capsule centre (flyers pitch and bank the body with it). */
	FQuat MeshTilt = FQuat::Identity;
};
