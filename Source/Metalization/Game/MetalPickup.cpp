#include "MetalPickup.h"

#include "MetalAudio.h"
#include "MetalGameMode.h"
#include "MetalBotCharacter.h"
#include "MetalFX.h"
#include "MetalPlayerController.h"
#include "MetalWeaponData.h"
#include "Components/PointLightComponent.h"
#include "Components/SphereComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "UObject/ConstructorHelpers.h"

AMetalPickup::AMetalPickup()
{
	PrimaryActorTick.bCanEverTick = true;

	Trigger = CreateDefaultSubobject<USphereComponent>(TEXT("Trigger"));
	Trigger->InitSphereRadius(70.f);
	Trigger->SetCollisionProfileName(TEXT("Trigger"));
	Trigger->SetCanEverAffectNavigation(false);
	RootComponent = Trigger;

	Visual = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Visual"));
	Visual->SetupAttachment(Trigger);
	Visual->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Visual->SetCanEverAffectNavigation(false);

	Light = CreateDefaultSubobject<UPointLightComponent>(TEXT("Light"));
	Light->SetupAttachment(Trigger);
	Light->SetAttenuationRadius(250.f);
	Light->SetIntensity(800.f);
	Light->SetCastShadows(false);
}

UStaticMesh* AMetalPickup::GetModelMesh() const
{
	// Loaded by path, so Config/DefaultGame.ini cooks /Game/Metalization/Props.
	const TCHAR* Path = Type == EMetalPickupType::Health ? TEXT("/Game/Metalization/Props/SM_Wrench.SM_Wrench")
		: Type == EMetalPickupType::Ammo ? TEXT("/Game/Metalization/Props/SM_AmmoCrate.SM_AmmoCrate")
		: nullptr;
	return Path ? LoadObject<UStaticMesh>(nullptr, Path) : nullptr;
}

FLinearColor AMetalPickup::GetColor() const
{
	switch (Type)
	{
	case EMetalPickupType::Health:   return FLinearColor(0.1f, 1.f, 0.3f);
	case EMetalPickupType::Ammo:     return FLinearColor(1.f, 0.75f, 0.1f);
	case EMetalPickupType::Grenades: return FLinearColor(0.4f, 0.6f, 1.f);
	default:                         return FLinearColor(1.f, 0.3f, 0.9f);
	}
}

void AMetalPickup::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);
	UpdateVisual();
}

void AMetalPickup::UpdateVisual()
{
	// Weapons show their prop model, repairs a wrench and ammo an ammo crate (graybox models from
	// Art/Graybox/Pickups), and grenades a glowing sphere. A missing model falls back to a glowing cube.
	UStaticMesh* Mesh = nullptr;
	FVector Scale(1.f);
	FRotator Rotation = FRotator::ZeroRotator;
	if (Type == EMetalPickupType::Weapon && Weapon && Weapon->PropMesh)
	{
		Mesh = Weapon->PropMesh;
		Scale = FVector(1.6f);
	}
	else if (UStaticMesh* Model = GetModelMesh())
	{
		Mesh = Model;
		// The wrench is modelled lying flat: stand it on edge, tilted, so its outline shows as it spins.
		Scale = FVector(Type == EMetalPickupType::Health ? 1.7f : 1.2f);
		Rotation = Type == EMetalPickupType::Health ? FRotator(0.f, 0.f, 70.f) : FRotator::ZeroRotator;
	}
	else
	{
		Mesh = LoadObject<UStaticMesh>(nullptr, Type == EMetalPickupType::Grenades ? TEXT("/Engine/BasicShapes/Sphere.Sphere") : TEXT("/Engine/BasicShapes/Cube.Cube"));
		Scale = FVector(0.35f);
	}
	Visual->SetStaticMesh(Mesh);
	Visual->SetRelativeScale3D(Scale);
	Visual->SetRelativeRotation(Rotation);
	Light->SetLightColor(GetColor());
}

