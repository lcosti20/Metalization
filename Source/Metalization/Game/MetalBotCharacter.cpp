#include "MetalBotCharacter.h"

#include "MetalGameMode.h"
#include "MetalBotAIController.h"
#include "MetalCapitalShip.h"
#include "MetalFlyerAIController.h"
#include "MetalBotAnimInstance.h"
#include "MetalBotArchetype.h"
#include "MetalFX.h"
#include "MetalPlayerController.h"
#include "MetalProjectile.h"
#include "MetalWeaponData.h"
#include "Camera/CameraComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/DamageEvents.h"
#include "Engine/OverlapResult.h"
#include "Engine/World.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/DamageType.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/SpringArmComponent.h"
#include "Kismet/GameplayStatics.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "PhysicsEngine/PhysicsAsset.h"
#include "TimerManager.h"

DEFINE_LOG_CATEGORY(LogMetal);

FOnMetalBotDied AMetalBotCharacter::OnAnyBotDied;

AMetalBotCharacter::AMetalBotCharacter(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	PrimaryActorTick.bCanEverTick = true;

	GetCapsuleComponent()->InitCapsuleSize(34.f, 68.f);
	// Weapons trace on Visibility; the capsule is the hit volume, hit zones refine it per bone.
	GetCapsuleComponent()->SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);

	// FBX characters from Blender face +Y; rotate so they face the actor's +X.
	GetMesh()->SetRelativeLocationAndRotation(FVector(0.f, 0.f, -68.f), FRotator(0.f, -90.f, 0.f));
	GetMesh()->SetAnimInstanceClass(UMetalBotAnimInstance::StaticClass());
	GetMesh()->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;

	WeaponMesh = CreateDefaultSubobject<USkeletalMeshComponent>(TEXT("WeaponMesh"));
	WeaponMesh->SetupAttachment(GetMesh());
	WeaponMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);

	UnarmedMesh = CreateDefaultSubobject<USkeletalMeshComponent>(TEXT("UnarmedMesh"));
	UnarmedMesh->SetupAttachment(GetMesh());
	UnarmedMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);

	CameraBoom = CreateDefaultSubobject<USpringArmComponent>(TEXT("CameraBoom"));
	CameraBoom->SetupAttachment(RootComponent);
	CameraBoom->TargetArmLength = DefaultArmLength;
	CameraBoom->SocketOffset = FVector(0.f, 55.f, 55.f);
	CameraBoom->bUsePawnControlRotation = true;
	CameraBoom->bEnableCameraLag = true;
	CameraBoom->CameraLagSpeed = 18.f;

	FollowCamera = CreateDefaultSubobject<UCameraComponent>(TEXT("FollowCamera"));
	FollowCamera->SetupAttachment(CameraBoom, USpringArmComponent::SocketName);
	FollowCamera->bUsePawnControlRotation = false;

	UCharacterMovementComponent* Move = GetCharacterMovement();
	Move->GravityScale = 1.6f;
	Move->AirControl = 0.6f;
	Move->MaxWalkSpeed = 500.f;
	Move->JumpZVelocity = 650.f;
	Move->MaxAcceleration = 2400.f;
	Move->BrakingDecelerationWalking = 2000.f;
	Move->RotationRate = FRotator(0.f, 600.f, 0.f);
	Move->bOrientRotationToMovement = false;
	// The player's own body keeps falling/landing while the player is busy driving another robot.
	Move->bRunPhysicsWithNoController = true;

	bUseControllerRotationYaw = true;
	bUseControllerRotationPitch = false;
	bUseControllerRotationRoll = false;

	AIControllerClass = AMetalBotAIController::StaticClass();
	AutoPossessAI = EAutoPossessAI::PlacedInWorldOrSpawned;
}

// --------------------------------------------------------------------------------------------
// Archetype
// --------------------------------------------------------------------------------------------

const FMetalBotRig& AMetalBotCharacter::GetRig() const
{
	static const FMetalBotRig DefaultRig;
	return Archetype ? Archetype->Rig : DefaultRig;
}

FName AMetalBotCharacter::GetArchetypeId() const
{
	return Archetype ? Archetype->ArchetypeId : NAME_None;
}

FText AMetalBotCharacter::GetDisplayName() const
{
	return Archetype ? Archetype->DisplayName : FText::FromString(GetName());
}

void AMetalBotCharacter::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);
	// So designers see the right robot (and its starting gun) when placing it in a level.
	ApplyArchetype();
	if (Archetype && Archetype->StartingWeapons.Num() > 0 && Archetype->StartingWeapons[0])
	{
		WeaponMesh->SetSkeletalMeshAsset(Archetype->StartingWeapons[0]->GetLevel(0).HeldMeshes.FindRef(Archetype->ArchetypeId));
	}
}

void AMetalBotCharacter::ApplyArchetype()
{
	if (Archetype == nullptr)
	{
		return;
	}
	GetCapsuleComponent()->SetCapsuleSize(Archetype->CapsuleRadius, Archetype->CapsuleHalfHeight);
	GetMesh()->SetSkeletalMeshAsset(Archetype->Mesh);
	MeshBaseLocation = FVector(0.f, 0.f, -Archetype->CapsuleHalfHeight) + Archetype->MeshOffset;
	GetMesh()->SetRelativeLocationAndRotation(MeshBaseLocation, MeshBaseRotation);
	GetMesh()->SetRelativeScale3D(FVector(Archetype->MeshScale));
	GetMesh()->SetAnimInstanceClass(UMetalBotAnimInstance::StaticClass());

	WeaponMesh->SetLeaderPoseComponent(GetMesh());
	UnarmedMesh->SetSkeletalMeshAsset(Archetype->UnarmedPart);
	UnarmedMesh->SetLeaderPoseComponent(GetMesh());

	GetCharacterMovement()->MaxWalkSpeed = Archetype->WalkSpeed;
	GetCharacterMovement()->JumpZVelocity = Archetype->JumpZVelocity;
	JumpMaxCount = FMath::Max(1, Archetype->MaxJumps);

	DefaultArmLength = Archetype->CameraDistance;
	DefaultCameraOffset = Archetype->CameraOffset;
	CameraBoom->TargetArmLength = DefaultArmLength;
	CameraBoom->SocketOffset = DefaultCameraOffset;

	Team = GetBaseTeam();
}

