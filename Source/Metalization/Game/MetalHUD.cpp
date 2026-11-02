#include "MetalHUD.h"

#include "MetalGameMode.h"
#include "MetalBotCharacter.h"
#include "MetalControlConsole.h"
#include "MetalDialogue.h"
#include "MetalBotAIController.h"
#include "MetalMission.h"
#include "MetalPlayerController.h"
#include "MetalCapitalShip.h"
#include "MetalPortal.h"
#include "MetalSpaceBattleGameMode.h"
#include "EngineUtils.h"
#include "MetalStalker.h"
#include "MetalWeaponData.h"
#include "Engine/Canvas.h"
#include "Engine/Engine.h"
#include "Engine/Font.h"

namespace
{
	// HUD palette (the pause menu uses the same amber).
	/** Main accent: names, objectives, prompts. */
	const FLinearColor Amber(1.f, 0.65f, 0.15f);
	/** Hacking, links, sneaking and the boost meter. */
	const FLinearColor Cyan(0.3f, 0.9f, 1.f);
	/** Hit confirmation, low health and warnings. */
	const FLinearColor Danger(1.f, 0.2f, 0.1f);
	/** Translucent backing behind text panels. */
	const FLinearColor Panel(0.f, 0.f, 0.f, 0.55f);
}

void AMetalHUD::MeasureText(const FString& Text, float& OutW, float& OutH, UFont* Font, float Scale)
{
	GetTextSize(Text, OutW, OutH, Font, Scale * TextScale);
}

void AMetalHUD::DrawTextShadowed(const FString& Text, float X, float Y, const FLinearColor& Color, UFont* Font, float Scale, bool bCentered)
{
	Scale *= TextScale;
	if (bCentered)
	{
		float W = 0.f, H = 0.f;
		GetTextSize(Text, W, H, Font, Scale);
		X -= W * 0.5f;
	}
	DrawText(Text, FLinearColor(0.f, 0.f, 0.f, Color.A * 0.8f), X + 2.f, Y + 2.f, Font, Scale);
	DrawText(Text, Color, X, Y, Font, Scale);
}

void AMetalHUD::DrawBar(float X, float Y, float Width, float Height, float Fraction, const FLinearColor& Color)
{
	DrawRect(Panel, X - 3.f, Y - 3.f, Width + 6.f, Height + 6.f);
	DrawRect(Color, X, Y, Width * FMath::Clamp(Fraction, 0.f, 1.f), Height);
}

