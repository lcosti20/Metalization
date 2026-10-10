#include "MetalizationFactionSpawner.h"

#include "MetalizationGroundUnit.h"
#include "MetalizationStructure.h"
#include "MetalizationAIManager.h"
#include "PlanetSurfaceSubsystem.h"

AMetalizationFactionSpawner::AMetalizationFactionSpawner()
{
	PrimaryActorTick.bCanEverTick = false;
}

void AMetalizationFactionSpawner::BeginPlay()
{
	Super::BeginPlay();
}

FVector AMetalizationFactionSpawner::SurfaceDirectionForWorld(const FVector& WorldLocation) const
{
	if (!PlanetActor.IsValid())
	{
		return FVector::UpVector;
	}

	UWorld* World = GetWorld();
	if (!World)
	{
		return FVector::UpVector;
	}

	UPlanetSurfaceSubsystem* SurfaceSubsystem = World->GetSubsystem<UPlanetSurfaceSubsystem>();
	if (!SurfaceSubsystem)
	{
		return PlanetActor.IsValid() ? (WorldLocation - PlanetActor.Get()->GetActorLocation()).GetSafeNormal() : FVector::UpVector;
	}

	return SurfaceSubsystem->DirectionFromWorld(WorldLocation);
}

void AMetalizationFactionSpawner::SetPlanetReference(AActor* InPlanet, float InRadius)
{
	PlanetActor = InPlanet;
	PlanetRadius = InRadius;
}

AMetalizationGroundUnit* AMetalizationFactionSpawner::SpawnUnitAtLocation(const FVector& WorldLocation)
{
	TSubclassOf<AMetalizationGroundUnit> SpawnClass = UnitClass;
	if (!SpawnClass)
	{
		SpawnClass = LoadClass<AMetalizationGroundUnit>(nullptr, TEXT("/Game/BP_MetalizationGroundUnit.BP_MetalizationGroundUnit_C"));
	}
	if (!SpawnClass)
	{
		UE_LOG(LogTemp, Error, TEXT("MetalizationFactionSpawner: UnitClass is not assigned and BP_MetalizationGroundUnit could not be loaded."));
		return nullptr;
	}

	UWorld* World = GetWorld();
	if (!World)
	{
		UE_LOG(LogTemp, Error, TEXT("MetalizationFactionSpawner: Cannot spawn a ground unit because the world is unavailable."));
		return nullptr;
	}

	const FVector Direction = SurfaceDirectionForWorld(WorldLocation);
	const FVector SpawnLocation = PlanetActor.IsValid() ? PlanetActor.Get()->GetActorLocation() + Direction * (PlanetRadius + 60.f) : WorldLocation;

	FActorSpawnParameters SpawnParams;
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	AMetalizationGroundUnit* Unit = World->SpawnActor<AMetalizationGroundUnit>(SpawnClass, SpawnLocation, FRotator::ZeroRotator, SpawnParams);
	if (!Unit)
	{
		UE_LOG(LogTemp, Error, TEXT("MetalizationFactionSpawner: Failed to spawn ground unit for team %d."), TeamId);
		return nullptr;
	}

	Unit->SetTeamId(TeamId);
	Unit->SetActorLocation(SpawnLocation, false);
	return Unit;
}

AMetalizationEnergyVent* AMetalizationFactionSpawner::SpawnVentAtLocation(const FVector& WorldLocation)
{
	if (!VentClass) return nullptr;
	UWorld* World = GetWorld();
	if (!World) return nullptr;

	const FVector Direction = SurfaceDirectionForWorld(WorldLocation);
	const FVector SpawnLocation = PlanetActor.IsValid() ? PlanetActor.Get()->GetActorLocation() + Direction * (PlanetRadius + 40.f) : WorldLocation;

	FActorSpawnParameters SpawnParams;
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	AMetalizationEnergyVent* Vent = World->SpawnActor<AMetalizationEnergyVent>(VentClass, SpawnLocation, FRotator::ZeroRotator, SpawnParams);
	if (!Vent)
	{
		return nullptr;
	}

	Vent->SetTeamId(TeamId);
	Vent->SetSurfaceDetails(PlanetActor.Get(), Direction, PlanetRadius);
	return Vent;
}

AMetalizationColony* AMetalizationFactionSpawner::SpawnColonyAtLocation(const FVector& WorldLocation)
{
	if (!ColonyClass) return nullptr;
	UWorld* World = GetWorld();
	if (!World) return nullptr;

	const FVector Direction = SurfaceDirectionForWorld(WorldLocation);
	const FVector SpawnLocation = PlanetActor.IsValid() ? PlanetActor.Get()->GetActorLocation() + Direction * (PlanetRadius + 50.f) : WorldLocation;

	FActorSpawnParameters SpawnParams;
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	AMetalizationColony* Colony = World->SpawnActor<AMetalizationColony>(ColonyClass, SpawnLocation, FRotator::ZeroRotator, SpawnParams);
	if (!Colony)
	{
		return nullptr;
	}

	Colony->SetTeamId(TeamId);
	Colony->SetSurfaceDetails(PlanetActor.Get(), Direction, PlanetRadius);
	return Colony;
}

void AMetalizationFactionSpawner::SpawnStartSetup()
{
	if (!PlanetActor.IsValid())
	{
		return;
	}

	const FVector PlanetCenter = PlanetActor->GetActorLocation();
	int32 SpawnedUnitCount = 0;
	for (int32 Index = 0; Index < 3; ++Index)
	{
		const float Angle = Index * (2.f * PI / 3.f);
		const float TeamSide = TeamId % 2 == 0 ? 1.f : -1.f;
		const FVector Offset = FVector(FMath::Cos(Angle), FMath::Sin(Angle), 0.1f) * (200.f * TeamSide);
		const FVector Direction = (PlanetCenter + Offset - PlanetCenter).GetSafeNormal();
		SpawnVentAtLocation(PlanetCenter + Direction * (PlanetRadius + 80.f));
		if (SpawnUnitAtLocation(PlanetCenter + Direction * (PlanetRadius + 70.f)))
		{
			++SpawnedUnitCount;
		}
	}
	UE_LOG(LogTemp, Display, TEXT("MetalizationFactionSpawner: Spawned %d of 3 starting units for team %d."), SpawnedUnitCount, TeamId);

	const FVector ColonyDirection = TeamId % 2 == 0 ? FVector::UpVector : FVector::DownVector;
	AMetalizationColony* Colony = SpawnColonyAtLocation(PlanetCenter + ColonyDirection * (PlanetRadius + 150.f));
	if (Colony)
	{
		Colony->SetTeamId(TeamId);
	}

	if (AIManagerClass)
	{
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		AMetalizationAIManager* AIManager = GetWorld()->SpawnActor<AMetalizationAIManager>(AIManagerClass, PlanetCenter, FRotator::ZeroRotator, Params);
		if (AIManager)
		{
			AIManager->SetTeamId(TeamId == 0 ? 1 : 0);
		}
	}
}
