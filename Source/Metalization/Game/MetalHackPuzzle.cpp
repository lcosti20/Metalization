#include "MetalHackPuzzle.h"

#include "MetalAudio.h"
#include "Brushes/SlateColorBrush.h"
#include "Brushes/SlateRoundedBoxBrush.h"
#include "Fonts/FontMeasure.h"
#include "Framework/Application/SlateApplication.h"
#include "Layout/Clipping.h"
#include "Rendering/DrawElements.h"
#include "Rendering/SlateRenderer.h"
#include "Styling/CoreStyle.h"

namespace
{
	// Window layout, in window units (1 unit = 1 px at 1080p).
	constexpr float WinW = 1120.f;
	constexpr float WinH = 660.f;
	constexpr float TabBarH = 38.f;
	constexpr float StatusH = 30.f;
	/** Size of the graph area between the tab bar and the status bar. */
	const FVector2f GraphSize(WinW, WinH - TabBarH - StatusH);

	// Node layout, in graph units.
	constexpr float HeaderH = 28.f;
	constexpr float RowH = 26.f;
	constexpr float BodyPad = 6.f;
	constexpr float PinInset = 14.f;
	constexpr float PinHitRadius = 13.f;
	/** A press and release closer than this (window units) is a click, which holds the wire for a second click. */
	constexpr float ClickSlop = 6.f;

	/** Node indices (the order BuildGraph adds them in). */
	enum ENode : int32 { NEvent, NGet, NSwitch, NCeasefire, NShoot };
	/** Pin indices (the order BuildGraph adds them in). */
	enum EPin : int32 { PEventExec, PEventTarget, PGetTarget, PGetTeam, PSwitchExec, PSwitchTeam, PSwitchRed, PSwitchBlue, PCeasefire, PShoot };

	/** The winning wiring: the original graph with the red and blue outputs swapped. */
	const FIntPoint Solution[] = {{PEventExec, PSwitchExec}, {PEventTarget, PGetTarget}, {PGetTeam, PSwitchTeam}, {PSwitchRed, PShoot}, {PSwitchBlue, PCeasefire}};

	// Palette, after the real editor's dark theme.
	const FLinearColor Frame(0.22f, 0.22f, 0.24f);
	const FLinearColor TabBar(0.02f, 0.02f, 0.022f);
	const FLinearColor TabFill(0.075f, 0.075f, 0.08f);
	const FLinearColor GraphFill(0.045f, 0.045f, 0.05f);
	const FLinearColor GridMinor(0.065f, 0.065f, 0.07f);
	const FLinearColor GridMajor(0.025f, 0.025f, 0.028f);
	const FLinearColor StatusFill(0.03f, 0.03f, 0.033f);
	const FLinearColor BlueprintBlue(0.05f, 0.45f, 1.f);
	/** Save & Compile, matching the HUD and pause menu amber. */
	const FLinearColor Amber(1.f, 0.65f, 0.15f);
	const FLinearColor CloseHover(0.8f, 0.12f, 0.08f);
	const FLinearColor TextDim(0.62f, 0.62f, 0.66f);

	/** @return Pin and wire colour for a value kind. */
	FLinearColor PinColor(EMetalHackPinType Type)
	{
		switch (Type)
		{
		case EMetalHackPinType::Object: return FLinearColor(0.05f, 0.45f, 1.f);
		case EMetalHackPinType::Team:   return FLinearColor(0.f, 0.72f, 0.5f);
		default:                        return FLinearColor(0.92f, 0.92f, 0.92f);
		}
	}

	const FSlateBrush* FlatBrush()
	{
		static const FSlateColorBrush Brush(FLinearColor::White);
		return &Brush;
	}

	const FSlateBrush* RoundBrush()
	{
		static const FSlateRoundedBoxBrush Brush(FLinearColor::White, 6.f);
		return &Brush;
	}

	/** Rounded on top only: node headers and tabs. */
	const FSlateBrush* TopRoundBrush()
	{
		static const FSlateRoundedBoxBrush Brush(FLinearColor::White, FVector4(6.f, 6.f, 0.f, 0.f));
		return &Brush;
	}

	/** Fully rounded: a circle on a square box. */
	const FSlateBrush* CircleBrush()
	{
		static const FSlateRoundedBoxBrush Brush(FLinearColor::White, 7.f);
		return &Brush;
	}

	const FSlateBrush* SmallCircleBrush()
	{
		static const FSlateRoundedBoxBrush Brush(FLinearColor::White, 3.5f);
		return &Brush;
	}

	FSlateFontInfo Font(const ANSICHAR* Style, int32 Size)
	{
		return FCoreStyle::GetDefaultFontStyle(Style, Size);
	}

