#include "MetalDevAutopilot.h"

#include "MetalBotAIController.h"
#include "MetalBotArchetype.h"
#include "MetalBotCharacter.h"
#include "MetalControlConsole.h"
#include "MetalPlayerController.h"
#include "MetalStalker.h"
#include "Camera/CameraActor.h"
#include "EngineUtils.h"
#include "Engine/World.h"
#include "Kismet/KismetSystemLibrary.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "UnrealClient.h"

namespace
{
	enum class EStep : uint8 { AimEnemy, AimHackable, AimBoss, AimGateGuard, Fire, StopFire, Tether, GoToConsole, Interact, Run, Stop, Jump, TurnLeft, TurnRight,
		AimLinkable, Climb, StopClimb, Boost, StopBoost, DodgeLeft, DodgeRight, AimEnemyGun };

	struct FAutopilotStep
	{
		float Time;
		EStep Action;
	};

	// Scripted smoke tests of the core loops, selected with -MetalAutopilot=<name>.
	const TMap<FString, TArray<FAutopilotStep>>& Scenarios()
	{
		static const TMap<FString, TArray<FAutopilotStep>> Map = {
			// Shoot, hijack the nearest enemy robot, fight with it, disconnect.
			{TEXT("combat"), {{1.5f, EStep::AimEnemy}, {1.6f, EStep::Fire}, {4.5f, EStep::StopFire}, {5.0f, EStep::AimHackable},
				{5.6f, EStep::Tether}, {7.0f, EStep::AimEnemy}, {7.1f, EStep::Fire}, {10.0f, EStep::StopFire}, {10.5f, EStep::Tether}}},
			// Hijack a guard through the energy fence, use the console, disconnect.
			{TEXT("gate"), {{1.0f, EStep::AimGateGuard}, {1.6f, EStep::Tether}, {2.5f, EStep::GoToConsole}, {3.2f, EStep::Interact}, {7.5f, EStep::Tether}}},
			// Hijack the Brute boss and turn its miniguns on the Soldiers.
			{TEXT("boss"), {{1.0f, EStep::AimBoss}, {1.6f, EStep::Tether}, {2.5f, EStep::AimEnemy}, {2.6f, EStep::Fire}, {8.0f, EStep::StopFire}}},
			// Free-roam movement in the arena: run, double-jump flip, turn, then shoot (action camera).
			{TEXT("arena"), {{1.0f, EStep::Run}, {2.0f, EStep::Jump}, {2.35f, EStep::Jump}, {3.6f, EStep::TurnLeft}, {4.4f, EStep::Jump},
				{5.4f, EStep::TurnRight}, {6.4f, EStep::Stop}, {7.0f, EStep::AimEnemy}, {7.2f, EStep::Fire}, {8.5f, EStep::StopFire}}},
			// Stalker flight (MZ_FlightTest): link to a parked Stalker, lift off, cruise, boost, shoot a gun
			// arm off an enemy Stalker and then finish it, dodge both ways, disconnect back to the body.
			{TEXT("flight"), {{1.0f, EStep::AimLinkable}, {1.6f, EStep::Tether}, {2.4f, EStep::Climb}, {3.0f, EStep::StopClimb},
				{3.0f, EStep::Run}, {4.4f, EStep::Boost}, {5.2f, EStep::StopBoost}, {6.0f, EStep::Stop}, {7.4f, EStep::AimEnemy},
				{7.6f, EStep::AimEnemyGun}, {7.7f, EStep::Fire}, {9.4f, EStep::AimEnemy}, {11.0f, EStep::StopFire}, {11.2f, EStep::DodgeLeft}, {12.2f, EStep::DodgeRight}, {13.2f, EStep::Tether}}},
		};
		return Map;
	}

	TArray<float> ParseFloats(const FString& Csv)
	{
		TArray<FString> Parts;
		Csv.ParseIntoArray(Parts, TEXT(","));
		TArray<float> Values;
		for (const FString& Part : Parts)
		{
			Values.Add(FCString::Atof(*Part));
		}
		return Values;
	}
}

AMetalDevAutopilot::AMetalDevAutopilot()
{
	PrimaryActorTick.bCanEverTick = true;
}

