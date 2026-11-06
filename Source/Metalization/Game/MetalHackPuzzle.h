// The hack puzzle: a joke "Blueprint editor" window the player rewires to finish a Hack Tether hijack.
//
// Hacking an enemy opens a window with a tab named BP_<Unit>Controller and a tiny graph:
//
//   [ Event: On Target ] -> [ Get Value: team ] -> [ Switch Team ]
//   [           target ] -> [ target     team ] -> [ team    red ] -> [ Call Ceasefire ]
//                                                  [        blue ] -> [ Call Shoot ]
//
// The nodes start in a random, tangled arrangement. The player drags nodes around and drags wires
// between pins; the hack succeeds when the graph is exactly the original with red and blue swapped
// (red -> Call Shoot, blue -> Call Ceasefire). After the first edit a "Save & Compile" button appears
// next to the tab; it (or Ctrl+S) ends the puzzle. The [X] cancels.
//
// It is not a real Blueprint: everything is drawn in NativePaint, and the world keeps running meanwhile.
// Built in code like the pause menu, so there are no widget assets. Gamepads drive a virtual cursor with
// the left stick (A grab/drop, X break links, Y compile, B cancel). AMetalPlayerController opens and closes it.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "MetalHackPuzzle.generated.h"

/** How a hack puzzle ended. */
enum class EMetalHackResult : uint8
{
	/** Compiled with the right wiring: the hijack goes ahead. */
	Success,
	/** Compiled with the wrong wiring: the target puts up a firewall. */
	Failed,
	/** Closed with [X] (or Escape / B), or abandoned because the target was lost. No penalty. */
	Cancelled
};

/** Kind of value a pin carries; only pins of the same kind can be wired together. */
enum class EMetalHackPinType : uint8
{
	/** Execution flow (white arrows). */
	Exec,
	/** A robot reference (blue). */
	Object,
	/** A team value (green). */
	Team
};

/** One node box of the fake graph. */
struct FMetalHackNode
{
	/** Title shown in the header ("Switch Team"). */
	FString Title;
	/** Header colour, after the real editor's node categories (events red, getters green, calls blue). */
	FLinearColor HeaderColor;
	/** Top-left corner in graph units. */
	FVector2f Position = FVector2f::ZeroVector;
	/** Width in graph units. */
	float Width = 170.f;
	/** Pin rows under the header. */
	int32 Rows = 1;
};

/** One pin on a node. */
struct FMetalHackPin
{
	/** Index of the owning node. */
	int32 Node = 0;
	/** Output (right edge) or input (left edge). */
	bool bOutput = false;
	/** What it carries. */
	EMetalHackPinType Type = EMetalHackPinType::Exec;
	/** Label next to the pin ("red", "target"); empty for plain exec pins. */
	FString Label;
	/** Row under the node header. */
	int32 Row = 0;
};

/**
 * Full-screen widget showing the fake Blueprint window. Reports the result once through the callback given
 * to Init; the owner then removes it.
 *
 * @see AMetalPlayerController::OpenHackPuzzle for what each result does.
 */
UCLASS()
class UMetalHackPuzzle : public UUserWidget
{
	GENERATED_BODY()

public:
	/**
	 * Builds the graph in a random tangled layout. Call once, before adding the widget to the viewport.
	 *
	 * @param InBlueprintName Tab title, e.g. "BP_ShooterController".
	 * @param Seed            Seeds the layout.
	 * @param InOnFinished    Called once with the result; the widget does nothing after that.
	 */
	void Init(const FString& InBlueprintName, int32 Seed, TFunction<void(EMetalHackResult)> InOnFinished);

	/** Save & Compile: finishes with Success if the wiring is right, otherwise Failed. */
	void Compile();

	/** Closes the window without compiling (the [X]). */
	void Cancel();

	/** @return True if the current wiring is the solution. */
	bool IsSolved() const;

	/** @return True once the player has changed anything (the Save & Compile button is showing). */
	bool IsDirty() const { return bDirty; }

	/** @return The tab title. */
	const FString& GetBlueprintName() const { return BlueprintName; }

	/** @return The wiring as text ("red->Call Shoot, ..."), for logs. */
	FString DescribeLinks() const;

	/**
	 * Dev autopilot shortcut: rewires the graph as a player would, then leaves it for Compile().
	 *
	 * @param bCorrect True to make the winning swap, false to make a wrong edit (red and blue both to Call Shoot).
	 */
	void DevRewire(bool bCorrect);

protected:
	/** Makes the widget focusable and hit-testable so it gets keys, mouse and gamepad. */
	virtual void NativeConstruct() override;

	/** Moves the gamepad cursor with the left stick. */
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;

