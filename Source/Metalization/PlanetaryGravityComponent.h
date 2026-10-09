#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "PlanetaryGravityComponent.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnSurfaceFrameUpdated, FVector, NewSurfaceUp, FQuat, NewSurfaceOrientation);

/**
 * Drop this on any Actor that should stick to and orient with the surface of the
 * nearest "Planet"-tagged actor: characters, physics props, vehicles, turrets, etc.
 * Handles finding the nearest planet, computing a stable (yaw-preserving) surface
 * frame, and optionally driving gravity on the owner's movement component.
 */
UCLASS(ClassGroup = (Custom), meta = (BlueprintSpawnableComponent))
class METALIZATION_API UPlanetaryGravityComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UPlanetaryGravityComponent();

	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

	/** Radius around the owner to search for gravity sources */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Planetary Gravity")
	float GravitySearchRadius = 10000.0f; // 100 meters

	/** If true, and the owner has a movement component, automatically set its gravity direction each tick */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Planetary Gravity")
	bool bDriveMovementComponentGravity = true;

	/** Broadcast whenever the surface frame is recomputed this tick */
	UPROPERTY(BlueprintAssignable, Category = "Planetary Gravity")
	FOnSurfaceFrameUpdated OnSurfaceFrameUpdated;

	/** Always-fresh surface-up vector computed from current owner position (no caching/lag) */
	UFUNCTION(BlueprintCallable, Category = "Planetary Gravity")
	FVector GetCurrentSurfaceUp() const;

	UFUNCTION(BlueprintCallable, Category = "Planetary Gravity")
	FQuat GetSurfaceOrientation() const { return SurfaceOrientation; }

	UFUNCTION(BlueprintCallable, Category = "Planetary Gravity")
	AActor* GetCurrentPlanet() const { return CurrentPlanet; }

	UFUNCTION(BlueprintCallable, Category = "Planetary Gravity")
	FVector GetPlanetCenter() const { return PlanetCenter; }

	/**
	 * Utility usable by any owner: smoothly rotates the owner so its local Z becomes
	 * the current surface-up and its local X faces DesiredForward (projected onto
	 * the surface plane internally by the caller if needed).
	 */
	UFUNCTION(BlueprintCallable, Category = "Planetary Gravity")
	void OrientOwnerToSurface(float DeltaTime, const FVector& DesiredForward, float InterpSpeed);

protected:
	/** World location of the current planet center */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Planetary Gravity")
	FVector PlanetCenter = FVector::ZeroVector;

	/** Active planet actor pulling the owner */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Planetary Gravity")
	AActor* CurrentPlanet = nullptr;

	// Persistent, incrementally-updated surface-aligned frame (no yaw/pitch baked in)
	FQuat SurfaceOrientation = FQuat::Identity;

	// Cached each tick so dependent systems (movement, camera, etc.) agree on the same "up"
	FVector CurrentSurfaceUp = FVector::UpVector;

	bool bSurfaceFrameInitialized = false;

	AActor* FindNearestPlanet() const;
	void SetPlanet(AActor* NearestPlanet);
	void UpdatePlanetaryFrame(float DeltaTime);
	void InitializeSurfaceFrame(const FVector& NewSurfaceUp, AActor* Owner);
	void UpdateSurfaceFrame(const FVector& NewSurfaceUp);
	void UpdateGravity(const FVector& NewSurfaceUp, AActor* Owner);
};
