#include "MetalPlayerController.h"

#include "MetalGameMode.h"
#include "MetalBotCharacter.h"
#include "MetalControlConsole.h"
#include "MetalFX.h"
#include "EngineUtils.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "InputAction.h"
#include "InputMappingContext.h"
#include "InputModifiers.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "GameFramework/CharacterMovementComponent.h"

namespace
{
	const FLinearColor TetherColor(0.2f, 0.9f, 1.f);
}

AMetalPlayerController::AMetalPlayerController()
{
	PrimaryActorTick.bCanEverTick = true;
}

// --------------------------------------------------------------------------------------------
// Input
// --------------------------------------------------------------------------------------------

UInputAction* AMetalPlayerController::MakeAction(const TCHAR* Name, int32 ValueType)
{
	UInputAction* Action = NewObject<UInputAction>(this, FName(FString(TEXT("IA_")) + Name));
	Action->ValueType = static_cast<EInputActionValueType>(ValueType);
	Actions.Add(Name, Action);
	return Action;
}

void AMetalPlayerController::CreateInputObjects()
{
	// Built in code so the demo has no input assets to keep in sync; designers can replace this
	// with an Input Mapping Context asset later without touching the handlers.
	MappingContext = NewObject<UInputMappingContext>(this, TEXT("IMC_Metalization"));
	auto Map = [this](UInputAction* Action, FKey Key, bool bNegate = false, bool bSwizzle = false, float Scale = 1.f)
	{
		FEnhancedActionKeyMapping& Mapping = MappingContext->MapKey(Action, Key);
		if (bSwizzle)
		{
			Mapping.Modifiers.Add(NewObject<UInputModifierSwizzleAxis>(this));
		}
		if (bNegate)
		{
			Mapping.Modifiers.Add(NewObject<UInputModifierNegate>(this));
		}
		if (Scale != 1.f)
		{
			UInputModifierScalar* Scalar = NewObject<UInputModifierScalar>(this);
			Scalar->Scalar = FVector(Scale);
			Mapping.Modifiers.Add(Scalar);
		}
	};
	auto NegateY = [this](UInputAction* Action, FKey Key, float Scale)
	{
		FEnhancedActionKeyMapping& Mapping = MappingContext->MapKey(Action, Key);
		UInputModifierNegate* Negate = NewObject<UInputModifierNegate>(this);
		Negate->bX = false;
		Negate->bZ = false;
		Mapping.Modifiers.Add(Negate);
		UInputModifierScalar* Scalar = NewObject<UInputModifierScalar>(this);
		Scalar->Scalar = FVector(Scale);
		Mapping.Modifiers.Add(Scalar);
	};

	constexpr int32 Bool = static_cast<int32>(EInputActionValueType::Boolean);
	constexpr int32 Axis1D = static_cast<int32>(EInputActionValueType::Axis1D);
	constexpr int32 Axis2D = static_cast<int32>(EInputActionValueType::Axis2D);

	UInputAction* MoveAction = MakeAction(TEXT("Move"), Axis2D);
	Map(MoveAction, EKeys::W, false, true);
	Map(MoveAction, EKeys::S, true, true);
	Map(MoveAction, EKeys::A, true);
	Map(MoveAction, EKeys::D);
	Map(MoveAction, EKeys::Gamepad_Left2D);

	UInputAction* LookAction = MakeAction(TEXT("Look"), Axis2D);
	NegateY(LookAction, EKeys::Mouse2D, 1.f);
	NegateY(LookAction, EKeys::Gamepad_Right2D, 2.5f);

	Map(MakeAction(TEXT("Jump"), Bool), EKeys::SpaceBar);
	Map(Actions[TEXT("Jump")], EKeys::Gamepad_FaceButton_Bottom);
	Map(MakeAction(TEXT("Fire"), Bool), EKeys::LeftMouseButton);
	Map(Actions[TEXT("Fire")], EKeys::Gamepad_RightTrigger);
	Map(MakeAction(TEXT("Aim"), Bool), EKeys::RightMouseButton);
	Map(Actions[TEXT("Aim")], EKeys::Gamepad_LeftTrigger);
	Map(MakeAction(TEXT("Reload"), Bool), EKeys::R);
	Map(Actions[TEXT("Reload")], EKeys::Gamepad_FaceButton_Left);
	Map(MakeAction(TEXT("Grenade"), Bool), EKeys::G);
	Map(Actions[TEXT("Grenade")], EKeys::Gamepad_RightShoulder);
	Map(MakeAction(TEXT("Tether"), Bool), EKeys::F);
	Map(Actions[TEXT("Tether")], EKeys::Gamepad_LeftShoulder);
	Map(MakeAction(TEXT("Interact"), Bool), EKeys::E);
	Map(Actions[TEXT("Interact")], EKeys::Gamepad_FaceButton_Right);
	Map(MakeAction(TEXT("Confirm"), Bool), EKeys::Enter);
	Map(Actions[TEXT("Confirm")], EKeys::Gamepad_Special_Right);
	Map(MakeAction(TEXT("CycleWeapon"), Axis1D), EKeys::MouseWheelAxis);
	Map(MakeAction(TEXT("NextWeapon"), Bool), EKeys::Gamepad_FaceButton_Top);
	const FKey NumberKeys[] = {EKeys::One, EKeys::Two, EKeys::Three, EKeys::Four, EKeys::Five, EKeys::Six};
	for (int32 Index = 0; Index < UE_ARRAY_COUNT(NumberKeys); ++Index)
	{
		Map(MakeAction(*FString::Printf(TEXT("Weapon%d"), Index + 1), Bool), NumberKeys[Index]);
	}
}

