#include "MetalCheckpoint.h"

#include "MetalControlConsole.h"
#include "MetalGameMode.h"
#include "MetalMission.h"
#include "MetalTriggerZone.h"
#include "EngineUtils.h"
#include "Engine/World.h"
#include "GameFramework/Controller.h"
#include "GameFramework/Info.h"
#include "GameFramework/PlayerController.h"

namespace
{
	/**
	 * @param World World the actor lives in.
	 * @param Actor Actor to test.
	 * @return True for actors loaded with the map that a snapshot can track (not game framework actors).
	 */
	bool IsTrackedPlacedActor(const UWorld* World, const AActor* Actor)
	{
		return IsValid(Actor) && Actor->IsNetStartupActor() && Actor->GetLevel() == World->PersistentLevel && !Actor->IsA<AInfo>() && !Actor->IsA<AController>();
	}
}

FString UMetalCheckpointSubsystem::KeyOf(const AActor* Actor)
{
	return Actor ? Actor->GetPathName(Actor->GetWorld()) : FString();
}

void UMetalCheckpointSubsystem::Capture(const AMetalGameMode* GameMode, const AMetalBotCharacter* PlayerBody, const FTransform& PlayerTransform)
{
	UWorld* World = GameMode ? GameMode->GetWorld() : nullptr;
	if (World == nullptr)
	{
		return;
	}
	Snapshot = FMetalLevelSnapshot();
	Snapshot.MapName = World->GetMapName();
	Snapshot.PlayerTransform = PlayerTransform;
	if (PlayerBody)
	{
		Snapshot.Weapons = PlayerBody->GetWeapons();
		Snapshot.WeaponIndex = FMath::Max(0, PlayerBody->GetCurrentWeaponIndex());
		Snapshot.Grenades = PlayerBody->Grenades;
	}
	Snapshot.ObjectiveIndex = GameMode->GetMission() ? GameMode->GetMission()->GetCurrentObjectiveIndex() : INDEX_NONE;
	Snapshot.Kills = GameMode->GetKills();

	for (TActorIterator<AActor> It(World); It; ++It)
	{
		AActor* Actor = *It;
		if (!IsTrackedPlacedActor(World, Actor) || Actor->IsActorBeingDestroyed())
		{
			continue;
		}
		const FString Key = KeyOf(Actor);
		if (const AMetalBotCharacter* Bot = Cast<AMetalBotCharacter>(Actor))
		{
			// A wreck still lying there counts as gone; the player's own body is never a placed actor.
			if (!Bot->IsAlive() || Bot->IsHidden())
			{
				continue;
			}
			FMetalBotSnapshot& Entry = Snapshot.Bots.Add(Key);
			Entry.Transform = Bot->GetActorTransform();
			Entry.Health = Bot->GetHealth();
			Entry.bDormant = Bot->IsDormant();
		}
		else if (const AMetalControlConsole* Console = Cast<AMetalControlConsole>(Actor); Console && Console->IsActivated())
		{
			Snapshot.ActivatedConsoles.Add(Key);
		}
		else if (const AMetalTriggerZone* Zone = Cast<AMetalTriggerZone>(Actor))
		{
			if (Zone->IsCheckpointUsed())
			{
				Snapshot.TriggeredZones.Add(Key);
			}
			if (Zone->HasPlayedDialogue())
			{
				Snapshot.PlayedDialogueZones.Add(Key);
			}
		}
		Snapshot.LiveActors.Add(Key);
	}
	bHasSnapshot = true;
	UE_LOG(LogMetal, Log, TEXT("[Checkpoint] snapshot of %s: objective %d, %d robots standing, %d placed actors"),
		*Snapshot.MapName, Snapshot.ObjectiveIndex, Snapshot.Bots.Num(), Snapshot.LiveActors.Num());
}

void UMetalCheckpointSubsystem::PrepareRetry(int32 Deaths, float Elapsed)
{
	const UWorld* World = GetGameInstance() ? GetGameInstance()->GetWorld() : nullptr;
	bRetryPending = true;
	RetryMap = World ? World->GetMapName() : FString();
	RetryDeaths = Deaths;
	RetryElapsed = Elapsed;
}

