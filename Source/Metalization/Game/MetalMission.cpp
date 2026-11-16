#include "MetalMission.h"

#include "MetalGameMode.h"
#include "MetalBotCharacter.h"
#include "MetalControlConsole.h"
#include "MetalPlayerController.h"
#include "MetalTriggerZone.h"
#include "MetalAudio.h"
#include "MetalWeaponData.h"
#include "Components/BillboardComponent.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"

AMetalMission::AMetalMission()
{
	PrimaryActorTick.bCanEverTick = true;
	Sprite = CreateDefaultSubobject<UBillboardComponent>(TEXT("Sprite"));
	RootComponent = Sprite;
	SetHidden(true);
}

void AMetalMission::StartMission(AMetalGameMode* InGameMode)
{
	GameMode = InGameMode;
	if (InGameMode && !MissionName.IsEmpty())
	{
		InGameMode->ShowBanner(MissionName, 4.f);
	}
	int32 FirstObjective = 0;
#if !UE_BUILD_SHIPPING
	// Dev shortcut for testing later parts of the mission: -MetalObjective=3
	FParse::Value(FCommandLine::Get(), TEXT("MetalObjective="), FirstObjective);
#endif
	ActivateObjective(FirstObjective);
}

void AMetalMission::RestoreObjective(AMetalGameMode* InGameMode, int32 Index, const TArray<int32>& Completed)
{
	GameMode = InGameMode;
	bLoadoutApplied = true;
	QuietUntil = GetWorld()->GetTimeSeconds() + 1.f;
	CurrentObjective = Index;
	Done.Init(false, Objectives.Num());
	Announced.Init(false, Objectives.Num());
	for (const int32 I : Completed)
	{
		if (Done.IsValidIndex(I))
		{
			Done[I] = true;
		}
	}
	// What was already unlocked at the checkpoint has been heard; what unlocks later still gets its lines.
	if (Objectives.IsValidIndex(Index))
	{
		for (int32 I = Index; I <= GroupEnd(Index); ++I)
		{
			Announced[I] = Done[I] || RequirementsMet(Objectives[I]);
		}
	}
	UE_LOG(LogMetal, Log, TEXT("Objective %d (checkpoint, %d of its group done): %s"), Index, Completed.Num(), *GetCurrentObjectiveText().ToString());
}

int32 AMetalMission::GroupEnd(int32 First) const
{
	int32 Last = First;
	while (Objectives.IsValidIndex(Last + 1) && Objectives[Last].bWithNext)
	{
		++Last;
	}
	return Last;
}

bool AMetalMission::RequirementsMet(const FMetalObjective& Objective) const
{
	for (const FName& Required : Objective.Requires)
	{
		for (int32 I = 0; I < Objectives.Num(); ++I)
		{
			// Objectives before the current group are done by definition.
			if (Objectives[I].Id == Required && I >= CurrentObjective && !(Done.IsValidIndex(I) && Done[I]))
			{
				return false;
			}
		}
	}
	return true;
}

bool AMetalMission::IsActive(int32 Index) const
{
	return Objectives.IsValidIndex(CurrentObjective) && Index >= CurrentObjective && Index <= GroupEnd(CurrentObjective)
		&& !Done[Index] && RequirementsMet(Objectives[Index]);
}

FText AMetalMission::GetCurrentObjectiveText() const
{
	if (Objectives.IsValidIndex(CurrentObjective))
	{
		for (int32 I = CurrentObjective; I <= GroupEnd(CurrentObjective); ++I)
		{
			if (IsActive(I))
			{
				return Objectives[I].Description;
			}
		}
	}
	return FText::GetEmpty();
}

TArray<FMetalObjectiveStatus> AMetalMission::GetObjectiveList() const
{
	TArray<FMetalObjectiveStatus> List;
	if (Objectives.IsValidIndex(CurrentObjective))
	{
		for (int32 I = CurrentObjective; I <= GroupEnd(CurrentObjective); ++I)
		{
			FMetalObjectiveStatus& Status = List.AddDefaulted_GetRef();
			Status.Index = I;
			Status.State = Done[I] ? EMetalObjectiveState::Done
				: RequirementsMet(Objectives[I]) ? EMetalObjectiveState::Active : EMetalObjectiveState::Locked;
		}
	}
	return List;
}

TArray<int32> AMetalMission::GetCompletedInGroup() const
{
	TArray<int32> Completed;
	if (Objectives.IsValidIndex(CurrentObjective))
	{
		for (int32 I = CurrentObjective; I <= GroupEnd(CurrentObjective); ++I)
		{
			if (Done[I])
			{
				Completed.Add(I);
			}
		}
	}
	return Completed;
}

