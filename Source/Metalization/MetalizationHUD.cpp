#include "MetalizationHUD.h"

#include "Engine/Canvas.h"
#include "MetalizationPlayerController.h"

void AMetalizationHUD::DrawHUD()
{
	Super::DrawHUD();

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
