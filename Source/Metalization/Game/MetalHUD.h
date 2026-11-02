// Canvas HUD for the demo: health, weapon/ammo, crosshair, hacking prompt and timer, objectives,
// the dialogue box and radio icon, and banners. Kept in code so it works without any UMG assets; a UMG version
// can read the same game mode / controller getters.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/HUD.h"
#include "MetalHUD.generated.h"

/**
 * The game's only HUD (AMetalGameMode::HUDClass). Everything is drawn with Canvas calls every frame from
 * state read off the player controller, the possessed robot and the game mode; the HUD itself stores nothing.
 */
UCLASS()
class AMetalHUD : public AHUD
{
	GENERATED_BODY()

public:
	/** Draws the whole HUD for this frame: combat panels, tether timer, prompts, battle status and mission text. */
	virtual void DrawHUD() override;

private:
	/**
	 * Draws a filled bar with a dark background.
	 *
	 * @param X, Y          Top-left corner in pixels.
	 * @param Width, Height Size in pixels.
	 * @param Fraction      Filled share, 0-1.
	 * @param Color         Fill colour.
	 */
	void DrawBar(float X, float Y, float Width, float Height, float Fraction, const FLinearColor& Color);

	/**
	 * Draws text with a one-pixel drop shadow for readability over bright scenes.
	 *
	 * @param Text      String to draw.
	 * @param X, Y      Position in pixels (top-left, or top-centre when bCentered).
	 * @param Color     Text colour.
	 * @param Font      Canvas font.
	 * @param Scale     Text scale.
	 * @param bCentered Centre the text horizontally on X.
	 */
	void DrawTextShadowed(const FString& Text, float X, float Y, const FLinearColor& Color, UFont* Font, float Scale = 1.f, bool bCentered = false);

	/**
	 * Space battles: tickets and both ships' health across the top, markers on the hangar and enemy subsystems.
	 *
	 * @param Battle The active battle mode.
	 * @param Viewer The possessed pawn (decides which ship is "ours").
	 * @param UI     Resolution scale factor (1 at 1080p).
	 */
	void DrawSpaceBattle(class AMetalSpaceBattleGameMode* Battle, const APawn* Viewer, float UI);

	/**
	 * Where to shoot to hit the enemy flyer nearest the crosshair, given the slugs' flight time.
	 *
	 * @param Flyer The player's flyer (its weapon gives the slug speed).
	 * @param UI    Resolution scale factor.
	 */
	void DrawLeadReticle(const class AMetalStalker* Flyer, float UI);

	/**
	 * The dialogue box at the bottom of the screen: speaker name over the word-wrapped line. It slides up
	 * when it opens and stays put across back-to-back lines.
	 *
	 * @param Line     Line on screen.
	 * @param OpenTime Seconds since the box opened.
	 * @param UI       Resolution scale factor.
	 */
	void DrawDialogueBox(const struct FMetalDialogueLine& Line, float OpenTime, float UI);

	/**
	 * A handheld radio with pulsing signal waves above Pawn's head, shown while an NPC talks to the player.
	 *
	 * @param Pawn The pawn the player controls.
	 * @param UI   Resolution scale factor.
	 */
	void DrawRadioIcon(const APawn* Pawn, float UI);

	/**
	 * A world-space marker; pinned to the screen edge with an arrow when bEdgeArrow and off screen.
	 *
	 * @param World      World position to mark.
	 * @param Label      Text under the marker.
	 * @param Color      Marker colour.
	 * @param UI         Resolution scale factor.
	 * @param bEdgeArrow Keep the marker on screen when the point is outside the view.
	 */
	void DrawMarker(const FVector& World, const FString& Label, const FLinearColor& Color, float UI, bool bEdgeArrow);

	/**
	 * Sniper scope: everything outside a circle goes black, with a fine crosshair and a range readout.
	 *
	 * @param Bot   The scoped robot (for the range to the aim point).
	 * @param Alpha 0..1 scope blend.
	 * @param UI    Resolution scale factor.
	 */
	void DrawScope(const class AMetalBotCharacter* Bot, float Alpha, float UI);

	/**
	 * "?" over enemies that are getting suspicious (filling up as they get closer to spotting the player) and
	 * "!" over ones that just spotted them.
	 *
	 * @param Viewer The player's robot.
	 * @param UI     Resolution scale factor.
	 */
	void DrawAwareness(const class AMetalBotCharacter* Viewer, float UI);

	/**
	 * End-of-mission results panel: time, kills, deaths, accuracy and the restart / menu keys.
	 *
	 * @param GameMode The finished mission's game mode.
	 * @param PC       The player (for accuracy).
	 * @param UI       Resolution scale factor.
	 */
	void DrawResults(const class AMetalGameMode* GameMode, const class AMetalPlayerController* PC, float UI);
};
