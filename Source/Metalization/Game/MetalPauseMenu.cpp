#include "MetalPauseMenu.h"

#include "MetalLevelSettings.h"
#include "MetalPlayerController.h"
#include "MetalTypes.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/Overlay.h"
#include "Components/OverlaySlot.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Components/WidgetSwitcher.h"
#include "InputCoreTypes.h"
#include "Kismet/GameplayStatics.h"
#include "Kismet/KismetSystemLibrary.h"

namespace
{
	// Same palette as the HUD.
	const FLinearColor Amber(1.f, 0.65f, 0.15f);
	const FLinearColor Idle(0.08f, 0.09f, 0.11f, 0.92f);
	const FLinearColor Dim(1.f, 1.f, 1.f, 0.85f);

	/**
	 * @param Color Fill colour.
	 * @return A rounded-rectangle brush with 6 px corners.
	 */
	FSlateBrush RoundedBrush(const FLinearColor& Color)
	{
		FSlateBrush Brush;
		Brush.DrawAs = ESlateBrushDrawType::RoundedBox;
		Brush.TintColor = Color;
		Brush.OutlineSettings.RoundingType = ESlateBrushRoundingType::FixedRadius;
		Brush.OutlineSettings.CornerRadii = FVector4(6.f, 6.f, 6.f, 6.f);
		return Brush;
	}

	/**
	 * @param bHighlighted Selected (amber) or idle (dark).
	 * @return Button style with the same look for normal and hovered, so only focus decides the highlight.
	 */
	FButtonStyle MenuStyle(bool bHighlighted)
	{
		FButtonStyle Style;
		const FLinearColor Base = bHighlighted ? Amber : Idle;
		Style.SetNormal(RoundedBrush(Base));
		Style.SetHovered(RoundedBrush(Base));
		Style.SetPressed(RoundedBrush(bHighlighted ? FLinearColor(1.f, 0.8f, 0.4f) : Base));
		Style.SetNormalPadding(FMargin(0.f));
		Style.SetPressedPadding(FMargin(0.f));
		return Style;
	}

	/**
	 * @param Text Text block to change.
	 * @param Size New font size, keeping the rest of its font.
	 */
	void SetFontSize(UTextBlock* Text, int32 Size)
	{
		FSlateFontInfo Font = Text->GetFont();
		Font.Size = Size;
		Text->SetFont(Font);
	}

	/**
	 * @param Key Key that was pressed.
	 * @return True for the keys that back out of the menu: Escape, P, gamepad Start and B.
	 */
	bool IsBackKey(const FKey& Key)
	{
		return Key == EKeys::Escape || Key == EKeys::P || Key == EKeys::Gamepad_Special_Right || Key == EKeys::Gamepad_FaceButton_Right;
	}
}

// --------------------------------------------------------------------------------------------
// Button
// --------------------------------------------------------------------------------------------

void UMetalMenuButton::Init(UTextBlock* InText, const FText& InLabel, TFunction<void()> InAction)
{
	Label = InLabel;
	Action = MoveTemp(InAction);
	Text = InText;
	Text->SetText(Label);
	Text->SetJustification(ETextJustify::Center);
	SetFontSize(Text, 24);
	SetContent(Text);
	OnClicked.AddDynamic(this, &UMetalMenuButton::HandleClicked);
	OnHovered.AddDynamic(this, &UMetalMenuButton::HandleHovered);
	UpdateHighlight();
}

void UMetalMenuButton::Press() const
{
	if (Action)
	{
		Action();
	}
}

void UMetalMenuButton::HandleClicked()
{
	Press();
}

void UMetalMenuButton::HandleHovered()
{
	// The mouse and the keyboard/gamepad share one selection.
	SetKeyboardFocus();
}

void UMetalMenuButton::UpdateHighlight()
{
	const int8 bNow = (HasKeyboardFocus() || IsHovered()) ? 1 : 0;
	if (bNow == bHighlighted)
	{
		return;
	}
	bHighlighted = bNow;
	SetStyle(MenuStyle(bNow != 0));
	if (Text)
	{
		Text->SetColorAndOpacity(bNow ? FLinearColor(0.05f, 0.05f, 0.05f) : Dim);
	}
}

// --------------------------------------------------------------------------------------------
// Menu
// --------------------------------------------------------------------------------------------

