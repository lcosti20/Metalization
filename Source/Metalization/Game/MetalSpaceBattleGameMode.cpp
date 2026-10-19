#include "MetalSpaceBattleGameMode.h"

#include "MetalCapitalShip.h"
#include "EngineUtils.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerStart.h"

void AMetalSpaceBattleGameMode::BeginPlay()
{
	Super::BeginPlay();
	InteriorReadyHandle = AMetalCapitalShip::OnAnyInteriorReady.AddUObject(this, &AMetalSpaceBattleGameMode::OnInteriorReady);
}

void AMetalSpaceBattleGameMode::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	AMetalCapitalShip::OnAnyInteriorReady.Remove(InteriorReadyHandle);
	Super::EndPlay(EndPlayReason);
}

AMetalCapitalShip* AMetalSpaceBattleGameMode::GetShip(EMetalTeam Team) const
{
	for (TActorIterator<AMetalCapitalShip> It(GetWorld()); It; ++It)
	{
		if (It->GetTeam() == Team)
		{
			return *It;
		}
	}
	return nullptr;
}

bool AMetalSpaceBattleGameMode::AreInteriorsReady() const
{
	for (TActorIterator<AMetalCapitalShip> It(GetWorld()); It; ++It)
	{
		const bool bHasInterior = It->ShipData && !It->ShipData->Interior.IsNull();
		if (bHasInterior && !It->IsInteriorReady())
		{
			return false;
		}
	}
	return true;
}

void AMetalSpaceBattleGameMode::HandleStartingNewPlayer_Implementation(APlayerController* NewPlayer)
{
	// Players log in before the ships have even begun play: wait for the interiors to stream in.
	if (!AreInteriorsReady())
	{
		PendingPlayers.AddUnique(NewPlayer);
		return;
	}
	Super::HandleStartingNewPlayer_Implementation(NewPlayer);
}

void AMetalSpaceBattleGameMode::OnInteriorReady(AMetalCapitalShip* Ship)
{
	if (AreInteriorsReady())
	{
		StartPendingPlayers();
	}
}

void AMetalSpaceBattleGameMode::StartPendingPlayers()
{
	TArray<TWeakObjectPtr<APlayerController>> Players = MoveTemp(PendingPlayers);
	for (const TWeakObjectPtr<APlayerController>& Player : Players)
	{
		if (Player.IsValid())
		{
			Super::HandleStartingNewPlayer_Implementation(Player.Get());
		}
	}
}

void AMetalSpaceBattleGameMode::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (PendingPlayers.Num() > 0 && GetWorld()->GetTimeSeconds() > InteriorLoadTimeout)
	{
		UE_LOG(LogMetal, Error, TEXT("Ship interiors still not loaded after %.0f s; spawning players anyway."), InteriorLoadTimeout);
		StartPendingPlayers();
	}
}

AActor* AMetalSpaceBattleGameMode::ChoosePlayerStart_Implementation(AController* Player)
{
	if (const AMetalCapitalShip* Ship = GetShip(PlayerTeam))
	{
		// A random start aboard our ship, preferring one nobody is standing on.
		TArray<APlayerStart*> Starts = Ship->GetSpawnPoints();
		for (int32 Index = 0; Index < Starts.Num(); ++Index)
		{
			Starts.Swap(Index, FMath::RandRange(Index, Starts.Num() - 1));
		}
		APlayerStart* Fallback = Starts.Num() > 0 ? Starts[0] : nullptr;
		for (APlayerStart* Start : Starts)
		{
			bool bOccupied = false;
			for (TActorIterator<APawn> It(GetWorld()); It && !bOccupied; ++It)
			{
				bOccupied = FVector::DistSquared(It->GetActorLocation(), Start->GetActorLocation()) < FMath::Square(150.f);
			}
			if (!bOccupied)
			{
				return Start;
			}
		}
		if (Fallback)
		{
			return Fallback;
		}
	}
	return Super::ChoosePlayerStart_Implementation(Player);
}