void AMetalHUD::DrawHUD()
{
	Super::DrawHUD();
	if (Canvas == nullptr || GEngine == nullptr)
	{
		return;
	}
	const float W = Canvas->ClipX;
	const float H = Canvas->ClipY;
	// Every size below is authored for 1080p and multiplied by UI so the layout scales with resolution.
	const float UI = FMath::Max(0.6f, H / 1080.f);
	const float Now = GetWorld()->GetTimeSeconds();
	UFont* Small = GEngine->GetSmallFont();
	UFont* Medium = GEngine->GetMediumFont();
	UFont* Large = GEngine->GetLargeFont();

	AMetalPlayerController* PC = Cast<AMetalPlayerController>(GetOwningPlayerController());
	AMetalGameMode* GameMode = GetWorld()->GetAuthGameMode<AMetalGameMode>();
	AMetalBotCharacter* Bot = PC ? Cast<AMetalBotCharacter>(PC->GetPawn()) : nullptr;

	// Damage vignette.
	if (PC)
	{
		const float DamageAlpha = FMath::Clamp(1.f - (Now - PC->GetLastDamageTime()) / 0.5f, 0.f, 1.f);
		if (DamageAlpha > 0.f)
		{
			DrawRect(FLinearColor(0.8f, 0.f, 0.f, 0.25f * DamageAlpha), 0.f, 0.f, W, H);
		}
	}

	// The arena keeps the screen clean while exploring; combat UI appears when shooting, hurt or hijacking.
	const bool bCombatUI = Bot && (GameMode == nullptr || !GameMode->bMinimalHUD || Bot->IsInCombatStance() || PC->GetHost() != nullptr
		|| Now - Bot->GetLastDamageTakenTime() < 4.f);

	// Scope first, so every other panel draws on top of the black surround.
	const float Scope = Bot ? Bot->GetScopeAlpha() : 0.f;
	if (Bot && Bot->IsAlive() && Scope > 0.02f)
	{
		DrawScope(Bot, Scope, UI);
	}
	if (Bot && Bot->IsAlive() && !Cast<AMetalStalker>(Bot))
	{
		DrawAwareness(Bot, UI);
	}

	if (Bot && Bot->IsAlive() && bCombatUI)
	{
		// Crosshair: cyan over a hackable robot, red briefly after a confirmed hit. The scope has its own.
		const bool bHackable = PC->GetHackCandidate() != nullptr;
		const bool bHit = Now - PC->GetLastHitConfirmTime() < 0.15f;
		const FLinearColor CrossColor = bHit ? Danger : (bHackable ? Cyan : FLinearColor::White);
		const float Gap = (Bot->IsAiming() ? 4.f : 8.f) * UI;
		const float Len = 10.f * UI;
		const float CX = W * 0.5f, CY = H * 0.5f;
		if (Scope < 0.5f)
		{
			DrawLine(CX - Gap - Len, CY, CX - Gap, CY, CrossColor, 2.f);
			DrawLine(CX + Gap, CY, CX + Gap + Len, CY, CrossColor, 2.f);
			DrawLine(CX, CY - Gap - Len, CX, CY - Gap, CrossColor, 2.f);
			DrawLine(CX, CY + Gap, CX, CY + Gap + Len, CrossColor, 2.f);
		}
		else if (bHit)
		{
			DrawRect(Danger, CX - 3.f * UI, CY - 3.f * UI, 6.f * UI, 6.f * UI);
		}
		if (bHackable)
		{
			const AMetalBotCharacter* Candidate = PC->GetHackCandidate();
			const TCHAR* Verb = Candidate->GetTeam() == Bot->GetTeam() ? TEXT("PILOT") : TEXT("HACK");
			DrawTextShadowed(FString::Printf(TEXT("[F] %s %s"), Verb, *Candidate->GetDisplayName().ToString().ToUpper()), CX, CY + 30.f * UI, Cyan, Medium, UI, true);
		}

		// Health.
		const float BarW = 360.f * UI;
		const float BarX = 40.f * UI;
		// Laid out from the bottom up with measured text: stance line, health bar, then the name above it.
		float StanceW = 0.f, StanceH = 0.f, NameW = 0.f, NameH = 0.f;
		MeasureText(TEXT("Ag"), StanceW, StanceH, Small, UI * 1.2f);
		MeasureText(TEXT("Ag"), NameW, NameH, Medium, UI);
		const float BarY = H - 14.f * UI - StanceH - 6.f * UI - 18.f * UI;
		const float HealthFraction = Bot->GetHealth() / FMath::Max(1.f, Bot->GetMaxHealth());
		const FString Name = Bot->GetDisplayName().ToString().ToUpper() + (Bot->IsHijacked() ? TEXT("  (HIJACKED)") : TEXT(""));
		DrawTextShadowed(Name, BarX, BarY - NameH - 4.f * UI, Bot->IsHijacked() ? Cyan : Amber, Medium, UI);
		DrawBar(BarX, BarY, BarW, 18.f * UI, HealthFraction, HealthFraction < 0.3f ? Danger : FLinearColor(0.3f, 1.f, 0.4f));
		// Stance: sneaking keeps you hidden, sprinting is loud.
		if (Bot->IsSneaking())
		{
			DrawTextShadowed(TEXT("SNEAKING - quiet, hard to spot"), BarX, BarY + 24.f * UI, Cyan, Small, UI * 1.2f);
		}
		else if (Bot->IsSprinting())
		{
			DrawTextShadowed(TEXT("SPRINTING - loud"), BarX, BarY + 24.f * UI, Amber, Small, UI * 1.2f);
		}

		// Flying: airspeed and the boost meter.
		if (const AMetalStalker* Flyer = Cast<AMetalStalker>(Bot))
		{
			DrawLeadReticle(Flyer, UI);
			const int32 Speed = FMath::RoundToInt(Flyer->GetVelocity().Size() / 100.f);
			DrawTextShadowed(FString::Printf(TEXT("SPEED %d m/s%s"), Speed, Flyer->IsBoosting() ? TEXT("   BOOST") : TEXT("")), BarX, BarY - 2.f * NameH - 8.f * UI, FLinearColor::White, Small, UI * 1.3f);
			const float Boost = Flyer->GetBoostFraction();
			DrawBar(BarX, BarY + 24.f * UI, BarW, 8.f * UI, Boost, Flyer->IsBoosting() ? Amber : Cyan);
		}

		// Weapon and ammo.
		const float RightX = W - 40.f * UI;
		// Laid out from the bottom up: grenade count, ammo, weapon name, then the weapon list above.
		float GW = 0.f, GH = 0.f, AW = 0.f, AH = 0.f, WW = 0.f, WH = 0.f, LW = 0.f, LH = 0.f;
		MeasureText(TEXT("Ag"), GW, GH, Small, UI * 1.3f);
		MeasureText(TEXT("Ag"), AW, AH, Large, UI);
		MeasureText(TEXT("Ag"), WW, WH, Medium, UI);
		MeasureText(TEXT("Ag"), LW, LH, Small, UI * 1.2f);
		const float GrenadeY = H - 14.f * UI - GH;
		const float AmmoY = GrenadeY - AH;
		const float WeaponNameY = AmmoY - WH;
		if (const FMetalWeaponSlot* Slot = Bot->GetCurrentWeapon(); Slot && Slot->Data)
		{
			const FString WeaponName = FString::Printf(TEXT("%s  LV%d"), *Slot->Data->DisplayName.ToString().ToUpper(), Slot->Level + 1);
			const int32 Guns = Bot->GetMuzzleCount();
			const FString Ammo = Bot->IsWeaponDisabled() ? (Guns > 1 ? TEXT("GUNS DESTROYED") : TEXT("WEAPON ARM DESTROYED"))
				: Bot->IsSwappingWeapon() ? TEXT("SWAPPING ARM...")
				: Bot->IsReloading() ? TEXT("RELOADING...")
				: Guns > 1 && Bot->GetWorkingMuzzleCount() < Guns ? FString::Printf(TEXT("GUNS %d / %d"), Bot->GetWorkingMuzzleCount(), Guns)
				: Slot->Data->bInfiniteAmmo ? TEXT("INF")
				: FString::Printf(TEXT("%d / %d"), Slot->Clip, Slot->Reserve);
			float TW = 0.f, TH = 0.f;
			MeasureText(WeaponName, TW, TH, Medium, UI);
			DrawTextShadowed(WeaponName, RightX - TW, WeaponNameY, Amber, Medium, UI);
			MeasureText(Ammo, TW, TH, Large, UI);
			DrawTextShadowed(Ammo, RightX - TW, AmmoY, FLinearColor::White, Large, UI);
		}
		if (Bot->GrenadeWeapon && !Bot->IsHijacked())
		{
			const FString GrenadeText = FString::Printf(TEXT("[G] %s x%d"), *Bot->GrenadeWeapon->DisplayName.ToString().ToUpper(), Bot->Grenades);
			float TW = 0.f, TH = 0.f;
			MeasureText(GrenadeText, TW, TH, Small, UI * 1.3f);
			DrawTextShadowed(GrenadeText, RightX - TW, GrenadeY, FLinearColor(0.6f, 0.75f, 1.f), Small, UI * 1.3f);
		}
		// Weapon list (1-6).
		const TArray<FMetalWeaponSlot>& Weapons = Bot->GetWeapons();
		for (int32 Index = 0; Index < Weapons.Num() && !Bot->IsHijacked(); ++Index)
		{
			if (Weapons[Index].Data)
			{
				const bool bCurrent = Bot->IsSwappingWeapon() ? Index == Bot->GetSwapTargetSlot() : Index == Bot->GetCurrentWeaponIndex();
				const FString Entry = FString::Printf(TEXT("%d %s"), Index + 1, *Weapons[Index].Data->DisplayName.ToString());
				float TW = 0.f, TH = 0.f;
				MeasureText(Entry, TW, TH, Small, UI * 1.2f);
				DrawTextShadowed(Entry, RightX - TW, WeaponNameY - 6.f * UI - (LH + 2.f * UI) * (Weapons.Num() - Index), bCurrent ? Amber : FLinearColor(1.f, 1.f, 1.f, 0.6f), Small, UI * 1.2f);
			}
		}

	}

	if (Bot && Bot->IsAlive())
	{
		// Possession timer (friendly links have none).
		if (PC->GetHost() && PC->IsAllyLink())
		{
			DrawTextShadowed(TEXT("LINKED   [F] disconnect"), W * 0.5f, 92.f * UI, Cyan, Medium, UI, true);
		}
		else if (PC->GetHost())
		{
			const float Fraction = PC->GetPossessionTimeRemaining() / FMath::Max(1.f, PC->GetPossessionDuration());
			const float TimerW = 420.f * UI;
			float TimerTW = 0.f, TimerTH = 0.f;
			MeasureText(TEXT("Ag"), TimerTW, TimerTH, Medium, UI);
			DrawTextShadowed(FString::Printf(TEXT("HACK TETHER  %.0fs   [F] disconnect"), PC->GetPossessionTimeRemaining()), W * 0.5f, 92.f * UI, Cyan, Medium, UI, true);
			DrawBar(W * 0.5f - TimerW * 0.5f, 92.f * UI + TimerTH + 4.f * UI, TimerW, 10.f * UI, Fraction, Fraction < 0.25f && FMath::Fmod(Now, 0.4f) < 0.2f ? Danger : Cyan);
		}

		if (AMetalControlConsole* Console = PC->GetNearbyConsole())
		{
			DrawTextShadowed(Console->PromptText.ToString(), W * 0.5f, H * 0.62f, FLinearColor::White, Medium, UI, true);
		}
	}

	if (AMetalSpaceBattleGameMode* Battle = Cast<AMetalSpaceBattleGameMode>(GameMode); Battle && !Battle->IsMissionComplete())
	{
		DrawSpaceBattle(Battle, PC ? PC->GetPawn() : nullptr, UI);
	}

	// Dialogue. Every line is a radio call unless the player's own robot is the one speaking.
	if (const UMetalDialogueSubsystem* Dialogue = GetWorld()->GetSubsystem<UMetalDialogueSubsystem>())
	{
		if (const FMetalDialogueLine* Line = Dialogue->GetCurrentLine())
		{
			DrawDialogueBox(*Line, Dialogue->GetOpenTime(), UI);
			const AMetalBotCharacter* OwnBody = PC ? PC->GetPlayerBody() : nullptr;
			if (PC && PC->GetPawn() && !(OwnBody && Line->Speaker.EqualToCaseIgnored(OwnBody->GetDisplayName())))
			{
				DrawRadioIcon(PC->GetPawn(), UI);
			}
		}
	}

	if (GameMode)
	{
		// Objective (top-left): label, objective text, then the hint card under it.
		float LabelW = 0.f, LabelH = 0.f, ObjW = 0.f, ObjH = 0.f;
		MeasureText(TEXT("Ag"), LabelW, LabelH, Small, UI * 1.2f);
		MeasureText(TEXT("Ag"), ObjW, ObjH, Medium, UI);
		const float ObjectiveY = 24.f * UI + LabelH;
		const float HintY = ObjectiveY + ObjH + 16.f * UI;
		const FText Objective = GameMode->GetObjectiveText();
		if (!Objective.IsEmpty())
		{
			DrawTextShadowed(TEXT("OBJECTIVE"), 40.f * UI, 24.f * UI, Amber, Small, UI * 1.2f);
			DrawTextShadowed(Objective.ToString(), 40.f * UI, ObjectiveY, FLinearColor::White, Medium, UI);
		}
		if (const AMetalMission* Mission = GameMode->GetMission(); Mission && !GameMode->IsMissionComplete())
		{
			// Control tip for the current step, in a panel under the objective.
			const FText Hint = Mission->GetCurrentHint();
			if (!Hint.IsEmpty())
			{
				float TW = 0.f, TH = 0.f;
				MeasureText(Hint.ToString(), TW, TH, Medium, UI);
				const float Y = HintY;
				DrawRect(Panel, 34.f * UI, Y - 6.f * UI, TW + 24.f * UI, TH + 12.f * UI);
				DrawRect(Cyan, 34.f * UI, Y - 6.f * UI, 4.f * UI, TH + 12.f * UI);
				DrawTextShadowed(Hint.ToString(), 46.f * UI, Y, FLinearColor(0.85f, 0.97f, 1.f), Medium, UI);
			}
			// Where to go (hidden while scoped so it doesn't clutter the sight picture).
			FVector Target;
			if (Bot && Scope < 0.5f && Mission->GetObjectiveMarker(Target))
			{
				const int32 Meters = FMath::RoundToInt(FVector::Dist(Target, Bot->GetActorLocation()) / 100.f);
				DrawMarker(Target, FString::Printf(TEXT("%d m"), Meters), Amber, UI, true);
			}
		}
		// System notices.
		if (const FMetalMessage* Notice = GameMode->GetNotice())
		{
			DrawTextShadowed(Notice->Text.ToString(), W * 0.5f, H * 0.30f, FLinearColor::White, Medium, UI, true);
		}
		// Banners.
		if (const FMetalMessage* Banner = GameMode->GetBanner())
		{
			DrawTextShadowed(Banner->Text.ToString(), W * 0.5f, H * 0.38f, Amber, Large, UI * 2.2f / TextScale, true);
		}
		if (GameMode->IsMissionComplete())
		{
			DrawResults(GameMode, PC, UI);
		}
	}
}

