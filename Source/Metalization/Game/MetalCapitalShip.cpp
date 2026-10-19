#include "MetalCapitalShip.h"

#include "MetalBotCharacter.h"
#include "MetalFX.h"
#include "MetalHangar.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Engine/DamageEvents.h"
#include "Engine/Level.h"
#include "Engine/LevelStreamingDynamic.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshSocket.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerStart.h"
#include "Materials/MaterialInstanceDynamic.h"

FOnMetalShipEvent AMetalCapitalShip::OnAnyInteriorReady;
FOnMetalShipEvent AMetalCapitalShip::OnAnyShipDestroyed;

namespace
{
	const FLinearColor Burnt(0.03f, 0.03f, 0.035f);
	const FLinearColor Fire(1.f, 0.45f, 0.1f);
	constexpr int32 MaxTurrets = 32;
}

// --------------------------------------------------------------------------------------------
// Subsystems
// --------------------------------------------------------------------------------------------

void UMetalShipSubsystemComponent::ResetHealth(float NewMaxHealth)
{
	MaxHealth = FMath::Max(1.f, NewMaxHealth);
	Health = MaxHealth;
}

bool UMetalShipSubsystemComponent::ApplyDamage(float Amount)
{
	if (IsDestroyed())
	{
		return false;
	}
	Health = FMath::Max(0.f, Health - Amount);
	return IsDestroyed();
}

// --------------------------------------------------------------------------------------------
// Ship
// --------------------------------------------------------------------------------------------

AMetalCapitalShip::AMetalCapitalShip()
{
	PrimaryActorTick.bCanEverTick = true;

	Hull = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Hull"));
	Hull->SetCollisionProfileName(UCollisionProfile::BlockAll_ProfileName);
	RootComponent = Hull;

	auto MakeSubsystem = [this](const TCHAR* Name, FName Socket, EMetalShipSubsystem Type)
	{
		UMetalShipSubsystemComponent* Part = CreateDefaultSubobject<UMetalShipSubsystemComponent>(Name);
		Part->SetupAttachment(Hull, Socket);
		Part->SetCollisionProfileName(UCollisionProfile::BlockAll_ProfileName);
		Part->Type = Type;
		return Part;
	};
	Engines = MakeSubsystem(TEXT("Engines"), TEXT("engines"), EMetalShipSubsystem::Engines);
	ShieldGeneratorL = MakeSubsystem(TEXT("ShieldGeneratorL"), TEXT("shieldgen_L"), EMetalShipSubsystem::ShieldGenerator);
	ShieldGeneratorR = MakeSubsystem(TEXT("ShieldGeneratorR"), TEXT("shieldgen_R"), EMetalShipSubsystem::ShieldGenerator);
	Comms = MakeSubsystem(TEXT("Comms"), TEXT("comms"), EMetalShipSubsystem::Comms);

	TurretBases = CreateDefaultSubobject<UInstancedStaticMeshComponent>(TEXT("TurretBases"));
	TurretBases->SetupAttachment(Hull);
	TurretBases->SetCollisionProfileName(UCollisionProfile::BlockAll_ProfileName);
	TurretGuns = CreateDefaultSubobject<UInstancedStaticMeshComponent>(TEXT("TurretGuns"));
	TurretGuns->SetupAttachment(Hull);
	TurretGuns->SetCollisionProfileName(UCollisionProfile::BlockAll_ProfileName);
}

void AMetalCapitalShip::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);
	// So level designers see the whole ship when placing it.
	ApplyShipData();
}

