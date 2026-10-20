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
class UMetalWeaponData;
class UStaticMesh;

/** The destructible parts of a capital ship. */
UENUM(BlueprintType)
enum class EMetalShipSubsystem : uint8
{
	/** Two per ship; while either stands the hull takes reduced damage. */
	ShieldGenerator,
	/** Losing them doubles the hangar's robot respawn delay. */
	Engines,
	/** No special effect yet beyond the battle mode's ticket penalty. */
	Comms,
	/** Lives in the interior hangar (AMetalHangar), reached by flying in through the hangar mouth. Losing it bleeds tickets. */
	LifeSupport
};

/** A ship class: meshes, toughness, team paint and interior. */
UCLASS(BlueprintType)
class UMetalCapitalShipData : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	/** Name of the ship class. */
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

	/** What the turrets fire (a projectile weapon). None = turrets are scenery. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Turrets")
	TObjectPtr<UMetalWeaponData> TurretWeapon;

	/** Turrets engage enemy flyers this close (cm). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Turrets")
	float TurretRange = 120000.f;

	/** Degrees per second. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Turrets")
	float TurretTurnRate = 70.f;

	/** Extra spread (degrees) on top of the weapon's, so turrets threaten rather than snipe. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Turrets")
	float TurretAimError = 2.5f;

	/** Starting health of the hull and each subsystem. */
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

	/** The life-support core in the interior hangar (it's shot by Stalkers that fly in through the enemy's hangar mouth). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Health")
	float LifeSupportHealth = 3000.f;

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

/**
 * A destructible part of a capital ship (shield generator, engines...). Takes damage through the ship:
 * the component only stores health, and AMetalCapitalShip decides what a hit does and what losing it means.
 */
UCLASS(ClassGroup=(Metalization), meta=(BlueprintSpawnableComponent))
class UMetalShipSubsystemComponent : public UStaticMeshComponent
{
	GENERATED_BODY()

public:
	/** Which subsystem this is. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Subsystem")
	EMetalShipSubsystem Type = EMetalShipSubsystem::ShieldGenerator;

	/** @return Current health. */
	UFUNCTION(BlueprintPure, Category="Subsystem")
	float GetHealth() const { return Health; }

	/** @return Health when undamaged. */
	UFUNCTION(BlueprintPure, Category="Subsystem")
	float GetMaxHealth() const { return MaxHealth; }

	/** @return True once health reaches 0. */
	UFUNCTION(BlueprintPure, Category="Subsystem")
	bool IsDestroyed() const { return Health <= 0.f; }

	/**
	 * Sets the maximum health and fully repairs the part.
	 *
	 * @param NewMaxHealth New maximum (at least 1).
	 */
	void ResetHealth(float NewMaxHealth);

	/**
	 * Subtracts health. Does nothing once destroyed.
	 *
	 * @param Amount Damage to take.
	 * @return True if this hit destroyed it.
	 */
	bool ApplyDamage(float Amount);

private:
	float MaxHealth = 1000.f;
	float Health = 1000.f;
};

/**
 * Broadcast for ship-wide events.
 *
 * @param Ship The ship concerned.
 */
DECLARE_MULTICAST_DELEGATE_OneParam(FOnMetalShipEvent, AMetalCapitalShip* /*Ship*/);

/**
 * Broadcast when a subsystem is destroyed.
 *
 * @param Ship      The ship that lost it.
 * @param Subsystem The destroyed part.
 */
DECLARE_MULTICAST_DELEGATE_TwoParams(FOnMetalSubsystemEvent, AMetalCapitalShip* /*Ship*/, UMetalShipSubsystemComponent* /*Subsystem*/);

/**
 * One team's capital ship. Builds itself from UMetalCapitalShipData (hull, four subsystems, socketed
 * turrets), streams in its interior level at InteriorLocation, links the hull and interior through a pair
 * of AMetalPortal actors, runs its AI turrets, and tracks hull and subsystem damage.
 *
 * @see AMetalSpaceBattleGameMode for the rules built on top of these events.
 * @see AMetalHangar for the interior side.
 */
UCLASS(Blueprintable)
class AMetalCapitalShip : public AActor
{
	GENERATED_BODY()

public:
	/** Creates the hull, the four subsystem components on their sockets, and the turret instance components. */
	AMetalCapitalShip();