void AMetalHUD::DrawScope(const AMetalBotCharacter* Bot, float Alpha, float UI)
{
	const float W = Canvas->ClipX;
	const float H = Canvas->ClipY;
	const float CX = W * 0.5f, CY = H * 0.5f;
	// The lens grows open as the scope comes up.
	const float Radius = H * 0.44f * FMath::Lerp(0.6f, 1.f, Alpha);
	const FLinearColor Black(0.f, 0.f, 0.f, FMath::Clamp(Alpha * 1.2f, 0.f, 1.f));
	// Black out everything outside the circle in horizontal strips.
	DrawRect(Black, 0.f, 0.f, W, FMath::Max(0.f, CY - Radius));
	DrawRect(Black, 0.f, CY + Radius, W, FMath::Max(0.f, H - CY - Radius));
	const float Step = FMath::Max(2.f, 3.f * UI);
	for (float Y = CY - Radius; Y < CY + Radius; Y += Step)
	{
		const float DY = FMath::Abs(Y + Step * 0.5f - CY);
		const float HalfWidth = FMath::Sqrt(FMath::Max(0.f, Radius * Radius - DY * DY));
		DrawRect(Black, 0.f, Y, CX - HalfWidth, Step);
		DrawRect(Black, CX + HalfWidth, Y, W - CX - HalfWidth, Step);
	}
	// Lens rim and a soft darkening inside it.
	constexpr int32 Segments = 72;
	for (int32 Segment = 0; Segment < Segments; ++Segment)
	{
		const float A0 = Segment * UE_TWO_PI / Segments;
		const float A1 = (Segment + 1) * UE_TWO_PI / Segments;
		DrawLine(CX + Radius * FMath::Cos(A0), CY + Radius * FMath::Sin(A0), CX + Radius * FMath::Cos(A1), CY + Radius * FMath::Sin(A1), FLinearColor(0.05f, 0.05f, 0.05f, Alpha), 6.f * UI);
	}
	if (Alpha < 0.7f)
	{
		return;
	}
	// Fine crosshair with thick outer posts and mil-dots.
	const FLinearColor Reticle(0.02f, 0.02f, 0.02f, 0.9f);
	const FLinearColor Red(1.f, 0.15f, 0.1f, 0.9f);
	DrawLine(CX - Radius, CY, CX + Radius, CY, Reticle, 1.5f);
	DrawLine(CX, CY - Radius, CX, CY + Radius, Reticle, 1.5f);
	for (const float Sign : {-1.f, 1.f})
	{
		DrawLine(CX + Sign * Radius, CY, CX + Sign * Radius * 0.35f, CY, Reticle, 5.f * UI);
		DrawLine(CX, CY + Sign * Radius, CX, CY + Sign * Radius * 0.35f, Reticle, 5.f * UI);
		for (int32 Dot = 1; Dot <= 4; ++Dot)
		{
			const float D = Radius * 0.07f * Dot;
			DrawRect(Reticle, CX + Sign * D - 2.f * UI, CY - 2.f * UI, 4.f * UI, 4.f * UI);
			DrawRect(Reticle, CX - 2.f * UI, CY + Sign * D - 2.f * UI, 4.f * UI, 4.f * UI);
		}
	}
	DrawRect(Red, CX - 2.f * UI, CY - 2.f * UI, 4.f * UI, 4.f * UI);
	// Range to whatever is under the crosshair.
	const int32 Range = FMath::RoundToInt(FVector::Dist(Bot->GetAimPoint(), Bot->GetActorLocation()) / 100.f);
	DrawTextShadowed(FString::Printf(TEXT("RANGE %d m"), Range), CX + Radius * 0.35f, CY + 12.f * UI, FLinearColor(0.9f, 1.f, 0.9f), GEngine->GetSmallFont(), UI * 1.3f);
}

