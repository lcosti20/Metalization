#include "MetalizationGroundUnit.h"

#include "Components/StaticMeshComponent.h"
#include "PlanetSurfaceSubsystem.h"

AMetalizationGroundUnit::AMetalizationGroundUnit()
{
	PrimaryActorTick.bCanEverTick = true;

	RootScene = CreateDefaultSubobject<USceneComponent>(TEXT("RootScene"));
	RootComponent = RootScene;

	VisualMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("VisualMesh"));
	VisualMesh->SetupAttachment(RootScene);
	VisualMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	VisualMesh->SetSimulatePhysics(false);
	VisualMesh->SetCanEverAffectNavigation(false);
}

void AMetalizationGroundUnit::BeginPlay()
{
	Super::BeginPlay();

	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	SurfaceSubsystem = World->GetSubsystem<UPlanetSurfaceSubsystem>();
	SetActorTickEnabled(true);
}

void AMetalizationGroundUnit::StopMovement()
{
	CurrentWaypointIndex = INDEX_NONE;
	PathDirections.Reset();
}

void AMetalizationGroundUnit::SetPath(const TArray<FVector>& InPath)
{
	PathDirections = InPath;
	CurrentWaypointIndex = PathDirections.Num() > 0 ? 0 : INDEX_NONE;
}

void AMetalizationGroundUnit::IssueMoveToDirection(const FVector& Direction)
{
	if (!SurfaceSubsystem.IsValid())
	{
		return;
	}

	const FVector CurrentDirection = SurfaceSubsystem->DirectionFromWorld(GetActorLocation());
	const FVector TargetDirection = Direction.IsNearlyZero() ? CurrentDirection : Direction.GetSafeNormal();
	SurfaceSubsystem->RequestPath(CurrentDirection, TargetDirection, [this](TArray<FVector> InPath)
	{
		if (InPath.Num() == 0)
		{
			StopMovement();
			return;
		}

		SetPath(InPath);
	});
}

void AMetalizationGroundUnit::IssueMoveToWorldLocation(const FVector& WorldLocation)
{
	if (!SurfaceSubsystem.IsValid())
	{	return;
	}

	const FVector CurrentDirection = SurfaceSubsystem->DirectionFromWorld(GetActorLocation());
	const FVector TargetDirection = SurfaceSubsystem->DirectionFromWorld(WorldLocation);
	SurfaceSubsystem->RequestPath(CurrentDirection, TargetDirection, [this](TArray<FVector> InPath)
	{
		if (InPath.Num() == 0)
		{
			StopMovement();
			return;
		}

		SetPath(InPath);
	});
}

void AMetalizationGroundUnit::ApplySurfaceTransform(const FVector& NewDirection, float Offset)
{
	if (!SurfaceSubsystem.IsValid())
	{
		return;
	}

	const FVector Up = SurfaceSubsystem->DirectionFromWorld(GetActorLocation());
	const FVector Tangent = (NewDirection - Up * FVector::DotProduct(Up, NewDirection)).GetSafeNormal();
	if (Tangent.IsNearlyZero())
	{
		return;
	}

	const FVector SurfacePoint = SurfaceSubsystem->SurfacePoint(NewDirection, Offset);
	SetActorLocation(SurfacePoint, false);
	SetActorRotation(FRotationMatrix::MakeFromXZ(Tangent, Up).Rotator());
}

void AMetalizationGroundUnit::UpdateMovement(float DeltaSeconds)
{
	if (!SurfaceSubsystem.IsValid() || CurrentWaypointIndex == INDEX_NONE)
	{
		return;
	}

	const FVector CurrentDirection = SurfaceSubsystem->DirectionFromWorld(GetActorLocation());
	if (!PathDirections.IsValidIndex(CurrentWaypointIndex))
	{
		StopMovement();
		return;
	}

	const FVector TargetDirection = PathDirections[CurrentWaypointIndex].GetSafeNormal();
	if (TargetDirection.IsNearlyZero())
	{
		StopMovement();
		return;
	}

	FVector NextDirection = CurrentDirection;
	const float DistanceToTravel = MoveSpeed * DeltaSeconds;
	const bool bReached = SurfaceSubsystem->StepToward(NextDirection, TargetDirection, DistanceToTravel);
	ApplySurfaceTransform(NextDirection, SurfaceOffset);

	if (bReached || FVector::Distance(CurrentDirection, TargetDirection) <= ReachDistance)
	{
		if (CurrentWaypointIndex + 1 >= PathDirections.Num())
		{
			StopMovement();
			return;
		}

		++CurrentWaypointIndex;
	}
}

void AMetalizationGroundUnit::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	UpdateMovement(DeltaSeconds);
}