void AMetalBotCharacter::BeginPlay()
{
	Super::BeginPlay();

	if (Archetype == nullptr)
	{
		UE_LOG(LogMetal, Error, TEXT("%s has no Archetype - run Tools/AssetPipeline/run_pipeline.ps1 and assign one."), *GetName());
		return;
	}

	ApplyArchetype();
	MaxHealth = Archetype->MaxHealth;
	Health = MaxHealth;
	ZoneDamage.Init(0.f, Archetype->HitZones.Num());

	for (USkeletalMesh* Part : Archetype->Parts)
	{
		if (Part == nullptr)
		{
			continue;
		}
		USkeletalMeshComponent* PartComponent = NewObject<USkeletalMeshComponent>(this);
		PartComponent->SetupAttachment(GetMesh());
		PartComponent->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		PartComponent->SetSkeletalMeshAsset(Part);
		PartComponent->RegisterComponent();
		PartComponent->SetLeaderPoseComponent(GetMesh());
		PartMeshes.Add(PartComponent);
	}

	if (GrenadeWeapon == nullptr)
	{
		GrenadeWeapon = Archetype->GrenadeWeapon;
		Grenades = Archetype->StartingGrenades;
	}
	// Add the starting loadout without disturbing anything already given (e.g. a pickup touched on spawn).
	for (UMetalWeaponData* Weapon : Archetype->StartingWeapons)
	{
		if (!Weapons.ContainsByPredicate([Weapon](const FMetalWeaponSlot& Slot) { return Slot.Data == Weapon; }))
		{
			GiveWeapon(Weapon, false);
		}
	}
	EquipWeapon(Weapons.Num() > 0 ? FMath::Max(0, CurrentWeapon) : INDEX_NONE);
	RefreshWeaponVisual();
	ConfigureRotationForController();
}

void AMetalBotCharacter::PossessedBy(AController* NewController)
{
	Super::PossessedBy(NewController);
	ConfigureRotationForController();
}

void AMetalBotCharacter::ConfigureRotationForController()
{
	const bool bPlayer = Cast<APlayerController>(GetController()) != nullptr;
	// Players strafe like the original game; AI turns smoothly towards its focus.
	// With the action camera, TickActionCamera() takes over every frame.
	bUseControllerRotationYaw = bPlayer && !UsesActionCamera();
	GetCharacterMovement()->bUseControllerDesiredRotation = !bPlayer;
	GetCharacterMovement()->bOrientRotationToMovement = false;
	GetCharacterMovement()->RotationRate = FRotator(0.f, 600.f, 0.f);
}

bool AMetalBotCharacter::UsesActionCamera() const
{
	const UWorld* World = GetWorld();
	const AMetalGameMode* GameMode = World ? World->GetAuthGameMode<AMetalGameMode>() : nullptr;
	return GameMode && GameMode->bActionCamera && IsPlayerControlled();
}

bool AMetalBotCharacter::IsInCombatStance() const
{
	if (!IsPlayerControlled())
	{
		return bHasAITarget || bFireHeld;
	}
	const UWorld* World = GetWorld();
	return !UsesActionCamera() || bAiming || bFireHeld || (World && World->GetTimeSeconds() - LastFireTime < 1.5f);
}

// --------------------------------------------------------------------------------------------
// State
// --------------------------------------------------------------------------------------------

void AMetalBotCharacter::SetTeam(EMetalTeam NewTeam)
{
	Team = NewTeam;
	ApplyTeamColors();
}

bool AMetalBotCharacter::IsHostileTo(const AMetalBotCharacter* Other) const
{
	return Other && Other != this && Team != EMetalTeam::Neutral && Other->Team != EMetalTeam::Neutral && Team != Other->Team;
}

bool AMetalBotCharacter::IsStunned() const
{
	return GetWorld() && GetWorld()->GetTimeSeconds() < StunEndTime;
}

void AMetalBotCharacter::SetDormant(bool bNewDormant)
{
	bDormant = bNewDormant;
	StopFire();
	if (bDormant)
	{
		GetCharacterMovement()->StopMovementImmediately();
	}
}

void AMetalBotCharacter::Stun(float Duration)
{
	if (!bDead)
	{
		StunEndTime = FMath::Max(StunEndTime, GetWorld()->GetTimeSeconds() + Duration);
		StopFire();
		GetCharacterMovement()->StopMovementImmediately();
	}
}

void AMetalBotCharacter::Heal(float Amount)
{
	if (!bDead)
	{
		Health = FMath::Min(MaxHealth, Health + Amount);
	}
}

bool AMetalBotCharacter::CanBeHackedBy(const AMetalBotCharacter* Hacker) const
{
	if (Archetype == nullptr || Hacker == nullptr || Hacker == this || bDead || bHijacked || IsPlayerControlled())
	{
		return false;
	}
	// Two rules: same-team robots need bAllyHackable (an open-ended "link", e.g. boarding a parked
	// Stalker), enemy robots need bHackable (a timed hijack that ends in self-destruct).
	if (Team == Hacker->GetTeam())
	{
		// Only idle ones (parked, or left hovering): never one an AI pilot is flying.
		return Archetype->bAllyHackable && GetController() == nullptr;
	}
	return Archetype->bHackable && Hacker->IsHostileTo(this);
}

EMetalTeam AMetalBotCharacter::GetBaseTeam() const
{
	return bOverrideTeam ? TeamOverride : (Archetype ? Archetype->Team : EMetalTeam::Villains);
}

