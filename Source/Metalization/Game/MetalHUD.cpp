#include "MetalHUD.h"

#include "MetalGameMode.h"
#include "MetalBotCharacter.h"
#include "MetalControlConsole.h"
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
			const AMetalBotCharacter* Candidate = PC->GetHackCandidate();
			const TCHAR* Verb = Candidate->GetTeam() == Bot->GetTeam() ? TEXT("PILOT") : TEXT("HACK");
			DrawTextShadowed(FString::Printf(TEXT("[F] %s %s"), Verb, *Candidate->GetDisplayName().ToString().ToUpper()), CX, CY + 30.f * UI, Cyan, Medium, UI, true);
		}

		// Health.
		const float BarW = 360.f * UI;
		const float BarX = 40.f * UI;
		const float BarY = H - 70.f * UI;
		const float HealthFraction = Bot->GetHealth() / FMath::Max(1.f, Bot->GetMaxHealth());
		const FString Name = Bot->GetDisplayName().ToString().ToUpper() + (Bot->IsHijacked() ? TEXT("  (HIJACKED)") : TEXT(""));
		DrawTextShadowed(Name, BarX, BarY - 34.f * UI, Bot->IsHijacked() ? Cyan : Amber, Medium, UI);
		DrawBar(BarX, BarY, BarW, 18.f * UI, HealthFraction, HealthFraction < 0.3f ? Danger : FLinearColor(0.3f, 1.f, 0.4f));

		// Flying: airspeed and the boost meter.
		if (const AMetalStalker* Flyer = Cast<AMetalStalker>(Bot))
		{
			DrawLeadReticle(Flyer, UI);
			const int32 Speed = FMath::RoundToInt(Flyer->GetVelocity().Size() / 100.f);
			DrawTextShadowed(FString::Printf(TEXT("SPEED %d m/s%s"), Speed, Flyer->IsBoosting() ? TEXT("   BOOST") : TEXT("")), BarX, BarY - 64.f * UI, FLinearColor::White, Small, UI * 1.3f);
			const float Boost = Flyer->GetBoostFraction();
			DrawBar(BarX, BarY + 30.f * UI, BarW, 8.f * UI, Boost, Flyer->IsBoosting() ? Amber : Cyan);
		}

		// Weapon and ammo.
		const float RightX = W - 40.f * UI;
		if (const FMetalWeaponSlot* Slot = Bot->GetCurrentWeapon(); Slot && Slot->Data)
		{
			const FString WeaponName = FString::Printf(TEXT("%s  LV%d"), *Slot->Data->DisplayName.ToString().ToUpper(), Slot->Level + 1);
			const int32 Guns = Bot->GetMuzzleCount();
			const FString Ammo = Bot->IsWeaponDisabled() ? (Guns > 1 ? TEXT("GUNS DESTROYED") : TEXT("WEAPON ARM DESTROYED"))
				: Bot->IsReloading() ? TEXT("RELOADING...")
				: Guns > 1 && Bot->GetWorkingMuzzleCount() < Guns ? FString::Printf(TEXT("GUNS %d / %d"), Bot->GetWorkingMuzzleCount(), Guns)
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
		// Possession timer (friendly links have none).
		if (PC->GetHost() && PC->IsAllyLink())
		{
			DrawTextShadowed(TEXT("LINKED   [F] disconnect"), W * 0.5f, 92.f * UI, Cyan, Medium, UI, true);
		}
		else if (PC->GetHost())
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

	if (AMetalSpaceBattleGameMode* Battle = Cast<AMetalSpaceBattleGameMode>(GameMode); Battle && !Battle->IsMissionComplete())
	{
		DrawSpaceBattle(Battle, PC ? PC->GetPawn() : nullptr, UI);
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
		const AMetalCapitalShip* Ship = Battle->GetShip(Team);
		if (Ship == nullptr)
		{
			return;
		}
		const float BarW = 240.f * UI;
		const float Hull = Ship->GetHullHealth() / FMath::Max(1.f, Ship->GetMaxHullHealth());
		DrawBar(CenterX - BarW * 0.5f, 56.f * UI, BarW, 8.f * UI, Hull, Ship->IsShielded() ? FLinearColor(0.4f, 0.8f, 1.f) : FLinearColor(1.f, 0.6f, 0.2f));
		const TArray<TObjectPtr<UMetalShipSubsystemComponent>>& Parts = Ship->GetSubsystems();
		const float Pip = 14.f * UI;
		const float Gap = 6.f * UI;
		float X = CenterX - (Parts.Num() * (Pip + Gap) - Gap) * 0.5f;
		for (const UMetalShipSubsystemComponent* Part : Parts)
		{
			const float Fraction = Part->GetHealth() / FMath::Max(1.f, Part->GetMaxHealth());
			DrawRect(FLinearColor(0.f, 0.f, 0.f, 0.6f), X, 72.f * UI, Pip, Pip);
			DrawRect(Part->IsDestroyed() ? FLinearColor(0.25f, 0.05f, 0.05f) : FLinearColor::LerpUsingHSV(FLinearColor(1.f, 0.2f, 0.1f), FLinearColor(0.3f, 1.f, 0.4f), Fraction),
				X + 2.f, 72.f * UI + 2.f, Pip - 4.f, Pip - 4.f);
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