void AMetalDevAutopilot::SpawnIfRequested(UWorld* World)
{
	const TCHAR* Cmd = FCommandLine::Get();
	FString Value;
	if (FParse::Param(Cmd, TEXT("MetalAutopilot")) || FParse::Value(Cmd, TEXT("MetalAutopilot="), Value) || FParse::Value(Cmd, TEXT("MetalShots="), Value) ||
		FParse::Value(Cmd, TEXT("MetalTeleport="), Value) || FParse::Value(Cmd, TEXT("MetalCam="), Value) || FParse::Value(Cmd, TEXT("MetalQuitAt="), Value))
	{
		World->SpawnActor<AMetalDevAutopilot>();
	}
}

void AMetalDevAutopilot::BeginPlay()
{
	Super::BeginPlay();
	const TCHAR* Cmd = FCommandLine::Get();
	StartTime = GetWorld()->GetTimeSeconds();
	FString Value;
	bAutopilot = FParse::Param(Cmd, TEXT("MetalAutopilot")) || FParse::Value(Cmd, TEXT("MetalAutopilot="), Scenario);
	if (bAutopilot && !Scenarios().Contains(Scenario))
	{
		Scenario = TEXT("combat");
	}
	if (FParse::Value(Cmd, TEXT("MetalShots="), Value, false))
	{
		ShotTimes = ParseFloats(Value);
	}
	FParse::Value(Cmd, TEXT("MetalQuitAt="), QuitAt);

	APlayerController* PC = GetWorld()->GetFirstPlayerController();
	if (FParse::Value(Cmd, TEXT("MetalTeleport="), Value, false) && PC && PC->GetPawn())
	{
		const TArray<float> V = ParseFloats(Value);
		if (V.Num() >= 4)
		{
			PC->GetPawn()->TeleportTo(FVector(V[0], V[1], V[2]), FRotator(0.f, V[3], 0.f));
			PC->SetControlRotation(FRotator(0.f, V[3], 0.f));
		}
	}
	if (FParse::Value(Cmd, TEXT("MetalCam="), Value, false))
	{
		TArray<FString> Entries;
		Value.ParseIntoArray(Entries, TEXT(";"));
		for (const FString& Entry : Entries)
		{
			const TArray<float> V = ParseFloats(Entry);
			if (V.Num() >= 5)
			{
				Cameras.Add(FTransform(FRotator(V[3], V[4], 0.f), FVector(V[0], V[1], V[2])));
			}
		}
		ViewFromCamera(0);
	}
	UE_LOG(LogMetal, Display, TEXT("[Autopilot] active (scenario=%s shots=%d quit=%.1f)"), bAutopilot ? *Scenario : TEXT("none"), ShotTimes.Num(), QuitAt);
}

void AMetalDevAutopilot::ViewFromCamera(int32 Index)
{
	APlayerController* PC = GetWorld()->GetFirstPlayerController();
	if (PC && Cameras.IsValidIndex(Index))
	{
		ACameraActor* Camera = GetWorld()->SpawnActor<ACameraActor>(Cameras[Index].GetLocation(), Cameras[Index].Rotator());
		PC->SetViewTarget(Camera);
	}
}

AMetalBotCharacter* AMetalDevAutopilot::FindNearest(bool bHackableOnly, FName Tag) const
{
	const APlayerController* PC = GetWorld()->GetFirstPlayerController();
	const AMetalBotCharacter* Me = PC ? Cast<AMetalBotCharacter>(PC->GetPawn()) : nullptr;
	AMetalBotCharacter* Best = nullptr;
	float BestDistance = TNumericLimits<float>::Max();
	for (TActorIterator<AMetalBotCharacter> It(GetWorld()); Me && It; ++It)
	{
		if (!It->IsAlive() || !Me->IsHostileTo(*It) || (bHackableOnly && !It->CanBeHackedBy(Me)) || (!Tag.IsNone() && !It->ActorHasTag(Tag)))
		{
			continue;
		}
		const float Distance = FVector::Dist(It->GetActorLocation(), Me->GetActorLocation());
		if (Distance < BestDistance)
		{
			Best = *It;
			BestDistance = Distance;
		}
	}
	return Best;
}

AMetalBotCharacter* AMetalDevAutopilot::FindNearestLinkable() const
{
	const AMetalPlayerController* PC = Cast<AMetalPlayerController>(GetWorld()->GetFirstPlayerController());
	const AMetalBotCharacter* Me = PC ? PC->GetPlayerBody() : nullptr;
	AMetalBotCharacter* Best = nullptr;
	float BestDistance = TNumericLimits<float>::Max();
	for (TActorIterator<AMetalBotCharacter> It(GetWorld()); Me && It; ++It)
	{
		if (It->GetTeam() == Me->GetTeam() && It->CanBeHackedBy(Me))
		{
			const float Distance = FVector::Dist(It->GetActorLocation(), Me->GetActorLocation());
			if (Distance < BestDistance)
			{
				Best = *It;
				BestDistance = Distance;
			}
		}
	}
	return Best;
}