void AMetalBotCharacter::SetHijacked(bool bNewHijacked)
{
	bHijacked = bNewHijacked;
	SetTeam(bHijacked ? EMetalTeam::Heroes : GetBaseTeam());
	SetMaterialScalar(TEXT("HackGlow"), bHijacked ? 1.f : 0.f);
}

float AMetalBotCharacter::GetPossessionDuration() const
{
	return Archetype ? Archetype->PossessionDuration : 20.f;
}

void AMetalBotCharacter::SelfDestruct(float Delay)
{
	if (bDead)
	{
		return;
	}
	SetDormant(true);
	FTimerHandle Handle;
	GetWorldTimerManager().SetTimer(Handle, FTimerDelegate::CreateWeakLambda(this, [this]()
	{
		if (!bDead)
		{
			// Hurts whoever this robot's current team is hostile to.
			UGameplayStatics::ApplyRadialDamageWithFalloff(this, 90.f, 20.f, GetActorLocation(), 150.f, 450.f, 1.f,
				UDamageType::StaticClass(), {this}, this, nullptr, ECC_Visibility);
			Die(nullptr);
			Explode();
		}
	}), Delay, false);
}

// --------------------------------------------------------------------------------------------
// Weapons
// --------------------------------------------------------------------------------------------

const FMetalWeaponSlot* AMetalBotCharacter::GetCurrentWeapon() const
{
	return Weapons.IsValidIndex(CurrentWeapon) ? &Weapons[CurrentWeapon] : nullptr;
}

void AMetalBotCharacter::SetWeapons(const TArray<FMetalWeaponSlot>& NewWeapons, int32 EquipIndex)
{
	Weapons = NewWeapons;
	CurrentWeapon = INDEX_NONE;
	EquipWeapon(EquipIndex);
}

bool AMetalBotCharacter::GiveWeapon(UMetalWeaponData* Weapon, bool bEquip)
{
	if (Weapon == nullptr)
	{
		return false;
	}
	// Already owned: upgrade one level (capped at the max) and refill. A pickup only counts as used if it
	// actually upgraded the weapon or added ammo, so full-and-maxed players leave it on the ground.
	for (int32 Index = 0; Index < Weapons.Num(); ++Index)
	{
		FMetalWeaponSlot& Slot = Weapons[Index];
		if (Slot.Data == Weapon)
		{
			const bool bUpgraded = Slot.Level < Weapon->GetMaxLevel();
			Slot.Level = FMath::Min(Slot.Level + 1, Weapon->GetMaxLevel());
			const FMetalWeaponLevel& Stats = Weapon->GetLevel(Slot.Level);
			const bool bAmmo = Slot.Reserve < Stats.MaxReserveAmmo;
			Slot.Clip = Stats.ClipSize;
			Slot.Reserve = Stats.MaxReserveAmmo;
			if (bEquip || Index == CurrentWeapon)
			{
				// Clear first so EquipWeapon doesn't early-out, which refreshes the mesh for the new level.
				CurrentWeapon = INDEX_NONE;
				EquipWeapon(Index);
			}
			return bUpgraded || bAmmo;
		}
	}
	FMetalWeaponSlot& Slot = Weapons.AddDefaulted_GetRef();
	Slot.Data = Weapon;
	Slot.Clip = Weapon->GetLevel(0).ClipSize;
	Slot.Reserve = Weapon->GetLevel(0).MaxReserveAmmo / 2;
	if (bEquip)
	{
		EquipWeapon(Weapons.Num() - 1);
	}
	return true;
}

bool AMetalBotCharacter::AddAmmoFraction(float Fraction)
{
	bool bChanged = false;
	for (FMetalWeaponSlot& Slot : Weapons)
	{
		if (Slot.Data && !Slot.Data->bInfiniteAmmo)
		{
			const int32 Max = Slot.Data->GetLevel(Slot.Level).MaxReserveAmmo;
			const int32 NewReserve = FMath::Min(Max, Slot.Reserve + FMath::CeilToInt(Max * Fraction));
			bChanged |= NewReserve != Slot.Reserve;
			Slot.Reserve = NewReserve;
		}
	}
	return bChanged;
}

void AMetalBotCharacter::EquipWeapon(int32 SlotIndex)
{
	if (!Weapons.IsValidIndex(SlotIndex) || SlotIndex == CurrentWeapon)
	{
		return;
	}
	CurrentWeapon = SlotIndex;
	ReloadEndTime = 0.f;
	if (const UWorld* World = GetWorld())
	{
		NextFireTime = World->GetTimeSeconds() + 0.2f;
	}
	RefreshWeaponVisual();
}

void AMetalBotCharacter::CycleWeapon(int32 Direction)
{
	if (Weapons.Num() > 1)
	{
		EquipWeapon((FMath::Max(0, CurrentWeapon) + Direction + Weapons.Num()) % Weapons.Num());
	}
}

void AMetalBotCharacter::RefreshWeaponVisual()
{
	USkeletalMesh* Held = nullptr;
	if (const FMetalWeaponSlot* Slot = GetCurrentWeapon(); Slot && Slot->Data && !bWeaponDisabled)
	{
		Held = Slot->Data->GetLevel(Slot->Level).HeldMeshes.FindRef(GetArchetypeId());
	}
	WeaponMesh->SetSkeletalMeshAsset(Held);
	WeaponMesh->SetLeaderPoseComponent(GetMesh());
	WeaponMesh->SetVisibility(Held != nullptr);
	UnarmedMesh->SetVisibility(Held == nullptr && UnarmedMesh->GetSkeletalMeshAsset() != nullptr);
	RefreshMaterials();
}

