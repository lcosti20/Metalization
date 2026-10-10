#include "MetalizationGroundUnit.h"

#include "Components/StaticMeshComponent.h"
#include "MetalizationGameState.h"
#include "MetalizationStructure.h"
#include "PlanetSurfaceSubsystem.h"
#include "UObject/ConstructorHelpers.h"

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

	static ConstructorHelpers::FObjectFinder<UStaticMesh> DefaultUnitMesh(TEXT("/Engine/BasicShapes/Cube.Cube"));
	if (DefaultUnitMesh.Succeeded())
	{
		VisualMesh->SetStaticMesh(DefaultUnitMesh.Object);
		VisualMesh->SetRelativeScale3D(FVector(0.5f));
	}
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
	if (VisualMesh && !VisualMesh->GetStaticMesh())
	{
		UE_LOG(LogTemp, Warning, TEXT("%s has no unit mesh assigned; displaying the fallback cube."), *GetName());
		if (UStaticMesh* DefaultUnitMesh = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube")))
		{
			VisualMesh->SetStaticMesh(DefaultUnitMesh);
			VisualMesh->SetRelativeScale3D(FVector(0.5f));
		}
	}

	if (AMetalizationGameState* GameState = World->GetGameState<AMetalizationGameState>())
	{
		GameState->RegisterTeam(TeamId, 0.f);
		GameState->IncrementUnitCount(TeamId, 1);
	}
	CurrentHealth = MaxHealth;
	AttackCooldownRemaining = 0.f;
	SetActorTickEnabled(true);
}

void AMetalizationGroundUnit::Destroyed()
{
	Super::Destroyed();

	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	if (AMetalizationGameState* GameState = World->GetGameState<AMetalizationGameState>())
	{
		GameState->IncrementUnitCount(TeamId, -1);
	}
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
	PendingMoveTargetDirection = FVector::UpVector;
}

void AMetalizationGroundUnit::SetPath(const TArray<FVector>& InPath)
{
	PathDirections = InPath;
	CurrentWaypointIndex = PathDirections.Num() > 0 ? 0 : INDEX_NONE;
}

void AMetalizationGroundUnit::IssueMoveToDirection(const FVector& Direction)
{
	if (CurrentTask == EMetalizationUnitTask::Idle || CurrentTask == EMetalizationUnitTask::Move)
	{
		CurrentTask = EMetalizationUnitTask::Move;
		CurrentTargetStructure.Reset();
		HarvestSource.Reset();
		DeliveryTarget.Reset();
		AttackTarget.Reset();
		CarriedEnergy = 0.f;
	}

	if (!SurfaceSubsystem.IsValid())
	{
		return;
	}

	const FVector CurrentDirection = SurfaceSubsystem->DirectionFromWorld(GetActorLocation());
	const FVector TargetDirection = Direction.IsNearlyZero() ? CurrentDirection : Direction.GetSafeNormal();
	PendingMoveTargetDirection = TargetDirection;
	SurfaceSubsystem->RequestPath(CurrentDirection, TargetDirection, [this](TArray<FVector> InPath)
	{
		if (InPath.Num() == 0)
		{
			const FVector CurrentDirection = SurfaceSubsystem.IsValid() ? SurfaceSubsystem->DirectionFromWorld(GetActorLocation()) : FVector::UpVector;
			const FVector FallbackDirection = (PendingMoveTargetDirection.IsNearlyZero() ? CurrentDirection : PendingMoveTargetDirection).GetSafeNormal();
			if (!FallbackDirection.IsNearlyZero())
			{
				SetPath(TArray<FVector>{FallbackDirection});
			}
			else
			{
				StopMovement();
			}
			return;
		}

		SetPath(InPath);
	});
}

void AMetalizationGroundUnit::IssueMoveToWorldLocation(const FVector& WorldLocation)
{
	IssueMoveToDirection(SurfaceSubsystem.IsValid() ? SurfaceSubsystem->DirectionFromWorld(WorldLocation) : FVector::UpVector);
}

void AMetalizationGroundUnit::IssueStopOrder()
{
	CurrentTask = EMetalizationUnitTask::Idle;
	CurrentTargetStructure.Reset();
	HarvestSource.Reset();
	DeliveryTarget.Reset();
	AttackTarget.Reset();
	CarriedEnergy = 0.f;
	StopMovement();
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
	AttackTarget.Reset();
	CarriedEnergy = 0.f;
	if (HarvestSource.IsValid())
	{
		IssueMoveToWorldLocation(HarvestSource->GetActorLocation());
	}
}

void AMetalizationGroundUnit::IssueAttackOrder(AActor* TargetActor)
{
	if (!TargetActor || TargetActor == this)
	{
		return;
	}

	if (AMetalizationGroundUnit* EnemyUnit = Cast<AMetalizationGroundUnit>(TargetActor))
	{
		if (EnemyUnit->GetTeamId() == TeamId)
		{
			return;
		}
	}
	else if (AMetalizationStructure* EnemyStructure = Cast<AMetalizationStructure>(TargetActor))
	{
		if (EnemyStructure->GetTeamId() == TeamId)
		{
			return;
		}
	}

	CurrentTask = EMetalizationUnitTask::Attack;
	AttackTarget = TargetActor;
	CurrentTargetStructure.Reset();
	HarvestSource.Reset();
	DeliveryTarget.Reset();
	CarriedEnergy = 0.f;
	if (SurfaceSubsystem.IsValid())
	{
		IssueMoveToWorldLocation(TargetActor->GetActorLocation());
	}
}