	/** @return Size of the text in local units. */
	FVector2f MeasureText(const FString& Text, const FSlateFontInfo& FontInfo)
	{
		if (FSlateApplication::IsInitialized() && FSlateApplication::Get().GetRenderer())
		{
			const FVector2f Size = FSlateApplication::Get().GetRenderer()->GetFontMeasureService()->Measure(Text, FontInfo);
			return Size;
		}
		return FVector2f(Text.Len() * FontInfo.Size * 0.75f, FontInfo.Size * 1.4f);
	}

	void DrawBox(FSlateWindowElementList& Out, int32 Layer, const FGeometry& Geometry, const FVector2f& Position, const FVector2f& Size,
		const FLinearColor& Color, const FSlateBrush* Brush = FlatBrush())
	{
		FSlateDrawElement::MakeBox(Out, Layer, Geometry.ToPaintGeometry(Size, FSlateLayoutTransform(Position)), Brush, ESlateDrawEffect::None, Color);
	}

	void DrawString(FSlateWindowElementList& Out, int32 Layer, const FGeometry& Geometry, const FVector2f& Position, const FString& Text,
		const FSlateFontInfo& FontInfo, const FLinearColor& Color)
	{
		FSlateDrawElement::MakeText(Out, Layer, Geometry.ToPaintGeometry(FVector2f(1000.f, 60.f), FSlateLayoutTransform(Position)), Text, FontInfo,
			ESlateDrawEffect::None, Color);
	}

	/** Text centred on a point. */
	void DrawStringCentered(FSlateWindowElementList& Out, int32 Layer, const FGeometry& Geometry, const FVector2f& Center, const FString& Text,
		const FSlateFontInfo& FontInfo, const FLinearColor& Color)
	{
		DrawString(Out, Layer, Geometry, Center - MeasureText(Text, FontInfo) * 0.5f, Text, FontInfo, Color);
	}

	void DrawPolyline(FSlateWindowElementList& Out, int32 Layer, const FGeometry& Geometry, TArray<FVector2f> Points, const FLinearColor& Color, float Thickness)
	{
		FSlateDrawElement::MakeLines(Out, Layer, Geometry.ToPaintGeometry(), MoveTemp(Points), ESlateDrawEffect::None, Color, true, Thickness);
	}

	/** A graph wire: a horizontal-tangent spline, which loops back like the real editor's when it runs right to left. */
	void DrawWire(FSlateWindowElementList& Out, int32 Layer, const FGeometry& Geometry, const FVector2f& Start, const FVector2f& End,
		const FLinearColor& Color, float Thickness)
	{
		const float Tangent = FMath::Clamp(FMath::Abs(End.X - Start.X) * 1.1f + FMath::Abs(End.Y - Start.Y) * 0.3f, 90.f, 520.f);
		FSlateDrawElement::MakeSpline(Out, Layer, Geometry.ToPaintGeometry(), Start, FVector2f(Tangent, 0.f), End, FVector2f(Tangent, 0.f), Thickness,
			ESlateDrawEffect::None, Color);
	}

	bool Inside(const FSlateRect& Rect, const FVector2f& Point)
	{
		return Point.X >= Rect.Left && Point.X <= Rect.Right && Point.Y >= Rect.Top && Point.Y <= Rect.Bottom;
	}

}

// --------------------------------------------------------------------------------------------
// Graph
// --------------------------------------------------------------------------------------------

void UMetalHackPuzzle::Init(const FString& InBlueprintName, TFunction<void(EMetalHackResult)> InOnFinished)
{
	BlueprintName = InBlueprintName;
	OnFinished = MoveTemp(InOnFinished);
	BuildGraph();
	Pointer = FVector2f(WinW * 0.5f, WinH * 0.5f);
}

void UMetalHackPuzzle::BuildGraph()
{
	// Header colours follow the real editor: events red, pure getters green, flow control grey, calls blue.
	// Fixed, tidy layout, centred in the graph and reading left to right: the event and Switch Team share a row
	// (a straight exec wire), the getter sits just under the gap between them, and the two calls stack on the right.
	Nodes = {
		{TEXT("Event: On Target"), FLinearColor(0.55f, 0.06f, 0.05f), FVector2f(105.f, 230.f), 190.f, 2},
		{TEXT("Get Value: team"), FLinearColor(0.16f, 0.4f, 0.1f), FVector2f(345.f, 300.f), 180.f, 1},
		{TEXT("Switch Team"), FLinearColor(0.32f, 0.32f, 0.34f), FVector2f(585.f, 230.f), 170.f, 2},
		{TEXT("Call Ceasefire"), FLinearColor(0.06f, 0.25f, 0.55f), FVector2f(845.f, 190.f), 170.f, 1},
		{TEXT("Call Shoot"), FLinearColor(0.06f, 0.25f, 0.55f), FVector2f(845.f, 330.f), 150.f, 1},
	};
	using EType = EMetalHackPinType;
	Pins = {
		{NEvent, true, EType::Exec, TEXT(""), 0},
		{NEvent, true, EType::Object, TEXT("target"), 1},
		{NGet, false, EType::Object, TEXT("target"), 0},
		{NGet, true, EType::Team, TEXT("team"), 0},
		{NSwitch, false, EType::Exec, TEXT(""), 0},
		{NSwitch, false, EType::Team, TEXT("team"), 1},
		{NSwitch, true, EType::Exec, TEXT("red"), 0},
		{NSwitch, true, EType::Exec, TEXT("blue"), 1},
		{NCeasefire, false, EType::Exec, TEXT(""), 0},
		{NShoot, false, EType::Exec, TEXT(""), 0},
	};
	// The robot's own logic: hold fire on red (its own side), shoot blue.
	Links = {{PEventExec, PSwitchExec}, {PEventTarget, PGetTarget}, {PGetTeam, PSwitchTeam}, {PSwitchRed, PCeasefire}, {PSwitchBlue, PShoot}};
}