void AMetalBotCharacter::StartFire()
{
	bFireHeld = true;
	if (UsesActionCamera() && GetController())
	{
		// Running freely the robot may face the camera; square up to the crosshair before the first shot.
		SetActorRotation(FRotator(0.f, GetController()->GetControlRotation().Yaw, 0.f));
	}
}

void AMetalBotCharacter::StopFire()
{
	bFireHeld = false;
}

void AMetalBotCharacter::Reload()
{
	const FMetalWeaponSlot* Slot = GetCurrentWeapon();
	if (Slot == nullptr || Slot->Data == nullptr || Slot->Data->bInfiniteAmmo || IsReloading())
	{
		return;
	}
	if (Slot->Clip < Slot->Data->GetLevel(Slot->Level).ClipSize && Slot->Reserve > 0)
	{
		ReloadEndTime = GetWorld()->GetTimeSeconds() + Slot->Data->ReloadTime;
	}
}

void AMetalBotCharacter::FinishReload()
{
	ReloadEndTime = 0.f;
	if (Weapons.IsValidIndex(CurrentWeapon))
	{
		FMetalWeaponSlot& Slot = Weapons[CurrentWeapon];
		const int32 Taken = FMath::Min(Slot.Data->GetLevel(Slot.Level).ClipSize - Slot.Clip, Slot.Reserve);
		Slot.Clip += Taken;
		Slot.Reserve -= Taken;
	}
}

FVector AMetalBotCharacter::GetEyesLocation() const
{
	const FName HeadBone = GetRig().Head;
	if (GetMesh()->GetBoneIndex(HeadBone) != INDEX_NONE)
	{
		return GetMesh()->GetBoneLocation(HeadBone);
	}
	return GetActorLocation() + FVector(0.f, 0.f, BaseEyeHeight);
}

FVector AMetalBotCharacter::GetMuzzleLocation() const
{
	// Robots with built-in guns alternate between them; MuzzleCycle is advanced by FireOnce().
	if (Archetype && Archetype->MuzzleBones.Num() > 0)
	{
		const FName Bone = Archetype->MuzzleBones[MuzzleCycle % Archetype->MuzzleBones.Num()];
		if (GetMesh()->GetBoneIndex(Bone) != INDEX_NONE)
		{
			return GetMesh()->GetBoneLocation(Bone);
		}
	}
	const FName WeaponBone = GetRig().WeaponBone;
	const FRotator AimRotation(GetAimPitch(), GetActorRotation().Yaw, 0.f);
	float Offset = 45.f;
	if (const FMetalWeaponSlot* Slot = GetCurrentWeapon(); Slot && Slot->Data)
	{
		Offset = Slot->Data->MuzzleOffset;
	}
	if (GetMesh()->GetBoneIndex(WeaponBone) != INDEX_NONE)
	{
		return GetMesh()->GetBoneLocation(WeaponBone) + AimRotation.Vector() * Offset * GetMesh()->GetRelativeScale3D().X;
	}
	return GetActorLocation() + AimRotation.Vector() * GetCapsuleComponent()->GetScaledCapsuleRadius() * 1.5f;
}

int32 AMetalBotCharacter::GetMuzzleCount() const
{
	return Archetype ? Archetype->MuzzleBones.Num() : 0;
}

int32 AMetalBotCharacter::GetWorkingMuzzleCount() const
{
	return bWeaponDisabled ? 0 : FMath::Max(0, GetMuzzleCount() - BrokenMuzzles.Num());
}

FVector AMetalBotCharacter::GetAimForward() const
{
	return FRotator(GetAimPitch(), GetActorRotation().Yaw, 0.f).Vector();
}

FVector AMetalBotCharacter::GetAimPoint() const
{
	if (const APlayerController* PC = Cast<APlayerController>(GetController()))
	{
		FVector ViewLocation;
		FRotator ViewRotation;
		PC->GetPlayerViewPoint(ViewLocation, ViewRotation);
		const FVector Direction = ViewRotation.Vector();
		// Trace along the crosshair to find what the player is pointing at; the shot itself then flies from
		// the muzzle to that point. Projecting the robot onto the view ray gives the distance along it at
		// which the robot stands, so the trace starts level with the robot and walls between the camera
		// and the robot are ignored.
		const FVector Start = ViewLocation + Direction * FVector::DotProduct(GetActorLocation() - ViewLocation, Direction);
		const FVector End = ViewLocation + Direction * 30000.f;
		FCollisionQueryParams Params(SCENE_QUERY_STAT(MetalAim), false, this);
		FHitResult Hit;
		if (GetWorld()->LineTraceSingleByChannel(Hit, Start, End, ECC_Visibility, Params))
		{
			return Hit.ImpactPoint;
		}
		return End;
	}
	if (bHasAITarget)
	{
		return AITargetPoint;
	}
	return GetEyesLocation() + GetActorForwardVector() * 1000.f;
}

float AMetalBotCharacter::GetAimPitch() const
{
	if (bDead || bDormant)
	{
		return 0.f;
	}
	if (const AController* C = GetController(); C && C->IsPlayerController())
	{
		return FMath::Clamp(FRotator::NormalizeAxis(C->GetControlRotation().Pitch), -60.f, 60.f);
	}
	if (bHasAITarget)
	{
		return FMath::Clamp((AITargetPoint - GetEyesLocation()).Rotation().Pitch, -60.f, 60.f);
	}
	return 0.f;
}

