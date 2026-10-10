#include "MetalizationAIManager.h"

#include "MetalizationGroundUnit.h"
#include "MetalizationStructure.h"
#include "Kismet/GameplayStatics.h"

AMetalizationAIManager::AMetalizationAIManager()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.TickInterval = 0.1f;
}

void AMetalizationAIManager::BeginPlay()
{
	Super::BeginPlay();
	TimeSinceLastOrderCheck = 0.f;
}

AMetalizationGroundUnit* AMetalizationAIManager::GetNextUnitToOrder() const
{
	TArray<AActor*> FoundActors;
	UGameplayStatics::GetAllActorsOfClass(GetWorld(), AMetalizationGroundUnit::StaticClass(), FoundActors);

	for (AActor* Actor : FoundActors)
	{
		if (AMetalizationGroundUnit* Unit = Cast<AMetalizationGroundUnit>(Actor))
		{
			if (Unit->GetTeamId() == TeamId && !Unit->HasMovementPath())
			{
				return Unit;
			}
		}
	}
	return nullptr;
}

AMetalizationEnergyVent* AMetalizationAIManager::FindNearestOwnedVent(const AMetalizationGroundUnit* Unit) const
{
	if (!Unit)
	{
		return nullptr;
	}

	TArray<AActor*> FoundStructures;
	UGameplayStatics::GetAllActorsOfClass(GetWorld(), AMetalizationEnergyVent::StaticClass(), FoundStructures);

	AMetalizationEnergyVent* BestVent = nullptr;
	float BestDistanceSq = TNumericLimits<float>::Max();

	for (AActor* Actor : FoundStructures)
	{
		if (AMetalizationEnergyVent* Vent = Cast<AMetalizationEnergyVent>(Actor))
		{
			if (Vent->GetTeamId() != TeamId || Vent->GetStoredEnergy() <= 0.f)
			{
				continue;
			}

			const float DistSq = FVector::DistSquared(Unit->GetActorLocation(), Vent->GetActorLocation());
			if (DistSq < BestDistanceSq)
			{
				BestDistanceSq = DistSq;
				BestVent = Vent;
			}
		}
	}
	return BestVent;
}

AMetalizationEnergyVent* AMetalizationAIManager::FindNearestCapturableVent(const AMetalizationGroundUnit* Unit) const
{
	if (!Unit)
	{
		return nullptr;
	}

	TArray<AActor*> FoundStructures;
	UGameplayStatics::GetAllActorsOfClass(GetWorld(), AMetalizationEnergyVent::StaticClass(), FoundStructures);

	AMetalizationEnergyVent* BestVent = nullptr;
	float BestDistanceSq = TNumericLimits<float>::Max();

	for (AActor* Actor : FoundStructures)
	{
		if (AMetalizationEnergyVent* Vent = Cast<AMetalizationEnergyVent>(Actor))
		{
			if (Vent->GetTeamId() == TeamId)
			{
				continue;
			}

			const float DistSq = FVector::DistSquared(Unit->GetActorLocation(), Vent->GetActorLocation());
			if (DistSq < BestDistanceSq)
			{
				BestDistanceSq = DistSq;
				BestVent = Vent;
			}
		}
	}
	return BestVent;
}

AMetalizationColony* AMetalizationAIManager::FindNearestOwnedColony(const AMetalizationGroundUnit* Unit) const
{
	if (!Unit)
	{
		return nullptr;
	}

	TArray<AActor*> FoundStructures;
	UGameplayStatics::GetAllActorsOfClass(GetWorld(), AMetalizationColony::StaticClass(), FoundStructures);

	AMetalizationColony* BestColony = nullptr;
	float BestDistanceSq = TNumericLimits<float>::Max();

	for (AActor* Actor : FoundStructures)
	{
		if (AMetalizationColony* Colony = Cast<AMetalizationColony>(Actor))
		{
			if (Colony->GetTeamId() != TeamId)
			{
				continue;
			}

			const float DistSq = FVector::DistSquared(Unit->GetActorLocation(), Colony->GetActorLocation());
			if (DistSq < BestDistanceSq)
			{
				BestDistanceSq = DistSq;
				BestColony = Colony;
			}
		}
	}
	return BestColony;
}

