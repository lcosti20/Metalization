// See-through energy field that stops shots but not sight lines or the Hack Tether.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "MetalEnergyField.generated.h"

class UMaterialInstanceDynamic;
class UStaticMeshComponent;
struct FCollisionQueryParams;

/**
 * A glowing, semi-transparent energy field (a scaled engine cube; the level scripts set its size and the
 * M_EnergyField material). It blocks robots, hitscan shots (ECC_Visibility), projectiles and blast damage.
 * Sight and the Hack Tether see through it: those traces call IgnoreAll first. Every shot that lands on
 * the field starts a ring wave on its surface at the impact point (up to MaxRipples at once, oldest replaced).
 */
UCLASS()
class AMetalEnergyField : public AActor
{
	GENERATED_BODY()

public:
	/** Builds the field slab from the engine cube with its blocking collision. */
	AMetalEnergyField();

	/** Field colour (the material's Color parameter). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Energy Field")
	FLinearColor FieldColor = FLinearColor(1.f, 0.3f, 0.1f);

	/** Seconds a ripple takes to spread and fade (matches the material's RippleLife). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Energy Field")
	float RippleLife = 0.9f;

	/** Starts a ring wave on the field surface at Location (world space). */
	void AddRipple(const FVector& Location);

	/** Makes a trace pass through every energy field: sight lines and the Hack Tether use this. */
	static void IgnoreAll(const UWorld* World, FCollisionQueryParams& Params);

	/** Advances the ripples; ticking switches itself off once they have all faded. */
	virtual void Tick(float DeltaSeconds) override;

	/** Shots that land on the field ripple it instead of hurting anything. */
	virtual float TakeDamage(float DamageAmount, const FDamageEvent& DamageEvent, AController* EventInstigator, AActor* DamageCauser) override;

protected:
	/** Creates the dynamic material that carries the ripple parameters. */
	virtual void BeginPlay() override;

	/** The visible field slab, which also does the blocking. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Energy Field")
	TObjectPtr<UStaticMeshComponent> Field;

private:
	/** Must match the HitN parameters in M_EnergyField (ue_setup_gameplay.py). */
	static constexpr int32 MaxRipples = 4;

	/** Writes each ripple's centre and age into the material (age -1 = unused slot). */
	void PushRipples();

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> Material;

	/** World-space centre of each ripple. */
	FVector RippleCenters[MaxRipples];
	/** World time each ripple started, or a negative number for an unused slot. */
	float RippleStarts[MaxRipples] = {-1.f, -1.f, -1.f, -1.f};
};
