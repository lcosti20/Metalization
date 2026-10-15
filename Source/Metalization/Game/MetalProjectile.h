// Bolts, rockets and grenades. Configured from the firing weapon's UMetalWeaponData.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "MetalProjectile.generated.h"

class UMetalWeaponData;
class UProjectileMovementComponent;
class USphereComponent;
class UStaticMeshComponent;

UCLASS(NotPlaceable)
class AMetalProjectile : public AActor
{
	GENERATED_BODY()

public:
	AMetalProjectile();

	/** Call right after spawning. */
	void Launch(const UMetalWeaponData* Weapon, float InDamage, const FVector& Velocity);

	virtual void Tick(float DeltaSeconds) override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

protected:
	UFUNCTION()
	void OnHit(UPrimitiveComponent* HitComponent, AActor* OtherActor, UPrimitiveComponent* OtherComp, FVector NormalImpulse, const FHitResult& Hit);

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
	bool bDetonated = false;
};