void AMetalDevAutopilot::AimAt(const AActor* Target) const
{
	APlayerController* PC = GetWorld()->GetFirstPlayerController();
	if (PC && Target)
	{
		FVector ViewLocation;
		FRotator ViewRotation;
		PC->GetPlayerViewPoint(ViewLocation, ViewRotation);
		PC->SetControlRotation((Target->GetActorLocation() - ViewLocation).Rotation());
	}
}

void AMetalDevAutopilot::RunStep(int32 Step)
{
	AMetalPlayerController* PC = Cast<AMetalPlayerController>(GetWorld()->GetFirstPlayerController());
	AMetalBotCharacter* Me = PC ? Cast<AMetalBotCharacter>(PC->GetPawn()) : nullptr;
	if (Me == nullptr)
	{
		return;
	}
	const EStep Action = Scenarios()[Scenario][Step].Action;
	AimBone = NAME_None;
	static const TCHAR* Names[] = {TEXT("aim at enemy"), TEXT("aim at hackable"), TEXT("aim at boss"), TEXT("aim at gate guard"), TEXT("fire"), TEXT("stop fire"), TEXT("tether"), TEXT("go to console"), TEXT("interact"),
		TEXT("run"), TEXT("stop"), TEXT("jump"), TEXT("turn left"), TEXT("turn right"),
		TEXT("aim at linkable"), TEXT("climb"), TEXT("stop climb"), TEXT("boost"), TEXT("stop boost"), TEXT("dodge left"), TEXT("dodge right"), TEXT("aim at enemy gun")};
	UE_LOG(LogMetal, Display, TEXT("[Autopilot] step %d: %s"), Step, Names[static_cast<int32>(Action)]);
	switch (Action)
	{
	case EStep::AimEnemy:    AimTarget = FindNearest(false, NAME_None); break;
	case EStep::AimHackable: AimTarget = FindNearest(true, NAME_None); break;
	case EStep::AimBoss:     AimTarget = FindNearest(true, TEXT("Boss")); break;
	case EStep::AimGateGuard: AimTarget = FindNearest(true, TEXT("GateGuard")); break;
	case EStep::Fire:        Me->StartFire(); break;
	case EStep::StopFire:    Me->StopFire(); AimTarget.Reset(); break;
	case EStep::Tether:      PC->ToggleTether(); AimTarget.Reset(); break;
	case EStep::Interact:    PC->Interact(); break;
	case EStep::Run:         bRunning = true; break;
	case EStep::Stop:        bRunning = false; break;
	case EStep::Jump:        Me->StopJumping(); Me->Jump(); break;
	case EStep::TurnLeft:    PC->SetControlRotation(PC->GetControlRotation() + FRotator(0.f, -90.f, 0.f)); break;
	case EStep::TurnRight:   PC->SetControlRotation(PC->GetControlRotation() + FRotator(0.f, 90.f, 0.f)); break;
	case EStep::AimLinkable: AimTarget = FindNearestLinkable(); break;
	case EStep::AimEnemyGun:
		AimTarget = FindNearest(false, NAME_None);
		AimBone = AimTarget.IsValid() && AimTarget->Archetype && AimTarget->Archetype->MuzzleBones.Num() > 0 ? AimTarget->Archetype->MuzzleBones[0] : NAME_None;
		break;
	case EStep::Climb:       bClimbing = true; break;
	case EStep::StopClimb:   bClimbing = false; break;
	case EStep::Boost:       if (AMetalStalker* Flyer = Cast<AMetalStalker>(Me)) { Flyer->SetBoostHeld(true); } break;
	case EStep::StopBoost:   if (AMetalStalker* Flyer = Cast<AMetalStalker>(Me)) { Flyer->SetBoostHeld(false); } break;
	case EStep::DodgeLeft:   if (AMetalStalker* Flyer = Cast<AMetalStalker>(Me)) { Flyer->Dodge(-1.f); } break;
	case EStep::DodgeRight:  if (AMetalStalker* Flyer = Cast<AMetalStalker>(Me)) { Flyer->Dodge(1.f); } break;
	case EStep::GoToConsole:
		for (TActorIterator<AMetalControlConsole> It(GetWorld()); It; ++It)
		{
			Me->TeleportTo(It->GetActorLocation() + It->GetActorForwardVector() * 120.f + FVector(0.f, 0.f, 80.f), Me->GetActorRotation());
			break;
		}
		break;
	}
}

