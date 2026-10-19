// Capital ships for the space battles. One AMetalCapitalShip per team: a big hull assembled from the
// modular graybox meshes (UMetalCapitalShipData), destructible subsystems, and an interior level that
// is streamed in as its own instance far above the battle, so both teams can share one interior map.
//
// Subclass it (in C++ or Blueprint) for ship classes with special rules; the OnSubsystemDestroyed,
// OnShipDestroyed and OnInteriorReady events and GetSpawnPoints are the extension points.

#pragma once

#include "CoreMinimal.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/DataAsset.h"
#include "GameFramework/Actor.h"
#include "MetalTypes.h"
#include "MetalCapitalShip.generated.h"

class AMetalBotCharacter;
class AMetalHangar;
class AMetalPortal;
class UMaterialInterface;
class APlayerStart;
class UInstancedStaticMeshComponent;
class ULevelStreamingDynamic;
class UMaterialInstanceDynamic;
class UMetalBotArchetype;
class UStaticMesh;

UENUM(BlueprintType)
enum class EMetalShipSubsystem : uint8
{
	ShieldGenerator,
	Engines,
	Comms,
	LifeSupport
};

/** A ship class: meshes, toughness, team paint and interior. */
UCLASS(BlueprintType)
class UMetalCapitalShipData : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Identity")
	FText DisplayName;

	/** The hull carries the sockets the other parts are mounted on (engines, shieldgen_L/R, comms, turret_NN, hangar_portal). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Meshes")
	TObjectPtr<UStaticMesh> HullMesh;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Meshes")
	TObjectPtr<UStaticMesh> EnginesMesh;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Meshes")
	TObjectPtr<UStaticMesh> ShieldGeneratorMesh;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Meshes")
	TObjectPtr<UStaticMesh> CommsMesh;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Meshes")
	TObjectPtr<UStaticMesh> TurretBaseMesh;

	/** Mounted on the turret base's "gun" socket. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Meshes")
	TObjectPtr<UStaticMesh> TurretGunMesh;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Health")
	float HullHealth = 20000.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Health")
	float ShieldGeneratorHealth = 2500.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Health")
	float EnginesHealth = 4000.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Health")
	float CommsHealth = 2000.f;

	/** Hull damage multiplier while any shield generator still stands. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Health", meta=(ClampMin="0", ClampMax="1"))
	float ShieldedHullDamageScale = 0.25f;

	/** Paint for the "_Hull" (Body) and "_Accent" material slots, per team. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Visual")
	TMap<EMetalTeam, FMetalTeamColors> TeamColors;

	/** Streamed in once per ship (a level instance at the ship's InteriorLocation). Must contain an AMetalHangar. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Interior")
	TSoftObjectPtr<UWorld> Interior;

	/** Robot parked on each hangar pad. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Interior")
	TObjectPtr<UMetalBotArchetype> HangarRobot;

	/** Seconds before a destroyed hangar robot is replaced on its pad. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Interior")
	float HangarRespawnDelay = 20.f;

	/** Opening of the hangar mouth (cm): matches the hull's and the interior shell's hangar_portal sockets. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Interior")
	FVector2D HangarMouthSize = FVector2D(8000.f, 3000.f);

	/** Surface of the hangar portal windows (screen-space sample of a "PortalTexture" parameter). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Interior")
	TObjectPtr<UMaterialInterface> PortalMaterial;
};

/** A destructible part of a capital ship (shield generator, engines...). Takes damage through the ship. */
UCLASS(ClassGroup=(Metalization), meta=(BlueprintSpawnableComponent))
class UMetalShipSubsystemComponent : public UStaticMeshComponent
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Subsystem")
	EMetalShipSubsystem Type = EMetalShipSubsystem::ShieldGenerator;

	UFUNCTION(BlueprintPure, Category="Subsystem")
	float GetHealth() const { return Health; }

	UFUNCTION(BlueprintPure, Category="Subsystem")
	float GetMaxHealth() const { return MaxHealth; }

	UFUNCTION(BlueprintPure, Category="Subsystem")
	bool IsDestroyed() const { return Health <= 0.f; }

	void ResetHealth(float NewMaxHealth);
	/** Returns true if this hit destroyed it. */
	bool ApplyDamage(float Amount);

private:
	float MaxHealth = 1000.f;
	float Health = 1000.f;
};

DECLARE_MULTICAST_DELEGATE_OneParam(FOnMetalShipEvent, AMetalCapitalShip* /*Ship*/);

UCLASS(Blueprintable)
class AMetalCapitalShip : public AActor
{
	GENERATED_BODY()

public:
	AMetalCapitalShip();

	/** Fired when any ship's interior has streamed in (the game mode waits for these before spawning players). */
	static FOnMetalShipEvent OnAnyInteriorReady;
	static FOnMetalShipEvent OnAnyShipDestroyed;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Metalization")
	TObjectPtr<UMetalCapitalShipData> ShipData;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Metalization")
	EMetalTeam Team = EMetalTeam::Heroes;