void AMetalCapitalShip::ApplyShipData()
{
	if (ShipData == nullptr)
	{
		return;
	}
	TArray<UStaticMeshComponent*> Parts;
	GetComponents<UStaticMeshComponent>(Parts);
	for (UStaticMeshComponent* Part : Parts)
	{
		Part->SetCullDistance(DrawDistance);
	}
	Hull->SetStaticMesh(ShipData->HullMesh);
	Engines->SetStaticMesh(ShipData->EnginesMesh);
	ShieldGeneratorL->SetStaticMesh(ShipData->ShieldGeneratorMesh);
	ShieldGeneratorR->SetStaticMesh(ShipData->ShieldGeneratorMesh);
	Comms->SetStaticMesh(ShipData->CommsMesh);

	// One turret per turret_NN socket on the hull, the gun on top of the base's "gun" socket.
	TurretBases->ClearInstances();
	TurretGuns->ClearInstances();
	TurretBases->SetStaticMesh(ShipData->TurretBaseMesh);
	TurretGuns->SetStaticMesh(ShipData->TurretGunMesh);
	const UStaticMeshSocket* GunSocket = ShipData->TurretBaseMesh ? ShipData->TurretBaseMesh->FindSocket(TEXT("gun")) : nullptr;
	const FTransform GunOffset = GunSocket ? FTransform(GunSocket->RelativeRotation, GunSocket->RelativeLocation) : FTransform::Identity;
	for (int32 Index = 1; ShipData->HullMesh && Index <= MaxTurrets; ++Index)
	{
		const UStaticMeshSocket* Socket = ShipData->HullMesh->FindSocket(*FString::Printf(TEXT("turret_%02d"), Index));
		if (Socket == nullptr)
		{
			break;
		}
		const FTransform Mount(Socket->RelativeRotation, Socket->RelativeLocation);
		TurretBases->AddInstance(Mount);
		TurretGuns->AddInstance(GunOffset * Mount);
	}
}

void AMetalCapitalShip::BeginPlay()
{
	Super::BeginPlay();
	ApplyShipData();
	Subsystems = {Engines, ShieldGeneratorL, ShieldGeneratorR, Comms};
	if (ShipData)
	{
		MaxHullHealth = ShipData->HullHealth;
		Engines->ResetHealth(ShipData->EnginesHealth);
		ShieldGeneratorL->ResetHealth(ShipData->ShieldGeneratorHealth);
		ShieldGeneratorR->ResetHealth(ShipData->ShieldGeneratorHealth);
		Comms->ResetHealth(ShipData->CommsHealth);
	}
	HullHealth = MaxHullHealth;
	ApplyTeamColors();
	LoadInterior();
}

void AMetalCapitalShip::ApplyTeamColors()
{
	const FMetalTeamColors* Colors = ShipData ? ShipData->TeamColors.Find(Team) : nullptr;
	TArray<UStaticMeshComponent*> Parts;
	GetComponents<UStaticMeshComponent>(Parts);
	for (UStaticMeshComponent* Part : Parts)
	{
		const TArray<FName> Slots = Part->GetMaterialSlotNames();
		for (int32 Index = 0; Index < Slots.Num(); ++Index)
		{
			// Every slot gets its own instance so hits can flash it.
			UMaterialInstanceDynamic* MID = Part->CreateDynamicMaterialInstance(Index);
			const FString Slot = Slots[Index].ToString();
			if (MID && Colors && Slot.EndsWith(TEXT("_Hull")))
			{
				MID->SetVectorParameterValue(TEXT("Tint"), Colors->Body);
			}
			else if (MID && Colors && Slot.EndsWith(TEXT("_Accent")))
			{
				MID->SetVectorParameterValue(TEXT("Tint"), Colors->Accent);
			}
		}
	}
}

// --------------------------------------------------------------------------------------------
// Interior
// --------------------------------------------------------------------------------------------

void AMetalCapitalShip::LoadInterior()
{
	if (ShipData == nullptr || ShipData->Interior.IsNull())
	{
		UE_LOG(LogMetal, Warning, TEXT("%s has no interior level; nobody can spawn aboard."), *GetName());
		return;
	}
	bool bSuccess = false;
	InteriorStreaming = ULevelStreamingDynamic::LoadLevelInstanceBySoftObjectPtr(this, ShipData->Interior, InteriorLocation, FRotator::ZeroRotator, bSuccess);
	if (!bSuccess || InteriorStreaming == nullptr)
	{
		UE_LOG(LogMetal, Error, TEXT("%s could not load its interior %s."), *GetName(), *ShipData->Interior.ToString());
		return;
	}
	InteriorStreaming->OnLevelShown.AddDynamic(this, &AMetalCapitalShip::HandleInteriorShown);
}

