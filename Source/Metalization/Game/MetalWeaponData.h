// Data-driven weapon definition. One asset per weapon; upgrade levels live inside the asset.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "MetalWeaponData.generated.h"

class USkeletalMesh;
class UStaticMesh;

/** How a weapon delivers its damage. Selects the firing path in AMetalBotCharacter. */
UENUM(BlueprintType)
enum class EMetalFireMode : uint8
{
	/** Instant trace (laser, shotgun pellets). */
	Hitscan,
	/** Travelling projectile (repeater bolts, rockets). */
	Projectile,
	/** Lobbed with gravity (grenades). */
	Thrown,
	/** Short-range swing (robots whose weapon arm was destroyed). */
	Melee
};

/** Stats for one upgrade level of a weapon. */
USTRUCT(BlueprintType)
struct FMetalWeaponLevel
{
	GENERATED_BODY()

	/** Weapon mesh per holder skeleton, keyed by archetype id ("player", "shooter"...). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Visual")
	TMap<FName, TObjectPtr<USkeletalMesh>> HeldMeshes;

	/** Damage per hit (per pellet for shotguns). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Stats")
	float Damage = 10.f;

	/** Seconds between shots. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Stats")
	float FireInterval = 0.2f;

	/** Rounds fired before a reload is needed. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Stats")
	int32 ClipSize = 20;

	/** Most rounds that can be carried outside the clip. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Stats")
	int32 MaxReserveAmmo = 200;
};

/**
 * Data asset describing one weapon (Laser, Repeater, Shotgun, grenades, ship cannons...).
 * Shared fields here apply at every upgrade level; per-level stats and meshes live in Levels.
 * Robots hold weapons as FMetalWeaponSlot entries that point at one of these assets.
 *
 * @see AMetalBotCharacter::FireOnce
 * @see AMetalPickup, which grants or upgrades weapons.
 */
UCLASS(BlueprintType)
class UMetalWeaponData : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Weapon")
	FText DisplayName;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Weapon")
	EMetalFireMode FireMode = EMetalFireMode::Hitscan;

	/** Hold to keep firing. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Weapon")
	bool bAutomatic = true;

	/** Infinite reserve ammo (the default laser). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Weapon")
	bool bInfiniteAmmo = false;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Weapon")
	float ReloadTime = 1.2f;

	/** Shots per trigger pull (shotgun pellets). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Weapon")
	int32 Pellets = 1;

	/** Cone half-angle in degrees. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Weapon")
	float SpreadDegrees = 1.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Weapon")
	float Range = 6000.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Projectile")
	float ProjectileSpeed = 5000.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Projectile")
	float ProjectileGravityScale = 0.f;

	/** 0 for direct-hit only. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Projectile")
	float ExplosionRadius = 0.f;

	/** Seconds an explosion disables robot electronics (EMP grenade). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Projectile")
	float StunDuration = 0.f;

	/** Distance in front of the weapon bone where shots appear. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Visual")
	float MuzzleOffset = 45.f;

	/** Size multiplier for projectile slugs and their impacts (craft-sized cannons). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Visual")
	float ProjectileScale = 1.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Visual")
	FLinearColor TracerColor = FLinearColor(1.f, 0.4f, 0.1f);

	/** Spread (cone half-angle) when fired without aiming. Negative = same as SpreadDegrees. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Weapon")
	float HipSpreadDegrees = -1.f;

	/** Camera field of view while aiming this weapon (the default action camera uses 60). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Aim")
	float AimFOV = 60.f;

	/** Camera distance while aiming, as a fraction of the robot's normal distance. 0 = scope at eye level. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Aim")
	float AimArmLengthScale = 0.55f;

	/** Draws a scope overlay and hides the robot's own body while aiming (sniper). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Aim")
	bool bScopeOverlay = false;

	/** Mouse/stick look speed multiplier while aiming. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Aim")
	float AimSensitivity = 1.f;

	/** AI holding this weapon shows a laser sight while it has a target, so players can read where it aims. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Aim")
	bool bLaserSight = false;

	/** Enemies within this distance hear a shot and come to investigate. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Stealth")
	float NoiseRadius = 2200.f;

	/** Sound library names (see UMetalSoundLibrary). None = silent. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Audio")
	FName FireSound;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Audio")
	FName ReloadSound = TEXT("reload");

	/** Played where a shot lands (hitscan) or a projectile hits/explodes. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Audio")
	FName ImpactSound = TEXT("impact");

	/** Static mesh used for pickups and thrown grenades. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Visual")
	TObjectPtr<UStaticMesh> PropMesh;

	/** Upgrade levels, index 0 is the base weapon. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Weapon")
	TArray<FMetalWeaponLevel> Levels;

	/**
	 * Looks up the stats for an upgrade level.
	 *
	 * @param LevelIndex Upgrade level; values outside [0, GetMaxLevel()] are clamped.
	 * @return The level's stats, or a default-constructed level if the asset has none.
	 */
	const FMetalWeaponLevel& GetLevel(int32 LevelIndex) const;

	/** @return Index of the highest upgrade level (0 when the weapon has no upgrades). */
	int32 GetMaxLevel() const { return FMath::Max(0, Levels.Num() - 1); }
};