	/**
	 * Where this ship's interior instance is placed. It never moves, even if the ship does. Keep every
	 * ship's interior well apart (they are self-contained rooms) and above KillZ (UE's default is -10.5 km).
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Metalization")
	FVector InteriorLocation = FVector(0.f, 0.f, 2000000.f);

	/**
	 * The ship's parts stop drawing beyond this (cm), so it can't be seen from the interiors floating
	 * 20+ km away (they cull at AMetalHangar::InteriorDrawDistance). Keep it below that separation.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Metalization")
	float DrawDistance = 1500000.f;

	UFUNCTION(BlueprintPure, Category="Metalization")
	EMetalTeam GetTeam() const { return Team; }

	UFUNCTION(BlueprintPure, Category="Metalization")
	float GetHullHealth() const { return HullHealth; }

	UFUNCTION(BlueprintPure, Category="Metalization")
	float GetMaxHullHealth() const { return MaxHullHealth; }

	UFUNCTION(BlueprintPure, Category="Metalization")
	bool IsShipDestroyed() const { return bShipDestroyed; }

	/** True while any shield generator stands (hull damage is scaled down). */
	UFUNCTION(BlueprintPure, Category="Metalization")
	bool IsShielded() const;

	UFUNCTION(BlueprintPure, Category="Metalization")
	bool IsInteriorReady() const { return bInteriorReady; }

	const TArray<TObjectPtr<UMetalShipSubsystemComponent>>& GetSubsystems() const { return Subsystems; }
	AMetalHangar* GetHangar() const { return Hangar.Get(); }
	/** The two ends of the hangar portal: on the hull, and at the interior hangar's mouth. */
	AMetalPortal* GetOuterPortal() const { return OuterPortal.Get(); }
	AMetalPortal* GetInnerPortal() const { return InnerPortal.Get(); }
	/** The streamed interior level, once loaded. */
	ULevel* GetInteriorLevel() const;
	bool IsInInterior(const AActor* Actor) const;

	/** Where this ship's players (re)spawn: the player starts in its interior by default. */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category="Metalization")
	TArray<APlayerStart*> GetSpawnPoints() const;

	virtual float TakeDamage(float DamageAmount, struct FDamageEvent const& DamageEvent, AController* EventInstigator, AActor* DamageCauser) override;
	virtual void OnConstruction(const FTransform& Transform) override;
	virtual void Tick(float DeltaSeconds) override;

protected:
	virtual void BeginPlay() override;

	UFUNCTION(BlueprintNativeEvent, Category="Metalization")
	void OnSubsystemDestroyed(UMetalShipSubsystemComponent* Subsystem);

	UFUNCTION(BlueprintNativeEvent, Category="Metalization")
	void OnShipDestroyed();

	UFUNCTION(BlueprintNativeEvent, Category="Metalization")
	void OnInteriorReady();

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Components")
	TObjectPtr<UStaticMeshComponent> Hull;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Components")
	TObjectPtr<UMetalShipSubsystemComponent> Engines;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Components")
	TObjectPtr<UMetalShipSubsystemComponent> ShieldGeneratorL;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Components")
	TObjectPtr<UMetalShipSubsystemComponent> ShieldGeneratorR;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Components")
	TObjectPtr<UMetalShipSubsystemComponent> Comms;

	/** Turrets are scenery for now (one instance per turret_NN socket); AI turrets come with the battle AI. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Components")
	TObjectPtr<UInstancedStaticMeshComponent> TurretBases;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Components")
	TObjectPtr<UInstancedStaticMeshComponent> TurretGuns;

private:
	void ApplyShipData();
	void LoadInterior();
	UFUNCTION()
	void HandleInteriorShown();
	void ApplyTeamColors();
	void CreatePortals();
	void FlashComponent(UPrimitiveComponent* Component);
	void DestroySubsystem(UMetalShipSubsystemComponent* Subsystem);
	void DestroyShip();
	AMetalBotCharacter* FindAttacker(AController* EventInstigator, AActor* DamageCauser) const;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UMetalShipSubsystemComponent>> Subsystems;

	UPROPERTY(Transient)
	TObjectPtr<ULevelStreamingDynamic> InteriorStreaming;

	UPROPERTY(Transient)
	TMap<TObjectPtr<UPrimitiveComponent>, float> HitFlash;

	TWeakObjectPtr<AMetalHangar> Hangar;
	TWeakObjectPtr<AMetalPortal> OuterPortal;
	TWeakObjectPtr<AMetalPortal> InnerPortal;
	float MaxHullHealth = 20000.f;
	float HullHealth = 20000.f;
	bool bShipDestroyed = false;
	bool bInteriorReady = false;
	float DestroyedTime = -1.f;
	int32 WreckBursts = 0;
};