AMetalizationColony* AMetalizationAIManager::FindNearestEnemyColony(const AMetalizationGroundUnit* Unit) const
{
	if (!Unit)
	{
		return nullptr;
	}

	TArray<AActor*> FoundStructures;
	UGameplayStatics::GetAllActorsOfClass(GetWorld(), AMetalizationColony::StaticClass(), FoundStructures);

	AMetalizationColony* BestColony = nullptr;
	float BestDistanceSq = TNumericLimits<float>::Max();

	for (AActor* Actor : FoundStructures)
	{
		if (AMetalizationColony* Colony = Cast<AMetalizationColony>(Actor))
		{
			if (Colony->GetTeamId() == TeamId)
			{
				continue;
			}

			const float DistSq = FVector::DistSquared(Unit->GetActorLocation(), Colony->GetActorLocation());
			if (DistSq < BestDistanceSq)
			{
				BestDistanceSq = DistSq;
				BestColony = Colony;
			}
		}
	}
	return BestColony;
}

AMetalizationStructure* AMetalizationAIManager::FindNearestEnemyStructure(const AMetalizationGroundUnit* Unit) const
{
	if (!Unit)
	{
		return nullptr;
	}

	TArray<AActor*> FoundStructures;
	UGameplayStatics::GetAllActorsOfClass(GetWorld(), AMetalizationStructure::StaticClass(), FoundStructures);

	AMetalizationStructure* BestTarget = nullptr;
	float BestDistanceSq = TNumericLimits<float>::Max();

	for (AActor* Actor : FoundStructures)
	{
		if (AMetalizationStructure* Structure = Cast<AMetalizationStructure>(Actor))
		{
			if (Structure->GetTeamId() == TeamId)
			{
				continue;
			}

			const float DistSq = FVector::DistSquared(Unit->GetActorLocation(), Structure->GetActorLocation());
			if (DistSq < BestDistanceSq)
			{
				BestDistanceSq = DistSq;
				BestTarget = Structure;
			}
		}
	}
	return BestTarget;
}

void AMetalizationAIManager::UpdateAIOrders()
{
	TArray<AActor*> FoundActors;
	UGameplayStatics::GetAllActorsOfClass(GetWorld(), AMetalizationGroundUnit::StaticClass(), FoundActors);

	for (AActor* Actor : FoundActors)
	{
		if (AMetalizationGroundUnit* Unit = Cast<AMetalizationGroundUnit>(Actor))
		{
			if (Unit->GetTeamId() != TeamId || Unit->HasMovementPath())
			{
				continue;
			}

			AMetalizationEnergyVent* OwnedVent = FindNearestOwnedVent(Unit);
			AMetalizationColony* OwnedColony = FindNearestOwnedColony(Unit);
			AMetalizationEnergyVent* CapturableVent = FindNearestCapturableVent(Unit);
			AMetalizationColony* EnemyColony = FindNearestEnemyColony(Unit);
			AMetalizationStructure* EnemyTarget = FindNearestEnemyStructure(Unit);

			if (OwnedColony && OwnedVent)
			{
				Unit->IssueHarvestOrder(OwnedVent, OwnedColony);
				continue;
			}

			if (CapturableVent)
			{
				Unit->IssueCaptureOrder(CapturableVent);
				continue;
			}

			if (EnemyColony)
			{
				Unit->IssueCaptureOrder(EnemyColony);
				continue;
			}

			if (EnemyTarget)
			{
				Unit->IssueCaptureOrder(EnemyTarget);
				continue;
			}

			if (OwnedVent)
			{
				Unit->IssueMoveToWorldLocation(OwnedVent->GetActorLocation());
				continue;
			}

			if (OwnedColony)
			{
				Unit->IssueMoveToWorldLocation(OwnedColony->GetActorLocation());
			}
		}
	}
}

void AMetalizationAIManager::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	TimeSinceLastOrderCheck += DeltaSeconds;
	if (TimeSinceLastOrderCheck >= OrderCheckInterval)
	{
		TimeSinceLastOrderCheck = 0.f;
		UpdateAIOrders();
	}
}