void AMetalHUD::DrawAwareness(const AMetalBotCharacter* Viewer, float UI)
{
	const float Now = GetWorld()->GetTimeSeconds();
	UFont* Large = GEngine->GetLargeFont();
	for (TActorIterator<AMetalBotAIController> It(GetWorld()); It; ++It)
	{
		const AMetalBotCharacter* Enemy = Cast<AMetalBotCharacter>(It->GetPawn());
		if (Enemy == nullptr || !Enemy->IsAlive() || !Enemy->IsHostileTo(Viewer)
			|| FVector::DistSquared(Enemy->GetActorLocation(), Viewer->GetActorLocation()) > FMath::Square(6000.f))
		{
			continue;
		}
		const bool bAlert = Now - It->GetAlertTime() < 2.5f;
		const float Suspicion = It->GetSuspicion();
		if (!bAlert && (Suspicion < 0.02f || It->GetState() == EMetalAIState::Combat))
		{
			continue;
		}
		const FVector Screen = Project(Enemy->GetActorLocation() + FVector(0.f, 0.f, Enemy->GetSimpleCollisionHalfHeight() + 45.f));
		if (Screen.Z <= 0.f)
		{
			continue;
		}
		if (bAlert)
		{
			// Pops in big, then settles.
			const float Pop = 1.f + 0.6f * FMath::Max(0.f, 1.f - (Now - It->GetAlertTime()) / 0.25f);
			DrawTextShadowed(TEXT("!"), Screen.X, Screen.Y - 30.f * UI * Pop, Danger, Large, UI * 1.6f * Pop / TextScale, true);
			continue;
		}
		// A "?" whose meter fills from yellow to orange as the robot gets closer to noticing.
		const FLinearColor Color = FLinearColor::LerpUsingHSV(FLinearColor(1.f, 0.95f, 0.3f), FLinearColor(1.f, 0.45f, 0.1f), Suspicion);
		DrawTextShadowed(TEXT("?"), Screen.X, Screen.Y - 30.f * UI, Color, Large, UI * 1.3f / TextScale, true);
		const float BarW = 34.f * UI;
		DrawBar(Screen.X - BarW * 0.5f, Screen.Y + 6.f * UI, BarW, 4.f * UI, Suspicion, Color);
	}
}