TSharedRef<SWidget> UMetalPauseMenu::RebuildWidget()
{
	if (WidgetTree && WidgetTree->RootWidget == nullptr)
	{
		BuildTree();
	}
	return Super::RebuildWidget();
}

void UMetalPauseMenu::BuildTree()
{
	UOverlay* Root = WidgetTree->ConstructWidget<UOverlay>(UOverlay::StaticClass(), TEXT("Root"));
	WidgetTree->RootWidget = Root;

	// Dim the paused game behind the menu.
	UBorder* Shade = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("Shade"));
	Shade->SetBrushColor(FLinearColor(0.f, 0.f, 0.f, 0.6f));
	UOverlaySlot* ShadeSlot = Root->AddChildToOverlay(Shade);
	ShadeSlot->SetHorizontalAlignment(HAlign_Fill);
	ShadeSlot->SetVerticalAlignment(VAlign_Fill);

	USizeBox* Column = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(), TEXT("Column"));
	Column->SetWidthOverride(520.f);
	UOverlaySlot* ColumnSlot = Root->AddChildToOverlay(Column);
	ColumnSlot->SetHorizontalAlignment(HAlign_Center);
	ColumnSlot->SetVerticalAlignment(VAlign_Center);

	UVerticalBox* Stack = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("Stack"));
	Column->AddChild(Stack);

	Title = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("Title"));
	Title->SetJustification(ETextJustify::Center);
	Title->SetColorAndOpacity(Amber);
	SetFontSize(Title, 44);
	Stack->AddChildToVerticalBox(Title)->SetPadding(FMargin(0.f, 0.f, 0.f, 28.f));

	Switcher = WidgetTree->ConstructWidget<UWidgetSwitcher>(UWidgetSwitcher::StaticClass(), TEXT("Panels"));
	Stack->AddChildToVerticalBox(Switcher);

	// Button actions are lambdas stored on the buttons; they capture the menu weakly so a press after the
	// menu is gone (e.g. mid level travel) does nothing.
	AMetalPlayerController* PC = GetOwningPlayer<AMetalPlayerController>();
	TWeakObjectPtr<UMetalPauseMenu> WeakThis(this);

	UVerticalBox* Main = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("MainPanel"));
	Switcher->AddChild(Main);
	AddButton(Main, MainButtons, NSLOCTEXT("Metalization", "Continue", "Continue"), [PC]() { if (PC) { PC->ClosePauseMenu(); } });
	AddButton(Main, MainButtons, NSLOCTEXT("Metalization", "LevelSelect", "Level Select"), [WeakThis]() { if (WeakThis.IsValid()) { WeakThis->ShowLevelPanel(); } });
	AddButton(Main, MainButtons, NSLOCTEXT("Metalization", "Quit", "Quit"), [PC]() { UKismetSystemLibrary::QuitGame(PC, PC, EQuitPreference::Quit, false); });

	UVerticalBox* Levels = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("LevelPanel"));
	Switcher->AddChild(Levels);
	const FString Current = UGameplayStatics::GetCurrentLevelName(this, true);
	for (const FMetalLevelEntry& Entry : GetDefault<UMetalLevelSettings>()->Levels)
	{
		if (Entry.Map.IsNull())
		{
			continue;
		}
		const bool bCurrent = FPackageName::GetShortName(Entry.Map.ToSoftObjectPath().GetLongPackageName()) == Current;
		const FText Label = bCurrent ? FText::Format(NSLOCTEXT("Metalization", "CurrentLevel", "{0}  (current)"), Entry.DisplayName) : Entry.DisplayName;
		const TSoftObjectPtr<UWorld> Map = Entry.Map;
		AddButton(Levels, LevelButtons, Label, [WeakThis, Map]()
		{
			if (WeakThis.IsValid())
			{
				UE_LOG(LogMetal, Display, TEXT("Level Select: opening %s"), *Map.ToString());
				UGameplayStatics::OpenLevelBySoftObjectPtr(WeakThis.Get(), Map);
			}
		});
	}
	if (LevelButtons.Num() == 0)
	{
		UTextBlock* None = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("NoLevels"));
		None->SetText(NSLOCTEXT("Metalization", "NoLevels", "No levels listed (Project Settings > Game > Metalization Levels)"));
		None->SetJustification(ETextJustify::Center);
		None->SetAutoWrapText(true);
		Levels->AddChildToVerticalBox(None)->SetPadding(FMargin(0.f, 0.f, 0.f, 12.f));
	}
	// A gap, then Back.
	Levels->AddChildToVerticalBox(WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(), TEXT("Gap")))->SetPadding(FMargin(0.f, 10.f));
	AddButton(Levels, LevelButtons, NSLOCTEXT("Metalization", "Back", "Back"), [WeakThis]() { if (WeakThis.IsValid()) { WeakThis->ShowMainPanel(); } });
}

