// Pause menu: Continue, Level Select (a list of UMetalLevelSettings levels plus Back), Quit.
//
// Built in code like the HUD, so there are no widget assets to keep in sync. It works with the mouse,
// keyboard (arrows + Enter) and gamepad (stick/d-pad + A). Escape, P, Start or B backs out: from Level
// Select to the pause menu, from the pause menu back into the game. AMetalPlayerController opens and
// closes it and owns the pause.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Components/Button.h"
#include "MetalPauseMenu.generated.h"

class UTextBlock;
class UVerticalBox;
class UWidgetSwitcher;

/** A menu button that runs a native callback, so each level button can carry its own level. */
UCLASS()
class UMetalMenuButton : public UButton
{
	GENERATED_BODY()

public:
	void Init(UTextBlock* InText, const FText& InLabel, TFunction<void()> InAction);
	const FText& GetLabel() const { return Label; }
	void Press() const;

	/** Highlight while hovered or focused (keyboard / gamepad selection). */
	void UpdateHighlight();

private:
	UFUNCTION()
	void HandleClicked();

	UFUNCTION()
	void HandleHovered();

	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> Text;

	FText Label;
	TFunction<void()> Action;
	int8 bHighlighted = -1;
};

UCLASS()
class UMetalPauseMenu : public UUserWidget
{
	GENERATED_BODY()

public:
	void ShowMainPanel();
	void ShowLevelPanel();
	bool IsShowingLevels() const;

	/** Presses the visible button with this label, exactly as a click would. For the dev autopilot. */
	bool PressButton(const FString& Label);
	bool HasButton(const FString& Label) const;

protected:
	virtual TSharedRef<SWidget> RebuildWidget() override;
	virtual void NativeConstruct() override;
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;
	virtual FReply NativeOnKeyDown(const FGeometry& InGeometry, const FKeyEvent& InKeyEvent) override;

private:
	void BuildTree();
	UMetalMenuButton* AddButton(UVerticalBox* Panel, TArray<TObjectPtr<UMetalMenuButton>>& List, const FText& Label, TFunction<void()> Action);
	void Back();
	void FocusFirstButton();
	const TArray<TObjectPtr<UMetalMenuButton>>& VisibleButtons() const;

	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> Title;

	UPROPERTY(Transient)
	TObjectPtr<UWidgetSwitcher> Switcher;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UMetalMenuButton>> MainButtons;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UMetalMenuButton>> LevelButtons;
};