void AMetalHUD::DrawResults(const AMetalGameMode* GameMode, const AMetalPlayerController* PC, float UI)
{
	const float W = Canvas->ClipX;
	const float H = Canvas->ClipY;
	UFont* Medium = GEngine->GetMediumFont();
	UFont* Small = GEngine->GetSmallFont();
	float RowW = 0.f, RowH = 0.f, FootW = 0.f, FootH = 0.f;
	MeasureText(TEXT("Ag"), RowW, RowH, Medium, UI * 1.1f);
	MeasureText(TEXT("Ag"), FootW, FootH, Small, UI * 1.3f);
	// A centred panel under the end-of-mission banner: four label/value rows and a footer with the keys.
	const float RowStep = RowH + 10.f * UI;
	const float PanelW = 760.f * UI;
	const float PanelH = 22.f * UI + 4.f * RowStep + 12.f * UI + FootH + 18.f * UI;
	const float X = (W - PanelW) * 0.5f;
	const float Y = H * 0.46f;
	DrawRect(FLinearColor(0.02f, 0.03f, 0.05f, 0.8f), X, Y, PanelW, PanelH);
	DrawRect(Amber, X, Y, PanelW, 3.f * UI);

	const float Time = GameMode->GetMissionTime();
	const float Accuracy = PC ? PC->GetAccuracy() : -1.f;
	const FString Rows[][2] = {
		{TEXT("TIME"), FString::Printf(TEXT("%d:%02d"), FMath::FloorToInt(Time / 60.f), FMath::FloorToInt(FMath::Fmod(Time, 60.f)))},
		{TEXT("ENEMIES DESTROYED"), FString::FromInt(GameMode->GetKills())},
		{TEXT("TIMES SCRAPPED"), FString::FromInt(GameMode->GetDeaths())},
		{TEXT("ACCURACY"), Accuracy < 0.f ? TEXT("-") : FString::Printf(TEXT("%d%%"), FMath::RoundToInt(Accuracy * 100.f))},
	};
	float RowY = Y + 22.f * UI;
	// Labels left-aligned, values right-aligned.
	for (const FString* Row : Rows)
	{
		DrawTextShadowed(Row[0], X + 30.f * UI, RowY, FLinearColor(1.f, 1.f, 1.f, 0.75f), Medium, UI);
		float TW = 0.f, TH = 0.f;
		MeasureText(Row[1], TW, TH, Medium, UI * 1.1f);
		DrawTextShadowed(Row[1], X + PanelW - 30.f * UI - TW, RowY, FLinearColor::White, Medium, UI * 1.1f);
		RowY += RowStep;
	}
	DrawTextShadowed(TEXT("[ENTER] play again      [ESC] menu"), W * 0.5f, Y + PanelH - FootH - 16.f * UI, Amber, Small, UI * 1.3f, true);
}

