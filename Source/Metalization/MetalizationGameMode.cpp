// Copyright Epic Games, Inc. All Rights Reserved.

#include "MetalizationGameMode.h"

#include "Kismet/GameplayStatics.h"
#include "MetalizationCharacter.h"
#include "MetalizationFactionSpawner.h"
#include "MetalizationGameState.h"
#include "MetalizationHUD.h"
#include "MetalizationPlayerController.h"
#include "PlanetSurfaceSubsystem.h"

AMetalizationGameMode::AMetalizationGameMode()
{
	DefaultPawnClass = AMetalizationCharacter::StaticClass();
	PlayerControllerClass = AMetalizationPlayerController::StaticClass();
	HUDClass = AMetalizationHUD::StaticClass();
	DefaultGameStateClass = AMetalizationGameState::StaticClass();
}

void AMetalizationGameMode::BeginPlay()
{
	Super::BeginPlay();
	SetupPlanetAndSpawners();
}

void AMetalizationGameMode::SetupPlanetAndSpawners()
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	TArray<AActor*> PlanetActors;
	UGameplayStatics::GetAllActorsWithTag(World, PlanetTag, PlanetActors);
	if (PlanetActors.Num() == 0)
	{
		UE_LOG(LogTemp, Warning, TEXT("MetalizationGameMode: No actor tagged '%s' found. Add a Planet actor tagged with '%s' to begin the RTS setup."), *PlanetTag.ToString(), *PlanetTag.ToString());
		return;
	}

	AActor* PlanetActor = PlanetActors[0];
	if (UPlanetSurfaceSubsystem* SurfaceSubsystem = World->GetSubsystem<UPlanetSurfaceSubsystem>())
	{
		SurfaceSubsystem->RegisterPlanet(PlanetActor, PlanetRadius);
	}

	if (!FactionSpawnerClass)
	{
		UE_LOG(LogTemp, Warning, TEXT("MetalizationGameMode: FactionSpawnerClass is not assigned. Set the class in the GameMode to spawn the RTS factions."));
		return;
	}

	FActorSpawnParameters SpawnParams;
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

	AMetalizationFactionSpawner* PlayerSpawner = World->SpawnActor<AMetalizationFactionSpawner>(FactionSpawnerClass, PlanetActor->GetActorLocation(), FRotator::ZeroRotator, SpawnParams);
	if (PlayerSpawner)
	{
		PlayerSpawner->SetPlanetReference(PlanetActor, PlanetRadius);
		PlayerSpawner->SetTeamId(PlayerTeamId);
		PlayerSpawner->SpawnStartSetup();
	}

	AMetalizationFactionSpawner* EnemySpawner = World->SpawnActor<AMetalizationFactionSpawner>(FactionSpawnerClass, PlanetActor->GetActorLocation(), FRotator::ZeroRotator, SpawnParams);
	if (EnemySpawner)
	{
		EnemySpawner->SetPlanetReference(PlanetActor, PlanetRadius);
		EnemySpawner->SetTeamId(EnemyTeamId);
		EnemySpawner->SpawnStartSetup();
	}
}
