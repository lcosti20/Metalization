#include "MetalSpaceBattleGameMode.h"

#include "MetalCapitalShip.h"
#include "MetalPlayerController.h"
#include "MetalStalker.h"
#include "EngineUtils.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerStart.h"

void AMetalSpaceBattleGameMode::BeginPlay()
{
	Super::BeginPlay();
	Tickets = {{EMetalTeam::Heroes, StartingTickets}, {EMetalTeam::Villains, StartingTickets}};
	InteriorReadyHandle = AMetalCapitalShip::OnAnyInteriorReady.AddUObject(this, &AMetalSpaceBattleGameMode::OnInteriorReady);
	SubsystemHandle = AMetalCapitalShip::OnAnySubsystemDestroyed.AddUObject(this, &AMetalSpaceBattleGameMode::OnSubsystemDestroyed);
	ShipDestroyedHandle = AMetalCapitalShip::OnAnyShipDestroyed.AddUObject(this, &AMetalSpaceBattleGameMode::OnShipDestroyed);
	BotDiedHandle = AMetalBotCharacter::OnAnyBotDied.AddUObject(this, &AMetalSpaceBattleGameMode::OnCombatantDied);
}

void AMetalSpaceBattleGameMode::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	AMetalCapitalShip::OnAnyInteriorReady.Remove(InteriorReadyHandle);
	AMetalCapitalShip::OnAnySubsystemDestroyed.Remove(SubsystemHandle);
	AMetalCapitalShip::OnAnyShipDestroyed.Remove(ShipDestroyedHandle);
	AMetalBotCharacter::OnAnyBotDied.Remove(BotDiedHandle);
	Super::EndPlay(EndPlayReason);
}

// --------------------------------------------------------------------------------------------
// Tickets
// --------------------------------------------------------------------------------------------

void AMetalSpaceBattleGameMode::SpendTickets(EMetalTeam Team, int32 Amount)
{
	int32* Count = Tickets.Find(Team);
	if (bBattleOver || Count == nullptr || Amount <= 0)
	{
		return;
	}
	*Count = FMath::Max(0, *Count - Amount);
	if (*Count == 0)
	{
		EndBattle(Team == EMetalTeam::Heroes ? EMetalTeam::Villains : EMetalTeam::Heroes);
	}
}

void AMetalSpaceBattleGameMode::OnCombatantDied(AMetalBotCharacter* Bot, AController* Killer)
{
	if (Bot == nullptr)
	{
		return;
	}
	// A Stalker only costs a ticket if its pilot dies with it: an AI pilot. Players wake up in their body,
	// and empty parked ones are just hardware.
	const AController* Pilot = Bot->GetController();
	const bool bCombatant = Bot->IsA<AMetalStalker>() ? (Pilot && !Pilot->IsPlayerController()) : (Pilot != nullptr);
	if (bCombatant)
	{
		SpendTickets(Bot->GetBaseTeam(), 1);
	}
}

void AMetalSpaceBattleGameMode::OnSubsystemDestroyed(AMetalCapitalShip* Ship, UMetalShipSubsystemComponent* Subsystem)
{
	const bool bLifeSupport = Subsystem->Type == EMetalShipSubsystem::LifeSupport;
	const bool bOurs = Ship->GetTeam() == PlayerTeam;
	static const TMap<EMetalShipSubsystem, FText> Names = {
		{EMetalShipSubsystem::ShieldGenerator, NSLOCTEXT("Metalization", "ShieldGen", "shield generator")},
		{EMetalShipSubsystem::Engines, NSLOCTEXT("Metalization", "Engines", "engines")},
		{EMetalShipSubsystem::Comms, NSLOCTEXT("Metalization", "Comms", "comms array")},
		{EMetalShipSubsystem::LifeSupport, NSLOCTEXT("Metalization", "LifeSupport", "life support")},
	};
	ShowMessage(FText::GetEmpty(), FText::Format(bOurs ? NSLOCTEXT("Metalization", "LostSystem", "Our {0} is down!")
		: NSLOCTEXT("Metalization", "KilledSystem", "Enemy {0} destroyed!"), Names.FindRef(Subsystem->Type)), 3.f);
	if (bLifeSupport)
	{
		NextBleedTime.Add(Ship->GetTeam(), GetWorld()->GetTimeSeconds() + LifeSupportBleedInterval);
	}
	SpendTickets(Ship->GetTeam(), bLifeSupport ? LifeSupportTicketPenalty : SubsystemTicketPenalty);
}

void AMetalSpaceBattleGameMode::OnShipDestroyed(AMetalCapitalShip* Ship)
{
	SpendTickets(Ship->GetTeam(), GetTickets(Ship->GetTeam()));
}

void AMetalSpaceBattleGameMode::EndBattle(EMetalTeam InWinner)
{
	if (bBattleOver)
	{
		return;
	}
	bBattleOver = true;
	Winner = InWinner;
	UE_LOG(LogMetal, Display, TEXT("Battle over: team %d wins (tickets %d / %d)"), static_cast<int32>(Winner), GetTickets(EMetalTeam::Heroes), GetTickets(EMetalTeam::Villains));
	CompleteMission(Winner == PlayerTeam ? NSLOCTEXT("Metalization", "Victory", "VICTORY") : NSLOCTEXT("Metalization", "Defeat", "DEFEAT"));
}

void AMetalSpaceBattleGameMode::PlayerDied(AMetalPlayerController* Controller, AMetalBotCharacter* Body)
{
	if (bBattleOver)
	{
		return;
	}
	Super::PlayerDied(Controller, Body);
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
	const float Now = GetWorld()->GetTimeSeconds();
	for (TPair<EMetalTeam, float>& Bleed : NextBleedTime)
	{
		if (Now >= Bleed.Value)
		{
			Bleed.Value = Now + LifeSupportBleedInterval;
			SpendTickets(Bleed.Key, 1);
		}
	}
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
		// Fisher-Yates shuffle, then take the first start with no pawn within 1.5 m.
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
