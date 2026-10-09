// Copyright Epic Games, Inc. All Rights Reserved.

#include "MetalizationPlayerController.h"

#include "EnhancedInputSubsystems.h"
#include "Engine/EngineTypes.h"
#include "Engine/LocalPlayer.h"
#include "InputCoreTypes.h"
#include "InputMappingContext.h"
#include "Blueprint/UserWidget.h"
#include "MetalizationCharacter.h"
#include "Widgets/Input/SVirtualJoystick.h"
#include "Kismet/GameplayStatics.h"

void AMetalizationPlayerController::BeginPlay()
{
	Super::BeginPlay();

	if (!IsLocalPlayerController())
	{
		return;
	}

	bShowMouseCursor = true;
	bEnableClickEvents = true;
	bEnableMouseOverEvents = true;

	FInputModeGameAndUI InputMode;
	InputMode.SetHideCursorDuringCapture(false);
	InputMode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
	SetInputMode(InputMode);

	if (ShouldUseTouchControls())
	{
		MobileControlsWidget = CreateWidget<UUserWidget>(this, MobileControlsWidgetClass);
		if (MobileControlsWidget)
		{
			MobileControlsWidget->AddToPlayerScreen(0);
		}
		else
		{
			UE_LOG(LogTemp, Error, TEXT("Could not spawn mobile controls widget."));
		}
	}
}

void AMetalizationPlayerController::SetupInputComponent()
{
	Super::SetupInputComponent();

	if (!IsLocalPlayerController())
	{
		return;
	}

	if (UEnhancedInputLocalPlayerSubsystem* Subsystem =
		ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(GetLocalPlayer()))
	{
		for (UInputMappingContext* CurrentContext : DefaultMappingContexts)
		{
			if (CurrentContext)
			{
				Subsystem->AddMappingContext(CurrentContext, 0);
			}
		}

		if (!ShouldUseTouchControls())
		{
			for (UInputMappingContext* CurrentContext : MobileExcludedMappingContexts)
			{
				if (CurrentContext)
				{
					Subsystem->AddMappingContext(CurrentContext, 0);
				}
			}
		}
	}

	if (InputComponent)
	{
		InputComponent->BindKey(EKeys::LeftMouseButton, IE_Pressed, this, &AMetalizationPlayerController::BeginSelection);
		InputComponent->BindKey(EKeys::LeftMouseButton, IE_Released, this, &AMetalizationPlayerController::EndSelection);
		InputComponent->BindKey(EKeys::RightMouseButton, IE_Released, this, &AMetalizationPlayerController::IssueMoveOrder);
		InputComponent->BindAxis(TEXT("RTSZoom"), this, &AMetalizationPlayerController::ZoomCamera);
	}
}

void AMetalizationPlayerController::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	if (!IsLocalPlayerController())
	{
		return;
	}

	float MouseX = 0.0f;
	float MouseY = 0.0f;
	const bool bHasMousePosition = GetMousePosition(MouseX, MouseY);
	int32 ViewportWidth = 0;
	int32 ViewportHeight = 0;
	GetViewportSize(ViewportWidth, ViewportHeight);

	if (bSelectionBoxActive && bHasMousePosition)
	{
		SelectionCurrent = FVector2D(MouseX, MouseY);
	}

	if (!IsInputKeyDown(EKeys::MiddleMouseButton))
	{
		if (!bSelectionBoxActive)
		{
			float PanRight = 0.0f;
			float PanForward = 0.0f;
			if (bHasMousePosition && ViewportWidth > 0 && ViewportHeight > 0 &&
				MouseX >= 0.0f && MouseX <= ViewportWidth && MouseY >= 0.0f && MouseY <= ViewportHeight)
			{
				if (MouseX < EdgePanMargin)
				{
					PanRight = -FMath::Clamp((EdgePanMargin - MouseX) / EdgePanMargin, 0.0f, 1.0f);
				}
				else if (MouseX > ViewportWidth - EdgePanMargin)
				{
					PanRight = FMath::Clamp((MouseX - (ViewportWidth - EdgePanMargin)) / EdgePanMargin, 0.0f, 1.0f);
				}

				if (MouseY < EdgePanMargin)
				{
					PanForward = FMath::Clamp((EdgePanMargin - MouseY) / EdgePanMargin, 0.0f, 1.0f);
				}
				else if (MouseY > ViewportHeight - EdgePanMargin)
				{
					PanForward = -FMath::Clamp((MouseY - (ViewportHeight - EdgePanMargin)) / EdgePanMargin, 0.0f, 1.0f);
				}
			}

			const float KeyboardRight = (IsInputKeyDown(EKeys::D) ? 1.0f : 0.0f) -
				(IsInputKeyDown(EKeys::A) ? 1.0f : 0.0f);
			const float KeyboardForward = (IsInputKeyDown(EKeys::W) ? 1.0f : 0.0f) -
				(IsInputKeyDown(EKeys::S) ? 1.0f : 0.0f);
			if (AMetalizationCharacter* CameraPawn = Cast<AMetalizationCharacter>(GetPawn());
				CameraPawn && (!FMath::IsNearlyZero(PanRight + KeyboardRight) ||
					!FMath::IsNearlyZero(PanForward + KeyboardForward)))
			{
				CameraPawn->DoMove(PanRight + KeyboardRight, PanForward + KeyboardForward);
			}
		}
	}
	else if (AMetalizationCharacter* CameraPawn = Cast<AMetalizationCharacter>(GetPawn()))
	{
		float MouseDeltaX = 0.0f;
		float MouseDeltaY = 0.0f;
		GetInputMouseDelta(MouseDeltaX, MouseDeltaY);
		CameraPawn->DoLook(MouseDeltaX, -MouseDeltaY);
	}
}

