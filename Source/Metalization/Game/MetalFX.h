// Combat effects (muzzle flashes, impacts, explosions, beams). Each effect is a Niagara system from
// /Game/Metalization/FX, built by the asset pipeline (ue_setup_fx.py + MetalizationEditor's UMetalFXAuthoring),
// tinted through its User.Color parameter and sized by the component's scale. Every call site goes through
// these helpers.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "MetalFX.generated.h"

class UNiagaraComponent;
class UNiagaraSystem;
class UPointLightComponent;

/**
 * A short-lived effect: one Niagara component plus an optional point light that fades out. Spawn it through the
 * static helpers; it destroys itself once the particles and the light are done, unless it is a persistent beam
 * (Lifetime <= 0), which the caller re-aims with SetBeamEndpoints and destroys.
 */
UCLASS(NotPlaceable, Transient)
class AMetalFX : public AActor
{
	GENERATED_BODY()

public:
	/** Creates the (inactive) Niagara component and the (hidden) light. */
	AMetalFX();

	/**
	 * Glowing line from Start to End: a bright core, a faint halo and crackling sparks.
	 *
	 * @param World    World to spawn in. Null returns null.
	 * @param Start    One end of the beam.
	 * @param End      The other end.
	 * @param Color    Glow colour.
	 * @param Width    Core thickness in cm.
	 * @param Lifetime > 0: a tracer that thins out and fades in about 0.1 s. <= 0: a persistent beam (laser
	 *                 sight, tether) kept until destroyed.
	 * @return The beam actor, or null.
	 */
	static AMetalFX* SpawnBeam(UWorld* World, const FVector& Start, const FVector& End, const FLinearColor& Color, float Width = 3.f, float Lifetime = 0.08f);

	/**
	 * Swelling glow with drifting motes and a light (pickups, consoles, small sparks of light).
	 *
	 * @param World    World to spawn in. Null returns null.
	 * @param Location Centre of the burst.
	 * @param Color    Glow and light colour.
	 * @param Radius   Glow radius in cm; the light reaches four times as far.
	 * @param Lifetime Seconds for the light to fade.
	 * @return The burst actor, or null.
	 */
	static AMetalFX* SpawnBurst(UWorld* World, const FVector& Location, const FLinearColor& Color, float Radius, float Lifetime = 0.3f);

	/**
	 * Muzzle flash: a hot star, flame petals and sparks thrown along Direction, with a quick light.
	 *
	 * @param World     World to spawn in. Null returns null.
	 * @param Location  Muzzle position.
	 * @param Direction Firing direction (needn't be normalized).
	 * @param Color     Flash colour (the weapon's tracer colour).
	 * @param Size      Flash radius in cm.
	 * @return The flash actor, or null.
	 */
	static AMetalFX* SpawnMuzzleFlash(UWorld* World, const FVector& Location, const FVector& Direction, const FLinearColor& Color, float Size);

	/**
	 * Bullet impact: a flash, sparks bouncing off the surface and a dust puff.
	 *
	 * @param World    World to spawn in. Null returns null.
	 * @param Location Hit point.
	 * @param Normal   Surface normal at the hit point (the sparks fly along it).
	 * @param Color    Spark colour.
	 * @param Size     Flash radius in cm.
	 * @return The impact actor, or null.
	 */
	static AMetalFX* SpawnImpact(UWorld* World, const FVector& Location, const FVector& Normal, const FLinearColor& Color, float Size);

	/**
	 * Explosion: white-hot core, fireball, flying sparks, a rising smoke cloud and a bright light.
	 *
	 * @param World    World to spawn in. Null returns null.
	 * @param Location Centre of the blast.
	 * @param Color    Fire colour.
	 * @param Radius   Fireball radius in cm; the light reaches four times as far.
	 * @param Lifetime Seconds for the light to fade.
	 * @return The explosion actor, or null.
	 */
	static AMetalFX* SpawnExplosion(UWorld* World, const FVector& Location, const FLinearColor& Color, float Radius, float Lifetime = 0.45f);

	/**
	 * Re-aims a beam (the effect is a 100 cm unit beam stretched between the points).
	 *
	 * @param Start New start point.
	 * @param End   New end point.
	 */
	void SetBeamEndpoints(const FVector& Start, const FVector& End);

	/** Fades the light; destroys the actor once the light has faded and the particles are gone. */
	virtual void Tick(float DeltaSeconds) override;

private:
	/**
	 * Spawns an actor at Transform playing System, tinted Color, with an optional light.
	 *
	 * @param World       World to spawn in.
	 * @param SystemName  Asset name under /Game/Metalization/FX (e.g. NS_Impact).
	 * @param Transform   Actor location and rotation; its scale goes on the effect only, not the light.
	 * @param Color       Value for User.Color.
	 * @param LightRadius Point light radius in cm; 0 for no light.
	 * @param Lifetime    Seconds for the light to fade; <= 0 makes the effect persistent.
	 * @return The actor, or null.
	 */
	static AMetalFX* SpawnEffect(UWorld* World, const TCHAR* SystemName, const FTransform& Transform, const FLinearColor& Color, float LightRadius, float Lifetime);

	/** Keeps the effect and the light apart so scaling the effect doesn't touch the light. */
	UPROPERTY(VisibleAnywhere)
	TObjectPtr<USceneComponent> Root;

	/** The particle system being played. */
	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UNiagaraComponent> Effect;

	/** Optional light, faded out over Lifetime. */
	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UPointLightComponent> Light;

	/** Seconds for the light to fade; <= 0 is a persistent effect that never expires. */
	float Lifetime = 0.f;
	/** Seconds alive so far. */
	float Age = 0.f;
	/** Light intensity at spawn, faded to 0 over the lifetime. */
	float StartLightIntensity = 0.f;
	/** Core thickness of a beam, in cm. */
	float BeamWidth = 3.f;
};

namespace MetalAssets
{
	/**
	 * Unlit additive glow material created by the import pipeline (params: Color, Intensity), used on glowing
	 * props (consoles, lifts, pickups, projectiles, trigger zones, jet flames); the combat effects are Niagara.
	 * Loaded by path and cached; it's force-cooked through DirectoriesToAlwaysCook in DefaultGame.ini.
	 *
	 * @return The material, or null (with a warning) if the asset pipeline hasn't been run.
	 */
	UMaterialInterface* GetGlowMaterial();

	/**
	 * A Niagara system from /Game/Metalization/FX, loaded once and kept (rooted) for the rest of the session.
	 * /Game/Metalization/FX is force-cooked through DirectoriesToAlwaysCook in DefaultGame.ini.
	 *
	 * @param Name Asset name, e.g. NS_Explosion.
	 * @return The system, or null (with a warning, once) if the asset pipeline hasn't built it.
	 */
	UNiagaraSystem* GetEffect(const TCHAR* Name);
}