	/** Fired when any ship's interior has streamed in (the game mode waits for these before spawning players). */
	static FOnMetalShipEvent OnAnyInteriorReady;

	/** Fired when any ship's hull reaches 0. */
	static FOnMetalShipEvent OnAnyShipDestroyed;

	/** Fired when any ship loses a subsystem. */
	static FOnMetalSubsystemEvent OnAnySubsystemDestroyed;

	/** The ship class. Required. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Metalization")
	TObjectPtr<UMetalCapitalShipData> ShipData;

	/** Side this ship fights for. */
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

	/** AI-piloted Stalkers this ship's hangar keeps in the fight (on top of the parked ones players can take). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Metalization")
	int32 AIPilots = 3;

	/** @return The ship's team. */
	UFUNCTION(BlueprintPure, Category="Metalization")
	EMetalTeam GetTeam() const { return Team; }

	/** @return Current hull health. */
	UFUNCTION(BlueprintPure, Category="Metalization")
	float GetHullHealth() const { return HullHealth; }

	/** @return Hull health when undamaged. */
	UFUNCTION(BlueprintPure, Category="Metalization")
	float GetMaxHullHealth() const { return MaxHullHealth; }

	/** @return True once the hull has been destroyed. */
	UFUNCTION(BlueprintPure, Category="Metalization")
	bool IsShipDestroyed() const { return bShipDestroyed; }

	/** True while any shield generator stands (hull damage is scaled down). */
	UFUNCTION(BlueprintPure, Category="Metalization")
	bool IsShielded() const;

	/** @return True once the interior level is visible, the hangar initialised and the portals made. */
	UFUNCTION(BlueprintPure, Category="Metalization")
	bool IsInteriorReady() const { return bInteriorReady; }

	/** @return Every subsystem: engines, both shield generators, comms, then any registered later (life support). */
	const TArray<TObjectPtr<UMetalShipSubsystemComponent>>& GetSubsystems() const { return Subsystems; }

	/**
	 * Adds a subsystem that lives outside the ship actor (the interior's life support) and resets its health.
	 *
	 * @param Subsystem Component to track.
	 * @param Health    Its maximum health.
	 */
	void RegisterSubsystem(UMetalShipSubsystemComponent* Subsystem, float Health);

	/**
	 * Damages one subsystem (no friendly fire). Used for subsystems that aren't components of this actor,
	 * whose owner forwards the damage here.
	 *
	 * @param Subsystem       Part to damage.
	 * @param Damage          Amount.
	 * @param EventInstigator Controller responsible, for the friendly-fire check.
	 * @param DamageCauser    Actor that caused it, for the friendly-fire check.
	 * @return The damage dealt.
	 */
	float ApplySubsystemDamage(UMetalShipSubsystemComponent* Subsystem, float Damage, AController* EventInstigator, AActor* DamageCauser);

	/** @return The hangar in this ship's interior, once streamed in. */
	AMetalHangar* GetHangar() const { return Hangar.Get(); }

	/** The two ends of the hangar portal: on the hull, and at the interior hangar's mouth. */
	AMetalPortal* GetOuterPortal() const { return OuterPortal.Get(); }

	/** @return The portal end at the interior hangar's mouth. */
	AMetalPortal* GetInnerPortal() const { return InnerPortal.Get(); }

	/** The streamed interior level, once loaded. */
	ULevel* GetInteriorLevel() const;

	/**
	 * @param Actor Actor to test.
	 * @return True if Actor belongs to this ship's streamed interior level.
	 */
	bool IsInInterior(const AActor* Actor) const;

	/** Where this ship's players (re)spawn: the player starts in its interior by default. */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category="Metalization")
	TArray<APlayerStart*> GetSpawnPoints() const;

	/**
	 * Routes damage to the subsystem that was hit, or else to the hull (scaled down while shielded).
	 * Ignores damage from its own team, including its own turrets' shots.
	 *
	 * @return Damage actually applied.
	 */
	virtual float TakeDamage(float DamageAmount, struct FDamageEvent const& DamageEvent, AController* EventInstigator, AActor* DamageCauser) override;

	/** Applies the ship data in the editor so designers see the whole ship. */
	virtual void OnConstruction(const FTransform& Transform) override;

	/** Runs the turrets, fades hit flashes and plays the wreck explosions. */
	virtual void Tick(float DeltaSeconds) override;