void AMetalPlayerController::SetupInputComponent()
{
	Super::SetupInputComponent();
	CreateInputObjects();

	UEnhancedInputComponent* Input = Cast<UEnhancedInputComponent>(InputComponent);
	if (Input == nullptr)
	{
		UE_LOG(LogMetal, Error, TEXT("Enhanced Input is not the default input component class."));
		return;
	}
	Input->BindAction(Actions[TEXT("Move")], ETriggerEvent::Triggered, this, &AMetalPlayerController::Move);
	Input->BindAction(Actions[TEXT("Look")], ETriggerEvent::Triggered, this, &AMetalPlayerController::Look);
	Input->BindAction(Actions[TEXT("Jump")], ETriggerEvent::Started, this, &AMetalPlayerController::JumpPressed);
	Input->BindAction(Actions[TEXT("Jump")], ETriggerEvent::Completed, this, &AMetalPlayerController::JumpReleased);
	Input->BindAction(Actions[TEXT("Fire")], ETriggerEvent::Started, this, &AMetalPlayerController::FirePressed);
	Input->BindAction(Actions[TEXT("Fire")], ETriggerEvent::Completed, this, &AMetalPlayerController::FireReleased);
	Input->BindAction(Actions[TEXT("Aim")], ETriggerEvent::Started, this, &AMetalPlayerController::AimPressed);
	Input->BindAction(Actions[TEXT("Aim")], ETriggerEvent::Completed, this, &AMetalPlayerController::AimReleased);
	Input->BindAction(Actions[TEXT("Reload")], ETriggerEvent::Started, this, &AMetalPlayerController::ReloadPressed);
	Input->BindAction(Actions[TEXT("Grenade")], ETriggerEvent::Started, this, &AMetalPlayerController::GrenadePressed);
	Input->BindAction(Actions[TEXT("Tether")], ETriggerEvent::Started, this, &AMetalPlayerController::TetherPressed);
	Input->BindAction(Actions[TEXT("Interact")], ETriggerEvent::Started, this, &AMetalPlayerController::InteractPressed);
	Input->BindAction(Actions[TEXT("Confirm")], ETriggerEvent::Started, this, &AMetalPlayerController::ConfirmPressed);
	Input->BindAction(Actions[TEXT("CycleWeapon")], ETriggerEvent::Triggered, this, &AMetalPlayerController::CycleWeaponAxis);
	Input->BindAction(Actions[TEXT("NextWeapon")], ETriggerEvent::Started, this, &AMetalPlayerController::NextWeapon);
	for (int32 Index = 0; Index < 6; ++Index)
	{
		Input->BindAction(Actions[*FString::Printf(TEXT("Weapon%d"), Index + 1)], ETriggerEvent::Started, this, &AMetalPlayerController::SelectWeapon, Index);
	}
}