void AMetalDevAutopilot::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	const float T = GetWorld()->GetTimeSeconds() - StartTime;

	if (bAutopilot)
	{
		const TArray<FAutopilotStep>& Steps = Scenarios()[Scenario];
		while (NextStep < Steps.Num() && T >= Steps[NextStep].Time)
		{
			RunStep(NextStep++);
		}
		if (AMetalBotCharacter* Target = AimTarget.Get())
		{
			if (!AimBone.IsNone() && Target->GetMesh()->GetBoneIndex(AimBone) != INDEX_NONE)
			{
				FVector ViewLocation;
				FRotator ViewRotation;
				GetWorld()->GetFirstPlayerController()->GetPlayerViewPoint(ViewLocation, ViewRotation);
				GetWorld()->GetFirstPlayerController()->SetControlRotation((Target->GetMesh()->GetBoneLocation(AimBone) - ViewLocation).Rotation());
			}
			else
			{
				AimAt(Target);
			}
		}
		const APlayerController* PC = GetWorld()->GetFirstPlayerController();
		if (APawn* Pawn = PC ? PC->GetPawn() : nullptr; Pawn && bRunning)
		{
			Pawn->AddMovementInput(FRotator(0.f, PC->GetControlRotation().Yaw, 0.f).Vector());
		}
		if (AMetalStalker* Flyer = PC ? Cast<AMetalStalker>(PC->GetPawn()) : nullptr; Flyer && bClimbing)
		{
			Flyer->AddVerticalInput(1.f);
		}
	}

	if (ShotTimes.IsValidIndex(ShotIndex) && T >= ShotTimes[ShotIndex])
	{
		FScreenshotRequest::RequestScreenshot(FString::Printf(TEXT("MetalShot_%02d.png"), ShotIndex), true, false);
		UE_LOG(LogMetal, Display, TEXT("[Autopilot] screenshot %d at %.1fs"), ShotIndex, T);
		ShotIndex++;
		// Switch after the capture has been taken at the end of this frame.
		CameraSwitchTime = T + 0.3f;
	}
	if (CameraSwitchTime > 0.f && T >= CameraSwitchTime)
	{
		CameraSwitchTime = -1.f;
		ViewFromCamera(ShotIndex);
	}

	if (T >= NextLogTime)
	{
		NextLogTime = T + 1.f;
		const APlayerController* PC = GetWorld()->GetFirstPlayerController();
		if (const AMetalBotCharacter* Me = PC ? Cast<AMetalBotCharacter>(PC->GetPawn()) : nullptr)
		{
			const FMetalWeaponSlot* Slot = Me->GetCurrentWeapon();
			UE_LOG(LogMetal, Display, TEXT("[Autopilot] t=%.1f pawn=%s hp=%.0f loc=%s hijacked=%d clip=%d"),
				T, *Me->GetName(), Me->GetHealth(), *Me->GetActorLocation().ToCompactString(), Me->IsHijacked(), Slot ? Slot->Clip : -1);
			if (const AMetalStalker* Flyer = Cast<AMetalStalker>(Me))
			{
				UE_LOG(LogMetal, Display, TEXT("[Autopilot]    flight speed=%.0f boost=%.2f boosting=%d guns=%d"),
					Flyer->GetVelocity().Size(), Flyer->GetBoostFraction(), Flyer->IsBoosting(), Flyer->GetWorkingMuzzleCount());
			}
		}
		for (TActorIterator<AMetalBotCharacter> It(GetWorld()); It; ++It)
		{
			if (const AMetalBotAIController* AI = Cast<AMetalBotAIController>(It->GetController()))
			{
				UE_LOG(LogMetal, Display, TEXT("[Autopilot]    %s hp=%.0f state=%d"), *It->GetName(), It->GetHealth(), static_cast<int32>(AI->GetState()));
			}
			else if (It->IsA<AMetalStalker>() && !It->IsPlayerControlled())
			{
				UE_LOG(LogMetal, Display, TEXT("[Autopilot]    %s hp=%.0f team=%d guns=%d"), *It->GetName(), It->GetHealth(), static_cast<int32>(It->GetTeam()), It->GetWorkingMuzzleCount());
			}
		}
	}

	if (QuitAt > 0.f && T >= QuitAt)
	{
		QuitAt = -1.f;
		UKismetSystemLibrary::QuitGame(this, nullptr, EQuitPreference::Quit, false);
	}
}
