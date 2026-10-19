// Data-driven weapon definition. One asset per weapon; upgrade levels live inside the asset.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "MetalWeaponData.generated.h"

class USkeletalMesh;
class UStaticMesh;

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

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Stats")
	float Damage = 10.f;

	/** Seconds between shots. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Stats")
	float FireInterval = 0.2f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Stats")
	int32 ClipSize = 20;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Stats")
	int32 MaxReserveAmmo = 200;
};

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

	/** Static mesh used for pickups and thrown grenades. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Visual")
	TObjectPtr<UStaticMesh> PropMesh;

	/** Upgrade levels, index 0 is the base weapon. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Weapon")
	TArray<FMetalWeaponLevel> Levels;

	const FMetalWeaponLevel& GetLevel(int32 LevelIndex) const;
	int32 GetMaxLevel() const { return FMath::Max(0, Levels.Num() - 1); }
};