void AMetalHUD::DrawDialogueBox(const FMetalDialogueLine& Line, float OpenTime, float UI)
{
	const float W = Canvas->ClipX;
	const float H = Canvas->ClipY;
	UFont* Small = GEngine->GetSmallFont();
	UFont* Medium = GEngine->GetMediumFont();

	// Centred between the health panel (bottom left) and the weapon panel (bottom right).
	const float BoxW = FMath::Clamp(W - 960.f * UI, 520.f * UI, 1000.f * UI);
	const float Pad = 16.f * UI;
	const float TextW = BoxW - 2.f * Pad;

	// Word-wrap the line to the box, keeping any line breaks the writer put in.
	TArray<FString> Rows;
	TArray<FString> Paragraphs;
	Line.Line.ToString().ParseIntoArray(Paragraphs, TEXT("\n"), false);
	for (const FString& Paragraph : Paragraphs)
	{
		TArray<FString> Words;
		Paragraph.ParseIntoArrayWS(Words);
		FString Row;
		for (const FString& Word : Words)
		{
			const FString Candidate = Row.IsEmpty() ? Word : Row + TEXT(" ") + Word;
			float TW = 0.f, TH = 0.f;
			MeasureText(Candidate, TW, TH, Medium, UI);
			if (TW > TextW && !Row.IsEmpty())
			{
				Rows.Add(Row);
				Row = Word;
			}
			else
			{
				Row = Candidate;
			}
		}
		Rows.Add(Row);
	}

	const FString Name = Line.Speaker.ToString().ToUpper();
	float NameW = 0.f, NameH = 0.f, RowW = 0.f, RowH = 0.f;
	if (!Name.IsEmpty())
	{
		MeasureText(Name, NameW, NameH, Small, UI * 1.3f);
		NameH += 6.f * UI;
	}
	MeasureText(TEXT("Ag"), RowW, RowH, Medium, UI);
	const float BoxH = Pad + NameH + Rows.Num() * RowH + Pad;

	// Pops up: slides in from below and fades in over the first 0.2 s.
	const float Pop = 1.f - FMath::Square(1.f - FMath::Clamp(OpenTime / 0.2f, 0.f, 1.f));
	const float X = (W - BoxW) * 0.5f;
	const float Y = H - 24.f * UI - BoxH + (1.f - Pop) * 40.f * UI;
	DrawRect(FLinearColor(0.02f, 0.04f, 0.06f, 0.75f * Pop), X, Y, BoxW, BoxH);
	DrawRect(FLinearColor(Amber.R, Amber.G, Amber.B, Pop), X, Y, BoxW, 3.f * UI);
	float TextY = Y + Pad;
	if (!Name.IsEmpty())
	{
		DrawTextShadowed(Name, X + Pad, TextY, FLinearColor(Amber.R, Amber.G, Amber.B, Pop), Small, UI * 1.3f);
		TextY += NameH;
	}
	for (const FString& Row : Rows)
	{
		DrawTextShadowed(Row, X + Pad, TextY, FLinearColor(1.f, 0.95f, 0.8f, Pop), Medium, UI);
		TextY += RowH;
	}
}

void AMetalHUD::DrawRadioIcon(const APawn* Pawn, float UI)
{
	const FVector Screen = Project(Pawn->GetActorLocation() + FVector(0.f, 0.f, Pawn->GetSimpleCollisionHalfHeight() + 5.f));
	if (Screen.Z <= 0.f)
	{
		return;
	}
	const float Now = GetWorld()->GetTimeSeconds();
	// The radio's bottom edge sits on the point above the head, bobbing slightly so it reads as live.
	const float BodyW = 16.f * UI;
	const float BodyH = 22.f * UI;
	const float L = Screen.X - BodyW * 0.5f;
	const float T = Screen.Y - BodyH - (1.f + FMath::Sin(Now * 4.f)) * 2.f * UI;
	DrawRect(FLinearColor(0.02f, 0.05f, 0.07f, 0.85f), L, T, BodyW, BodyH);
	DrawLine(L, T, L + BodyW, T, Cyan, 2.f);
	DrawLine(L + BodyW, T, L + BodyW, T + BodyH, Cyan, 2.f);
	DrawLine(L + BodyW, T + BodyH, L, T + BodyH, Cyan, 2.f);
	DrawLine(L, T + BodyH, L, T, Cyan, 2.f);
	// Display window and speaker grille.
	DrawRect(FLinearColor(Cyan.R, Cyan.G, Cyan.B, 0.8f), L + 3.f * UI, T + 3.f * UI, BodyW - 6.f * UI, 5.f * UI);
	for (int32 Slot = 0; Slot < 3; ++Slot)
	{
		const float GrilleY = T + (12.f + 3.f * Slot) * UI;
		DrawLine(L + 4.f * UI, GrilleY, L + BodyW - 4.f * UI, GrilleY, FLinearColor(Cyan.R, Cyan.G, Cyan.B, 0.6f), 1.f);
	}
	// Antenna, with signal waves going out both sides of its tip.
	const FVector2D Tip(Screen.X, T - 14.f * UI);
	DrawLine(Tip.X, T, Tip.X, Tip.Y, Cyan, 2.f);
	DrawRect(Cyan, Tip.X - 2.f * UI, Tip.Y - 2.f * UI, 4.f * UI, 4.f * UI);
	constexpr int32 Segments = 6;
	const float HalfArc = FMath::DegreesToRadians(50.f);
	for (int32 Wave = 0; Wave < 3; ++Wave)
	{
		// Each ring flashes a quarter cycle after the one inside it, so the signal ripples outward.
		const float Phase = FMath::Fmod(Now * 1.5f + 1.f - Wave * 0.25f, 1.f);
		const FLinearColor Color(Cyan.R, Cyan.G, Cyan.B, FMath::Lerp(0.2f, 1.f, 1.f - Phase));
		const float Radius = (7.f + 5.f * Wave) * UI;
		for (const float Facing : {0.f, UE_PI})
		{
			for (int32 Segment = 0; Segment < Segments; ++Segment)
			{
				const float A0 = Facing - HalfArc + Segment * 2.f * HalfArc / Segments;
				const float A1 = Facing - HalfArc + (Segment + 1) * 2.f * HalfArc / Segments;
				DrawLine(Tip.X + Radius * FMath::Cos(A0), Tip.Y + Radius * FMath::Sin(A0), Tip.X + Radius * FMath::Cos(A1), Tip.Y + Radius * FMath::Sin(A1), Color, 2.f);
			}
		}
	}
}