void AMetalBotCharacter::FireOnce()
{
	UWorld* World = GetWorld();
	if (bWeaponDisabled || !Weapons.IsValidIndex(CurrentWeapon))
	{
		if (Archetype && Archetype->MeleeWeapon)
		{
			FMetalWeaponSlot Melee;
			Melee.Data = Archetype->MeleeWeapon;
			FireMelee(Melee);
			NextFireTime = World->GetTimeSeconds() + Melee.Data->GetLevel(0).FireInterval;
		}
		return;
	}

	LastFireTime = World->GetTimeSeconds();
	FMetalWeaponSlot& Slot = Weapons[CurrentWeapon];
	const UMetalWeaponData* Data = Slot.Data;
	const FMetalWeaponLevel& Stats = Data->GetLevel(Slot.Level);
	if (Data->FireMode == EMetalFireMode::Melee)
	{
		FireMelee(Slot);
		NextFireTime = World->GetTimeSeconds() + Stats.FireInterval;
		return;
	}
	if (!Data->bInfiniteAmmo && Slot.Clip <= 0)
	{
		Reload();
		return;
	}

	// Guns whose arm was shot off are skipped in the firing order.
	for (int32 Skip = 0; Skip < GetMuzzleCount() && BrokenMuzzles.Contains(Archetype->MuzzleBones[MuzzleCycle % GetMuzzleCount()]); ++Skip)
	{
		MuzzleCycle++;
	}
	const FVector Muzzle = GetMuzzleLocation();
	MuzzleCycle++;
	FVector AimDirection = (GetAimPoint() - Muzzle).GetSafeNormal();
	// The aim point can be behind the muzzle (target right next to the robot, or the camera looking past
	// the body); shooting backwards through the robot looks wrong, so fall back to straight ahead.
	if (FVector::DotProduct(AimDirection, GetAimForward()) < 0.2f)
	{
		AimDirection = GetAimForward();
	}
	const float AIError = (!IsPlayerControlled() && Archetype) ? Archetype->AimErrorDegrees : 0.f;
	const float Cone = FMath::DegreesToRadians(Data->SpreadDegrees + AIError);

	// One iteration per pellet, each randomised inside the spread cone. Hitscan weapons trace and damage
	// immediately; projectile and thrown weapons spawn an AMetalProjectile that deals damage on impact.
	bool bHitRobot = false;
	for (int32 Pellet = 0; Pellet < FMath::Max(1, Data->Pellets); ++Pellet)
	{
		const FVector Direction = FMath::VRandCone(AimDirection, Cone);
		if (Data->FireMode == EMetalFireMode::Hitscan)
		{
			const FVector End = Muzzle + Direction * Data->Range;
			FCollisionQueryParams Params(SCENE_QUERY_STAT(MetalShot), false, this);
			FHitResult Hit;
			const bool bHit = World->LineTraceSingleByChannel(Hit, Muzzle, End, ECC_Visibility, Params);
			const FVector Impact = bHit ? FVector(Hit.ImpactPoint) : End;
			AMetalFX::SpawnBeam(World, Muzzle, Impact, Data->TracerColor, 2.5f, 0.07f);
			if (bHit)
			{
				AMetalFX::SpawnBurst(World, Impact, Data->TracerColor, 10.f, 0.12f);
				if (AActor* HitActor = Hit.GetActor())
				{
					bHitRobot |= HitActor->IsA<AMetalBotCharacter>();
					UGameplayStatics::ApplyPointDamage(HitActor, Stats.Damage, Direction, Hit, GetController(), this, UDamageType::StaticClass());
				}
			}
		}
		else
		{
			FActorSpawnParameters Params;
			Params.Owner = this;
			Params.Instigator = this;
			Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
			if (AMetalProjectile* Projectile = World->SpawnActor<AMetalProjectile>(Muzzle, Direction.Rotation(), Params))
			{
				FVector Velocity = Direction * Data->ProjectileSpeed;
				if (Data->FireMode == EMetalFireMode::Thrown)
				{
					Velocity += FVector(0.f, 0.f, 350.f);
				}
				Projectile->Launch(Data, Stats.Damage, Velocity);
			}
		}
	}

	AMetalFX::SpawnBurst(World, Muzzle, Data->TracerColor, 8.f, 0.06f);
	if (UMetalBotAnimInstance* Anim = Cast<UMetalBotAnimInstance>(GetMesh()->GetAnimInstance()))
	{
		Anim->PlayRecoil();
	}
	if (bHitRobot)
	{
		if (AMetalPlayerController* PC = Cast<AMetalPlayerController>(GetController()))
		{
			PC->NotifyHitConfirmed();
		}
	}
	if (!Data->bInfiniteAmmo)
	{
		Slot.Clip--;
		if (Slot.Clip <= 0)
		{
			Reload();
		}
	}
	NextFireTime = World->GetTimeSeconds() + Stats.FireInterval;
}

void AMetalBotCharacter::FireMelee(const FMetalWeaponSlot& Slot)
{
	UWorld* World = GetWorld();
	const FMetalWeaponLevel& Stats = Slot.Data->GetLevel(Slot.Level);
	const FVector Center = GetActorLocation() + GetActorForwardVector() * (GetCapsuleComponent()->GetScaledCapsuleRadius() + Slot.Data->Range * 0.5f);
	TArray<FOverlapResult> Overlaps;
	World->OverlapMultiByObjectType(Overlaps, Center, FQuat::Identity, FCollisionObjectQueryParams(ECC_Pawn), FCollisionShape::MakeSphere(Slot.Data->Range * 0.6f));
	for (const FOverlapResult& Overlap : Overlaps)
	{
		AMetalBotCharacter* Victim = Cast<AMetalBotCharacter>(Overlap.GetActor());
		if (Victim && IsHostileTo(Victim) && Victim->IsAlive())
		{
			UGameplayStatics::ApplyDamage(Victim, Stats.Damage, GetController(), this, UDamageType::StaticClass());
		}
	}
	AMetalFX::SpawnBurst(World, Center, Slot.Data->TracerColor, 25.f, 0.12f);
	if (UMetalBotAnimInstance* Anim = Cast<UMetalBotAnimInstance>(GetMesh()->GetAnimInstance()))
	{
		Anim->PlayRecoil();
	}
}

