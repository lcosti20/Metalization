#include "MetalizationGroundUnit.h"

#include "Components/StaticMeshComponent.h"
#include "MetalizationStructure.h"
#include "PlanetSurfaceSubsystem.h"

AMetalizationGroundUnit::AMetalizationGroundUnit()
{
	PrimaryActorTick.bCanEverTick = true;

	Tags.Add(TEXT("Selectable"));

	RootScene = CreateDefaultSubobject<USceneComponent>(TEXT("RootScene"));
	RootComponent = RootScene;

	VisualMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("VisualMesh"));
	VisualMesh->SetupAttachment(RootScene);
	VisualMesh->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	VisualMesh->SetCollisionResponseToAllChannels(ECR_Ignore);
	VisualMesh->SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);
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

void AMetalizationGroundUnit::SetSelected(bool bSelected)
{
	bIsSelected = bSelected;
	if (VisualMesh)
	{
		VisualMesh->SetRenderCustomDepth(bSelected);
		VisualMesh->SetCustomDepthStencilValue(bSelected ? 1 : 0);
	}
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
	CurrentTask = EMetalizationUnitTask::Move;
	CurrentTargetStructure.Reset();
	HarvestSource.Reset();
	DeliveryTarget.Reset();
	CarriedEnergy = 0.f;

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
	IssueMoveToDirection(SurfaceSubsystem.IsValid() ? SurfaceSubsystem->DirectionFromWorld(WorldLocation) : FVector::UpVector);
}

void AMetalizationGroundUnit::IssueCaptureOrder(AActor* TargetStructure)
{
	CurrentTask = EMetalizationUnitTask::Capture;
	CurrentTargetStructure = Cast<AMetalizationStructure>(TargetStructure);
	HarvestSource.Reset();
	DeliveryTarget.Reset();
	CarriedEnergy = 0.f;
	if (CurrentTargetStructure.IsValid())
	{
		IssueMoveToWorldLocation(CurrentTargetStructure->GetActorLocation());
	}
}

void AMetalizationGroundUnit::IssueHarvestOrder(AActor* SourceVent, AActor* DestinationColony)
{
	CurrentTask = EMetalizationUnitTask::Harvest;
	HarvestSource = Cast<AMetalizationStructure>(SourceVent);
	DeliveryTarget = Cast<AMetalizationStructure>(DestinationColony);
	CurrentTargetStructure.Reset();
	CarriedEnergy = 0.f;
	if (HarvestSource.IsValid())
	{
		IssueMoveToWorldLocation(HarvestSource->GetActorLocation());
	}
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

void AMetalizationGroundUnit::UpdateTaskState(float DeltaSeconds)
{
	if (CurrentTask == EMetalizationUnitTask::Capture && CurrentTargetStructure.IsValid())
	{
		AMetalizationStructure* Target = CurrentTargetStructure.Get();
		if (!Target->IsValidLowLevel())
		{
			CurrentTask = EMetalizationUnitTask::Idle;
			CurrentTargetStructure.Reset();
			return;
		}

		if (FVector::DistSquared(GetActorLocation(), Target->GetActorLocation()) <= Target->CaptureRadius * Target->CaptureRadius)
		{
			Target->ApplyCaptureProgress(CaptureRate * DeltaSeconds, TeamId);
		}
		else if (PathDirections.Num() == 0)
		{
			IssueMoveToWorldLocation(Target->GetActorLocation());
		}
		return;
	}

	if (CurrentTask == EMetalizationUnitTask::Harvest)
	{
		AMetalizationStructure* Vent = HarvestSource.Get();
		AMetalizationStructure* Colony = DeliveryTarget.Get();

		if (!Vent || !Colony)
		{
			CurrentTask = EMetalizationUnitTask::Idle;
			HarvestSource.Reset();
			DeliveryTarget.Reset();
			return;
		}

		if (CarriedEnergy <= KINDA_SMALL_NUMBER && FVector::DistSquared(GetActorLocation(), Vent->GetActorLocation()) <= 75.f * 75.f)
		{
			if (AMetalizationEnergyVent* EnergyVent = Cast<AMetalizationEnergyVent>(Vent))
			{
				CarriedEnergy += EnergyVent->HarvestEnergy(GatherRate * DeltaSeconds);
			}
		}
		else if (CarriedEnergy > KINDA_SMALL_NUMBER && FVector::DistSquared(GetActorLocation(), Colony->GetActorLocation()) <= 75.f * 75.f)
		{
			if (AMetalizationColony* Settlement = Cast<AMetalizationColony>(Colony))
			{
				Settlement->DepositEnergy(CarriedEnergy);
				CarriedEnergy = 0.f;
			}
		}
		else if (CarriedEnergy <= KINDA_SMALL_NUMBER && PathDirections.Num() == 0)
		{
			IssueMoveToWorldLocation(Vent->GetActorLocation());
		}
		else if (CarriedEnergy > KINDA_SMALL_NUMBER && PathDirections.Num() == 0)
		{
			IssueMoveToWorldLocation(Colony->GetActorLocation());
		}
	}
}

void AMetalizationGroundUnit::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	UpdateMovement(DeltaSeconds);
	UpdateTaskState(DeltaSeconds);
}
