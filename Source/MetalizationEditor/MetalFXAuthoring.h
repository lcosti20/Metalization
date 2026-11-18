// Builds the game's Niagara effects from code so the asset pipeline can regenerate them (Python can't edit
// Niagara emitters). Every system is a lightweight (stateless) emitter set cloned from the engine's
// MinimalLightweight template and configured property by property.

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "MetalFXAuthoring.generated.h"

class UNiagaraSystem;

UCLASS()
class UMetalFXAuthoring : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/**
	 * Dumps a Niagara system's emitters, modules and renderers (every edited property as text) to the log.
	 *
	 * @param SystemPath Object path of the system, e.g. /Niagara/DefaultAssets/Templates/Systems/MinimalLightweight.
	 * @return The dump, or an error message.
	 */
	UFUNCTION(BlueprintCallable, Category = "Metalization|FX")
	static FString DescribeNiagaraSystem(const FString& SystemPath);

	/**
	 * Creates or rebuilds every effect system in Folder (NS_MuzzleFlash, NS_Impact, NS_Explosion, NS_Burst,
	 * NS_Beam, NS_Tracer) and saves them. Each system tints its particles with the User.Color parameter.
	 *
	 * @param Folder         Content folder, e.g. /Game/Metalization/FX.
	 * @param SpriteMaterial Additive soft-dot particle material (colour x alpha).
	 * @param HotMaterial    Brighter, whiter version of SpriteMaterial for flashes and sparks.
	 * @param SmokeMaterial  Translucent soft puff material.
	 * @param FireMaterial   Translucent glowing puff (fireballs), visible on bright backgrounds too.
	 * @param BeamMaterial   Additive material for the beam core meshes.
	 * @param BeamMesh       Unit beam mesh: 100 cm along local Z, centred on its origin (the engine cylinder).
	 * @return Number of systems written without errors (6 when everything worked; see LogMetalFX otherwise).
	 */
	UFUNCTION(BlueprintCallable, Category = "Metalization|FX")
	static int32 BuildEffectSystems(const FString& Folder, UMaterialInterface* SpriteMaterial, UMaterialInterface* HotMaterial,
		UMaterialInterface* SmokeMaterial, UMaterialInterface* FireMaterial, UMaterialInterface* BeamMaterial, UStaticMesh* BeamMesh);
};
