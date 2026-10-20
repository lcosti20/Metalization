// Lightweight, asset-free combat effects (tracers, muzzle flashes, explosions, tether beams).
// Placeholders until proper Niagara/MetaSound assets exist; every call site goes through
// these helpers so they can be swapped in one place.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "MetalFX.generated.h"

class UStaticMeshComponent;
class UPointLightComponent;
class UMaterialInstanceDynamic;

/**
 * A short-lived glowing mesh (engine cylinder or sphere with the M_FX_Glow material) plus an optional
 * point light. Spawn it through the static helpers; it fades its glow and light over its lifetime and
 * destroys itself, unless spawned with Lifetime <= 0, in which case the caller owns and destroys it.
 */
UCLASS(NotPlaceable, Transient)
class AMetalFX : public AActor
{
	GENERATED_BODY()

public:
	/** Creates the collision-free mesh and the (initially hidden) light. */
	AMetalFX();

	/**
	 * Glowing line from Start to End (laser bolts, tether). The beam thins to nothing as it fades.
	 *
	 * @param World    World to spawn in. Null returns null.
	 * @param Start    One end of the beam.
	 * @param End      The other end.
	 * @param Color    Glow colour.
	 * @param Width    Beam thickness in cm.
	 * @param Lifetime Seconds before it disappears; <= 0 keeps it until destroyed.
	 * @return The beam actor, or null.
	 */
	static AMetalFX* SpawnBeam(UWorld* World, const FVector& Start, const FVector& End, const FLinearColor& Color, float Width = 3.f, float Lifetime = 0.08f);

	/**
	 * Expanding glowing sphere with a light (impacts, explosions).
	 *
	 * @param World    World to spawn in. Null returns null.
	 * @param Location Centre of the burst.
	 * @param Color    Glow and light colour.
	 * @param Radius   Final radius in cm; the light reaches four times as far.
	 * @param Lifetime Seconds to expand and fade.
	 * @return The burst actor, or null.
	 */
	static AMetalFX* SpawnBurst(UWorld* World, const FVector& Location, const FLinearColor& Color, float Radius, float Lifetime = 0.3f);

	/**
	 * Re-aims a persistent beam spawned with Lifetime <= 0.
	 *
	 * @param Start New start point.
	 * @param End   New end point.
	 */
	void SetBeamEndpoints(const FVector& Start, const FVector& End);

	/** Scales towards EndScale and fades the glow and light; destroys the actor at the end of its lifetime. */
	virtual void Tick(float DeltaSeconds) override;

private:
	/**
	 * Shared setup for beams and bursts.
	 *
	 * @param Mesh        Engine shape to show.
	 * @param Color       Glow colour.
	 * @param InLifetime  Seconds to live (<= 0 for persistent).
	 * @param LightRadius Point light radius in cm; 0 for no light.
	 */
	void Setup(UStaticMesh* Mesh, const FLinearColor& Color, float InLifetime, float LightRadius);

	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UStaticMeshComponent> MeshComponent;

	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UPointLightComponent> Light;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> Material;

	FVector StartScale = FVector::OneVector;
	FVector EndScale = FVector::OneVector;
	float BeamWidth = 3.f;
	float Lifetime = 0.f;
	float Age = 0.f;
	float StartLightIntensity = 0.f;
	float StartGlow = 0.f;
};

namespace MetalAssets
{
	/**
	 * Unlit additive glow material created by the import pipeline (params: Color, Intensity).
	 * Loaded by path and cached; it's force-cooked through DirectoriesToAlwaysCook in DefaultGame.ini.
	 *
	 * @return The material, or null (with a warning) if the asset pipeline hasn't been run.
	 */
	UMaterialInterface* GetGlowMaterial();
}