	/** Draws the window, grid, wires, nodes and cursor. */
	virtual int32 NativePaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect,
		FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const override;

	virtual FReply NativeOnMouseButtonDown(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent) override;
	virtual FReply NativeOnMouseButtonDoubleClick(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent) override;
	virtual FReply NativeOnMouseButtonUp(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent) override;
	virtual FReply NativeOnMouseMove(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent) override;

	/** Ctrl+S compiles, Escape cancels; gamepad A/X/Y/B act on the virtual cursor. */
	virtual FReply NativeOnKeyDown(const FGeometry& InGeometry, const FKeyEvent& InKeyEvent) override;
	virtual FReply NativeOnKeyUp(const FGeometry& InGeometry, const FKeyEvent& InKeyEvent) override;

	/** Records the left stick for the virtual cursor (and keeps Slate from treating it as navigation). */
	virtual FReply NativeOnAnalogValueChanged(const FGeometry& InGeometry, const FAnalogInputEvent& InAnalogEvent) override;

private:
	/** Fills Nodes and Pins and wires the original (wrong) graph. */
	void BuildGraph();

	/** Places the nodes at random, keeping the most tangled of many tries. */
	void Scramble(int32 Seed);

	/** @return How tangled the current layout is (crossing wires plus wires running backwards), or -1 if nodes overlap too much. */
	int32 TangleScore() const;

	/**
	 * @param InGeometry The widget's geometry.
	 * @return Geometry of the window, in window units (the window is centred and scaled to fit).
	 */
	FGeometry MakeWindowGeometry(const FGeometry& InGeometry) const;

	/** @return A pin's centre in graph units (origin at the graph area's top-left). */
	FVector2f PinPosition(int32 Pin) const;

	/** @return The node's height in graph units. */
	float NodeHeight(int32 Node) const;

	/** @return The topmost pin under a graph point, or INDEX_NONE (nodes on top hide the pins under them). */
	int32 PinAt(const FVector2f& Point) const;

	/** @return The topmost node under a graph point, or INDEX_NONE. */
	int32 NodeAt(const FVector2f& Point) const;

	/** @return True if a wire may join these two pins. */
	bool CanConnect(int32 A, int32 B) const;

	/** Wires two pins, replacing whatever the input (and an exec output) was wired to. */
	void Connect(int32 A, int32 B);

	/** Removes every wire on a pin. */
	void BreakLinks(int32 Pin);

	/** Marks the graph changed, which shows the Save & Compile button. */
	void MarkDirty();

	/** Calls the result callback once. */
	void Finish(EMetalHackResult Result);

	/** Pointer pressed (mouse button or gamepad A) at the current pointer position. */
	void PointerDown(bool bBreak);

	/** Pointer released: drops a dragged wire or node. */
	void PointerUp();

	/** Moves the pointer (window units) and drags whatever is held. */
	void PointerMoved(const FVector2f& WindowPoint);

	/** Rectangles of the window's buttons, in window units. */
	FSlateRect TabRect() const;
	FSlateRect SaveRect() const;
	FSlateRect CloseRect() const;
	FSlateRect GraphRect() const;

	/** Nodes, in a fixed order (see the .cpp). */
	TArray<FMetalHackNode> Nodes;
	/** Pins of all nodes, in a fixed order (see the .cpp). */
	TArray<FMetalHackPin> Pins;
	/** Wires: X is the output pin, Y the input pin. */
	TArray<FIntPoint> Links;
	/** Node indices back to front; the last one is drawn on top. */
	TArray<int32> DrawOrder;

	/** Tab title. */
	FString BlueprintName;
	/** Result callback. */
	TFunction<void(EMetalHackResult)> OnFinished;

	/** Pointer in window units (mouse or virtual cursor). */
	FVector2f Pointer = FVector2f(560.f, 330.f);
	/** Left stick, for the virtual cursor. */
	FVector2f Stick = FVector2f::ZeroVector;
	/** Grab offset from the dragged node's corner. */
	FVector2f GrabOffset = FVector2f::ZeroVector;
	/** Where the dragged node started, to tell a click from a move. */
	FVector2f GrabStart = FVector2f::ZeroVector;
	/** Node being dragged, or INDEX_NONE. */
	int32 DragNode = INDEX_NONE;
	/** Pin a wire is being dragged from, or INDEX_NONE. */
	int32 DragPin = INDEX_NONE;
	/** Something was changed, so Save & Compile shows. */
	bool bDirty = false;
	/** The last input came from a gamepad: draw the virtual cursor and gamepad hints. */
	bool bGamepad = false;
	/** The result has been reported. */
	bool bFinished = false;
};