void AMetalPickup::BeginPlay()
{
	Super::BeginPlay();
	// Again at runtime: pickups placed in a map keep the mesh they were saved with, which predates the models.
	UpdateVisual();
	VisualBase = Visual->GetRelativeLocation();
	Trigger->OnComponentBeginOverlap.AddDynamic(this, &AMetalPickup::OnOverlap);
	// Weapons and the wrench/crate models keep their own materials; the plain shapes glow in the type's colour.
	const bool bHasModel = Type == EMetalPickupType::Weapon ? Weapon && Weapon->PropMesh : GetModelMesh() != nullptr;
	if (!bHasModel)
	{
		if (UMaterialInterface* Glow = MetalAssets::GetGlowMaterial())
		{
			UMaterialInstanceDynamic* MID = UMaterialInstanceDynamic::Create(Glow, this);
			MID->SetVectorParameterValue(TEXT("Color"), GetColor());
			MID->SetScalarParameterValue(TEXT("Intensity"), 3.f);
			Visual->SetMaterial(0, MID);
		}
	}
}

void AMetalPickup::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	Age += DeltaSeconds;
	Visual->SetRelativeLocation(VisualBase + FVector(0.f, 0.f, 10.f * FMath::Sin(Age * 2.5f)));
	Visual->AddRelativeRotation(FRotator(0.f, 90.f * DeltaSeconds, 0.f));
}

void AMetalPickup::OnOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor, UPrimitiveComponent* OtherComp, int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult)
{
	AMetalBotCharacter* Bot = Cast<AMetalBotCharacter>(OtherActor);
	AMetalPlayerController* PC = Bot ? Cast<AMetalPlayerController>(Bot->GetController()) : nullptr;
	// Only the player's own body collects items (a hijacked robot is about to blow up anyway).
	if (PC == nullptr || PC->GetPlayerBody() != Bot || !Bot->IsAlive())
	{
		return;
	}

	bool bUsed = false;
	FText Message;
	switch (Type)
	{
	case EMetalPickupType::Health:
		bUsed = Bot->GetHealth() < Bot->GetMaxHealth();
		Bot->Heal(Amount);
		Message = NSLOCTEXT("Metalization", "PickupHealth", "Repair kit");
		break;
	case EMetalPickupType::Ammo:
		bUsed = Bot->AddAmmoFraction(Amount);
		Message = NSLOCTEXT("Metalization", "PickupAmmo", "Ammo");
		break;
	case EMetalPickupType::Grenades:
		bUsed = true;
		Bot->Grenades += FMath::RoundToInt(Amount);
		Message = FText::Format(NSLOCTEXT("Metalization", "PickupGrenades", "+{0} grenades"), FMath::RoundToInt(Amount));
		break;
	case EMetalPickupType::Weapon:
		if (Weapon)
		{
			const bool bOwned = Bot->GetWeapons().ContainsByPredicate([this](const FMetalWeaponSlot& Slot) { return Slot.Data == Weapon; });
			bUsed = Bot->GiveWeapon(Weapon, true);
			Message = FText::Format(bOwned ? NSLOCTEXT("Metalization", "PickupUpgrade", "{0} UPGRADED") : NSLOCTEXT("Metalization", "PickupWeapon", "New weapon: {0}"), Weapon->DisplayName);
		}
		break;
	}

	if (bUsed)
	{
		if (AMetalGameMode* GameMode = GetWorld()->GetAuthGameMode<AMetalGameMode>())
		{
			GameMode->ShowMessage(FText::GetEmpty(), Message, 2.f);
		}
		AMetalFX::SpawnBurst(GetWorld(), GetActorLocation(), GetColor(), 40.f, 0.25f);
		// Only the player collects pickups, so the sound is theirs: non-positional, never lost behind the camera.
		MetalAudio::Play2D(this, TEXT("pickup"), 0.8f);
		Destroy();
	}
}