float UMetalHackPuzzle::NodeHeight(int32 Node) const
{
	return HeaderH + BodyPad + Nodes[Node].Rows * RowH + BodyPad;
}

FVector2f UMetalHackPuzzle::PinPosition(int32 Pin) const
{
	const FMetalHackPin& P = Pins[Pin];
	const FMetalHackNode& N = Nodes[P.Node];
	const float Y = N.Position.Y + HeaderH + BodyPad + P.Row * RowH + RowH * 0.5f;
	return FVector2f(P.bOutput ? N.Position.X + N.Width - PinInset : N.Position.X + PinInset, Y);
}

int32 UMetalHackPuzzle::PinAt(const FVector2f& Point) const
{
	// The nodes never overlap, so the nearest pin in reach is the one.
	int32 Best = INDEX_NONE;
	float BestDistance = PinHitRadius;
	for (int32 Pin = 0; Pin < Pins.Num(); ++Pin)
	{
		const float Distance = FVector2f::Distance(PinPosition(Pin), Point);
		if (Distance <= BestDistance)
		{
			Best = Pin;
			BestDistance = Distance;
		}
	}
	return Best;
}

bool UMetalHackPuzzle::CanConnect(int32 A, int32 B) const
{
	return Pins.IsValidIndex(A) && Pins.IsValidIndex(B) && Pins[A].Node != Pins[B].Node && Pins[A].bOutput != Pins[B].bOutput
		&& Pins[A].Type == Pins[B].Type;
}

void UMetalHackPuzzle::Connect(int32 A, int32 B)
{
	const int32 Out = Pins[A].bOutput ? A : B;
	const int32 In = Pins[A].bOutput ? B : A;
	// Inputs take one wire, and so do exec outputs (flow goes one way); data outputs can feed many inputs.
	Links.RemoveAll([Out, In, this](const FIntPoint& Link) { return Link.Y == In || (Pins[Out].Type == EMetalHackPinType::Exec && Link.X == Out); });
	Links.Add(FIntPoint(Out, In));
	MarkDirty();
}

void UMetalHackPuzzle::BreakLinks(int32 Pin)
{
	if (Links.RemoveAll([Pin](const FIntPoint& Link) { return Link.X == Pin || Link.Y == Pin; }) > 0)
	{
		MarkDirty();
		MetalAudio::Play2D(this, TEXT("ui_move"), 0.5f);
	}
}

void UMetalHackPuzzle::MarkDirty()
{
	bDirty = true;
}

bool UMetalHackPuzzle::IsSolved() const
{
	if (Links.Num() != UE_ARRAY_COUNT(Solution))
	{
		return false;
	}
	for (const FIntPoint& Link : Solution)
	{
		if (!Links.Contains(Link))
		{
			return false;
		}
	}
	return true;
}

FString UMetalHackPuzzle::DescribeLinks() const
{
	auto PinName = [this](int32 Pin)
	{
		return Nodes[Pins[Pin].Node].Title + TEXT(".") + (Pins[Pin].Label.IsEmpty() ? FString(TEXT("exec")) : Pins[Pin].Label);
	};
	TArray<FString> Parts;
	for (const FIntPoint& Link : Links)
	{
		Parts.Add(PinName(Link.X) + TEXT(" -> ") + PinName(Link.Y));
	}
	return FString::Join(Parts, TEXT(", "));
}

void UMetalHackPuzzle::DevRewire(bool bCorrect)
{
	// The player's moves: red onto Call Shoot (which unhooks blue there and red from Call Ceasefire), then blue
	// onto Call Ceasefire. The wrong version stops after the first move and leaves blue dangling.
	Connect(PSwitchRed, PShoot);
	if (bCorrect)
	{
		Connect(PSwitchBlue, PCeasefire);
	}
}

// --------------------------------------------------------------------------------------------
// Results
// --------------------------------------------------------------------------------------------

void UMetalHackPuzzle::Compile()
{
	// Compiling with a wire in hand (Ctrl+S, Y) puts a picked-up wire back first.
	DropWire(INDEX_NONE);
	Finish(IsSolved() ? EMetalHackResult::Success : EMetalHackResult::Failed);
}

