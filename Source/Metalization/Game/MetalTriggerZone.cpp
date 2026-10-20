#include "MetalTriggerZone.h"

#include "MetalGameMode.h"
#include "MetalBotCharacter.h"
#include "MetalFX.h"
#include "MetalPlayerController.h"
#include "Components/BoxComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "UObject/ConstructorHelpers.h"

AMetalTriggerZone::AMetalTriggerZone()
{
	Box = CreateDefaultSubobject<UBoxComponent>(TEXT("Box"));
	Box->SetBoxExtent(FVector(200.f, 200.f, 150.f));
	Box->SetCollisionProfileName(TEXT("Trigger"));
	Box->SetCanEverAffectNavigation(false);
	RootComponent = Box;

	Marker = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Marker"));
	Marker->SetupAttachment(Box);
	Marker->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Marker->SetCastShadow(false);
	Marker->SetCanEverAffectNavigation(false);
	static ConstructorHelpers::FObjectFinder<UStaticMesh> Cylinder(TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
	if (Cylinder.Succeeded())
	{
		Marker->SetStaticMesh(Cylinder.Object);
	}
}

void AMetalTriggerZone::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);
	const FVector Extent = Box->GetUnscaledBoxExtent();
	// Thin glowing disc on the floor of the volume.
	Marker->SetRelativeLocation(FVector(0.f, 0.f, -Extent.Z + 1.f));
	Marker->SetRelativeScale3D(FVector(Extent.X / 50.f, Extent.Y / 50.f, 0.02f));
	Marker->SetVisibility(bShowMarker);
}

void AMetalTriggerZone::BeginPlay()
{
	Super::BeginPlay();
	Box->OnComponentBeginOverlap.AddDynamic(this, &AMetalTriggerZone::OnOverlap);
	if (UMaterialInterface* Glow = MetalAssets::GetGlowMaterial())
	{
		UMaterialInstanceDynamic* MID = UMaterialInstanceDynamic::Create(Glow, this);
		MID->SetVectorParameterValue(TEXT("Color"), MarkerColor);
		MID->SetScalarParameterValue(TEXT("Intensity"), 1.5f);
		Marker->SetMaterial(0, MID);
	}
}

bool AMetalTriggerZone::ContainsActor(const AActor* Actor) const
{
	return Actor && Box->IsOverlappingActor(Actor);
}

void AMetalTriggerZone::OnOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor, UPrimitiveComponent* OtherComp, int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult)
{
	AMetalBotCharacter* Bot = Cast<AMetalBotCharacter>(OtherActor);
	const AMetalPlayerController* PC = Bot ? Cast<AMetalPlayerController>(Bot->GetController()) : nullptr;
	// Hijacked robots don't count: respawning puts the player's own body at the checkpoint.
	if (!bIsCheckpoint || bTriggered || PC == nullptr || PC->GetPlayerBody() != Bot)
	{
		return;
	}
	bTriggered = true;
	if (AMetalGameMode* GameMode = GetWorld()->GetAuthGameMode<AMetalGameMode>())
	{
		GameMode->SetCheckpoint(FTransform(Bot->GetActorRotation(), GetActorLocation()));
	}
	Bot->Heal(Bot->GetMaxHealth());
	Marker->SetVisibility(false);
}
