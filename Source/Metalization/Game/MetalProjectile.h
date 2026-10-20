// Bolts, rockets and grenades. Configured from the firing weapon's UMetalWeaponData.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "MetalProjectile.generated.h"

class UMetalWeaponData;
class UProjectileMovementComponent;
class USphereComponent;
class UStaticMeshComponent;

/**
 * A physical shot spawned by AMetalBotCharacter::FireOnce / ThrowGrenade and by capital ship turrets.
 * The spawner must set Instigator (for friendly-fire and kill credit) and then call Launch().
 * Direct-hit shots deal point damage (so hit zones apply); explosive ones deal radial damage with
 * falloff and can stun; thrown ones bounce and explode on a fuse or on touching a robot.
 */
UCLASS(NotPlaceable)
class AMetalProjectile : public AActor
{
	GENERATED_BODY()

public:
	/** Creates the collision sphere (ignoring Visibility so it never blocks hitscan traces), visual and movement. */
	AMetalProjectile();

	/**
	 * Configures and fires the projectile. Call right after spawning.
	 *
	 * @param Weapon   Weapon whose explosion, stun, colour, scale, gravity and fire mode apply. Must not be null.
	 * @param InDamage Damage dealt on a direct hit, or at the centre of the explosion.
	 * @param Velocity Initial velocity in cm/s.
	 */
	void Launch(const UMetalWeaponData* Weapon, float InDamage, const FVector& Velocity);

	/** Counts down a grenade's fuse. */
	virtual void Tick(float DeltaSeconds) override;

	/** Stops the shooter ignoring this projectile. */
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

protected:
	/** Detonates on impact, except that a grenade with fuse left only goes off when it hits a robot. */
	UFUNCTION()
	void OnHit(UPrimitiveComponent* HitComponent, AActor* OtherActor, UPrimitiveComponent* OtherComp, FVector NormalImpulse, const FHitResult& Hit);

	/**
	 * Deals the damage and plays the impact, then destroys the projectile. Only runs once.
	 *
	 * @param DirectHit What was struck, or null for a fuse explosion in mid-air.
	 */
	void Detonate(const FHitResult* DirectHit);

	UPROPERTY(VisibleAnywhere)
	TObjectPtr<USphereComponent> Collision;

	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UStaticMeshComponent> Visual;

	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UProjectileMovementComponent> Movement;

private:
	float Damage = 0.f;
	float ExplosionRadius = 0.f;
	float StunDuration = 0.f;
	float FuseTime = 0.f;
	FLinearColor Color = FLinearColor::White;
	float Scale = 1.f;
	bool bDetonated = false;
};