void AMetalHUD::DrawMarker(const FVector& World, const FString& Label, const FLinearColor& Color, float UI, bool bEdgeArrow)
{
	const float W = Canvas->ClipX;
	const float H = Canvas->ClipY;
	const FVector Screen = Project(World);
	const float Margin = 50.f * UI;
	const bool bOnScreen = Screen.Z > 0.f && Screen.X > Margin && Screen.X < W - Margin && Screen.Y > Margin && Screen.Y < H - Margin;
	if (bOnScreen)
	{
		const float S = 9.f * UI;
		DrawLine(Screen.X, Screen.Y - S, Screen.X + S, Screen.Y, Color, 2.f);
		DrawLine(Screen.X + S, Screen.Y, Screen.X, Screen.Y + S, Color, 2.f);
		DrawLine(Screen.X, Screen.Y + S, Screen.X - S, Screen.Y, Color, 2.f);
		DrawLine(Screen.X - S, Screen.Y, Screen.X, Screen.Y - S, Color, 2.f);
		DrawTextShadowed(Label, Screen.X, Screen.Y + 12.f * UI, Color, GEngine->GetSmallFont(), UI * 1.1f, true);
		return;
	}
	if (!bEdgeArrow)
	{
		return;
	}
	// Off screen (or behind): an arrow on the edge pointing the way to turn.
	// Points behind the camera project mirrored through the screen centre, so flip the direction for them.
	FVector2D Dir(Screen.X - W * 0.5f, Screen.Y - H * 0.5f);
	if (Screen.Z <= 0.f)
	{
		Dir = -Dir;
	}
	Dir = Dir.GetSafeNormal();
	if (Dir.IsNearlyZero())
	{
		Dir = FVector2D(0.f, 1.f);
	}
	// Push the arrow out from the centre along Dir until it meets the nearer of the vertical or
	// horizontal margin lines, so it hugs the screen edge.
	const float Scale = FMath::Min((W * 0.5f - Margin) / FMath::Max(FMath::Abs(Dir.X), KINDA_SMALL_NUMBER), (H * 0.5f - Margin) / FMath::Max(FMath::Abs(Dir.Y), KINDA_SMALL_NUMBER));
	const FVector2D Tip = FVector2D(W * 0.5f, H * 0.5f) + Dir * Scale;
	const FVector2D Side(-Dir.Y, Dir.X);
	const FVector2D Back = Tip - Dir * 18.f * UI;
	DrawLine(Tip.X, Tip.Y, Back.X + Side.X * 10.f * UI, Back.Y + Side.Y * 10.f * UI, Color, 3.f);
	DrawLine(Tip.X, Tip.Y, Back.X - Side.X * 10.f * UI, Back.Y - Side.Y * 10.f * UI, Color, 3.f);
	const FVector2D TextAt = Tip - Dir * 40.f * UI;
	DrawTextShadowed(Label, TextAt.X, TextAt.Y - 8.f * UI, Color, GEngine->GetSmallFont(), UI * 1.1f, true);
}

