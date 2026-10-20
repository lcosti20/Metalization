// Floating collectible: repairs, ammo, grenades, new weapons and weapon upgrades.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "MetalPickup.generated.h"

class UMetalWeaponData;
class USphereComponent;
class UStaticMeshComponent;
class UPointLightComponent;

/** What a pickup gives. Amount's meaning depends on the type (see AMetalPickup::Amount). */
UENUM(BlueprintType)
enum class EMetalPickupType : uint8
{
	/** Repairs Amount health. */
	Health,
	/** Refills Amount (0-1) of every weapon's reserve. */
	Ammo,
	/** Adds Amount grenades. */
	Grenades,
	/** Grants Weapon, or upgrades it a level if already owned. */
	Weapon
};

/**
 * A bobbing, spinning collectible. Only the player's own body can collect it, and it stays in the world
 * if it would have no effect (full health, full ammo, weapon already maxed and full).
 */
UCLASS()
class AMetalPickup : public AActor
{
	GENERATED_BODY()

public:
	/** Creates the trigger sphere, visual mesh and coloured light. */
	AMetalPickup();

	/** What the pickup gives. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Pickup")
	EMetalPickupType Type = EMetalPickupType::Health;

	/** Weapon granted or upgraded when Type is Weapon. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Pickup", meta=(EditCondition="Type==EMetalPickupType::Weapon"))
	TObjectPtr<UMetalWeaponData> Weapon;

	/** Health points, ammo fraction (0-1) or grenade count depending on Type. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Pickup")
	float Amount = 50.f;

	/** Picks the visual: the weapon's prop mesh, or an engine sphere/cube for the other types. */
	virtual void OnConstruction(const FTransform& Transform) override;

	/** Bobs and spins the visual. */
	virtual void Tick(float DeltaSeconds) override;

protected:
	/** Binds the overlap event and applies the glow material to non-weapon visuals. */
	virtual void BeginPlay() override;

	/** Applies the pickup to the player's body, shows a notice and destroys itself if it was used. */
	UFUNCTION()
	void OnOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor, UPrimitiveComponent* OtherComp, int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult);

	UPROPERTY(VisibleAnywhere)
	TObjectPtr<USphereComponent> Trigger;

	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UStaticMeshComponent> Visual;

	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UPointLightComponent> Light;

private:
	/** @return The type's colour (green repair, amber ammo, blue grenades, pink weapon). */
	FLinearColor GetColor() const;
	float Age = 0.f;
	FVector VisualBase = FVector::ZeroVector;
};
