#include "MetalHUD.h"

#include "MetalGameMode.h"
#include "MetalBotCharacter.h"
#include "MetalControlConsole.h"
#include "MetalPlayerController.h"
#include "MetalWeaponData.h"
#include "Engine/Canvas.h"
#include "Engine/Engine.h"
#include "Engine/Font.h"

namespace
{
	const FLinearColor Amber(1.f, 0.65f, 0.15f);
	const FLinearColor Cyan(0.3f, 0.9f, 1.f);
	const FLinearColor Danger(1.f, 0.2f, 0.1f);
	const FLinearColor Panel(0.f, 0.f, 0.f, 0.55f);
}

void AMetalHUD::DrawTextShadowed(const FString& Text, float X, float Y, const FLinearColor& Color, UFont* Font, float Scale, bool bCentered)
{
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

	if (Bot && Bot->IsAlive() && bCombatUI)
	{
		// Crosshair: cyan over a hackable robot, red briefly after a confirmed hit.
		const bool bHackable = PC->GetHackCandidate() != nullptr;
		const bool bHit = Now - PC->GetLastHitConfirmTime() < 0.15f;
		const FLinearColor CrossColor = bHit ? Danger : (bHackable ? Cyan : FLinearColor::White);
		const float Gap = (Bot->IsAiming() ? 4.f : 8.f) * UI;
		const float Len = 10.f * UI;
		const float CX = W * 0.5f, CY = H * 0.5f;
		DrawLine(CX - Gap - Len, CY, CX - Gap, CY, CrossColor, 2.f);
		DrawLine(CX + Gap, CY, CX + Gap + Len, CY, CrossColor, 2.f);
		DrawLine(CX, CY - Gap - Len, CX, CY - Gap, CrossColor, 2.f);
		DrawLine(CX, CY + Gap, CX, CY + Gap + Len, CrossColor, 2.f);
		if (bHackable)
		{
			DrawTextShadowed(FString::Printf(TEXT("[F] HACK %s"), *PC->GetHackCandidate()->GetDisplayName().ToString().ToUpper()), CX, CY + 30.f * UI, Cyan, Medium, UI, true);
		}

		// Health.
		const float BarW = 360.f * UI;
		const float BarX = 40.f * UI;
		const float BarY = H - 70.f * UI;
		const float HealthFraction = Bot->GetHealth() / FMath::Max(1.f, Bot->GetMaxHealth());
		const FString Name = Bot->GetDisplayName().ToString().ToUpper() + (Bot->IsHijacked() ? TEXT("  (HIJACKED)") : TEXT(""));
		DrawTextShadowed(Name, BarX, BarY - 34.f * UI, Bot->IsHijacked() ? Cyan : Amber, Medium, UI);
		DrawBar(BarX, BarY, BarW, 18.f * UI, HealthFraction, HealthFraction < 0.3f ? Danger : FLinearColor(0.3f, 1.f, 0.4f));

		// Weapon and ammo.
		const float RightX = W - 40.f * UI;
		if (const FMetalWeaponSlot* Slot = Bot->GetCurrentWeapon(); Slot && Slot->Data)
		{
			const FString WeaponName = FString::Printf(TEXT("%s  LV%d"), *Slot->Data->DisplayName.ToString().ToUpper(), Slot->Level + 1);
			const FString Ammo = Bot->IsWeaponDisabled() ? TEXT("WEAPON ARM DESTROYED")
				: Bot->IsReloading() ? TEXT("RELOADING...")
				: Slot->Data->bInfiniteAmmo ? TEXT("INF")
				: FString::Printf(TEXT("%d / %d"), Slot->Clip, Slot->Reserve);
			float TW = 0.f, TH = 0.f;
			GetTextSize(WeaponName, TW, TH, Medium, UI);
			DrawTextShadowed(WeaponName, RightX - TW, BarY - 34.f * UI, Amber, Medium, UI);
			GetTextSize(Ammo, TW, TH, Large, UI);
			DrawTextShadowed(Ammo, RightX - TW, BarY - 6.f * UI, FLinearColor::White, Large, UI);
		}
		if (Bot->GrenadeWeapon && !Bot->IsHijacked())
		{
			const FString GrenadeText = FString::Printf(TEXT("[G] %s x%d"), *Bot->GrenadeWeapon->DisplayName.ToString().ToUpper(), Bot->Grenades);
			float TW = 0.f, TH = 0.f;
			GetTextSize(GrenadeText, TW, TH, Small, UI * 1.3f);
			DrawTextShadowed(GrenadeText, RightX - TW, BarY + 34.f * UI, FLinearColor(0.6f, 0.75f, 1.f), Small, UI * 1.3f);
		}
		// Weapon list (1-6).
		const TArray<FMetalWeaponSlot>& Weapons = Bot->GetWeapons();
		for (int32 Index = 0; Index < Weapons.Num() && !Bot->IsHijacked(); ++Index)
		{
			if (Weapons[Index].Data)
			{
				const bool bCurrent = Index == Bot->GetCurrentWeaponIndex();
				const FString Entry = FString::Printf(TEXT("%d %s"), Index + 1, *Weapons[Index].Data->DisplayName.ToString());
				float TW = 0.f, TH = 0.f;
				GetTextSize(Entry, TW, TH, Small, UI * 1.2f);
				DrawTextShadowed(Entry, RightX - TW, BarY - (70.f + 22.f * (Weapons.Num() - 1 - Index)) * UI, bCurrent ? Amber : FLinearColor(1.f, 1.f, 1.f, 0.6f), Small, UI * 1.2f);
			}
		}

	}

	if (Bot && Bot->IsAlive())
	{
		// Possession timer.
		if (PC->GetHost())
		{
			const float Fraction = PC->GetPossessionTimeRemaining() / FMath::Max(1.f, PC->GetPossessionDuration());
			const float TimerW = 420.f * UI;
			DrawTextShadowed(FString::Printf(TEXT("HACK TETHER  %.0fs   [F] disconnect"), PC->GetPossessionTimeRemaining()), W * 0.5f, 92.f * UI, Cyan, Medium, UI, true);
			DrawBar(W * 0.5f - TimerW * 0.5f, 122.f * UI, TimerW, 10.f * UI, Fraction, Fraction < 0.25f && FMath::Fmod(Now, 0.4f) < 0.2f ? Danger : Cyan);
		}

		if (AMetalControlConsole* Console = PC->GetNearbyConsole())
		{
			DrawTextShadowed(Console->PromptText.ToString(), W * 0.5f, H * 0.62f, FLinearColor::White, Medium, UI, true);
		}
	}

	if (GameMode)
	{
		// Objective (top-left).
		const FText Objective = GameMode->GetObjectiveText();
		if (!Objective.IsEmpty())
		{
			DrawTextShadowed(TEXT("OBJECTIVE"), 40.f * UI, 30.f * UI, Amber, Small, UI * 1.2f);
			DrawTextShadowed(Objective.ToString(), 40.f * UI, 50.f * UI, FLinearColor::White, Medium, UI);
		}
		// Dialogue subtitles.
		if (const FMetalMessage* Line = GameMode->GetDialogue())
		{
			const FString Text = FString::Printf(TEXT("%s: %s"), *Line->Speaker.ToString().ToUpper(), *Line->Text.ToString());
			float TW = 0.f, TH = 0.f;
			GetTextSize(Text, TW, TH, Medium, UI);
			DrawRect(Panel, W * 0.5f - TW * 0.5f - 14.f, H - 190.f * UI - 8.f, TW + 28.f, TH + 16.f);
			DrawTextShadowed(Text, W * 0.5f, H - 190.f * UI, FLinearColor(1.f, 0.95f, 0.8f), Medium, UI, true);
		}
		// System notices.
		if (const FMetalMessage* Notice = GameMode->GetNotice())
		{
			DrawTextShadowed(Notice->Text.ToString(), W * 0.5f, H * 0.30f, FLinearColor::White, Medium, UI, true);
		}
		// Banners.
		if (const FMetalMessage* Banner = GameMode->GetBanner())
		{
			DrawTextShadowed(Banner->Text.ToString(), W * 0.5f, H * 0.38f, Amber, Large, UI * 2.2f, true);
		}
		if (GameMode->IsMissionComplete())
		{
			const float Time = GameMode->GetMissionTime();
			const FString Stats = FString::Printf(TEXT("Time %d:%02d    Enemies destroyed %d"), FMath::FloorToInt(Time / 60.f), FMath::FloorToInt(FMath::Fmod(Time, 60.f)), GameMode->GetKills());
			DrawTextShadowed(Stats, W * 0.5f, H * 0.48f, FLinearColor::White, Medium, UI * 1.2f, true);
			DrawTextShadowed(TEXT("Press ENTER to play again"), W * 0.5f, H * 0.54f, FLinearColor(1.f, 1.f, 1.f, 0.7f), Medium, UI, true);
		}
	}
}
