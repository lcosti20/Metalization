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
	/**
	 * Sets the label and the action, and hooks up click and hover. Call once after constructing.
	 *
	 * @param InText   Text block to use as the button's content.
	 * @param InLabel  Label shown, also used by PressButton/HasButton lookups.
	 * @param InAction Run when the button is pressed.
	 */
	void Init(UTextBlock* InText, const FText& InLabel, TFunction<void()> InAction);

	/** @return The button's label. */
	const FText& GetLabel() const { return Label; }

	/** Runs the button's action. */
	void Press() const;

	/** Highlight while hovered or focused (keyboard / gamepad selection). */
	void UpdateHighlight();

private:
	/** OnClicked handler: presses the button. */
	UFUNCTION()
	void HandleClicked();

	/** OnHovered handler: moves keyboard focus here, so mouse and keyboard/gamepad share one selection. */
	UFUNCTION()
	void HandleHovered();

	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> Text;

	FText Label;
	TFunction<void()> Action;
	/** Last applied highlight state: -1 unknown (forces the first update), 0 off, 1 on. */
	int8 bHighlighted = -1;
};

/**
 * The pause menu widget, created by AMetalPlayerController::OpenPauseMenu. Its whole widget tree is built
 * in BuildTree(): a dimmed backdrop, a title and a widget switcher with the main panel (index 0) and the
 * Level Select panel (index 1).
 */
UCLASS()
class UMetalPauseMenu : public UUserWidget
{
	GENERATED_BODY()

public:
	/** Shows Continue / Level Select / Quit and focuses the first button. */
	void ShowMainPanel();

	/** Shows the level list and focuses the first entry. */
	void ShowLevelPanel();

	/** @return True while the Level Select panel is showing. */
	bool IsShowingLevels() const;

	/**
	 * Presses the visible button with this label, exactly as a click would. For the dev autopilot.
	 *
	 * @param Label Exact label text (e.g. "Level Select", "Arena").
	 * @return True if a button was found and pressed.
	 */
	bool PressButton(const FString& Label);

	/**
	 * @param Label Exact label text.
	 * @return True if the visible panel has a button with this label.
	 */
	bool HasButton(const FString& Label) const;

protected:
	/** Builds the widget tree the first time, since there is no widget blueprint to supply one. */
	virtual TSharedRef<SWidget> RebuildWidget() override;

	/** Makes the menu focusable and shows the main panel. */
	virtual void NativeConstruct() override;

	/** Keeps a button focused and refreshes every button's highlight. */
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;

	/** Back keys (Escape, P, Start, B) go back a panel or close the menu. */
	virtual FReply NativeOnKeyDown(const FGeometry& InGeometry, const FKeyEvent& InKeyEvent) override;

private:
	/** Constructs the backdrop, title, both panels and their buttons. Level buttons come from UMetalLevelSettings. */
	void BuildTree();

	/**
	 * Creates a menu button at the bottom of a panel.
	 *
	 * @param Panel  Panel to add it to.
	 * @param List   Button list for that panel (MainButtons or LevelButtons).
	 * @param Label  Button text.
	 * @param Action Run when pressed.
	 * @return The new button.
	 */
	UMetalMenuButton* AddButton(UVerticalBox* Panel, TArray<TObjectPtr<UMetalMenuButton>>& List, const FText& Label, TFunction<void()> Action);

	/** From Level Select, returns to the main panel; from the main panel, closes the menu. */
	void Back();

	/** Gives keyboard focus to the first button on the visible panel. */
	void FocusFirstButton();

	/** @return The buttons on the visible panel. */
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
