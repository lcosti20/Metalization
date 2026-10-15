// Floating collectible: repairs, ammo, grenades, new weapons and weapon upgrades.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "MetalPickup.generated.h"

class UMetalWeaponData;
class USphereComponent;
class UStaticMeshComponent;
class UPointLightComponent;

UENUM(BlueprintType)
enum class EMetalPickupType : uint8
{
	Health,
	Ammo,
	Grenades,
	/** Grants Weapon, or upgrades it a level if already owned. */
	Weapon
};

UCLASS()
class AMetalPickup : public AActor
{
	GENERATED_BODY()

public:
	AMetalPickup();

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Pickup")
	EMetalPickupType Type = EMetalPickupType::Health;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Pickup", meta=(EditCondition="Type==EMetalPickupType::Weapon"))
	TObjectPtr<UMetalWeaponData> Weapon;

	/** Health points, ammo fraction (0-1) or grenade count depending on Type. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Pickup")
	float Amount = 50.f;

	virtual void OnConstruction(const FTransform& Transform) override;
	virtual void Tick(float DeltaSeconds) override;

protected:
	virtual void BeginPlay() override;

	UFUNCTION()
	void OnOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor, UPrimitiveComponent* OtherComp, int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult);

	UPROPERTY(VisibleAnywhere)
	TObjectPtr<USphereComponent> Trigger;

	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UStaticMeshComponent> Visual;

	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UPointLightComponent> Light;

private:
	FLinearColor GetColor() const;
	float Age = 0.f;
	FVector VisualBase = FVector::ZeroVector;
};
