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

UCLASS(NotPlaceable, Transient)
class AMetalFX : public AActor
{
	GENERATED_BODY()

public:
	AMetalFX();

	/** Glowing line from Start to End (laser bolts, tether). */
	static AMetalFX* SpawnBeam(UWorld* World, const FVector& Start, const FVector& End, const FLinearColor& Color, float Width = 3.f, float Lifetime = 0.08f);

	/** Expanding glowing sphere with a light (impacts, explosions). */
	static AMetalFX* SpawnBurst(UWorld* World, const FVector& Location, const FLinearColor& Color, float Radius, float Lifetime = 0.3f);

	/** Re-aims a persistent beam spawned with Lifetime <= 0. */
	void SetBeamEndpoints(const FVector& Start, const FVector& End);

	virtual void Tick(float DeltaSeconds) override;

private:
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
	/** Unlit additive glow material created by the import pipeline (params: Color, Intensity). */
	UMaterialInterface* GetGlowMaterial();
}