void AMetalPlayerController::BeginPlay()
{
	Super::BeginPlay();
	if (UEnhancedInputLocalPlayerSubsystem* Subsystem = ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(GetLocalPlayer()))
	{
		Subsystem->AddMappingContext(MappingContext, 0);
	}
	SetInputMode(FInputModeGameOnly());
	bShowMouseCursor = false;
	if (PlayerCameraManager)
	{
		PlayerCameraManager->ViewPitchMin = -70.f;
		PlayerCameraManager->ViewPitchMax = 60.f;
	}
	BotDiedHandle = AMetalBotCharacter::OnAnyBotDied.AddUObject(this, &AMetalPlayerController::OnBotDied);
}

void AMetalPlayerController::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	AMetalBotCharacter::OnAnyBotDied.Remove(BotDiedHandle);
	Super::EndPlay(EndPlayReason);
}

AMetalBotCharacter* AMetalPlayerController::GetBot() const
{
	return Cast<AMetalBotCharacter>(GetPawn());
}

bool AMetalPlayerController::CanAct() const
{
	const AMetalBotCharacter* Bot = GetBot();
	return Bot && Bot->IsAlive() && !Bot->IsDormant() && !Bot->IsStunned();
}

void AMetalPlayerController::Move(const FInputActionValue& Value)
{
	if (!CanAct())
	{
		return;
	}
	const FVector2D Axis = Value.Get<FVector2D>();
	const FRotator Yaw(0.f, GetControlRotation().Yaw, 0.f);
	GetPawn()->AddMovementInput(FRotationMatrix(Yaw).GetUnitAxis(EAxis::X), Axis.Y);
	GetPawn()->AddMovementInput(FRotationMatrix(Yaw).GetUnitAxis(EAxis::Y), Axis.X);
}

void AMetalPlayerController::Look(const FInputActionValue& Value)
{
	const FVector2D Axis = Value.Get<FVector2D>();
	AddYawInput(Axis.X);
	AddPitchInput(Axis.Y);
}

void AMetalPlayerController::JumpPressed()
{
	if (CanAct())
	{
		GetBot()->Jump();
	}
}

void AMetalPlayerController::JumpReleased()
{
	if (AMetalBotCharacter* Bot = GetBot())
	{
		Bot->StopJumping();
	}
}

void AMetalPlayerController::FirePressed()
{
	if (CanAct())
	{
		GetBot()->StartFire();
	}
}

void AMetalPlayerController::FireReleased()
{
	if (AMetalBotCharacter* Bot = GetBot())
	{
		Bot->StopFire();
	}
}

void AMetalPlayerController::AimPressed()
{
	if (AMetalBotCharacter* Bot = GetBot())
	{
		Bot->SetAiming(true);
	}
}

void AMetalPlayerController::AimReleased()
{
	if (AMetalBotCharacter* Bot = GetBot())
	{
		Bot->SetAiming(false);
	}
}

void AMetalPlayerController::ReloadPressed()
{
	if (CanAct())
	{
		GetBot()->Reload();
	}
}

void AMetalPlayerController::GrenadePressed()
{
	if (CanAct())
	{
		GetBot()->ThrowGrenade();
	}
}

void AMetalPlayerController::CycleWeaponAxis(const FInputActionValue& Value)
{
	const float Axis = Value.Get<float>();
	if (CanAct() && !FMath::IsNearlyZero(Axis))
	{
		GetBot()->CycleWeapon(Axis > 0.f ? 1 : -1);
	}
}

void AMetalPlayerController::NextWeapon()
{
	if (CanAct())
	{
		GetBot()->CycleWeapon(1);
	}
}

void AMetalPlayerController::SelectWeapon(int32 Index)
{
	if (CanAct())
	{
		GetBot()->EquipWeapon(Index);
	}
}