void UMetalHackPuzzle::Cancel()
{
	Finish(EMetalHackResult::Cancelled);
}

void UMetalHackPuzzle::Finish(EMetalHackResult Result)
{
	if (bFinished)
	{
		return;
	}
	bFinished = true;
	DragPin = INDEX_NONE;
	bWireSticky = false;
	// The owner removes this widget from inside the callback, so move the callback out first.
	if (TFunction<void(EMetalHackResult)> Callback = MoveTemp(OnFinished))
	{
		Callback(Result);
	}
}

// --------------------------------------------------------------------------------------------
// Layout
// --------------------------------------------------------------------------------------------

FGeometry UMetalHackPuzzle::MakeWindowGeometry(const FGeometry& InGeometry) const
{
	// Authored for 1080p; shrinks on smaller screens and always leaves a margin round the window.
	const FVector2f Local = InGeometry.GetLocalSize();
	const float Scale = FMath::Max(0.3f, FMath::Min3(Local.X * 0.94f / WinW, Local.Y * 0.88f / WinH, Local.Y / 1080.f));
	const FVector2f Offset = (Local - FVector2f(WinW, WinH) * Scale) * 0.5f;
	return InGeometry.MakeChild(FVector2f(WinW, WinH), FSlateLayoutTransform(Scale, Offset));
}

FSlateRect UMetalHackPuzzle::TabRect() const
{
	const float TextW = MeasureText(BlueprintName + TEXT("*"), Font("Regular", 10)).X;
	return FSlateRect(8.f, 6.f, 8.f + 36.f + TextW + 16.f, TabBarH);
}

FSlateRect UMetalHackPuzzle::SaveRect() const
{
	const float Left = TabRect().Right + 12.f;
	return FSlateRect(Left, 8.f, Left + 160.f, TabBarH - 5.f);
}

FSlateRect UMetalHackPuzzle::CloseRect() const
{
	return FSlateRect(WinW - 40.f, 5.f, WinW - 6.f, TabBarH - 5.f);
}

FSlateRect UMetalHackPuzzle::GraphRect() const
{
	return FSlateRect(0.f, TabBarH, WinW, WinH - StatusH);
}

// --------------------------------------------------------------------------------------------
// Input
// --------------------------------------------------------------------------------------------

void UMetalHackPuzzle::NativeConstruct()
{
	Super::NativeConstruct();
	SetIsFocusable(true);
	SetVisibility(ESlateVisibility::Visible);
}

void UMetalHackPuzzle::PointerDown(bool bBreak)
{
	if (bFinished)
	{
		return;
	}
	PressPointer = Pointer;
	if (Inside(CloseRect(), Pointer))
	{
		Cancel();
		return;
	}
	if (bDirty && Inside(SaveRect(), Pointer))
	{
		Compile();
		return;
	}
	const FSlateRect Graph = GraphRect();
	const int32 Pin = Inside(Graph, Pointer) ? PinAt(Pointer - FVector2f(Graph.Left, Graph.Top)) : INDEX_NONE;

	// Second click of click-to-connect: join the held wire to this pin. Clicking empty space or the pin the
	// wire came from just lets it go; clicking another pin it can't join lets it go and starts from that pin.
	if (bWireSticky)
	{
		const bool bJoins = CanConnect(DragPin, Pin);
		const bool bOwnPin = Pin == DragPin || Pin == DetachedLink.Y;
		DropWire(Pin);
		if (bJoins || bOwnPin || Pin == INDEX_NONE || bBreak)
		{
			return;
		}
	}
	if (Pin == INDEX_NONE)
	{
		return;
	}
	if (bBreak)
	{
		BreakLinks(Pin);
		return;
	}
	PickUpWire(Pin);
}

void UMetalHackPuzzle::PickUpWire(int32 Pin)
{
	DetachedLink = FIntPoint(INDEX_NONE, INDEX_NONE);
	DragPin = Pin;
	bWireSticky = false;
	if (!Pins[Pin].bOutput)
	{
		// Grabbing a wired input picks that wire up by its end, so it can be moved to another pin.
		const int32 Index = Links.IndexOfByPredicate([Pin](const FIntPoint& Link) { return Link.Y == Pin; });
		if (Index != INDEX_NONE)
		{
			DetachedLink = Links[Index];
			DragPin = DetachedLink.X;
			Links.RemoveAt(Index);
		}
	}
	MetalAudio::Play2D(this, TEXT("ui_move"), 0.35f);
}

