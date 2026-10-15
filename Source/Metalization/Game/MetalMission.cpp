#include "MetalMission.h"

#include "MetalGameMode.h"
#include "MetalBotCharacter.h"
#include "MetalControlConsole.h"
#include "MetalTriggerZone.h"
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

FText AMetalMission::GetCurrentObjectiveText() const
{
	return Objectives.IsValidIndex(CurrentObjective) ? Objectives[CurrentObjective].Description : FText::GetEmpty();
}

void AMetalMission::PlayDialogue(const TArray<FMetalDialogueLine>& Lines) const
{
	if (AMetalGameMode* GM = GameMode.Get())
	{
		for (const FMetalDialogueLine& Line : Lines)
		{
			GM->ShowMessage(Line.Speaker, Line.Line, Line.Duration);
		}
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
	if (!Objectives.IsValidIndex(CurrentObjective) || Now < NextCheckTime)
	{
		return;
	}
	NextCheckTime = Now + 0.25f;
	if (IsObjectiveComplete(Objectives[CurrentObjective]))
	{
		if (AMetalGameMode* GM = GameMode.Get())
		{
			GM->ShowMessage(FText::GetEmpty(), FText::Format(NSLOCTEXT("Metalization", "ObjectiveDone", "Objective complete: {0}"), Objectives[CurrentObjective].Description), 3.f);
		}
		ActivateObjective(CurrentObjective + 1);
	}
}
