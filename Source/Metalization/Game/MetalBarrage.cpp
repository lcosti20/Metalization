#include "MetalBarrage.h"

#include "MetalAudio.h"
#include "MetalBotCharacter.h"
#include "MetalFX.h"
#include "Components/BoxComponent.h"
#include "Engine/World.h"
#include "GameFramework/DamageType.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"
#include "TimerManager.h"

AMetalBarrage::AMetalBarrage()
{
	PrimaryActorTick.bCanEverTick = true;
	Area = CreateDefaultSubobject<UBoxComponent>(TEXT("Area"));
	Area->SetCollisionProfileName(TEXT("Trigger"));
	Area->SetBoxExtent(FVector(1000.f, 300.f, 300.f));
	RootComponent = Area;
}

void AMetalBarrage::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	const APlayerController* PC = GetWorld()->GetFirstPlayerController();
	const AMetalBotCharacter* Player = PC ? Cast<AMetalBotCharacter>(PC->GetPawn()) : nullptr;
	const float Now = GetWorld()->GetTimeSeconds();
	if (Player == nullptr || !Player->IsAlive() || !Area->IsOverlappingActor(Player))
	{
		// Give the player a moment to react the first time they step in.
		NextShotTime = FMath::Max(NextShotTime, Now + 0.8f);
		return;
	}
	if (Now < NextShotTime)
	{
		return;
	}
	NextShotTime = Now + Interval * FMath::FRandRange(0.8f, 1.2f);

	// Aim behind the player along the way they're heading, and keep the point inside the box.
	FVector Heading = Player->GetVelocity().GetSafeNormal2D();
	if (Heading.IsNearlyZero())
	{
		Heading = GetActorForwardVector();
	}
	FVector Target = Player->GetActorLocation() - Heading * TrailDistance + FVector(FMath::FRandRange(-1.f, 1.f), FMath::FRandRange(-1.f, 1.f), 0.f) * Scatter;
	const FVector Local = GetActorTransform().InverseTransformPosition(Target);
	const FVector Extent = Area->GetUnscaledBoxExtent();
	Target = GetActorTransform().TransformPosition(FVector(FMath::Clamp(Local.X, -Extent.X, Extent.X), FMath::Clamp(Local.Y, -Extent.Y, Extent.Y), Local.Z));
	// Down onto the ground.
	FHitResult Hit;
	FCollisionQueryParams Params(SCENE_QUERY_STAT(MetalBarrage), false);
	Params.AddIgnoredActor(Player);
	if (GetWorld()->LineTraceSingleByChannel(Hit, Target + FVector(0.f, 0.f, 400.f), Target - FVector(0.f, 0.f, 1000.f), ECC_Visibility, Params))
	{
		Target = Hit.ImpactPoint;
	}
	Fire(Target);
}

void AMetalBarrage::Fire(const FVector& Location)
{
	// A red warning flash and the incoming whistle, then the blast.
	AMetalFX::SpawnBurst(GetWorld(), Location + FVector(0.f, 0.f, 10.f), FLinearColor(1.f, 0.1f, 0.05f), 60.f, FlightTime);
	MetalAudio::PlayAt(this, TEXT("grenade_throw"), Location + FVector(0.f, 0.f, 600.f), 0.7f, 0.2f);
	FTimerHandle Handle;
	GetWorldTimerManager().SetTimer(Handle, FTimerDelegate::CreateWeakLambda(this, [this, Location]()
	{
		AMetalFX::SpawnBurst(GetWorld(), Location + FVector(0.f, 0.f, 60.f), FLinearColor(1.f, 0.5f, 0.15f), BlastRadius * 0.8f, 0.45f);
		MetalAudio::PlayAt(this, TEXT("explosion"), Location, 0.9f, 0.1f);
		// Damage is credited to no one, so the player's own team takes it; Red robots are never in the trench.
		UGameplayStatics::ApplyRadialDamageWithFalloff(this, Damage, Damage * 0.3f, Location, BlastRadius * 0.4f, BlastRadius, 1.f,
			UDamageType::StaticClass(), {}, this, nullptr, ECC_Visibility);
	}), FlightTime, false);
}