void AMetalBotCharacter::ThrowGrenade()
{
	if (GrenadeWeapon == nullptr || Grenades <= 0 || bDead || bDormant || IsStunned())
	{
		return;
	}
	Grenades--;
	const FVector Start = GetEyesLocation() + GetActorForwardVector() * 40.f;
	const FVector Direction = (GetAimPoint() - Start).GetSafeNormal();
	FActorSpawnParameters Params;
	Params.Owner = this;
	Params.Instigator = this;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	if (AMetalProjectile* Projectile = GetWorld()->SpawnActor<AMetalProjectile>(Start, Direction.Rotation(), Params))
	{
		Projectile->Launch(GrenadeWeapon, GrenadeWeapon->GetLevel(0).Damage, Direction * GrenadeWeapon->ProjectileSpeed + FVector(0.f, 0.f, 350.f));
	}
}

// --------------------------------------------------------------------------------------------
// Damage
// --------------------------------------------------------------------------------------------

int32 AMetalBotCharacter::FindHitZone(const FDamageEvent& DamageEvent) const
{
	if (Archetype == nullptr || !DamageEvent.IsOfType(FPointDamageEvent::ClassID))
	{
		return INDEX_NONE;
	}
	const FPointDamageEvent& Point = static_cast<const FPointDamageEvent&>(DamageEvent);
	const FHitResult& Hit = Point.HitInfo;

	// Shots that hit the mesh's physics bodies (flyers' arms stick far out of the capsule) know their bone.
	if (!Hit.BoneName.IsNone() && Hit.GetComponent() == GetMesh())
	{
		for (int32 Index = 0; Index < Archetype->HitZones.Num(); ++Index)
		{
			const FName ZoneBone = Archetype->HitZones[Index].Bone;
			if (Hit.BoneName == ZoneBone || GetMesh()->BoneIsChildOf(Hit.BoneName, ZoneBone))
			{
				return Index;
			}
		}
	}
	// Otherwise the shot hit the capsule. Treat the shot as a ray and pick the zone whose bone passes
	// closest to it, within the zone's (scaled) radius. Projectile hits have no TraceStart, so back the
	// ray up a little from the impact point instead.
	const FVector RayStart = Hit.TraceStart.IsZero() ? FVector(Hit.ImpactPoint) - Point.ShotDirection * 50.f : FVector(Hit.TraceStart);
	const float Scale = GetMesh()->GetRelativeScale3D().X;

	int32 Best = INDEX_NONE;
	float BestDistance = TNumericLimits<float>::Max();
	for (int32 Index = 0; Index < Archetype->HitZones.Num(); ++Index)
	{
		const FMetalHitZone& Zone = Archetype->HitZones[Index];
		if (GetMesh()->GetBoneIndex(Zone.Bone) == INDEX_NONE)
		{
			continue;
		}
		const float Distance = FMath::PointDistToLine(GetMesh()->GetBoneLocation(Zone.Bone), Point.ShotDirection, RayStart);
		if (Distance < Zone.Radius * Scale && Distance < BestDistance)
		{
			Best = Index;
			BestDistance = Distance;
		}
	}
	return Best;
}

float AMetalBotCharacter::TakeDamage(float DamageAmount, FDamageEvent const& DamageEvent, AController* EventInstigator, AActor* DamageCauser)
{
	if (bDead || DamageAmount <= 0.f)
	{
		return 0.f;
	}

	// Work out which robot is responsible: the causer itself (hitscan, melee), the instigator of a
	// projectile, or the pawn of the instigating controller (radial damage).
	AMetalBotCharacter* Attacker = Cast<AMetalBotCharacter>(DamageCauser);
	if (Attacker == nullptr && DamageCauser)
	{
		Attacker = Cast<AMetalBotCharacter>(DamageCauser->GetInstigator());
	}
	if (Attacker == nullptr && EventInstigator)
	{
		Attacker = Cast<AMetalBotCharacter>(EventInstigator->GetPawn());
	}
	if (Attacker && Attacker != this && !Attacker->IsHostileTo(this))
	{
		return 0.f; // no friendly fire
	}
	// Shots from a capital ship's turrets carry the ship as their owner.
	if (const AMetalCapitalShip* Ship = Cast<AMetalCapitalShip>(DamageCauser ? DamageCauser->GetOwner() : nullptr); Ship && Ship->GetTeam() == Team)
	{
		return 0.f;
	}

	float Damage = Super::TakeDamage(DamageAmount, DamageEvent, EventInstigator, DamageCauser);
	const int32 Zone = FindHitZone(DamageEvent);
	if (Archetype && Archetype->HitZones.IsValidIndex(Zone))
	{
		const FMetalHitZone& HitZone = Archetype->HitZones[Zone];
		Damage *= HitZone.DamageMultiplier;
		// Break the zone only on the hit that pushes it past its durability, not on every hit after.
		const bool bWasIntact = HitZone.Durability <= 0.f || ZoneDamage[Zone] < HitZone.Durability;
		ZoneDamage[Zone] += Damage;
		if (bWasIntact && HitZone.Durability > 0.f && ZoneDamage[Zone] >= HitZone.Durability)
		{
			BreakZone(HitZone);
		}
	}
	if (Attacker == this)
	{
		// Splash from your own grenades and rockets still hurts, but only half as much.
		Damage *= 0.5f;
	}

	Health -= Damage;
	HitFlash = 1.f;
	LastDamageTakenTime = GetWorld()->GetTimeSeconds();

	const FVector From = Attacker ? Attacker->GetActorLocation() : (DamageCauser ? DamageCauser->GetActorLocation() : GetActorLocation() - GetActorForwardVector());
	if (UMetalBotAnimInstance* Anim = Cast<UMetalBotAnimInstance>(GetMesh()->GetAnimInstance()))
	{
		Anim->PlayHitReact((GetActorLocation() - From).GetSafeNormal2D());
	}
	if (AMetalBotAIController* AI = Cast<AMetalBotAIController>(GetController()))
	{
		AI->NotifyDamaged(Attacker);
	}
	if (AMetalFlyerAIController* Pilot = Cast<AMetalFlyerAIController>(GetController()))
	{
		Pilot->NotifyDamaged(Attacker);
	}
	if (AMetalPlayerController* PC = Cast<AMetalPlayerController>(GetController()))
	{
		PC->NotifyDamageTaken();
	}

	if (Health <= 0.f)
	{
		Die(EventInstigator);
	}
	return Damage;
}

