// Pause menu and title menu. In game: Continue, Restart Mission, Level Select (the UMetalLevelSettings levels
// plus Back), Controls, Sound, Credits, Title Screen, Quit. On the title screen: Start Mission, Level Select,
// Controls, Sound, Credits, Quit. Sound has two volume sliders (Music / Background and Sound Effects); Credits
// lists the licensed music (keep it in sync with CREDITS.txt).
//
// Built in code like the HUD, so there are no widget assets to keep in sync. It works with the mouse,
// keyboard (arrows + Enter, left/right moves a slider) and gamepad (stick/d-pad + A). Escape, P, Start or B
// backs out: from any sub-panel to the main panel, from the main panel back into the game (not on the title screen). AMetalPlayerController opens and
// closes it and owns the pause.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Components/Button.h"
#include "MetalAudio.h"
#include "MetalPauseMenu.generated.h"

class UBorder;
class USlider;
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

	/** The button's label widget. */
	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> Text;

	/** Label text, also used for lookups. */
	FText Label;
	/** Run when the button is pressed. */
	TFunction<void()> Action;
	/** Last applied highlight state: -1 unknown (forces the first update), 0 off, 1 on. */
	int8 bHighlighted = -1;
};

/**
 * The pause menu widget, created by AMetalPlayerController::OpenPauseMenu. Its whole widget tree is built
 * in BuildTree(): a dimmed backdrop, a title and a widget switcher with the main panel (index 0), the
 * Level Select panel (index 1), the Controls panel (index 2), the Sound panel (index 3) and the Credits
 * panel (index 4). AMetalTitlePlayerController uses it too,
 * with bTitleMode set.
 */
UCLASS()
class UMetalPauseMenu : public UUserWidget
{
	GENERATED_BODY()

public:
	/**
	 * Title-screen mode: Start Mission / Level Select / Controls / Quit over the title backdrop, and Back on
	 * the main panel does nothing. Set before the widget is added to the viewport.
	 */
	bool bTitleMode = false;

	/** Shows the main buttons and focuses the first one. */
	void ShowMainPanel();

	/** Shows the controls reference and its Back button. */
	void ShowControlsPanel();

	/** Shows the level list and focuses the first entry. */
	void ShowLevelPanel();

	/** @return True while the Level Select panel is showing. */
	bool IsShowingLevels() const;

	/** @return True while the Controls panel is showing. */
	bool IsShowingControls() const;

	/** Shows the two volume sliders and focuses the first. */
	void ShowSoundPanel();

	/** @return True while the Sound panel is showing. */
	bool IsShowingSound() const;

	/** Shows the credits and their Back button. */
	void ShowCreditsPanel();

	/** @return True while the Credits panel is showing. */
	bool IsShowingCredits() const;

	/**
	 * Moves a volume slider, exactly as pressing left/right on it would. For the dev autopilot.
	 *
	 * @param Group Which slider.
	 * @param Delta Change, e.g. -0.3 for 30% quieter.
	 */
	void StepVolume(EMetalVolume Group, float Delta);

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
	/**
	 * Adds a labelled volume slider row (label, slider, percentage) to the Sound panel.
	 *
	 * @param Panel Sound panel.
	 * @param Label Row label.
	 * @param Group Volume setting the slider shows and changes.
	 */
	void AddVolumeRow(UVerticalBox* Panel, const FText& Label, EMetalVolume Group);

	/** OnValueChanged of the music slider. */
	UFUNCTION()
	void HandleMusicChanged(float Value);

	/** OnValueChanged of the effects slider. */
	UFUNCTION()
	void HandleEffectsChanged(float Value);

	/** Saves and applies a slider's new value, refreshes its percentage and ticks when it crosses a 5% step. */
	void OnVolumeChanged(EMetalVolume Group, float Value);

	/** Highlights the focused slider row; hovering a row gives its slider focus, like the buttons. */
	void UpdateSliderRows();

	/** Constructs the backdrop, title, the panels and their buttons. Level buttons come from UMetalLevelSettings. */
	void BuildTree();

	/**
	 * Creates a menu button at the bottom of a panel.
	 *
	 * @param Panel  Panel to add it to.
	 * @param List   Button list for that panel (MainButtons, LevelButtons or ControlsButtons).
	 * @param Label  Button text.
	 * @param Action Run when pressed.
	 * @return The new button.
	 */
	UMetalMenuButton* AddButton(UVerticalBox* Panel, TArray<TObjectPtr<UMetalMenuButton>>& List, const FText& Label, TFunction<void()> Action);

	/** From any sub-panel, returns to the main panel; from the main panel, closes the menu (except in title mode). */
	void Back();

	/** Gives keyboard focus to the first button on the visible panel. */
	void FocusFirstButton();

	/** @return The buttons on the visible panel. */
	const TArray<TObjectPtr<UMetalMenuButton>>& VisibleButtons() const;

	/** Heading above the panels. */
	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> Title;

	/** Shows one panel at a time: main, Level Select or Controls. */
	UPROPERTY(Transient)
	TObjectPtr<UWidgetSwitcher> Switcher;

	/** Buttons on the main panel, top to bottom. */
	UPROPERTY(Transient)
	TArray<TObjectPtr<UMetalMenuButton>> MainButtons;

	/** Buttons on the Level Select panel. */
	UPROPERTY(Transient)
	TArray<TObjectPtr<UMetalMenuButton>> LevelButtons;

	/** Buttons on the Controls panel. */
	UPROPERTY(Transient)
	TArray<TObjectPtr<UMetalMenuButton>> ControlsButtons;

	/** Buttons on the Sound panel (just Back; the sliders are separate). */
	UPROPERTY(Transient)
	TArray<TObjectPtr<UMetalMenuButton>> SoundButtons;

	/** Buttons on the Credits panel. */
	UPROPERTY(Transient)
	TArray<TObjectPtr<UMetalMenuButton>> CreditsButtons;

	/** The volume sliders, indexed by EMetalVolume. */
	UPROPERTY(Transient)
	TArray<TObjectPtr<USlider>> Sliders;

	/** Each slider's row background, for the highlight. */
	UPROPERTY(Transient)
	TArray<TObjectPtr<UBorder>> SliderRows;

	/** Each slider's label. */
	UPROPERTY(Transient)
	TArray<TObjectPtr<UTextBlock>> SliderLabels;

	/** Each slider's percentage text. */
	UPROPERTY(Transient)
	TArray<TObjectPtr<UTextBlock>> SliderValues;

	/** Last 5% step each slider was at, so dragging ticks once per step rather than every frame. */
	TArray<int32> SliderSteps;
};