void AMetalizationGroundUnit::ApplyDamage(float DamageAmount, int32 DamageSourceTeamId)
{
	if (CurrentHealth <= 0.f)
	{
		return;
	}

	CurrentHealth = FMath::Max(0.f, CurrentHealth - DamageAmount);
	if (CurrentHealth <= 0.f)
	{
		Destroy();
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

	const float AngleToWaypoint = FMath::Acos(FMath::Clamp(FVector::DotProduct(CurrentDirection, TargetDirection), -1.f, 1.f));
	const float DistanceToWaypoint = AngleToWaypoint * SurfaceSubsystem->GetRadius();
	if (bReached || DistanceToWaypoint <= ReachDistance)
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
	if (CurrentHealth <= 0.f)
	{
		return;
	}

	if (AttackCooldownRemaining > 0.f)
	{
		AttackCooldownRemaining = FMath::Max(0.f, AttackCooldownRemaining - DeltaSeconds);
	}

	if (CurrentTask == EMetalizationUnitTask::Capture && CurrentTargetStructure.IsValid())
	{
		AMetalizationStructure* Target = CurrentTargetStructure.Get();
		if (!Target->IsValidLowLevel())
		{
			CurrentTask = EMetalizationUnitTask::Idle;
			CurrentTargetStructure.Reset();
			return;
		}

		if (FVector::DistSquared(GetActorLocation(), Target->GetActorLocation()) <= Target->GetCaptureRadius() * Target->GetCaptureRadius())
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

		const bool bAtVent = FVector::DistSquared(GetActorLocation(), Vent->GetActorLocation()) <= 75.f * 75.f;
		const bool bAtColony = FVector::DistSquared(GetActorLocation(), Colony->GetActorLocation()) <= 75.f * 75.f;

		if (CarriedEnergy <= KINDA_SMALL_NUMBER && bAtVent)
		{
			if (AMetalizationEnergyVent* EnergyVent = Cast<AMetalizationEnergyVent>(Vent))
			{
				const float RemainingCapacity = FMath::Max(0.f, CarryCapacity - CarriedEnergy);
				if (RemainingCapacity > KINDA_SMALL_NUMBER)
				{
					const float Harvested = EnergyVent->HarvestEnergy(FMath::Min(GatherRate * DeltaSeconds, RemainingCapacity));
					CarriedEnergy += Harvested;
				}
			}
		}
		else if (CarriedEnergy > KINDA_SMALL_NUMBER && bAtColony)
		{
			if (AMetalizationColony* Settlement = Cast<AMetalizationColony>(Colony))
			{
				const float Deposited = Settlement->DepositEnergy(CarriedEnergy);
				CarriedEnergy = FMath::Max(0.f, CarriedEnergy - Deposited);
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
		return;
	}

	if (CurrentTask == EMetalizationUnitTask::Attack)
	{
		AActor* TargetActor = AttackTarget.Get();
		if (!TargetActor)
		{
			AttackTarget.Reset();
			CurrentTask = EMetalizationUnitTask::Idle;
			return;
		}

		if (AMetalizationGroundUnit* Target = Cast<AMetalizationGroundUnit>(TargetActor))
		{
			if (!Target->IsAlive() || Target->GetTeamId() == TeamId)
			{
				AttackTarget.Reset();
				CurrentTask = EMetalizationUnitTask::Idle;
				return;
			}

			const float DistanceSq = FVector::DistSquared(GetActorLocation(), Target->GetActorLocation());
			if (DistanceSq <= AttackRange * AttackRange)
			{
				if (AttackCooldownRemaining <= 0.f)
				{
					Target->ApplyDamage(AttackDamage, TeamId);
					AttackCooldownRemaining = AttackCooldown;
				}
				StopMovement();
				return;
			}
			if (PathDirections.Num() == 0)
			{
				IssueMoveToWorldLocation(Target->GetActorLocation());
			}
			return;
		}

		if (AMetalizationStructure* TargetStructure = Cast<AMetalizationStructure>(TargetActor))
		{
			if (!TargetStructure->IsAlive() || TargetStructure->GetTeamId() == TeamId)
			{
				AttackTarget.Reset();
				CurrentTask = EMetalizationUnitTask::Idle;
				return;
			}

			const float DistanceSq = FVector::DistSquared(GetActorLocation(), TargetStructure->GetActorLocation());
			if (DistanceSq <= AttackRange * AttackRange)
			{
				if (AttackCooldownRemaining <= 0.f)
				{
					TargetStructure->ApplyDamage(AttackDamage, TeamId);
					AttackCooldownRemaining = AttackCooldown;
				}
				StopMovement();
				return;
			}
			if (PathDirections.Num() == 0)
			{
				IssueMoveToWorldLocation(TargetStructure->GetActorLocation());
			}
		}
	}
}

void AMetalizationGroundUnit::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (CurrentHealth <= 0.f)
	{
		return;
	}

	UpdateMovement(DeltaSeconds);
	UpdateTaskState(DeltaSeconds);
}
