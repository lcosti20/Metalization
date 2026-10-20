#include "MetalCapitalShip.h"

#include "MetalBotCharacter.h"
#include "MetalFX.h"
#include "MetalHangar.h"
#include "MetalPortal.h"
#include "MetalProjectile.h"
#include "MetalStalker.h"
#include "MetalWeaponData.h"
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
FOnMetalSubsystemEvent AMetalCapitalShip::OnAnySubsystemDestroyed;

namespace
{
	const FLinearColor Burnt(0.03f, 0.03f, 0.035f);
	const FLinearColor Fire(1.f, 0.45f, 0.1f);
	/** Highest turret_NN socket number looked for. */
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
	TurretGunOffset = GunOffset;
	Turrets.Reset();
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
		Turrets.AddDefaulted_GetRef().Mount = Mount;
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
	// Each ship loads its own instance of the interior map, offset to InteriorLocation, so two ships can
	// share one interior asset. Actors inside are only usable once the level is shown (HandleInteriorShown).
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
	CreatePortals();
	bInteriorReady = true;
	UE_LOG(LogMetal, Display, TEXT("%s interior ready at %s (%d spawn points)"), *GetName(), *InteriorLocation.ToCompactString(), GetSpawnPoints().Num());
	OnInteriorReady();
	OnAnyInteriorReady.Broadcast(this);
}