void AMetalBotCharacter::DisableWeapon()
{
	if (bWeaponDisabled)
	{
		return;
	}
	bWeaponDisabled = true;
	AMetalFX::SpawnBurst(GetWorld(), GetMuzzleLocation(), FLinearColor(1.f, 0.6f, 0.1f), 40.f, 0.4f);
	RefreshWeaponVisual();
}

void AMetalBotCharacter::BreakZone(const FMetalHitZone& Zone)
{
	if (Zone.bDisablesWeaponWhenBroken)
	{
		DisableWeapon();
	}
	if (!Zone.DisablesMuzzleBone.IsNone() && !BrokenMuzzles.Contains(Zone.DisablesMuzzleBone))
	{
		BrokenMuzzles.Add(Zone.DisablesMuzzleBone);
		const float Scale = GetMesh()->GetRelativeScale3D().X;
		AMetalFX::SpawnBurst(GetWorld(), GetMesh()->GetBoneLocation(Zone.DisablesMuzzleBone), FLinearColor(1.f, 0.6f, 0.1f), 120.f * Scale, 0.5f);
		// The arm comes off: hide it and drop its physics bodies so shots pass through the gap.
		GetMesh()->HideBoneByName(Zone.Bone, PBO_Term);
		if (GetWorkingMuzzleCount() == 0)
		{
			DisableWeapon();
		}
	}
}

void AMetalBotCharacter::Die(AController* Killer)
{
	if (bDead)
	{
		return;
	}
	bDead = true;
	Health = 0.f;
	StopFire();
	GetCharacterMovement()->StopMovementImmediately();
	GetCharacterMovement()->DisableMovement();
	GetCapsuleComponent()->SetCollisionEnabled(ECollisionEnabled::NoCollision);

	if (GetMesh()->GetPhysicsAsset())
	{
		GetMesh()->SetCollisionProfileName(TEXT("Ragdoll"));
		GetMesh()->SetAllBodiesSimulatePhysics(true);
		GetMesh()->SetSimulatePhysics(true);
		GetMesh()->WakeAllRigidBodies();
		GetMesh()->AddImpulse(-GetActorForwardVector() * 150.f + FVector(0.f, 0.f, 100.f), NAME_None, true);
		bRagdoll = true;
	}

	OnAnyBotDied.Broadcast(this, Killer);
	if (AMetalBotAIController* AI = Cast<AMetalBotAIController>(GetController()))
	{
		AI->NotifyPawnDied();
	}
	GetWorldTimerManager().SetTimer(ExplodeTimer, this, &AMetalBotCharacter::Explode, 2.5f, false);
}

void AMetalBotCharacter::Explode()
{
	if (IsActorBeingDestroyed() || IsHidden())
	{
		return;
	}
	const FVector Center = GetMesh()->GetComponentLocation() + FVector(0.f, 0.f, GetCapsuleComponent()->GetScaledCapsuleHalfHeight());
	AMetalFX::SpawnBurst(GetWorld(), Center, FLinearColor(1.f, 0.45f, 0.1f), 140.f * GetMesh()->GetRelativeScale3D().X, 0.5f);
	if (IsPlayerControlled())
	{
		// The game mode respawns the player and cleans this body up.
		SetActorHiddenInGame(true);
		return;
	}
	Destroy();
}

// --------------------------------------------------------------------------------------------
// Movement feel: action camera, double-jump flip, landing
// --------------------------------------------------------------------------------------------

void AMetalBotCharacter::TickActionCamera(float DeltaSeconds)
{
	const bool bAction = UsesActionCamera();
	const bool bCombat = IsInCombatStance();
	if (bAction)
	{
		// Free running: face the movement. Combat: turn quickly to the camera and strafe.
		UCharacterMovementComponent* Move = GetCharacterMovement();
		bUseControllerRotationYaw = false;
		Move->bOrientRotationToMovement = !bCombat;
		Move->bUseControllerDesiredRotation = bCombat;
		Move->RotationRate = FRotator(0.f, bCombat ? 1080.f : 720.f, 0.f);
	}

	// Exploring: centred behind and a little further out. Combat: over the shoulder. Aiming: zoomed in.
	const bool bRelaxed = bAction && !bCombat;
	const float ArmTarget = bAiming ? DefaultArmLength * 0.55f : (bRelaxed ? DefaultArmLength * 1.1f : DefaultArmLength);
	const FVector OffsetTarget = bRelaxed ? FVector(0.f, 0.f, DefaultCameraOffset.Z) : DefaultCameraOffset;
	CameraBoom->TargetArmLength = FMath::FInterpTo(CameraBoom->TargetArmLength, ArmTarget, DeltaSeconds, bRelaxed ? 4.f : 10.f);
	CameraBoom->SocketOffset = FMath::VInterpTo(CameraBoom->SocketOffset, OffsetTarget, DeltaSeconds, 6.f);
	FollowCamera->SetFieldOfView(FMath::FInterpTo(FollowCamera->FieldOfView, bAiming ? 60.f : 85.f, DeltaSeconds, 10.f));
}

void AMetalBotCharacter::OnJumped_Implementation()
{
	Super::OnJumped_Implementation();
	LowestFallSpeed = 0.f;
	// JumpCurrentCount is incremented before this is called, so 2+ means a mid-air jump.
	if (JumpCurrentCount >= 2)
	{
		FlipStartTime = GetWorld()->GetTimeSeconds();
	}
}