void AMetalPlayerController::InteractPressed()
{
	AMetalControlConsole* Console = NearbyConsole.Get();
	AMetalBotCharacter* Bot = GetBot();
	if (Console && Bot && CanAct())
	{
		const FText Result = Console->TryActivate(Bot);
		if (AMetalGameMode* GameMode = GetWorld()->GetAuthGameMode<AMetalGameMode>())
		{
			GameMode->ShowMessage(FText::GetEmpty(), Result, 3.5f);
		}
	}
}

void AMetalPlayerController::ConfirmPressed()
{
	if (AMetalGameMode* GameMode = GetWorld()->GetAuthGameMode<AMetalGameMode>())
	{
		if (GameMode->IsMissionComplete())
		{
			GameMode->RestartMission();
		}
	}
}

// --------------------------------------------------------------------------------------------
// Hack Tether
// --------------------------------------------------------------------------------------------

void AMetalPlayerController::OnPossess(APawn* InPawn)
{
	Super::OnPossess(InPawn);
	if (!bSwitchingBodies)
	{
		// A fresh spawn (start of level or respawn) is always the player's own body.
		PlayerBody = Cast<AMetalBotCharacter>(InPawn);
		Host.Reset();
		// Start looking slightly down at the robot rather than level with the horizon. Applied next
		// tick: the game mode resets the control rotation after possession (FinishRestartPlayer).
		bApplySpawnPitch = true;
	}
}

float AMetalPlayerController::GetPossessionTimeRemaining() const
{
	return Host.IsValid() ? FMath::Max(0.f, PossessionEndTime - GetWorld()->GetTimeSeconds()) : 0.f;
}

void AMetalPlayerController::NotifyHitConfirmed()
{
	LastHitConfirmTime = GetWorld()->GetTimeSeconds();
}

void AMetalPlayerController::NotifyDamageTaken()
{
	LastDamageTime = GetWorld()->GetTimeSeconds();
}

AMetalBotCharacter* AMetalPlayerController::FindHackCandidate() const
{
	const AMetalBotCharacter* Body = PlayerBody.Get();
	if (Body == nullptr || GetPawn() != Body || !CanAct())
	{
		return nullptr;
	}
	FVector ViewLocation;
	FRotator ViewRotation;
	GetPlayerViewPoint(ViewLocation, ViewRotation);
	const FVector ViewDirection = ViewRotation.Vector();

	// Generous aim assist: the most centred hackable robot inside a narrow cone with line of sight.
	AMetalBotCharacter* Best = nullptr;
	float BestDot = FMath::Cos(FMath::DegreesToRadians(7.f));
	for (TActorIterator<AMetalBotCharacter> It(GetWorld()); It; ++It)
	{
		AMetalBotCharacter* Candidate = *It;
		if (!Candidate->CanBeHacked() || FVector::Dist(Candidate->GetActorLocation(), Body->GetActorLocation()) > TetherRange)
		{
			continue;
		}
		const float Dot = FVector::DotProduct((Candidate->GetActorLocation() - ViewLocation).GetSafeNormal(), ViewDirection);
		if (Dot <= BestDot)
		{
			continue;
		}
		FCollisionQueryParams Params(SCENE_QUERY_STAT(MetalTether), false, Body);
		Params.AddIgnoredActor(Candidate);
		FHitResult Hit;
		if (!GetWorld()->LineTraceSingleByChannel(Hit, Body->GetEyesLocation(), Candidate->GetActorLocation(), ECC_Visibility, Params))
		{
			Best = Candidate;
			BestDot = Dot;
		}
	}
	return Best;
}

void AMetalPlayerController::TetherPressed()
{
	if (Host.IsValid())
	{
		EndPossession();
	}
	else if (AMetalBotCharacter* Target = FindHackCandidate())
	{
		BeginPossession(Target);
	}
	else if (AMetalGameMode* GameMode = GetWorld()->GetAuthGameMode<AMetalGameMode>(); GameMode && GetPawn() == PlayerBody.Get())
	{
		GameMode->ShowMessage(FText::GetEmpty(), NSLOCTEXT("Metalization", "NoTetherTarget", "Hack Tether: no enemy robot in range."), 1.5f);
	}
}