void AMetalCapitalShip::CreatePortals()
{
	AMetalHangar* InteriorHangar = Hangar.Get();
	if (InteriorHangar == nullptr || !Hull->DoesSocketExist(TEXT("hangar_portal")))
	{
		UE_LOG(LogMetal, Warning, TEXT("%s: no hangar portal (hangar %s, hull socket %d)"), *GetName(), InteriorHangar ? *InteriorHangar->GetName() : TEXT("missing"), Hull->DoesSocketExist(TEXT("hangar_portal")));
		return;
	}
	// The hull end rides along with the ship; the interior end stays put in the interior level.
	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	AMetalPortal* Outer = GetWorld()->SpawnActor<AMetalPortal>(AMetalPortal::StaticClass(), Hull->GetSocketTransform(TEXT("hangar_portal")), Params);
	Params.OverrideLevel = GetInteriorLevel();
	AMetalPortal* Inner = GetWorld()->SpawnActor<AMetalPortal>(AMetalPortal::StaticClass(), InteriorHangar->GetMouthTransform(), Params);
	if (Outer == nullptr || Inner == nullptr)
	{
		return;
	}
	Outer->AttachToComponent(Hull, FAttachmentTransformRules::KeepWorldTransform, TEXT("hangar_portal"));
	UMaterialInterface* Material = ShipData ? ShipData->PortalMaterial.Get() : nullptr;
	const FVector2D Mouth = ShipData ? ShipData->HangarMouthSize : FVector2D(8000.f, 3000.f);
	// Open space is in front of the hull end; the hangar is behind the interior end.
	Outer->Link(Inner, 1.f, Mouth, Material);
	Inner->Link(Outer, -1.f, Mouth, Material);
	// Looking out of the hangar, the view starts inside our own hull: don't draw it.
	Inner->AddHiddenActor(this);
	OuterPortal = Outer;
	InnerPortal = Inner;
	UE_LOG(LogMetal, Display, TEXT("%s hangar portal: hull end %s facing %s, interior end %s facing %s"), *GetName(),
		*Outer->GetActorLocation().ToCompactString(), *Outer->GetActorForwardVector().ToCompactString(),
		*Inner->GetActorLocation().ToCompactString(), *Inner->GetActorForwardVector().ToCompactString());
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
	if (const AMetalCapitalShip* Ship = Cast<AMetalCapitalShip>(DamageCauser ? DamageCauser->GetOwner() : nullptr); Ship && Ship->Team == Team)
	{
		return 0.f; // our own turrets
	}
	const float Damage = Super::TakeDamage(DamageAmount, DamageEvent, EventInstigator, DamageCauser);

	// Find which component was struck. Point damage carries the hit; radial damage lists the components
	// it reached, and the first one is used.
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
		return ApplySubsystemDamage(Subsystem, Damage, nullptr, nullptr);
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

void AMetalCapitalShip::RegisterSubsystem(UMetalShipSubsystemComponent* Subsystem, float Health)
{
	if (Subsystem)
	{
		Subsystem->ResetHealth(Health);
		Subsystems.AddUnique(Subsystem);
	}
}

float AMetalCapitalShip::ApplySubsystemDamage(UMetalShipSubsystemComponent* Subsystem, float Damage, AController* EventInstigator, AActor* DamageCauser)
{
	if (bShipDestroyed || Subsystem == nullptr || Subsystem->IsDestroyed() || Damage <= 0.f)
	{
		return 0.f;
	}
	if (const AMetalBotCharacter* Attacker = FindAttacker(EventInstigator, DamageCauser); Attacker && Attacker->GetTeam() == Team)
	{
		return 0.f;
	}
	FlashComponent(Subsystem);
	if (Subsystem->ApplyDamage(Damage))
	{
		DestroySubsystem(Subsystem);
	}
	return Damage;
}

void AMetalCapitalShip::FlashComponent(UPrimitiveComponent* Component)
{
	if (Component && (Component->GetOwner() == this || Subsystems.Contains(Component)))
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
	OnAnySubsystemDestroyed.Broadcast(this, Subsystem);
}

void AMetalCapitalShip::OnSubsystemDestroyed_Implementation(UMetalShipSubsystemComponent* Subsystem)
{
	// Without engines the hangar crews work slower: replacement robots take twice as long.
	if (AMetalHangar* InteriorHangar = Hangar.Get(); InteriorHangar && Subsystem->Type == EMetalShipSubsystem::Engines)
	{
		InteriorHangar->RespawnDelay *= 2.f;
	}
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

void AMetalCapitalShip::TickTurrets(float DeltaSeconds)
{
	const UMetalWeaponData* Weapon = ShipData ? ShipData->TurretWeapon.Get() : nullptr;
	if (Weapon == nullptr || bShipDestroyed || Turrets.Num() == 0)
	{
		return;
	}
	UWorld* World = GetWorld();
	const float Now = World->GetTimeSeconds();
	const FTransform HullToWorld = Hull->GetComponentTransform();
	// Transforms compose left to right in UE (A * B applies A, then B), so a gun's world transform is
	// Aim * TurretGunOffset * Mount * HullToWorld: aim on the base, base on the hull, hull in the world.

	// Pick targets a few times a second: the nearest enemy flyer each gun can see.
	if (Now >= NextTurretThinkTime)
	{
		NextTurretThinkTime = Now + 0.3f;
		TArray<AMetalStalker*> Enemies;
		for (TActorIterator<AMetalStalker> It(World); It; ++It)
		{
			if (It->IsAlive() && !It->IsParked() && It->GetController() && It->GetTeam() != Team && It->GetTeam() != EMetalTeam::Neutral
				&& FVector::DistSquared(It->GetActorLocation(), GetActorLocation()) < FMath::Square(ShipData->TurretRange + 30000.f))
			{
				Enemies.Add(*It);
			}
		}
		for (FTurret& Turret : Turrets)
		{
			const FVector Gun = (TurretGunOffset * Turret.Mount * HullToWorld).GetLocation();
			const FVector Up = (Turret.Mount * HullToWorld).GetRotation().GetUpVector();
			AMetalStalker* Best = nullptr;
			float BestDistance = ShipData->TurretRange;
			for (AMetalStalker* Enemy : Enemies)
			{
				const FVector ToEnemy = Enemy->GetActorLocation() - Gun;
				const float Distance = ToEnemy.Size();
				// Above the turret's horizon, in range, and not behind our own hull.
				if (Distance >= BestDistance || FVector::DotProduct(ToEnemy / Distance, Up) < -0.05f)
				{
					continue;
				}
				FCollisionQueryParams Params(SCENE_QUERY_STAT(MetalTurretSight), false, Enemy);
				FHitResult Hit;
				if (World->LineTraceSingleByChannel(Hit, Gun + Up * 600.f, Enemy->GetActorLocation(), ECC_Visibility, Params))
				{
					continue;
				}
				Best = Enemy;
				BestDistance = Distance;
			}
			Turret.Target = Best;
		}
	}

	for (int32 Index = 0; Index < Turrets.Num(); ++Index)
	{
		FTurret& Turret = Turrets[Index];
		const FTransform MountWorld = Turret.Mount * HullToWorld;
		float DesiredYaw = 0.f;
		float DesiredPitch = 0.f;
		FVector AimWorld = FVector::ZeroVector;
		const AActor* Target = Turret.Target.Get();
		if (Target)
		{
			const FVector Gun = (TurretGunOffset * MountWorld).GetLocation();
			const float Flight = FVector::Dist(Gun, Target->GetActorLocation()) / FMath::Max(1000.f, Weapon->ProjectileSpeed);
			AimWorld = Target->GetActorLocation() + Target->GetVelocity() * Flight;
			// Convert the lead point into the mount's frame, so yaw and pitch are relative to how the turret
			// is mounted (turrets sit on the hull's top, sides and belly). Pitch is limited to just above the
			// mount's horizon.
			const FVector Local = MountWorld.InverseTransformVectorNoScale(AimWorld - Gun);
			DesiredYaw = FMath::RadiansToDegrees(FMath::Atan2(Local.Y, Local.X));
			DesiredPitch = FMath::Clamp(FMath::RadiansToDegrees(FMath::Atan2(Local.Z, FVector2D(Local.X, Local.Y).Size())), -5.f, 85.f);
		}
		const float Step = ShipData->TurretTurnRate * DeltaSeconds;
		Turret.Yaw += FMath::Clamp(FRotator::NormalizeAxis(DesiredYaw - Turret.Yaw), -Step, Step);
		Turret.Pitch += FMath::Clamp(DesiredPitch - Turret.Pitch, -Step, Step);
		const FTransform Aim(FRotator(Turret.Pitch, Turret.Yaw, 0.f));
		const FTransform GunLocal = Aim * TurretGunOffset * Turret.Mount;
		// Only mark the render state dirty on the last instance, so the whole batch updates once per frame.
		TurretGuns->UpdateInstanceTransform(Index, GunLocal, false, Index == Turrets.Num() - 1);

		// Fire once the barrels are on the lead point.
		if (Target && Now >= Turret.NextFireTime)
		{
			const FTransform GunWorld = GunLocal * HullToWorld;
			const FVector Barrel = GunWorld.GetRotation().GetForwardVector();
			const FVector Muzzle = GunWorld.GetLocation() + Barrel * 2400.f;
			if (FVector::DotProduct(Barrel, (AimWorld - Muzzle).GetSafeNormal()) > FMath::Cos(FMath::DegreesToRadians(6.f)))
			{
				const FMetalWeaponLevel& Stats = Weapon->GetLevel(0);
				Turret.NextFireTime = Now + Stats.FireInterval * FMath::FRandRange(0.85f, 1.15f);
				FActorSpawnParameters Params;
				Params.Owner = this;
				Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
				const FVector Direction = FMath::VRandCone(Barrel, FMath::DegreesToRadians(Weapon->SpreadDegrees + ShipData->TurretAimError));
				if (AMetalProjectile* Shot = World->SpawnActor<AMetalProjectile>(Muzzle, Direction.Rotation(), Params))
				{
					Shot->Launch(Weapon, Stats.Damage, Direction * Weapon->ProjectileSpeed);
				}
				AMetalFX::SpawnBurst(World, Muzzle, Weapon->TracerColor, 150.f, 0.08f);
			}
		}
	}
}

void AMetalCapitalShip::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	TickTurrets(DeltaSeconds);

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