void AMetalBotCharacter::TickFlip(float Now)
{
	constexpr float FlipDuration = 0.5f;
	float Angle = 0.f;
	FlipTuck = 0.f;
	if (FlipStartTime >= 0.f)
	{
		const float T = (Now - FlipStartTime) / FlipDuration;
		if (T >= 1.f || bDead || !GetCharacterMovement()->IsFalling())
		{
			FlipStartTime = -1.f;
		}
		else
		{
			// One full forward somersault, eased in and out; the tuck peaks halfway through the flip.
			Angle = 360.f * FMath::InterpEaseInOut(0.f, 1.f, T, 1.6f);
			FlipTuck = FMath::Sin(T * UE_PI);
		}
	}
	if (bRagdoll)
	{
		return;
	}
	// Spin the whole body (and everything attached to it) forward around the capsule centre.
	// The mesh sits at MeshBaseLocation below the capsule centre, so rotating that offset as well as the
	// rotation keeps the pivot at the centre instead of at the robot's feet.
	const FQuat Flip(FVector::RightVector, FMath::DegreesToRadians(Angle));
	const FQuat Turn = MeshTilt * Flip;
	GetMesh()->SetRelativeLocationAndRotation(Turn.RotateVector(MeshBaseLocation), Turn * MeshBaseRotation);
}

void AMetalBotCharacter::Landed(const FHitResult& Hit)
{
	Super::Landed(Hit);
	FlipStartTime = -1.f;
	if (UMetalBotAnimInstance* Anim = Cast<UMetalBotAnimInstance>(GetMesh()->GetAnimInstance()))
	{
		Anim->PlayLand(FMath::Clamp(-LowestFallSpeed / 1400.f, 0.25f, 1.f));
	}
	LowestFallSpeed = 0.f;
}

void AMetalBotCharacter::FellOutOfWorld(const UDamageType& DamageType)
{
	Die(nullptr);
	Explode();
}

// --------------------------------------------------------------------------------------------
// Visual feedback
// --------------------------------------------------------------------------------------------

void AMetalBotCharacter::RefreshMaterials()
{
	DynamicMaterials.Reset();
	TArray<USkeletalMeshComponent*> Components = {GetMesh(), WeaponMesh, UnarmedMesh};
	for (USkeletalMeshComponent* Part : PartMeshes)
	{
		Components.Add(Part);
	}
	for (USkeletalMeshComponent* Component : Components)
	{
		if (Component == nullptr || Component->GetSkeletalMeshAsset() == nullptr)
		{
			continue;
		}
		for (int32 Index = 0; Index < Component->GetNumMaterials(); ++Index)
		{
			if (UMaterialInstanceDynamic* MID = Component->CreateDynamicMaterialInstance(Index))
			{
				DynamicMaterials.Add(MID);
			}
		}
	}
	SetMaterialScalar(TEXT("HackGlow"), bHijacked ? 1.f : 0.f);
	ApplyTeamColors();
}

void AMetalBotCharacter::ApplyTeamColors()
{
	const FMetalTeamColors* Colors = Archetype ? Archetype->TeamColors.Find(Team) : nullptr;
	if (Colors == nullptr)
	{
		return;
	}
	TArray<USkeletalMeshComponent*> Components = {GetMesh()};
	for (USkeletalMeshComponent* Part : PartMeshes)
	{
		Components.Add(Part);
	}
	for (USkeletalMeshComponent* Component : Components)
	{
		if (Component == nullptr)
		{
			continue;
		}
		// Paintable material slots are the ones whose names end in "_Body" or "_Accent". RefreshMaterials()
		// has already replaced them with dynamic instances, so the tint can be set in place.
		const TArray<FName> SlotNames = Component->GetMaterialSlotNames();
		for (int32 Index = 0; Index < SlotNames.Num(); ++Index)
		{
			UMaterialInstanceDynamic* MID = Cast<UMaterialInstanceDynamic>(Component->GetMaterial(Index));
			const FString Slot = SlotNames[Index].ToString();
			if (MID && Slot.EndsWith(TEXT("_Body")))
			{
				MID->SetVectorParameterValue(TEXT("Tint"), Colors->Body);
			}
			else if (MID && Slot.EndsWith(TEXT("_Accent")))
			{
				MID->SetVectorParameterValue(TEXT("Tint"), Colors->Accent);
			}
		}
	}
}

void AMetalBotCharacter::SetMaterialScalar(FName Parameter, float Value)
{
	for (UMaterialInstanceDynamic* MID : DynamicMaterials)
	{
		if (MID)
		{
			MID->SetScalarParameterValue(Parameter, Value);
		}
	}
}

void AMetalBotCharacter::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	const float Now = GetWorld()->GetTimeSeconds();

	if (HitFlash > 0.f)
	{
		HitFlash = FMath::Max(0.f, HitFlash - DeltaSeconds * 6.f);
		SetMaterialScalar(TEXT("HitFlash"), HitFlash);
	}
	if (bHijacked && !bDead)
	{
		SetMaterialScalar(TEXT("HackGlow"), 0.65f + 0.35f * FMath::Sin(Now * 6.f));
	}

	TickActionCamera(DeltaSeconds);
	TickFlip(Now);
	if (GetCharacterMovement()->IsFalling())
	{
		LowestFallSpeed = FMath::Min(LowestFallSpeed, static_cast<float>(GetVelocity().Z));
	}

	if (ReloadEndTime > 0.f && Now >= ReloadEndTime)
	{
		FinishReload();
	}

	if (bFireHeld && !bDead && !bDormant && !IsStunned() && !IsReloading() && Now >= NextFireTime)
	{
		FireOnce();
		const FMetalWeaponSlot* Slot = GetCurrentWeapon();
		if (Slot && Slot->Data && !Slot->Data->bAutomatic && IsPlayerControlled())
		{
			bFireHeld = false;
		}
	}
}