ULevel* AMetalCapitalShip::GetInteriorLevel() const
{
	return InteriorStreaming ? InteriorStreaming->GetLoadedLevel() : nullptr;
}

bool AMetalCapitalShip::IsInInterior(const AActor* Actor) const
{
	const ULevel* Level = GetInteriorLevel();
	return Actor && Level && Actor->GetLevel() == Level;
}

void AMetalCapitalShip::HandleInteriorShown()
{
	if (bInteriorReady)
	{
		return;
	}
	ULevel* Level = GetInteriorLevel();
	if (Level == nullptr)
	{
		return;
	}
	for (AActor* Actor : Level->Actors)
	{
		if (AMetalHangar* Found = Cast<AMetalHangar>(Actor))
		{
			Hangar = Found;
			Found->InitForShip(this);
			break;
		}
	}
	if (!Hangar.IsValid())
	{
		UE_LOG(LogMetal, Warning, TEXT("%s: interior %s has no AMetalHangar."), *GetName(), *ShipData->Interior.ToString());
	}
	bInteriorReady = true;
	UE_LOG(LogMetal, Display, TEXT("%s interior ready at %s (%d spawn points)"), *GetName(), *InteriorLocation.ToCompactString(), GetSpawnPoints().Num());
	OnInteriorReady();
	OnAnyInteriorReady.Broadcast(this);
}

TArray<APlayerStart*> AMetalCapitalShip::GetSpawnPoints_Implementation() const
{
	TArray<APlayerStart*> Starts;
	if (const ULevel* Level = GetInteriorLevel())
	{
		for (AActor* Actor : Level->Actors)
		{
			if (APlayerStart* Start = Cast<APlayerStart>(Actor))
			{
				Starts.Add(Start);
			}
		}
	}
	return Starts;
}

void AMetalCapitalShip::OnInteriorReady_Implementation()
{
}

// --------------------------------------------------------------------------------------------
// Damage
// --------------------------------------------------------------------------------------------

bool AMetalCapitalShip::IsShielded() const
{
	return !ShieldGeneratorL->IsDestroyed() || !ShieldGeneratorR->IsDestroyed();
}

AMetalBotCharacter* AMetalCapitalShip::FindAttacker(AController* EventInstigator, AActor* DamageCauser) const
{
	AMetalBotCharacter* Attacker = Cast<AMetalBotCharacter>(DamageCauser);
	if (Attacker == nullptr && DamageCauser)
	{
		Attacker = Cast<AMetalBotCharacter>(DamageCauser->GetInstigator());
	}
	if (Attacker == nullptr && EventInstigator)
	{
		Attacker = Cast<AMetalBotCharacter>(EventInstigator->GetPawn());
	}
	return Attacker;
}

float AMetalCapitalShip::TakeDamage(float DamageAmount, FDamageEvent const& DamageEvent, AController* EventInstigator, AActor* DamageCauser)
{
	if (bShipDestroyed || DamageAmount <= 0.f)
	{
		return 0.f;
	}
	if (const AMetalBotCharacter* Attacker = FindAttacker(EventInstigator, DamageCauser); Attacker && Attacker->GetTeam() == Team)
	{
		return 0.f; // no friendly fire
	}
	const float Damage = Super::TakeDamage(DamageAmount, DamageEvent, EventInstigator, DamageCauser);

	UPrimitiveComponent* HitComponent = nullptr;
	if (DamageEvent.IsOfType(FPointDamageEvent::ClassID))
	{
		HitComponent = static_cast<const FPointDamageEvent&>(DamageEvent).HitInfo.GetComponent();
	}
	else if (DamageEvent.IsOfType(FRadialDamageEvent::ClassID))
	{
		const FRadialDamageEvent& Radial = static_cast<const FRadialDamageEvent&>(DamageEvent);
		HitComponent = Radial.ComponentHits.Num() > 0 ? Radial.ComponentHits[0].GetComponent() : nullptr;
	}

	// Subsystems soak their own hits; everything else is hull, shrugged off while the shields hold.
	if (UMetalShipSubsystemComponent* Subsystem = Cast<UMetalShipSubsystemComponent>(HitComponent); Subsystem && !Subsystem->IsDestroyed())
	{
		FlashComponent(Subsystem);
		if (Subsystem->ApplyDamage(Damage))
		{
			DestroySubsystem(Subsystem);
		}
		return Damage;
	}
	const float HullDamage = Damage * (IsShielded() && ShipData ? ShipData->ShieldedHullDamageScale : 1.f);
	HullHealth = FMath::Max(0.f, HullHealth - HullDamage);
	FlashComponent(HitComponent ? HitComponent : Hull.Get());
	if (HullHealth <= 0.f)
	{
		DestroyShip();
	}
	return HullDamage;
}

