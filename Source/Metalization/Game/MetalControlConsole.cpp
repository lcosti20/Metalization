#include "MetalControlConsole.h"

#include "MetalAudio.h"

#include "MetalBotArchetype.h"
#include "MetalBotCharacter.h"
#include "MetalFX.h"
#include "MetalGate.h"
#include "Components/PointLightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "UObject/ConstructorHelpers.h"

namespace
{
	const FLinearColor LockedColor(1.f, 0.15f, 0.05f);
	const FLinearColor OpenColor(0.1f, 1.f, 0.3f);
}

AMetalControlConsole::AMetalControlConsole()
{
	static ConstructorHelpers::FObjectFinder<UStaticMesh> Cube(TEXT("/Engine/BasicShapes/Cube.Cube"));

	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));

	Base = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Base"));
	Base->SetupAttachment(RootComponent);
	Base->SetStaticMesh(Cube.Object);
	Base->SetRelativeScale3D(FVector(0.6f, 1.0f, 1.1f));
	Base->SetRelativeLocation(FVector(0.f, 0.f, 55.f));

	// Screen faces the actor's +X side, tilted back towards the operator.
	Screen = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Screen"));
	Screen->SetupAttachment(RootComponent);
	Screen->SetStaticMesh(Cube.Object);
	Screen->SetRelativeLocation(FVector(33.f, 0.f, 92.f));
	Screen->SetRelativeRotation(FRotator(25.f, 0.f, 0.f));
	Screen->SetRelativeScale3D(FVector(0.05f, 0.8f, 0.45f));
	Screen->SetCollisionEnabled(ECollisionEnabled::NoCollision);

	Light = CreateDefaultSubobject<UPointLightComponent>(TEXT("Light"));
	Light->SetupAttachment(RootComponent);
	Light->SetRelativeLocation(FVector(70.f, 0.f, 100.f));
	Light->SetIntensity(1500.f);
	Light->SetAttenuationRadius(350.f);
	Light->SetCastShadows(false);
}

void AMetalControlConsole::BeginPlay()
{
	Super::BeginPlay();
	if (UMaterialInterface* Glow = MetalAssets::GetGlowMaterial())
	{
		ScreenMaterial = UMaterialInstanceDynamic::Create(Glow, this);
		ScreenMaterial->SetScalarParameterValue(TEXT("Intensity"), 4.f);
		Screen->SetMaterial(0, ScreenMaterial);
	}
	SetScreenColor(LockedColor);
}

void AMetalControlConsole::SetScreenColor(const FLinearColor& Color)
{
	if (ScreenMaterial)
	{
		ScreenMaterial->SetVectorParameterValue(TEXT("Color"), Color);
	}
	Light->SetLightColor(Color);
}

void AMetalControlConsole::RestoreActivated()
{
	bActivated = true;
	SetScreenColor(OpenColor);
	for (AActor* Actor : LinkedGates)
	{
		if (AMetalGate* Gate = Cast<AMetalGate>(Actor))
		{
			Gate->OpenInstantly();
		}
	}
}

FText AMetalControlConsole::TryActivate(AMetalBotCharacter* User)
{
	if (bActivated)
	{
		return NSLOCTEXT("Metalization", "ConsoleDone", "The gate is already open.");
	}
	// The archetype's team, not GetTeam(): a hijacked Villain fights for the Heroes but keeps its clearance.
	// So the player's own body is refused at an enemy console and must hack a guard to use it.
	const EMetalTeam UserFaction = User && User->Archetype ? User->Archetype->Team : EMetalTeam::Neutral;
	if (UserFaction != RequiredFaction)
	{
		MetalAudio::PlayAt(this, TEXT("console_denied"), GetActorLocation(), 0.8f);
		return NSLOCTEXT("Metalization", "ConsoleDenied", "ACCESS DENIED - enemy personnel only. (Maybe borrow one?)");
	}
	bActivated = true;
	MetalAudio::PlayAt(this, TEXT("console_granted"), GetActorLocation(), 0.9f);
	SetScreenColor(OpenColor);
	AMetalFX::SpawnBurst(GetWorld(), Screen->GetComponentLocation(), OpenColor, 50.f, 0.3f);
	for (AActor* Actor : LinkedGates)
	{
		if (AMetalGate* Gate = Cast<AMetalGate>(Actor))
		{
			Gate->Open();
		}
	}
	return NSLOCTEXT("Metalization", "ConsoleOpened", "ACCESS GRANTED - gate opening.");
}