void UMetalHackPuzzle::DropWire(int32 Target)
{
	if (DragPin == INDEX_NONE)
	{
		return;
	}
	if (CanConnect(DragPin, Target))
	{
		Connect(DragPin, Target);
		MetalAudio::Play2D(this, TEXT("ui_move"), 0.6f);
	}
	else if (DetachedLink.X != INDEX_NONE)
	{
		// Let go over nothing: a picked-up wire snaps back rather than vanishing (Alt+click / X deletes wires).
		Links.Add(DetachedLink);
	}
	DetachedLink = FIntPoint(INDEX_NONE, INDEX_NONE);
	DragPin = INDEX_NONE;
	bWireSticky = false;
}

void UMetalHackPuzzle::PointerUp()
{
	if (bFinished || DragPin == INDEX_NONE || bWireSticky)
	{
		return;
	}
	if (FVector2f::Distance(Pointer, PressPointer) <= ClickSlop)
	{
		// A click, not a drag: keep holding the wire, and the next click says where it goes.
		bWireSticky = true;
		return;
	}
	const FSlateRect Graph = GraphRect();
	DropWire(PinAt(Pointer - FVector2f(Graph.Left, Graph.Top)));
}

void UMetalHackPuzzle::PointerMoved(const FVector2f& WindowPoint)
{
	Pointer = FVector2f(FMath::Clamp(WindowPoint.X, 0.f, WinW), FMath::Clamp(WindowPoint.Y, 0.f, WinH));
}

FReply UMetalHackPuzzle::NativeOnMouseButtonDown(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent)
{
	if (bFinished)
	{
		return FReply::Handled();
	}
	if (InMouseEvent.GetEffectingButton() == EKeys::RightMouseButton)
	{
		// Right-click lets go of a held wire.
		DropWire(INDEX_NONE);
		return FReply::Handled();
	}
	if (InMouseEvent.GetEffectingButton() != EKeys::LeftMouseButton)
	{
		return FReply::Handled();
	}
	bGamepad = false;
	PointerMoved(FVector2f(MakeWindowGeometry(InGeometry).AbsoluteToLocal(InMouseEvent.GetScreenSpacePosition())));
	PointerDown(InMouseEvent.IsAltDown());
	// Finishing (the [X] or Save & Compile) removes the widget: don't capture the mouse for a dead widget.
	return bFinished ? FReply::Handled() : FReply::Handled().CaptureMouse(TakeWidget());
}

FReply UMetalHackPuzzle::NativeOnMouseButtonDoubleClick(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent)
{
	return NativeOnMouseButtonDown(InGeometry, InMouseEvent);
}

FReply UMetalHackPuzzle::NativeOnMouseButtonUp(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent)
{
	if (InMouseEvent.GetEffectingButton() == EKeys::LeftMouseButton)
	{
		PointerMoved(FVector2f(MakeWindowGeometry(InGeometry).AbsoluteToLocal(InMouseEvent.GetScreenSpacePosition())));
		PointerUp();
	}
	return FReply::Handled().ReleaseMouseCapture();
}

FReply UMetalHackPuzzle::NativeOnMouseMove(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent)
{
	if (!InMouseEvent.GetCursorDelta().IsNearlyZero())
	{
		bGamepad = false;
		PointerMoved(FVector2f(MakeWindowGeometry(InGeometry).AbsoluteToLocal(InMouseEvent.GetScreenSpacePosition())));
	}
	return FReply::Handled();
}

FReply UMetalHackPuzzle::NativeOnKeyDown(const FGeometry& InGeometry, const FKeyEvent& InKeyEvent)
{
	const FKey Key = InKeyEvent.GetKey();
	if (Key == EKeys::Escape || Key == EKeys::Gamepad_FaceButton_Right || Key == EKeys::Gamepad_Special_Right)
	{
		bGamepad = Key.IsGamepadKey();
		// The first press lets go of a held wire; with nothing held it closes the window.
		if (DragPin != INDEX_NONE)
		{
			DropWire(INDEX_NONE);
		}
		else
		{
			Cancel();
		}
		return FReply::Handled();
	}
	if (Key == EKeys::S && InKeyEvent.IsControlDown())
	{
		if (bDirty)
		{
			Compile();
		}
		return FReply::Handled();
	}
	if (Key == EKeys::Gamepad_FaceButton_Top)
	{
		bGamepad = true;
		if (bDirty)
		{
			Compile();
		}
		return FReply::Handled();
	}
	if (Key == EKeys::Gamepad_FaceButton_Bottom)
	{
		bGamepad = true;
		if (!InKeyEvent.IsRepeat())
		{
			PointerDown(false);
		}
		return FReply::Handled();
	}
	if (Key == EKeys::Gamepad_FaceButton_Left)
	{
		bGamepad = true;
		PointerDown(true);
		PointerUp();
		return FReply::Handled();
	}
	// Swallow the rest so nothing reaches the game while the window is up.
	return FReply::Handled();
}

FReply UMetalHackPuzzle::NativeOnKeyUp(const FGeometry& InGeometry, const FKeyEvent& InKeyEvent)
{
	if (InKeyEvent.GetKey() == EKeys::Gamepad_FaceButton_Bottom)
	{
		PointerUp();
	}
	return FReply::Handled();
}