UMetalMenuButton* UMetalPauseMenu::AddButton(UVerticalBox* Panel, TArray<TObjectPtr<UMetalMenuButton>>& List, const FText& Label, TFunction<void()> Action)
{
	UMetalMenuButton* Button = WidgetTree->ConstructWidget<UMetalMenuButton>(UMetalMenuButton::StaticClass());
	Button->Init(WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass()), Label, MoveTemp(Action));
	UVerticalBoxSlot* ButtonSlot = Panel->AddChildToVerticalBox(Button);
	ButtonSlot->SetPadding(FMargin(0.f, 6.f));
	ButtonSlot->SetHorizontalAlignment(HAlign_Fill);
	List.Add(Button);
	return Button;
}

void UMetalPauseMenu::NativeConstruct()
{
	Super::NativeConstruct();
	SetIsFocusable(true);
	ShowMainPanel();
}

void UMetalPauseMenu::ShowMainPanel()
{
	Switcher->SetActiveWidgetIndex(0);
	Title->SetText(NSLOCTEXT("Metalization", "Paused", "PAUSED"));
	FocusFirstButton();
}

void UMetalPauseMenu::ShowLevelPanel()
{
	Switcher->SetActiveWidgetIndex(1);
	Title->SetText(NSLOCTEXT("Metalization", "LevelSelectTitle", "LEVEL SELECT"));
	FocusFirstButton();
}

bool UMetalPauseMenu::IsShowingLevels() const
{
	return Switcher && Switcher->GetActiveWidgetIndex() == 1;
}

const TArray<TObjectPtr<UMetalMenuButton>>& UMetalPauseMenu::VisibleButtons() const
{
	return IsShowingLevels() ? LevelButtons : MainButtons;
}

void UMetalPauseMenu::FocusFirstButton()
{
	if (VisibleButtons().Num() > 0)
	{
		VisibleButtons()[0]->SetKeyboardFocus();
	}
}

bool UMetalPauseMenu::HasButton(const FString& Label) const
{
	return VisibleButtons().ContainsByPredicate([&Label](const UMetalMenuButton* Button) { return Button->GetLabel().ToString() == Label; });
}

bool UMetalPauseMenu::PressButton(const FString& Label)
{
	for (const UMetalMenuButton* Button : VisibleButtons())
	{
		if (Button->GetLabel().ToString() == Label)
		{
			UE_LOG(LogMetal, Display, TEXT("Pause menu: pressed \"%s\""), *Button->GetLabel().ToString());
			Button->Press();
			return true;
		}
	}
	UE_LOG(LogMetal, Warning, TEXT("Pause menu: no \"%s\" button on this panel"), *Label);
	return false;
}

void UMetalPauseMenu::Back()
{
	if (IsShowingLevels())
	{
		ShowMainPanel();
	}
	else if (AMetalPlayerController* PC = GetOwningPlayer<AMetalPlayerController>())
	{
		PC->ClosePauseMenu();
	}
}

FReply UMetalPauseMenu::NativeOnKeyDown(const FGeometry& InGeometry, const FKeyEvent& InKeyEvent)
{
	if (IsBackKey(InKeyEvent.GetKey()))
	{
		Back();
		return FReply::Handled();
	}
	return Super::NativeOnKeyDown(InGeometry, InKeyEvent);
}

void UMetalPauseMenu::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);
	// A button must always be selected: the menu itself takes focus when it opens, and clicking the
	// backdrop drops it, so hand it to the first button whenever none has it.
	if (!HasFocusedDescendants())
	{
		FocusFirstButton();
	}
	for (UMetalMenuButton* Button : MainButtons)
	{
		Button->UpdateHighlight();
	}
	for (UMetalMenuButton* Button : LevelButtons)
	{
		Button->UpdateHighlight();
	}
}
