#include "PlanetaryGravityComponent.h"
#include "GameFramework/MovementComponent.h"
#include "Kismet/GameplayStatics.h"

UPlanetaryGravityComponent::UPlanetaryGravityComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
}

void UPlanetaryGravityComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	UpdatePlanetaryFrame(DeltaTime);
}

void UPlanetaryGravityComponent::UpdatePlanetaryFrame(float DeltaTime)
{
	AActor* Owner = GetOwner();
	if (!Owner)
	{
		return;
	}

	// Find and set the nearest planet as the current planetary reference.
	SetPlanet(FindNearestPlanet());

	// Compute the surface "up" vector: from planet center to owner location.
	FVector NewSurfaceUp = (Owner->GetActorLocation() - PlanetCenter).GetSafeNormal();

	// If degenerate (owner exactly at center or too close), fall back to the
	// previously initialized surface up, or world up.
	if (NewSurfaceUp.IsNearlyZero())
	{
		NewSurfaceUp = bSurfaceFrameInitialized ? SurfaceOrientation.GetAxisZ() : FVector::UpVector;
	}

	if (!bSurfaceFrameInitialized)
	{
		InitializeSurfaceFrame(NewSurfaceUp, Owner);
	}
	else
	{
		UpdateSurfaceFrame(NewSurfaceUp);
	}

	CurrentSurfaceUp = NewSurfaceUp;

	UpdateGravity(NewSurfaceUp, Owner);

	OnSurfaceFrameUpdated.Broadcast(CurrentSurfaceUp, SurfaceOrientation);
}

void UPlanetaryGravityComponent::InitializeSurfaceFrame(const FVector& NewSurfaceUp, AActor* Owner)
{
	// Seed the frame once, using current owner forward as the initial reference,
	// projected onto the tangent plane so it's horizontal relative to the surface.
	FVector InitialForward = FVector::VectorPlaneProject(Owner->GetActorForwardVector(), NewSurfaceUp).GetSafeNormal();
	if (InitialForward.IsNearlyZero())
	{
		InitialForward = FVector::VectorPlaneProject(FVector::ForwardVector, NewSurfaceUp).GetSafeNormal();
	}

	SurfaceOrientation = FRotationMatrix::MakeFromXZ(InitialForward, NewSurfaceUp).ToQuat();
	bSurfaceFrameInitialized = true;
}

void UPlanetaryGravityComponent::UpdateSurfaceFrame(const FVector& NewSurfaceUp)
{
	// Compute the smallest rotation taking the old up to the new up. This
	// preserves heading relative to the surface instead of reconstructing the
	// whole frame (which would cause yaw drift).
	FVector OldSurfaceUp = SurfaceOrientation.GetAxisZ();
	FQuat DeltaUpRot = FQuat::FindBetweenNormals(OldSurfaceUp, NewSurfaceUp);

	SurfaceOrientation = DeltaUpRot * SurfaceOrientation;
	SurfaceOrientation.Normalize();
}

void UPlanetaryGravityComponent::UpdateGravity(const FVector& NewSurfaceUp, AActor* Owner)
{
	UMovementComponent* MovementComp;
	UFunction* Func;

	if (!bDriveMovementComponentGravity)
	{
		return;
	}

	MovementComp = Owner->FindComponentByClass<UMovementComponent>();
	if (!MovementComp)
	{
		return;
	}

	// Try to call SetGravityDirection if the movement component implements it (via subclass or blueprint).
	// Use reflection to avoid a compile-time dependency on a specific subclass.
	Func = MovementComp->GetClass()->FindFunctionByName(TEXT("SetGravityDirection"));
	if (!Func)
	{
		return;
	}

	struct FSetGravityDirectionParams { FVector NewDirection; } Params;
	Params.NewDirection = -NewSurfaceUp;
	MovementComp->ProcessEvent(Func, &Params);
}

FVector UPlanetaryGravityComponent::GetCurrentSurfaceUp() const
{
	AActor* Owner = GetOwner();
	if (!Owner)
	{
		return FVector::UpVector;
	}

	FVector Up = (Owner->GetActorLocation() - PlanetCenter).GetSafeNormal();
	if (Up.IsNearlyZero())
	{
		return bSurfaceFrameInitialized ? SurfaceOrientation.GetAxisZ() : FVector::UpVector;
	}
	return Up;
}

void UPlanetaryGravityComponent::OrientOwnerToSurface(float DeltaTime, const FVector& DesiredForward, float InterpSpeed)
{
	AActor* Owner = GetOwner();
	if (!Owner)
	{
		return;
	}

	const FQuat TargetQuat = FRotationMatrix::MakeFromXZ(DesiredForward, CurrentSurfaceUp).ToQuat();
	const FQuat NewQuat = FMath::QInterpTo(Owner->GetActorQuat(), TargetQuat, DeltaTime, InterpSpeed);

	// Non-sweeping by design: trust the surface-normal math, avoid collision-deflection drift.
	Owner->SetActorRotation(NewQuat);
}

void UPlanetaryGravityComponent::SetPlanet(AActor* Planet)
{
	if (Planet)
	{
		CurrentPlanet = Planet;
		PlanetCenter = CurrentPlanet->GetActorLocation();
	}
}

AActor* UPlanetaryGravityComponent::FindNearestPlanet() const
{
	AActor* Owner = GetOwner();
	if (!Owner)
	{
		return nullptr;
	}

	TArray<AActor*> FoundPlanets;
	UGameplayStatics::GetAllActorsWithTag(GetWorld(), FName("Planet"), FoundPlanets);

	AActor* ClosestPlanet = nullptr;
	float MinDistanceSq = FLT_MAX;

	for (AActor* PlanetActor : FoundPlanets)
	{
		if (!PlanetActor)
		{
			continue;
		}

		float DistSq = FVector::DistSquared(Owner->GetActorLocation(), PlanetActor->GetActorLocation());
		if (DistSq <= GravitySearchRadius * GravitySearchRadius && DistSq < MinDistanceSq)
		{
			MinDistanceSq = DistSq;
			ClosestPlanet = PlanetActor;
		}
	}

	return ClosestPlanet;
}
