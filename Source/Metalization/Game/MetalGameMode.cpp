#include "MetalGameMode.h"

#include "MetalDevAutopilot.h"
#include "MetalPlayerCharacter.h"
#include "MetalHUD.h"
#include "MetalMission.h"
#include "MetalPlayerController.h"
#include "EngineUtils.h"
#include "Kismet/GameplayStatics.h"
#include "TimerManager.h"

AMetalGameMode::AMetalGameMode()
{
	PrimaryActorTick.bCanEverTick = true;
	DefaultPawnClass = AMetalPlayerCharacter::StaticClass();
	PlayerControllerClass = AMetalPlayerController::StaticClass();
	HUDClass = AMetalHUD::StaticClass();
}

void AMetalGameMode::BeginPlay()
{
	Super::BeginPlay();
	MissionStartTime = GetWorld()->GetTimeSeconds();
	BotDiedHandle = AMetalBotCharacter::OnAnyBotDied.AddUObject(this, &AMetalGameMode::OnBotDied);

	for (TActorIterator<AMetalMission> It(GetWorld()); It; ++It)
	{
		Mission = *It;
		break;
	}
	if (Mission)
	{
		Mission->StartMission(this);
	}
	else
	{
		UE_LOG(LogMetal, Warning, TEXT("No AMetalMission in this level; running without objectives."));
	}

#if !UE_BUILD_SHIPPING
	AMetalDevAutopilot::SpawnIfRequested(GetWorld());
#endif
}

void AMetalGameMode::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	AMetalBotCharacter::OnAnyBotDied.Remove(BotDiedHandle);
	Super::EndPlay(EndPlayReason);
}

void AMetalGameMode::ShowMessage(const FText& Speaker, const FText& Text, float Duration)
{
	if (Text.IsEmpty())
	{
		return;
	}
	if (Speaker.IsEmpty())
	{
		Notice = {Speaker, Text, Duration};
		NoticeTimeLeft = Duration;
		return;
	}
	DialogueQueue.Add({Speaker, Text, Duration});
	if (DialogueQueue.Num() == 1)
	{
		DialogueTimeLeft = Duration;
	}
}

void AMetalGameMode::ShowBanner(const FText& Text, float Duration)
{
	Banner = {FText::GetEmpty(), Text, Duration};
	BannerTimeLeft = Duration;
}

void AMetalGameMode::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	NoticeTimeLeft -= DeltaSeconds;
	BannerTimeLeft -= DeltaSeconds;
	if (DialogueQueue.Num() > 0)
	{
		DialogueTimeLeft -= DeltaSeconds;
		if (DialogueTimeLeft <= 0.f)
		{
			DialogueQueue.RemoveAt(0);
			DialogueTimeLeft = DialogueQueue.Num() > 0 ? DialogueQueue[0].Duration : 0.f;
		}
	}
}

FText AMetalGameMode::GetObjectiveText() const
{
	return Mission ? Mission->GetCurrentObjectiveText() : FText::GetEmpty();
}

float AMetalGameMode::GetMissionTime() const
{
	return (bMissionComplete ? MissionEndTime : GetWorld()->GetTimeSeconds()) - MissionStartTime;
}

void AMetalGameMode::SetCheckpoint(const FTransform& Transform)
{
	bHasCheckpoint = true;
	CheckpointTransform = Transform;
	ShowBanner(NSLOCTEXT("Metalization", "Checkpoint", "CHECKPOINT"), 2.f);
}

void AMetalGameMode::OnBotDied(AMetalBotCharacter* Bot, AController* Killer)
{
	if (Bot && Bot->GetTeam() == EMetalTeam::Villains && Killer && Killer->IsPlayerController())
	{
		Kills++;
	}
}

void AMetalGameMode::PlayerDied(AMetalPlayerController* Controller, AMetalBotCharacter* Body)
{
	if (Body)
	{
		SavedWeapons = Body->GetWeapons();
		SavedWeaponIndex = FMath::Max(0, Body->GetCurrentWeaponIndex());
		SavedGrenades = Body->Grenades;
	}
	ShowBanner(NSLOCTEXT("Metalization", "Scrapped", "SCRAPPED!"), 3.f);
	FTimerHandle Handle;
	GetWorldTimerManager().SetTimer(Handle, FTimerDelegate::CreateUObject(this, &AMetalGameMode::Respawn, Controller), 3.f, false);
}

void AMetalGameMode::Respawn(AMetalPlayerController* Controller)
{
	if (Controller == nullptr)
	{
		return;
	}
	APawn* OldBody = Controller->GetPawn();
	Controller->UnPossess();
	if (OldBody)
	{
		OldBody->Destroy();
	}
	if (AMetalBotCharacter* OldPlayerBody = Controller->GetPlayerBody(); OldPlayerBody && OldPlayerBody != OldBody)
	{
		OldPlayerBody->Destroy();
	}

	if (bHasCheckpoint)
	{
		RestartPlayerAtTransform(Controller, CheckpointTransform);
	}
	else
	{
		RestartPlayer(Controller);
	}
	if (AMetalBotCharacter* NewBody = Cast<AMetalBotCharacter>(Controller->GetPawn()))
	{
		if (SavedWeapons.Num() > 0)
		{
			NewBody->SetWeapons(SavedWeapons, SavedWeaponIndex);
			NewBody->AddAmmoFraction(0.5f);
		}
		NewBody->Grenades = FMath::Max(SavedGrenades, 2);
	}
}

void AMetalGameMode::CompleteMission(const FText& EndBanner)
{
	if (!bMissionComplete)
	{
		bMissionComplete = true;
		MissionEndTime = GetWorld()->GetTimeSeconds();
		ShowBanner(EndBanner.IsEmpty() ? NSLOCTEXT("Metalization", "MissionComplete", "MISSION COMPLETE") : EndBanner, 1000000.f);
	}
}

void AMetalGameMode::RestartMission()
{
	UGameplayStatics::OpenLevel(this, FName(*UGameplayStatics::GetCurrentLevelName(this)));
}