void AMetalHUD::DrawSpaceBattle(AMetalSpaceBattleGameMode* Battle, const APawn* Viewer, float UI)
{
	const float W = Canvas->ClipX;
	UFont* Large = GEngine->GetLargeFont();
	const EMetalTeam Mine = Battle->PlayerTeam;
	const EMetalTeam Theirs = Mine == EMetalTeam::Heroes ? EMetalTeam::Villains : EMetalTeam::Heroes;
	const FLinearColor TeamColors[] = {FLinearColor(0.35f, 0.6f, 1.f), FLinearColor(1.f, 0.4f, 0.3f)};
	const float Now = GetWorld()->GetTimeSeconds();

	// Each side: tickets in big numbers, the hull bar under them (blue while shielded), then one pip per
	// subsystem in GetSubsystems() order: shield generators, engines, comms, life support.
	auto DrawSide = [&](EMetalTeam Team, float CenterX, const TCHAR* Name)
	{
		const FLinearColor Color = TeamColors[Team == EMetalTeam::Heroes ? 0 : 1];
		DrawTextShadowed(FString::Printf(TEXT("%s  %d"), Name, Battle->GetTickets(Team)), CenterX, 18.f * UI, Color, Large, UI * 0.9f, true);
		float TicketW = 0.f, TicketH = 0.f;
		MeasureText(TEXT("Ag"), TicketW, TicketH, Large, UI * 0.9f);
		const float HullY = 18.f * UI + TicketH + 4.f * UI;
		const AMetalCapitalShip* Ship = Battle->GetShip(Team);
		if (Ship == nullptr)
		{
			return;
		}
		const float BarW = 240.f * UI;
		const float Hull = Ship->GetHullHealth() / FMath::Max(1.f, Ship->GetMaxHullHealth());
		DrawBar(CenterX - BarW * 0.5f, HullY, BarW, 8.f * UI, Hull, Ship->IsShielded() ? FLinearColor(0.4f, 0.8f, 1.f) : FLinearColor(1.f, 0.6f, 0.2f));
		const TArray<TObjectPtr<UMetalShipSubsystemComponent>>& Parts = Ship->GetSubsystems();
		const float Pip = 14.f * UI;
		const float Gap = 6.f * UI;
		float X = CenterX - (Parts.Num() * (Pip + Gap) - Gap) * 0.5f;
		for (const UMetalShipSubsystemComponent* Part : Parts)
		{
			const float Fraction = Part->GetHealth() / FMath::Max(1.f, Part->GetMaxHealth());
			DrawRect(FLinearColor(0.f, 0.f, 0.f, 0.6f), X, (HullY + 16.f * UI), Pip, Pip);
			DrawRect(Part->IsDestroyed() ? FLinearColor(0.25f, 0.05f, 0.05f) : FLinearColor::LerpUsingHSV(FLinearColor(1.f, 0.2f, 0.1f), FLinearColor(0.3f, 1.f, 0.4f), Fraction),
				X + 2.f, (HullY + 16.f * UI) + 2.f, Pip - 4.f, Pip - 4.f);
			X += Pip + Gap;
		}
	};
	DrawSide(Mine, W * 0.5f - 200.f * UI, Mine == EMetalTeam::Heroes ? TEXT("HEROES") : TEXT("VILLAINS"));
	DrawSide(Theirs, W * 0.5f + 200.f * UI, Theirs == EMetalTeam::Heroes ? TEXT("HEROES") : TEXT("VILLAINS"));

	if (Viewer == nullptr)
	{
		return;
	}
	// Out in space: the way home, and the enemy's targets. Aboard: the life-support core, once in sight.
	const AMetalCapitalShip* Home = Battle->GetShip(Mine);
	const AMetalCapitalShip* Enemy = Battle->GetShip(Theirs);
	// Ship interiors are streamed in 20 km above the battle (see AMetalCapitalShip), so anything within
	// 10 km of the battle plane is out in space.
	const bool bInSpace = FMath::Abs(Viewer->GetActorLocation().Z) < 1000000.f;
	if (bInSpace && Home && Home->GetOuterPortal())
	{
		DrawMarker(Home->GetOuterPortal()->GetActorLocation(), TEXT("HANGAR"), FLinearColor(0.3f, 0.9f, 1.f), UI, true);
	}
	if (Enemy)
	{
		static const TMap<EMetalShipSubsystem, const TCHAR*> Labels = {{EMetalShipSubsystem::ShieldGenerator, TEXT("SHIELD")},
			{EMetalShipSubsystem::Engines, TEXT("ENGINES")}, {EMetalShipSubsystem::Comms, TEXT("COMMS")}, {EMetalShipSubsystem::LifeSupport, TEXT("LIFE SUPPORT")}};
		const float Blink = FMath::Fmod(Now, 0.6f) < 0.3f ? 1.f : 0.6f;
		for (const UMetalShipSubsystemComponent* Part : Enemy->GetSubsystems())
		{
			const FVector Where = Part->Bounds.Origin;
			if (!Part->IsDestroyed() && FVector::DistSquared(Where, Viewer->GetActorLocation()) < FMath::Square(250000.f))
			{
				DrawMarker(Where, Labels.FindRef(Part->Type), FLinearColor(1.f, 0.35f, 0.25f, Blink), UI, false);
			}
		}
		if (bInSpace && Enemy->GetOuterPortal())
		{
			DrawMarker(Enemy->GetOuterPortal()->GetActorLocation(), TEXT("ENEMY HANGAR"), FLinearColor(1.f, 0.6f, 0.3f), UI, false);
		}
	}
}

void AMetalHUD::DrawLeadReticle(const AMetalStalker* Flyer, float UI)
{
	const FMetalWeaponSlot* Slot = Flyer->GetCurrentWeapon();
	if (Slot == nullptr || Slot->Data == nullptr || Slot->Data->FireMode != EMetalFireMode::Projectile)
	{
		return;
	}
	const float ShotSpeed = FMath::Max(1000.f, Slot->Data->ProjectileSpeed);
	const FVector2D Center(Canvas->ClipX * 0.5f, Canvas->ClipY * 0.5f);
	const AMetalStalker* Best = nullptr;
	FVector BestLead = FVector::ZeroVector;
	float BestScreenDistance = 300.f * UI;
	for (TActorIterator<AMetalStalker> It(GetWorld()); It; ++It)
	{
		if (!It->IsAlive() || It->IsParked() || !Flyer->IsHostileTo(*It) || FVector::DistSquared(It->GetActorLocation(), Flyer->GetActorLocation()) > FMath::Square(200000.f))
		{
			continue;
		}
		// First-order lead: where the target will be after the time a slug takes to cover the current
		// distance, assuming it keeps its velocity. Good enough at dogfight ranges.
		const FVector Lead = It->GetActorLocation() + It->GetVelocity() * (FVector::Dist(It->GetActorLocation(), Flyer->GetActorLocation()) / ShotSpeed);
		const FVector Screen = Project(Lead);
		const float ScreenDistance = FVector2D::Distance(FVector2D(Screen.X, Screen.Y), Center);
		if (Screen.Z > 0.f && ScreenDistance < BestScreenDistance)
		{
			Best = *It;
			BestLead = Lead;
			BestScreenDistance = ScreenDistance;
		}
	}
	if (Best == nullptr)
	{
		return;
	}
	// A ring on the lead point, tied back to the target.
	const FVector Lead = Project(BestLead);
	const FVector Target = Project(Best->GetActorLocation());
	const FLinearColor Color(1.f, 0.85f, 0.2f, 0.9f);
	const float R = 11.f * UI;
	for (int32 Segment = 0; Segment < 12; ++Segment)
	{
		const float A0 = Segment * UE_TWO_PI / 12.f;
		const float A1 = (Segment + 1) * UE_TWO_PI / 12.f;
		DrawLine(Lead.X + R * FMath::Cos(A0), Lead.Y + R * FMath::Sin(A0), Lead.X + R * FMath::Cos(A1), Lead.Y + R * FMath::Sin(A1), Color, 2.f);
	}
	if (Target.Z > 0.f)
	{
		DrawLine(Target.X, Target.Y, Lead.X, Lead.Y, FLinearColor(Color.R, Color.G, Color.B, 0.35f), 1.f);
	}
}