const FMetalLevelSnapshot* UMetalCheckpointSubsystem::TakeRetry(const UWorld* World, int32& OutDeaths, float& OutElapsed)
{
	const bool bRetry = bRetryPending && World && World->GetMapName() == RetryMap;
	bRetryPending = false;
	if (!bRetry)
	{
		// A fresh visit (restart, level select, a different map): the old checkpoint no longer applies.
		bHasSnapshot = false;
		Snapshot = FMetalLevelSnapshot();
		return nullptr;
	}
	OutDeaths = RetryDeaths;
	OutElapsed = RetryElapsed;
	// The snapshot stays: dying again before the next checkpoint goes back to the same one.
	return bHasSnapshot && Snapshot.MapName == World->GetMapName() ? &Snapshot : nullptr;
}

void UMetalCheckpointSubsystem::ApplyBeforeBeginPlay(UWorld* World, const FMetalLevelSnapshot& Snapshot)
{
	TArray<AActor*> Removed;
	int32 Moved = 0;
	for (TActorIterator<AActor> It(World); It; ++It)
	{
		AActor* Actor = *It;
		if (!IsTrackedPlacedActor(World, Actor))
		{
			continue;
		}
		const FString Key = KeyOf(Actor);
		if (!Snapshot.LiveActors.Contains(Key))
		{
			Removed.Add(Actor);
			continue;
		}
		if (const FMetalBotSnapshot* Bot = Snapshot.Bots.Find(Key))
		{
			Actor->SetActorLocationAndRotation(Bot->Transform.GetLocation(), Bot->Transform.GetRotation(), false, nullptr, ETeleportType::TeleportPhysics);
			Moved++;
		}
		else if (AMetalTriggerZone* Zone = Cast<AMetalTriggerZone>(Actor))
		{
			Zone->RestoreState(Snapshot.TriggeredZones.Contains(Key), Snapshot.PlayedDialogueZones.Contains(Key));
		}
	}
	for (AActor* Actor : Removed)
	{
		// A placed robot's AI controller was spawned with it; take that away too.
		if (const APawn* Pawn = Cast<APawn>(Actor); Pawn && Pawn->GetController() && !Pawn->GetController()->IsPlayerController())
		{
			Pawn->GetController()->Destroy();
		}
		Actor->Destroy();
	}
	UE_LOG(LogMetal, Log, TEXT("[Checkpoint] restoring %s: removed %d actors gone at the checkpoint, moved %d robots back"),
		*Snapshot.MapName, Removed.Num(), Moved);
}

void UMetalCheckpointSubsystem::ApplyAfterBeginPlay(UWorld* World, const FMetalLevelSnapshot& Snapshot)
{
	for (TActorIterator<AActor> It(World); It; ++It)
	{
		AActor* Actor = *It;
		if (!IsTrackedPlacedActor(World, Actor))
		{
			continue;
		}
		const FString Key = KeyOf(Actor);
		if (AMetalBotCharacter* Bot = Cast<AMetalBotCharacter>(Actor))
		{
			if (const FMetalBotSnapshot* Entry = Snapshot.Bots.Find(Key))
			{
				Bot->RestoreHealth(Entry->Health);
				if (Bot->IsDormant() != Entry->bDormant)
				{
					Bot->SetDormant(Entry->bDormant);
				}
			}
		}
		else if (AMetalControlConsole* Console = Cast<AMetalControlConsole>(Actor); Console && Snapshot.ActivatedConsoles.Contains(Key))
		{
			Console->RestoreActivated();
		}
	}

	const APlayerController* PC = World->GetFirstPlayerController();
	if (AMetalBotCharacter* Player = PC ? Cast<AMetalBotCharacter>(PC->GetPawn()) : nullptr)
	{
		if (Snapshot.Weapons.Num() > 0)
		{
			Player->SetWeapons(Snapshot.Weapons, Snapshot.WeaponIndex);
		}
		Player->Grenades = Snapshot.Grenades;
	}
	UE_LOG(LogMetal, Log, TEXT("[Checkpoint] restored %s at objective %d (%d consoles active, %d weapons)"),
		*Snapshot.MapName, Snapshot.ObjectiveIndex, Snapshot.ActivatedConsoles.Num(), Snapshot.Weapons.Num());
}
