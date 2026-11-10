#include "MetalMission.h"

#include "MetalGameMode.h"
#include "MetalBotCharacter.h"
#include "MetalControlConsole.h"
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

void AMetalMission::RestoreObjective(AMetalGameMode* InGameMode, int32 Index)
{
	GameMode = InGameMode;
	bLoadoutApplied = true;
	QuietUntil = GetWorld()->GetTimeSeconds() + 1.f;
	CurrentObjective = Index;
	UE_LOG(LogMetal, Log, TEXT("Objective %d (checkpoint): %s"), Index, *GetCurrentObjectiveText().ToString());
}

FText AMetalMission::GetCurrentObjectiveText() const
{
	return Objectives.IsValidIndex(CurrentObjective) ? Objectives[CurrentObjective].Description : FText::GetEmpty();
}

FText AMetalMission::GetCurrentHint() const
{
	return Objectives.IsValidIndex(CurrentObjective) ? Objectives[CurrentObjective].Hint : FText::GetEmpty();
}

bool AMetalMission::GetObjectiveMarker(FVector& OutLocation) const
{
	if (!Objectives.IsValidIndex(CurrentObjective) || !Objectives[CurrentObjective].bShowMarker)
	{
		return false;
	}
	const FMetalObjective& Objective = Objectives[CurrentObjective];
	switch (Objective.Type)
	{
	case EMetalObjectiveType::ReachZone:
	case EMetalObjectiveType::ActivateConsole:
		if (Objective.TargetActor)
		{
			OutLocation = Objective.TargetActor->GetActorLocation();
			return true;
		}
		return false;

	case EMetalObjectiveType::EliminateTagged:
	case EMetalObjectiveType::HijackTagged:
	{
		// The nearest robot still to deal with.
		const APlayerController* PC = GetWorld()->GetFirstPlayerController();
		const APawn* Viewer = PC ? PC->GetPawn() : nullptr;
		if (Viewer == nullptr || Objective.TargetTag.IsNone())
		{
			return false;
		}
		float Best = TNumericLimits<float>::Max();
		for (TActorIterator<AMetalBotCharacter> It(GetWorld()); It; ++It)
		{
			if (It->ActorHasTag(Objective.TargetTag) && It->IsAlive() && !It->IsHijacked())
			{
				const float Distance = FVector::DistSquared(It->GetActorLocation(), Viewer->GetActorLocation());
				if (Distance < Best)
				{
					Best = Distance;
					OutLocation = It->GetActorLocation() + FVector(0.f, 0.f, It->GetSimpleCollisionHalfHeight() + 40.f);
				}
			}
		}
		return Best < TNumericLimits<float>::Max();
	}
	}
	return false;
}

void AMetalMission::PlayDialogue(const TArray<FMetalDialogueLine>& Lines) const
{
	if (UMetalDialogueSubsystem* Dialogue = GetWorld()->GetSubsystem<UMetalDialogueSubsystem>())
	{
		Dialogue->PlayDialogue(Lines);
	}
}

void AMetalMission::ActivateObjective(int32 Index)
{
	CurrentObjective = Index;
	if (Objectives.IsValidIndex(Index))
	{
		PlayDialogue(Objectives[Index].StartDialogue);
		UE_LOG(LogMetal, Log, TEXT("Objective %d: %s"), Index, *Objectives[Index].Description.ToString());
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
	if (IsObjectiveComplete(Objectives[CurrentObjective]))
	{
		if (Now < QuietUntil)
		{
			ActivateObjective(CurrentObjective + 1);
			return;
		}
		if (AMetalGameMode* GM = GameMode.Get())
		{
			GM->ShowMessage(FText::GetEmpty(), FText::Format(NSLOCTEXT("Metalization", "ObjectiveDone", "Objective complete: {0}"), Objectives[CurrentObjective].Description), 3.f);
		}
		MetalAudio::Play2D(this, TEXT("objective_complete"), 0.8f);
		ActivateObjective(CurrentObjective + 1);
	}
}