void AMetalPlayerController::BeginPossession(AMetalBotCharacter* Target)
{
	AMetalBotCharacter* Body = PlayerBody.Get();
	if (Body == nullptr || Target == nullptr)
	{
		return;
	}
	if (AController* OldBrain = Target->GetController())
	{
		OldBrain->UnPossess();
		OldBrain->Destroy();
	}

	Body->StopFire();
	Body->SetAiming(false);
	Body->SetDormant(true);
	Target->SetHijacked(true);
	Target->GetCharacterMovement()->StopMovementImmediately();

	bSwitchingBodies = true;
	Possess(Target);
	bSwitchingBodies = false;
	SetControlRotation(FRotator(0.f, Target->GetActorRotation().Yaw, 0.f));

	Host = Target;
	PossessionDuration = Target->GetPossessionDuration();
	PossessionEndTime = GetWorld()->GetTimeSeconds() + PossessionDuration;
	TetherBeam = AMetalFX::SpawnBeam(GetWorld(), Body->GetMuzzleLocation(), Target->GetActorLocation(), TetherColor, 1.5f, 0.f);
	AMetalFX::SpawnBurst(GetWorld(), Target->GetActorLocation(), TetherColor, 90.f, 0.4f);

	if (AMetalGameMode* GameMode = GetWorld()->GetAuthGameMode<AMetalGameMode>())
	{
		GameMode->ShowMessage(FText::GetEmpty(), FText::Format(NSLOCTEXT("Metalization", "Hijacked", "HACK TETHER LINKED: {0}.  [F] to disconnect"), Target->GetDisplayName()), 3.f);
	}
}

void AMetalPlayerController::EndPossession()
{
	AMetalBotCharacter* OldHost = Host.Get();
	AMetalBotCharacter* Body = PlayerBody.Get();
	Host.Reset();
	if (TetherBeam)
	{
		TetherBeam->Destroy();
		TetherBeam = nullptr;
	}
	if (OldHost)
	{
		OldHost->StopFire();
		OldHost->SetAiming(false);
	}

	if (Body && Body->IsAlive())
	{
		bSwitchingBodies = true;
		Possess(Body);
		bSwitchingBodies = false;
		Body->SetDormant(false);
		SetControlRotation(FRotator(0.f, Body->GetActorRotation().Yaw, 0.f));
	}
	if (OldHost && OldHost->IsAlive())
	{
		// The tether burns out the host's brain: it stays on our side just long enough to blow up.
		OldHost->SelfDestruct(1.2f);
	}
}

void AMetalPlayerController::OnBotDied(AMetalBotCharacter* Bot, AController* Killer)
{
	if (Bot == nullptr)
	{
		return;
	}
	if (Bot == Host.Get())
	{
		EndPossession();
	}
	if (Bot == PlayerBody.Get())
	{
		if (Host.IsValid())
		{
			EndPossession();
		}
		if (AMetalGameMode* GameMode = GetWorld()->GetAuthGameMode<AMetalGameMode>())
		{
			GameMode->PlayerDied(this, Bot);
		}
	}
}

void AMetalPlayerController::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	if (AMetalBotCharacter* CurrentHost = Host.Get())
	{
		if (AMetalBotCharacter* Body = PlayerBody.Get(); TetherBeam && Body)
		{
			TetherBeam->SetBeamEndpoints(Body->GetMuzzleLocation(), CurrentHost->GetActorLocation());
		}
		if (GetWorld()->GetTimeSeconds() >= PossessionEndTime)
		{
			EndPossession();
		}
	}

	if (bApplySpawnPitch && GetPawn())
	{
		bApplySpawnPitch = false;
		SetControlRotation(FRotator(-12.f, GetControlRotation().Yaw, 0.f));
	}

	HackCandidate = FindHackCandidate();

	NearbyConsole.Reset();
	if (const APawn* MyPawn = GetPawn())
	{
		float BestDistance = TNumericLimits<float>::Max();
		for (TActorIterator<AMetalControlConsole> It(GetWorld()); It; ++It)
		{
			const float Distance = FVector::Dist(It->GetActorLocation(), MyPawn->GetActorLocation());
			if (Distance < It->InteractRadius && Distance < BestDistance && !It->IsActivated())
			{
				NearbyConsole = *It;
				BestDistance = Distance;
			}
		}
	}
}