protected:
	/** Sets up health and team paint and starts streaming the interior. */
	virtual void BeginPlay() override;

	/**
	 * Extension point called when a subsystem is destroyed. The default doubles the hangar's respawn
	 * delay when the engines go.
	 *
	 * @param Subsystem The destroyed part.
	 */
	UFUNCTION(BlueprintNativeEvent, Category="Metalization")
	void OnSubsystemDestroyed(UMetalShipSubsystemComponent* Subsystem);

	/** Extension point called once when the hull is destroyed. Empty by default. */
	UFUNCTION(BlueprintNativeEvent, Category="Metalization")
	void OnShipDestroyed();

	/** Extension point called once the interior is ready. Empty by default. */
	UFUNCTION(BlueprintNativeEvent, Category="Metalization")
	void OnInteriorReady();

	/** Root mesh; carries the sockets every other part is mounted on. */
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

	/** One instance per turret_NN socket. The gun instances are re-aimed every tick by TickTurrets(). */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Components")
	TObjectPtr<UInstancedStaticMeshComponent> TurretBases;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Components")
	TObjectPtr<UInstancedStaticMeshComponent> TurretGuns;

private:
	/** Assigns the meshes and cull distance, and rebuilds the turret instances from the hull's turret_NN sockets. */
	void ApplyShipData();

	/** Starts streaming the interior level as a level instance at InteriorLocation. */
	void LoadInterior();

	/** Streaming callback: finds the hangar, creates the portals and announces the interior is ready. */
	UFUNCTION()
	void HandleInteriorShown();

	/** Gives every material slot a dynamic instance (for hit flashes) and paints the _Hull/_Accent slots. */
	void ApplyTeamColors();

	/** Spawns and links the two portal ends: on the hull's hangar_portal socket and at the hangar mouth. */
	void CreatePortals();

	/**
	 * Picks targets for each turret, turns the guns at a limited rate towards the lead point and fires.
	 *
	 * @param DeltaSeconds Frame time.
	 */
	void TickTurrets(float DeltaSeconds);

	/**
	 * Starts a hit flash on one of this ship's components.
	 *
	 * @param Component Component that was hit.
	 */
	void FlashComponent(UPrimitiveComponent* Component);

	/**
	 * Blackens the part, plays an explosion and fires the destroyed events.
	 *
	 * @param Subsystem The part that just reached 0 health.
	 */
	void DestroySubsystem(UMetalShipSubsystemComponent* Subsystem);

	/** Marks the ship destroyed, starts the wreck explosions and fires the destroyed events. */
	void DestroyShip();

	/**
	 * @return The robot responsible for some damage: the causer, the causer's instigator, or the
	 *         instigating controller's pawn. Null if none is a robot.
	 */
	AMetalBotCharacter* FindAttacker(AController* EventInstigator, AActor* DamageCauser) const;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UMetalShipSubsystemComponent>> Subsystems;

	UPROPERTY(Transient)
	TObjectPtr<ULevelStreamingDynamic> InteriorStreaming;

	/** Components currently flashing, with the flash strength (1 fading to 0). */
	UPROPERTY(Transient)
	TMap<TObjectPtr<UPrimitiveComponent>, float> HitFlash;

	/** Runtime state of one turret (its meshes are instances in TurretBases/TurretGuns at the same index). */
	struct FTurret
	{
		/** Mount on the hull (component space) and the gun's offset on top of it. */
		FTransform Mount;
		/** Current gun aim relative to the mount, degrees. */
		float Yaw = 0.f;
		float Pitch = 0.f;
		float NextFireTime = 0.f;
		TWeakObjectPtr<AActor> Target;
	};
	TArray<FTurret> Turrets;
	/** The turret base mesh's "gun" socket: where the gun sits on the base. */
	FTransform TurretGunOffset;
	float NextTurretThinkTime = 0.f;

	TWeakObjectPtr<AMetalHangar> Hangar;
	TWeakObjectPtr<AMetalPortal> OuterPortal;
	TWeakObjectPtr<AMetalPortal> InnerPortal;
	float MaxHullHealth = 20000.f;
	float HullHealth = 20000.f;
	bool bShipDestroyed = false;
	bool bInteriorReady = false;
	float DestroyedTime = -1.f;
	/** Explosions played so far while the wreck breaks up. */
	int32 WreckBursts = 0;
};