TArray<FText> AMetalMission::GetCurrentHints() const
{
	TArray<FText> Hints;
	if (Objectives.IsValidIndex(CurrentObjective))
	{
		for (int32 I = CurrentObjective; I <= GroupEnd(CurrentObjective); ++I)
		{
			if (IsActive(I) && !Objectives[I].Hint.IsEmpty())
			{
				Hints.Add(Objectives[I].Hint);
			}
		}
	}
	return Hints;
}

void AMetalMission::GetObjectiveMarkers(TArray<FVector>& OutLocations) const
{
	if (!Objectives.IsValidIndex(CurrentObjective))
	{
		return;
	}
	const APlayerController* PC = GetWorld()->GetFirstPlayerController();
	const APawn* Viewer = PC ? PC->GetPawn() : nullptr;
	for (int32 I = CurrentObjective; I <= GroupEnd(CurrentObjective); ++I)
	{
		const FMetalObjective& Objective = Objectives[I];
		if (!IsActive(I) || !Objective.bShowMarker)
		{
			continue;
		}
		switch (Objective.Type)
		{
		case EMetalObjectiveType::ReachZone:
		case EMetalObjectiveType::ActivateConsole:
			if (Objective.TargetActor)
			{
				OutLocations.Add(Objective.TargetActor->GetActorLocation());
			}
			break;

		case EMetalObjectiveType::EliminateTagged:
		case EMetalObjectiveType::HijackTagged:
		{
			// The nearest robot still to deal with.
			if (Viewer == nullptr || Objective.TargetTag.IsNone())
			{
				break;
			}
			float Best = TNumericLimits<float>::Max();
			FVector Location;
			for (TActorIterator<AMetalBotCharacter> It(GetWorld()); It; ++It)
			{
				if (It->ActorHasTag(Objective.TargetTag) && It->IsAlive() && !It->IsHijacked())
				{
					const float Distance = FVector::DistSquared(It->GetActorLocation(), Viewer->GetActorLocation());
					if (Distance < Best)
					{
						Best = Distance;
						Location = It->GetActorLocation() + FVector(0.f, 0.f, It->GetSimpleCollisionHalfHeight() + 40.f);
					}
				}
			}
			if (Best < TNumericLimits<float>::Max())
			{
				OutLocations.Add(Location);
			}
			break;
		}
		}
	}
}

void AMetalMission::PlayDialogue(const TArray<FMetalDialogueLine>& Lines) const
{
	if (UMetalDialogueSubsystem* Dialogue = GetWorld()->GetSubsystem<UMetalDialogueSubsystem>())
	{
		Dialogue->PlayDialogue(Lines);
	}
}

void AMetalMission::AnnounceUnlocked()
{
	if (!Objectives.IsValidIndex(CurrentObjective))
	{
		return;
	}
	for (int32 I = CurrentObjective; I <= GroupEnd(CurrentObjective); ++I)
	{
		if (!Announced[I] && !Done[I] && RequirementsMet(Objectives[I]))
		{
			Announced[I] = true;
			PlayDialogue(Objectives[I].StartDialogue);
			UE_LOG(LogMetal, Log, TEXT("Objective %d: %s"), I, *Objectives[I].Description.ToString());
		}
	}
}

void AMetalMission::ActivateObjective(int32 Index)
{
	CurrentObjective = Index;
	if (Done.Num() != Objectives.Num())
	{
		Done.Init(false, Objectives.Num());
		Announced.Init(false, Objectives.Num());
	}
	if (Objectives.IsValidIndex(Index))
	{
		AnnounceUnlocked();
	}
	else if (AMetalGameMode* GM = GameMode.Get())
	{
		PlayDialogue(CompletionDialogue);
		GM->CompleteMission();
	}
}

bool AMetalMission::IsObjectiveComplete(const FMetalObjective& Objective) const
{
	// "PlayerBody" here is whatever the player currently controls, so hijacked robots count for zones.
	const APlayerController* PC = GetWorld()->GetFirstPlayerController();
	const AMetalBotCharacter* PlayerBody = PC ? Cast<AMetalBotCharacter>(PC->GetPawn()) : nullptr;

	switch (Objective.Type)
	{
	case EMetalObjectiveType::EliminateTagged:
		for (TActorIterator<AMetalBotCharacter> It(GetWorld()); It; ++It)
		{
			if (It->ActorHasTag(Objective.TargetTag) && It->IsAlive())
			{
				return false;
			}
		}
		return true;

	case EMetalObjectiveType::ReachZone:
	{
		const AMetalTriggerZone* Zone = Cast<AMetalTriggerZone>(Objective.TargetActor);
		return Zone && PlayerBody && Zone->ContainsActor(PlayerBody);
	}

	case EMetalObjectiveType::ActivateConsole:
	{
		const AMetalControlConsole* Console = Cast<AMetalControlConsole>(Objective.TargetActor);
		return Console && Console->IsActivated();
	}

	case EMetalObjectiveType::HijackTagged:
		return PlayerBody && PlayerBody->IsHijacked() && (Objective.TargetTag.IsNone() || PlayerBody->ActorHasTag(Objective.TargetTag));
	}
	return false;
}

