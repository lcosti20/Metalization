#include "MetalTitle.h"

#include "MetalDevAutopilot.h"
#include "MetalPauseMenu.h"
#include "Camera/CameraActor.h"
#include "EngineUtils.h"
#include "GameFramework/HUD.h"

AMetalTitleGameMode::AMetalTitleGameMode()
{
	DefaultPawnClass = nullptr;
	PlayerControllerClass = AMetalTitlePlayerController::StaticClass();
	HUDClass = AHUD::StaticClass();
	bStartPlayersAsSpectators = true;
}

AMetalTitlePlayerController::AMetalTitlePlayerController()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bTickEvenWhenPaused = true;
}

void AMetalTitlePlayerController::BeginPlay()
{
	Super::BeginPlay();
	if (!IsLocalController())
	{
		return;
	}
	for (TActorIterator<ACameraActor> It(GetWorld()); It; ++It)
	{
		if (It->ActorHasTag(TEXT("TitleCamera")))
		{
			TitleCamera = *It;
			break;
		}
	}
	if (TitleCamera)
	{
		// Orbit around the point the placed camera looks at, at the placed distance and height.
		const FVector Start = TitleCamera->GetActorLocation();
		FVector Ground = Start + TitleCamera->GetActorForwardVector() * 2000.f;
		Ground.Z = 100.f;
		Focus = Ground;
		const FVector Offset = Start - Focus;
		OrbitRadius = FVector(Offset.X, Offset.Y, 0.f).Size();
		OrbitHeight = Offset.Z;
		OrbitAngle = FMath::Atan2(Offset.Y, Offset.X);
		SetViewTarget(TitleCamera);
	}

	Menu = CreateWidget<UMetalPauseMenu>(this, UMetalPauseMenu::StaticClass());
	Menu->bTitleMode = true;
	Menu->AddToViewport(100);
	FInputModeUIOnly Mode;
	Mode.SetWidgetToFocus(Menu->TakeWidget());
	Mode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
	SetInputMode(Mode);
	bShowMouseCursor = true;
	Menu->ShowMainPanel();
#if !UE_BUILD_SHIPPING
	AMetalDevAutopilot::SpawnIfRequested(GetWorld());
#endif
}

void AMetalTitlePlayerController::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (TitleCamera)
	{
		OrbitAngle += DeltaSeconds * 0.06f;
		const FVector Location = Focus + FVector(FMath::Cos(OrbitAngle) * OrbitRadius, FMath::Sin(OrbitAngle) * OrbitRadius, OrbitHeight);
		TitleCamera->SetActorLocationAndRotation(Location, (Focus - Location).Rotation());
	}
}