FReply UMetalHackPuzzle::NativeOnAnalogValueChanged(const FGeometry& InGeometry, const FAnalogInputEvent& InAnalogEvent)
{
	const FKey Key = InAnalogEvent.GetKey();
	if (Key == EKeys::Gamepad_LeftX)
	{
		Stick.X = InAnalogEvent.GetAnalogValue();
	}
	else if (Key == EKeys::Gamepad_LeftY)
	{
		Stick.Y = InAnalogEvent.GetAnalogValue();
	}
	return FReply::Handled();
}

void UMetalHackPuzzle::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);
	// Virtual cursor: squared response, so small tilts give fine control and full tilt crosses the window in about a second.
	const float Tilt = Stick.Size();
	if (Tilt > 0.2f && !bFinished)
	{
		bGamepad = true;
		const float Speed = 1100.f * Tilt * Tilt;
		PointerMoved(Pointer + FVector2f(Stick.X, -Stick.Y) / Tilt * Speed * InDeltaTime);
	}
}

// --------------------------------------------------------------------------------------------
// Drawing
// --------------------------------------------------------------------------------------------

int32 UMetalHackPuzzle::NativePaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect,
	FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const
{
	int32 Layer = Super::NativePaint(Args, AllottedGeometry, MyCullingRect, OutDrawElements, LayerId, InWidgetStyle, bParentEnabled) + 1;
	FSlateWindowElementList& Out = OutDrawElements;
	const FGeometry Win = MakeWindowGeometry(AllottedGeometry);
	const FSlateFontInfo TitleFont = Font("Bold", 10);
	const FSlateFontInfo LabelFont = Font("Regular", 9);
	const FSlateFontInfo TabFont = Font("Regular", 10);

	// Light shade over the (still running) game, then the window: drop shadow, frame, tab bar.
	DrawBox(Out, Layer, AllottedGeometry, FVector2f::ZeroVector, AllottedGeometry.GetLocalSize(), FLinearColor(0.f, 0.f, 0.f, 0.2f));
	DrawBox(Out, ++Layer, Win, FVector2f(6.f, 10.f), FVector2f(WinW, WinH), FLinearColor(0.f, 0.f, 0.f, 0.5f), RoundBrush());
	DrawBox(Out, ++Layer, Win, FVector2f(-1.f, -1.f), FVector2f(WinW + 2.f, WinH + 2.f), Frame, RoundBrush());
	DrawBox(Out, ++Layer, Win, FVector2f::ZeroVector, FVector2f(WinW, TabBarH), TabBar, TopRoundBrush());
	++Layer;

	// Tab: blueprint icon, name (with the editor's unsaved-changes star), blue top edge.
	const FSlateRect Tab = TabRect();
	DrawBox(Out, Layer, Win, FVector2f(Tab.Left, Tab.Top), FVector2f(Tab.Right - Tab.Left, Tab.Bottom - Tab.Top), TabFill, TopRoundBrush());
	DrawBox(Out, Layer + 1, Win, FVector2f(Tab.Left + 4.f, Tab.Top), FVector2f(Tab.Right - Tab.Left - 8.f, 2.f), BlueprintBlue);
	DrawBox(Out, Layer + 1, Win, FVector2f(Tab.Left + 12.f, Tab.Top + 8.f), FVector2f(16.f, 16.f), BlueprintBlue, RoundBrush());
	DrawBox(Out, Layer + 2, Win, FVector2f(Tab.Left + 16.f, Tab.Top + 12.f), FVector2f(8.f, 8.f), FLinearColor(0.85f, 0.93f, 1.f), FlatBrush());
	DrawString(Out, Layer + 1, Win, FVector2f(Tab.Left + 36.f, Tab.Top + 6.f), BlueprintName + (bDirty ? TEXT("*") : TEXT("")), TabFont, FLinearColor::White);

	// Save & Compile appears once something has changed.
	if (bDirty)
	{
		const FSlateRect Save = SaveRect();
		const bool bHover = Inside(Save, Pointer);
		DrawBox(Out, Layer, Win, FVector2f(Save.Left, Save.Top), FVector2f(Save.Right - Save.Left, Save.Bottom - Save.Top),
			bHover ? FLinearColor(1.f, 0.78f, 0.35f) : Amber, RoundBrush());
		DrawStringCentered(Out, Layer + 1, Win, FVector2f((Save.Left + Save.Right) * 0.5f, (Save.Top + Save.Bottom) * 0.5f),
			bGamepad ? TEXT("(Y) Save & Compile") : TEXT("Save & Compile"), Font("Bold", 10), FLinearColor(0.04f, 0.03f, 0.02f));
	}

	// [X]: cancels the hack.
	const FSlateRect Close = CloseRect();
	if (Inside(Close, Pointer))
	{
		DrawBox(Out, Layer, Win, FVector2f(Close.Left, Close.Top), FVector2f(Close.Right - Close.Left, Close.Bottom - Close.Top), CloseHover, RoundBrush());
	}
	DrawStringCentered(Out, Layer + 1, Win, FVector2f((Close.Left + Close.Right) * 0.5f, (Close.Top + Close.Bottom) * 0.5f), TEXT("X"), Font("Bold", 12),
		FLinearColor(0.9f, 0.9f, 0.9f));
	Layer += 3;

	// Graph area, clipped so wires that loop out of it don't cover the tab bar.
	const FSlateRect GraphArea = GraphRect();
	const FGeometry Graph = Win.MakeChild(GraphSize, FSlateLayoutTransform(FVector2f(GraphArea.Left, GraphArea.Top)));
	OutDrawElements.PushClip(FSlateClippingZone(Graph));
	DrawBox(Out, Layer++, Graph, FVector2f::ZeroVector, GraphSize, GraphFill);
	for (float X = 0.f; X <= GraphSize.X; X += 16.f)
	{
		const bool bMajor = FMath::IsNearlyZero(FMath::Fmod(X, 128.f));
		DrawPolyline(Out, Layer + (bMajor ? 1 : 0), Graph, {FVector2f(X, 0.f), FVector2f(X, GraphSize.Y)}, bMajor ? GridMajor : GridMinor, 1.f);
	}
	for (float Y = 0.f; Y <= GraphSize.Y; Y += 16.f)
	{
		const bool bMajor = FMath::IsNearlyZero(FMath::Fmod(Y, 128.f));
		DrawPolyline(Out, Layer + (bMajor ? 1 : 0), Graph, {FVector2f(0.f, Y), FVector2f(GraphSize.X, Y)}, bMajor ? GridMajor : GridMinor, 1.f);
	}
	Layer += 2;
	// The editor's watermark, and a comment bubble with the brief.
	const FSlateFontInfo Watermark = Font("Bold", 28);
	DrawString(Out, Layer, Graph, GraphSize - MeasureText(TEXT("BLUEPRINT"), Watermark) - FVector2f(20.f, 14.f), TEXT("BLUEPRINT"), Watermark,
		FLinearColor(1.f, 1.f, 1.f, 0.08f));
	const FString Brief = TEXT("// TODO: shoot RED, spare BLUE. Rewire, then Save & Compile.");
	const FVector2f BriefSize = MeasureText(Brief, LabelFont);
	DrawBox(Out, Layer, Graph, FVector2f(12.f, 10.f), BriefSize + FVector2f(20.f, 10.f), FLinearColor(1.f, 1.f, 1.f, 0.1f), RoundBrush());
	DrawString(Out, Layer + 1, Graph, FVector2f(22.f, 15.f), Brief, LabelFont, FLinearColor(0.85f, 0.85f, 0.85f));
	Layer += 2;

	const FVector2f GraphPointer = Pointer - FVector2f(GraphArea.Left, GraphArea.Top);
	const int32 HoverPin = PinAt(GraphPointer);

	// Wires, behind the nodes as in the real editor.
	for (const FIntPoint& Link : Links)
	{
		const bool bHot = HoverPin == Link.X || HoverPin == Link.Y;
		DrawWire(Out, Layer, Graph, PinPosition(Link.X), PinPosition(Link.Y), PinColor(Pins[Link.X].Type) * (bHot ? 1.f : 0.85f), bHot ? 3.5f : 2.5f);
	}
	if (DragPin != INDEX_NONE)
	{
		const FVector2f PinPoint = PinPosition(DragPin);
		const bool bFromOutput = Pins[DragPin].bOutput;
		const bool bValid = HoverPin == INDEX_NONE || CanConnect(DragPin, HoverPin);
		DrawWire(Out, Layer + 1, Graph, bFromOutput ? PinPoint : GraphPointer, bFromOutput ? GraphPointer : PinPoint,
			bValid ? PinColor(Pins[DragPin].Type) : FLinearColor(0.9f, 0.15f, 0.1f), 3.f);
	}
	Layer += 2;

	// Nodes, each on its own layers.
	for (int32 Node = 0; Node < Nodes.Num(); ++Node)
	{
		const FMetalHackNode& N = Nodes[Node];
		const FVector2f Size(N.Width, NodeHeight(Node));
		DrawBox(Out, Layer + 1, Graph, N.Position, Size, FLinearColor(0.015f, 0.015f, 0.018f, 0.94f), RoundBrush());
		DrawBox(Out, Layer + 2, Graph, N.Position, FVector2f(N.Width, HeaderH), N.HeaderColor, TopRoundBrush());
		DrawString(Out, Layer + 3, Graph, N.Position + FVector2f(10.f, 5.f), N.Title, TitleFont, FLinearColor::White);

		for (int32 Pin = 0; Pin < Pins.Num(); ++Pin)
		{
			const FMetalHackPin& P = Pins[Pin];
			if (P.Node != Node)
			{
				continue;
			}
			const FVector2f C = PinPosition(Pin);
			const FLinearColor Color = PinColor(P.Type);
			const bool bLinked = Links.ContainsByPredicate([Pin](const FIntPoint& Link) { return Link.X == Pin || Link.Y == Pin; });
			if (Pin == HoverPin || Pin == DragPin)
			{
				DrawBox(Out, Layer + 2, Graph, C - FVector2f(10.f, 10.f), FVector2f(20.f, 20.f), FLinearColor(1.f, 1.f, 1.f, 0.18f), CircleBrush());
			}
			else if (CanConnect(DragPin, Pin))
			{
				// While a wire is held, ring every pin it could join, in the wire's colour.
				DrawBox(Out, Layer + 2, Graph, C - FVector2f(10.f, 10.f), FVector2f(20.f, 20.f), Color.CopyWithNewOpacity(0.3f), CircleBrush());
			}
			if (P.Type == EMetalHackPinType::Exec)
			{
				// The editor's exec arrow: an outlined pentagon, filled in when wired.
				DrawPolyline(Out, Layer + 3, Graph, {C + FVector2f(-5.f, -6.f), C + FVector2f(1.f, -6.f), C + FVector2f(6.f, 0.f), C + FVector2f(1.f, 6.f),
					C + FVector2f(-5.f, 6.f), C + FVector2f(-5.f, -6.f)}, Color, 1.5f);
				if (bLinked)
				{
					DrawBox(Out, Layer + 3, Graph, C + FVector2f(-5.f, -6.f), FVector2f(6.f, 12.f), Color);
					DrawPolyline(Out, Layer + 3, Graph, {C + FVector2f(1.f, -4.f), C + FVector2f(4.f, 0.f), C + FVector2f(1.f, 4.f)}, Color, 4.f);
				}
			}
			else
			{
				// Data pins: a ring, filled in when wired.
				DrawBox(Out, Layer + 3, Graph, C - FVector2f(6.f, 6.f), FVector2f(12.f, 12.f), Color, CircleBrush());
				if (!bLinked)
				{
					DrawBox(Out, Layer + 4, Graph, C - FVector2f(3.5f, 3.5f), FVector2f(7.f, 7.f), FLinearColor(0.015f, 0.015f, 0.018f), SmallCircleBrush());
				}
			}
			if (!P.Label.IsEmpty())
			{
				// The switch outputs are named after the teams, so tint them in team colours.
				const FLinearColor LabelColor = P.Label == TEXT("red") ? FLinearColor(1.f, 0.4f, 0.35f)
					: P.Label == TEXT("blue") ? FLinearColor(0.45f, 0.65f, 1.f) : FLinearColor(0.85f, 0.85f, 0.85f);
				const FVector2f LabelSize = MeasureText(P.Label, LabelFont);
				const float X = P.bOutput ? C.X - 12.f - LabelSize.X : C.X + 12.f;
				DrawString(Out, Layer + 3, Graph, FVector2f(X, C.Y - LabelSize.Y * 0.5f), P.Label, LabelFont, LabelColor);
			}
		}
		Layer += 5;
	}
	OutDrawElements.PopClip();

	// Status bar with the controls for whichever device is in use.
	DrawBox(Out, Layer, Win, FVector2f(0.f, WinH - StatusH), FVector2f(WinW, StatusH), StatusFill);
	const FString Hint = bWireSticky
		? (bGamepad ? TEXT("Wire held:  (A) on a ringed pin to connect it     (A) on empty space or (B) to let go")
			: TEXT("Wire held:  click a ringed pin to connect it     Click empty space, right-click or Esc to let go"))
		: bGamepad
		? TEXT("Left stick: cursor     (A) a pin, then (A) another pin to wire them     (X) break a pin's links     (Y) Save & Compile     (B) cancel")
		: TEXT("Click a pin, then another pin to wire them (or drag)     Alt+Click a pin to break its links     Ctrl+S  Save & Compile");
	DrawString(Out, Layer + 1, Win, FVector2f(12.f, WinH - StatusH + 7.f), Hint, LabelFont, TextDim);
	Layer += 2;

	// Gamepad cursor (the mouse has the system one).
	if (bGamepad)
	{
		DrawBox(Out, Layer, Win, Pointer - FVector2f(8.f, 8.f), FVector2f(16.f, 16.f), FLinearColor::White, CircleBrush());
		DrawBox(Out, Layer + 1, Win, Pointer - FVector2f(5.f, 5.f), FVector2f(10.f, 10.f), FLinearColor(0.f, 0.f, 0.f, 0.8f), CircleBrush());
		DrawBox(Out, Layer + 2, Win, Pointer - FVector2f(2.f, 2.f), FVector2f(4.f, 4.f), Amber, SmallCircleBrush());
		Layer += 3;
	}
	return Layer;
}