void AMetalizationPlayerController::BeginSelection()
{
	if (!GetMousePosition(SelectionStart.X, SelectionStart.Y))
	{
		return;
	}

	SelectionCurrent = SelectionStart;
	bSelectionBoxActive = true;
}

void AMetalizationPlayerController::EndSelection()
{
	if (!bSelectionBoxActive)
	{
		return;
	}

	float MouseX = SelectionCurrent.X;
	float MouseY = SelectionCurrent.Y;
	GetMousePosition(MouseX, MouseY);
	SelectionCurrent = FVector2D(MouseX, MouseY);
	bSelectionBoxActive = false;

	const bool bAddToSelection = IsInputKeyDown(EKeys::LeftShift) || IsInputKeyDown(EKeys::RightShift);
	if (FVector2D::Distance(SelectionStart, SelectionCurrent) >= 6.0f)
	{
		SelectActorsInBox();
		return;
	}

	FHitResult Hit;
	if (GetHitResultUnderCursorByChannel(UEngineTypes::ConvertToTraceType(ECC_Visibility), false, Hit) && Hit.GetActor() &&
		Hit.GetActor()->ActorHasTag(SelectableActorTag))
	{
		SelectActor(Hit.GetActor(), bAddToSelection);
	}
	else if (!bAddToSelection)
	{
		ClearSelection();
	}
}

void AMetalizationPlayerController::SelectActorsInBox()
{
	const bool bAddToSelection = IsInputKeyDown(EKeys::LeftShift) || IsInputKeyDown(EKeys::RightShift);
	if (!bAddToSelection)
	{
		SelectedActors.Reset();
	}

	const float MinX = FMath::Min(SelectionStart.X, SelectionCurrent.X);
	const float MaxX = FMath::Max(SelectionStart.X, SelectionCurrent.X);
	const float MinY = FMath::Min(SelectionStart.Y, SelectionCurrent.Y);
	const float MaxY = FMath::Max(SelectionStart.Y, SelectionCurrent.Y);
	TArray<AActor*> SelectableActors;
	UGameplayStatics::GetAllActorsWithTag(GetWorld(), SelectableActorTag, SelectableActors);

	for (AActor* Actor : SelectableActors)
	{
		if (!IsValid(Actor))
		{
			continue;
		}

		FVector2D ScreenPosition;
		if (ProjectWorldLocationToScreen(Actor->GetActorLocation(), ScreenPosition, true) &&
			ScreenPosition.X >= MinX && ScreenPosition.X <= MaxX &&
			ScreenPosition.Y >= MinY && ScreenPosition.Y <= MaxY)
		{
			SelectedActors.AddUnique(Actor);
		}
	}

	OnSelectionChanged.Broadcast();
}

void AMetalizationPlayerController::SelectActor(AActor* Actor, bool bAddToSelection)
{
	if (!IsValid(Actor))
	{
		return;
	}

	if (!bAddToSelection)
	{
		SelectedActors.Reset();
	}
	SelectedActors.AddUnique(Actor);
	OnSelectionChanged.Broadcast();
}

void AMetalizationPlayerController::ClearSelection()
{
	if (SelectedActors.IsEmpty())
	{
		return;
	}

	SelectedActors.Reset();
	OnSelectionChanged.Broadcast();
}

void AMetalizationPlayerController::IssueMoveOrder()
{
	if (SelectedActors.IsEmpty())
	{
		return;
	}

	FHitResult Hit;
	if (GetHitResultUnderCursorByChannel(UEngineTypes::ConvertToTraceType(ECC_Visibility), false, Hit))
	{
		OnMoveOrderIssued.Broadcast(Hit.Location);
	}
}

void AMetalizationPlayerController::ZoomCamera(float Amount)
{
	if (AMetalizationCharacter* CameraPawn = Cast<AMetalizationCharacter>(GetPawn()))
	{
		CameraPawn->DoZoom(Amount);
	}
}

bool AMetalizationPlayerController::ShouldUseTouchControls() const
{
	return SVirtualJoystick::ShouldDisplayTouchInterface() || bForceTouchControls;
}