void AMetalMission::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	const float Now = GetWorld()->GetTimeSeconds();
	if (!bLoadoutApplied && (PlayerStartingWeapons.Num() > 0 || PlayerStartingGrenades >= 0))
	{
		// Done on the first tick rather than in StartMission because the player's pawn hands out its own
		// archetype loadout in its BeginPlay, which may run after the game mode's.
		const APlayerController* PC = GetWorld()->GetFirstPlayerController();
		if (AMetalBotCharacter* Player = PC ? Cast<AMetalBotCharacter>(PC->GetPawn()) : nullptr; Player && Player->HasActorBegunPlay())
		{
			bLoadoutApplied = true;
			if (PlayerStartingWeapons.Num() > 0)
			{
				TArray<FMetalWeaponSlot> Loadout;
				for (UMetalWeaponData* Weapon : PlayerStartingWeapons)
				{
					if (Weapon)
					{
						FMetalWeaponSlot& Slot = Loadout.AddDefaulted_GetRef();
						Slot.Data = Weapon;
						Slot.Clip = Weapon->GetLevel(0).ClipSize;
						Slot.Reserve = Weapon->GetLevel(0).MaxReserveAmmo / 2;
					}
				}
				Player->SetWeapons(Loadout, 0);
			}
			if (PlayerStartingGrenades >= 0)
			{
				Player->Grenades = PlayerStartingGrenades;
			}
		}
	}
	if (!Objectives.IsValidIndex(CurrentObjective) || Now < NextCheckTime)
	{
		return;
	}
	NextCheckTime = Now + 0.25f;

	// Every unlocked objective of the group is checked, so they can be done in any order. Completing one
	// may unlock another, which is then checked on the next pass.
	const bool bQuiet = Now < QuietUntil;
	const int32 Last = GroupEnd(CurrentObjective);
	for (int32 I = CurrentObjective; I <= Last; ++I)
	{
		if (!IsActive(I) || !IsObjectiveComplete(Objectives[I]))
		{
			continue;
		}
		Done[I] = true;
		UE_LOG(LogMetal, Log, TEXT("Objective %d complete: %s"), I, *Objectives[I].Description.ToString());
		if (bQuiet)
		{
			continue;
		}
		if (AMetalGameMode* GM = GameMode.Get())
		{
			GM->ShowMessage(FText::GetEmpty(), FText::Format(NSLOCTEXT("Metalization", "ObjectiveDone", "Objective complete: {0}"), Objectives[I].Description), 3.f);
		}
		MetalAudio::Play2D(this, TEXT("objective_complete"), 0.8f);
		PlayDialogue(Objectives[I].CompleteDialogue);
		if (Objectives[I].bCheckpointOnComplete)
		{
			// Saved where the player's own body stands, even if they're driving a hijacked robot right now.
			const AMetalPlayerController* PC = Cast<AMetalPlayerController>(GetWorld()->GetFirstPlayerController());
			AMetalBotCharacter* Body = PC ? PC->GetPlayerBody() : nullptr;
			AMetalGameMode* GM = GameMode.Get();
			if (Body && Body->IsAlive() && GM)
			{
				GM->SetCheckpoint(FTransform(Body->GetActorRotation(), Body->GetActorLocation()), Body);
				Body->Heal(Body->GetMaxHealth());
			}
		}
	}

	bool bGroupDone = true;
	for (int32 I = CurrentObjective; I <= Last; ++I)
	{
		bGroupDone &= Done[I];
	}
	if (bGroupDone)
	{
		ActivateObjective(Last + 1);
	}
	else if (!bQuiet)
	{
		AnnounceUnlocked();
	}
	else
	{
		// Unlocked silently during the restore window: nothing to say about them later either.
		for (int32 I = CurrentObjective; I <= Last; ++I)
		{
			Announced[I] |= RequirementsMet(Objectives[I]);
		}
	}
}
