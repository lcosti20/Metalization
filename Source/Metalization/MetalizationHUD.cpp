#include "MetalizationHUD.h"

#include "Engine/Canvas.h"
#include "MetalizationGameState.h"
#include "MetalizationPlayerController.h"

void AMetalizationHUD::DrawTeamStats() const
{
	if (!Canvas || !GetWorld())
	{
		return;
	}

	const AMetalizationGameState* GameState = GetWorld()->GetGameState<AMetalizationGameState>();
	if (!GameState)
	{
		return;
	}

	const UFont* HudFont = GEngine ? GEngine->GetSmallFont() : nullptr;
	if (!HudFont)
	{
		return;
	}

	const int32 PlayerTeamId = 0;
	const float PlayerEnergy = GameState->GetTeamEnergy(PlayerTeamId);
	const int32 PlayerUnits = GameState->GetTeamUnitCount(PlayerTeamId);

	const FString TeamSummary = FString::Printf(TEXT("Team %d | Energy: %.0f | Units: %d"),
		PlayerTeamId, PlayerEnergy, PlayerUnits);
	const FString EnemySummary = FString::Printf(TEXT("Enemy | Energy: %.0f | Units: %d"),
		GameState->GetTeamEnergy(1), GameState->GetTeamUnitCount(1));

	FCanvasTextItem TextItem(FVector2D(20.0f, 20.0f), FText::FromString(TeamSummary), HudFont,
		FLinearColor::White);
	Canvas->DrawItem(TextItem);

	FCanvasTextItem EnemyTextItem(FVector2D(20.0f, 50.0f), FText::FromString(EnemySummary), HudFont,
		FLinearColor(0.85f, 0.5f, 0.5f, 1.0f));
	Canvas->DrawItem(EnemyTextItem);
}

void AMetalizationHUD::DrawHUD()
{
	Super::DrawHUD();
	DrawTeamStats();

	const AMetalizationPlayerController* RTSController =
		Cast<AMetalizationPlayerController>(PlayerOwner);
	if (!Canvas || !RTSController || !RTSController->IsSelectionBoxActive())
	{
		return;
	}

	const FVector2D Start = RTSController->GetSelectionStart();
	const FVector2D Current = RTSController->GetSelectionCurrent();
	const float Left = FMath::Min(Start.X, Current.X);
	const float Right = FMath::Max(Start.X, Current.X);
	const float Top = FMath::Min(Start.Y, Current.Y);
	const float Bottom = FMath::Max(Start.Y, Current.Y);
	const FLinearColor SelectionColor(0.1f, 0.75f, 1.0f, 1.0f);

	Canvas->K2_DrawLine(FVector2D(Left, Top), FVector2D(Right, Top), 1.5f, SelectionColor);
	Canvas->K2_DrawLine(FVector2D(Right, Top), FVector2D(Right, Bottom), 1.5f, SelectionColor);
	Canvas->K2_DrawLine(FVector2D(Right, Bottom), FVector2D(Left, Bottom), 1.5f, SelectionColor);
	Canvas->K2_DrawLine(FVector2D(Left, Bottom), FVector2D(Left, Top), 1.5f, SelectionColor);
}
