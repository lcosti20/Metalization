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
	if (!UnitClass) return nullptr;
	UWorld* World = GetWorld();
	if (!World) return nullptr;

	const FVector Direction = SurfaceDirectionForWorld(WorldLocation);
	const FVector SpawnLocation = PlanetActor.IsValid() ? PlanetActor.Get()->GetActorLocation() + Direction * (PlanetRadius + 60.f) : WorldLocation;

	FActorSpawnParameters SpawnParams;
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	AMetalizationGroundUnit* Unit = World->SpawnActor<AMetalizationGroundUnit>(UnitClass, SpawnLocation, FRotator::ZeroRotator, SpawnParams);
	if (!Unit)
	{
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
	for (int32 Index = 0; Index < 3; ++Index)
	{
		const float Angle = Index * (2.f * PI / 3.f);
		const FVector Offset = FVector(FMath::Cos(Angle), FMath::Sin(Angle), 0.1f) * 200.f;
		const FVector Direction = (PlanetCenter + Offset - PlanetCenter).GetSafeNormal();
		SpawnVentAtLocation(PlanetCenter + Direction * (PlanetRadius + 80.f));
		SpawnUnitAtLocation(PlanetCenter + Direction * (PlanetRadius + 70.f));
	}

	AMetalizationColony* Colony = SpawnColonyAtLocation(PlanetCenter + FVector::UpVector * (PlanetRadius + 150.f));
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