void AMetalCapitalShip::FlashComponent(UPrimitiveComponent* Component)
{
	if (Component && Component->GetOwner() == this)
	{
		HitFlash.Add(Component, 1.f);
	}
}

void AMetalCapitalShip::DestroySubsystem(UMetalShipSubsystemComponent* Subsystem)
{
	// Burnt out: blackened, and its glow strips go dark red.
	for (int32 Index = 0; Index < Subsystem->GetNumMaterials(); ++Index)
	{
		if (UMaterialInstanceDynamic* MID = Cast<UMaterialInstanceDynamic>(Subsystem->GetMaterial(Index)))
		{
			MID->SetVectorParameterValue(TEXT("Tint"), Burnt);
			MID->SetVectorParameterValue(TEXT("Color"), FLinearColor(0.25f, 0.02f, 0.f));
		}
	}
	HitFlash.Remove(Subsystem);
	AMetalFX::SpawnBurst(GetWorld(), Subsystem->Bounds.Origin, Fire, Subsystem->Bounds.SphereRadius * 1.2f, 1.2f);
	UE_LOG(LogMetal, Display, TEXT("%s lost subsystem %s"), *GetName(), *Subsystem->GetName());
	OnSubsystemDestroyed(Subsystem);
}

void AMetalCapitalShip::OnSubsystemDestroyed_Implementation(UMetalShipSubsystemComponent* Subsystem)
{
}

void AMetalCapitalShip::DestroyShip()
{
	if (bShipDestroyed)
	{
		return;
	}
	bShipDestroyed = true;
	DestroyedTime = GetWorld()->GetTimeSeconds();
	UE_LOG(LogMetal, Display, TEXT("%s destroyed"), *GetName());
	OnShipDestroyed();
	OnAnyShipDestroyed.Broadcast(this);
}

void AMetalCapitalShip::OnShipDestroyed_Implementation()
{
}

void AMetalCapitalShip::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	for (auto It = HitFlash.CreateIterator(); It; ++It)
	{
		It.Value() = FMath::Max(0.f, It.Value() - DeltaSeconds * 5.f);
		if (UPrimitiveComponent* Component = It.Key())
		{
			for (int32 Index = 0; Index < Component->GetNumMaterials(); ++Index)
			{
				if (UMaterialInstanceDynamic* MID = Cast<UMaterialInstanceDynamic>(Component->GetMaterial(Index)))
				{
					MID->SetScalarParameterValue(TEXT("HitFlash"), It.Value() * 0.6f);
				}
			}
		}
		if (It.Value() <= 0.f)
		{
			It.RemoveCurrent();
		}
	}

	// A destroyed ship breaks up in a chain of explosions along the hull.
	if (bShipDestroyed && WreckBursts < 24 && GetWorld()->GetTimeSeconds() - DestroyedTime > WreckBursts * 0.25f)
	{
		const FBox Box = Hull->Bounds.GetBox();
		AMetalFX::SpawnBurst(GetWorld(), FMath::RandPointInBox(Box), Fire, 2500.f + 2000.f * FMath::FRand(), 1.5f);
		++WreckBursts;
	}
}
