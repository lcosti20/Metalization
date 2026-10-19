// Canvas HUD for the demo: health, weapon/ammo, crosshair, hacking prompt and timer, objectives,
// dialogue subtitles and banners. Kept in code so it works without any UMG assets; a UMG version
// can read the same game mode / controller getters.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/HUD.h"
#include "MetalHUD.generated.h"

UCLASS()
class AMetalHUD : public AHUD
{
	GENERATED_BODY()

public:
	virtual void DrawHUD() override;

private:
	void DrawBar(float X, float Y, float Width, float Height, float Fraction, const FLinearColor& Color);
	void DrawTextShadowed(const FString& Text, float X, float Y, const FLinearColor& Color, UFont* Font, float Scale = 1.f, bool bCentered = false);
	/** Space battles: tickets and both ships' health across the top, markers on the hangar and enemy subsystems. */
	void DrawSpaceBattle(class AMetalSpaceBattleGameMode* Battle, const APawn* Viewer, float UI);
	/** Where to shoot to hit the enemy flyer nearest the crosshair, given the slugs' flight time. */
	void DrawLeadReticle(const class AMetalStalker* Flyer, float UI);
	/** A world-space marker; pinned to the screen edge with an arrow when bEdgeArrow and off screen. */
	void DrawMarker(const FVector& World, const FString& Label, const FLinearColor& Color, float UI, bool bEdgeArrow);
};
