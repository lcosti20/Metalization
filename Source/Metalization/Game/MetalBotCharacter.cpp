#include "MetalBotCharacter.h"

#include "AnimationRuntime.h"
#include "MetalAudio.h"
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
#include "Components/PointLightComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "UObject/ConstructorHelpers.h"
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

	// Stand-alone weapon meshes (no leader pose) for the acted-out swap: one in the off hand, two on the back.
	for (TObjectPtr<USkeletalMeshComponent>* Gun : {&CarriedGun, &RackGunA, &RackGunB})
	{
		const TCHAR* Name = Gun == &CarriedGun ? TEXT("CarriedGun") : (Gun == &RackGunA ? TEXT("RackGunA") : TEXT("RackGunB"));
		*Gun = CreateDefaultSubobject<USkeletalMeshComponent>(Name);
		(*Gun)->SetupAttachment(GetMesh());
		(*Gun)->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		(*Gun)->SetVisibility(false);
		(*Gun)->SetCastShadow(true);
	}

	static ConstructorHelpers::FObjectFinder<UStaticMesh> Cylinder(TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> Cone(TEXT("/Engine/BasicShapes/Cone.Cone"));
	JetPodMesh = Cylinder.Object;
	JetFlameMesh = Cone.Object;

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

	UCharacterMovementComponent* Move = GetCharacterMovement();
	Move->MaxWalkSpeed = Archetype->WalkSpeed;
	Move->JumpZVelocity = Archetype->JumpZVelocity;
	JumpMaxCount = FMath::Max(1, Archetype->MaxJumps);
	// Sneaking is the engine's crouch with a shorter capsule; TickFlip keeps the feet on the ground.
	Move->GetNavAgentPropertiesRef().bCanCrouch = Archetype->CrouchSpeed > 0.f;
	Move->MaxWalkSpeedCrouched = Archetype->CrouchSpeed > 0.f ? Archetype->CrouchSpeed : 200.f;
	Move->SetCrouchedHalfHeight(Archetype->CapsuleHalfHeight * FMath::Clamp(Archetype->CrouchHeightFraction, 0.3f, 1.f));

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
	RefreshRack();
	BuildJetpack();
	ConfigureRotationForController();
}

void AMetalBotCharacter::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (LaserSight)
	{
		LaserSight->Destroy();
		LaserSight = nullptr;
	}
	Super::EndPlay(EndPlayReason);
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

bool AMetalBotCharacter::CanBeHackedBy(const AMetalBotCharacter* Hacker, bool bIgnoreFirewall) const
{
	if (Archetype == nullptr || Hacker == nullptr || Hacker == this || bDead || bHijacked || IsPlayerControlled() || (!bIgnoreFirewall && IsFirewalled()))
	{
		return false;
	}
	// Two rules: same-team robots need bAllyHackable (an open-ended "link", e.g. boarding a parked
	// Stalker), enemy robots need bHackable (a timed hijack; the robot goes back to its side afterwards).
	if (Team == Hacker->GetTeam())
	{
		// Only idle ones (parked, or left hovering): never one an AI pilot is flying.
		return Archetype->bAllyHackable && GetController() == nullptr;
	}
	return Archetype->bHackable && Hacker->IsHostileTo(this);
}

void AMetalBotCharacter::RaiseFirewall(float Duration)
{
	FirewallEndTime = GetWorld()->GetTimeSeconds() + Duration;
	bFirewallGlow = true;
}

float AMetalBotCharacter::GetFirewallTimeRemaining() const
{
	const UWorld* World = GetWorld();
	return World ? FMath::Max(0.f, FirewallEndTime - World->GetTimeSeconds()) : 0.f;
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
	SwapStartTime = -1.f;
	CarriedGun->SetVisibility(false);
	RackOrder.Reset();
	EquipWeapon(EquipIndex);
	RefreshRack();
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
	RefreshRack();
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
	if (!Weapons.IsValidIndex(SlotIndex))
	{
		return;
	}
	if (IsSwappingWeapon())
	{
		// Changed their mind mid-swap: whatever was picked last is what gets drawn and attached.
		if (SlotIndex != SwapTargetSlot)
		{
			SwapTargetSlot = SlotIndex;
			if (SwapStage == 2)
			{
				PlaceWeaponOnBone(CarriedGun, SwapTargetSlot, GetRig().HandL, FTransform::Identity);
			}
			RefreshRack();
		}
		return;
	}
	if (SlotIndex == CurrentWeapon)
	{
		return;
	}
	ReloadEndTime = 0.f;
	const UWorld* World = GetWorld();
	// Players with an animated rig act the swap out; everything else (AI, respawn loadouts) swaps instantly.
	const bool bActOut = World && World->IsGameWorld() && HasActorBegunPlay() && Archetype && Archetype->bAnimatedWeaponSwap
		&& IsPlayerControlled() && !bDead && !bWeaponDisabled && Weapons.IsValidIndex(CurrentWeapon);
	if (bActOut)
	{
		SwapFromSlot = CurrentWeapon;
		SwapTargetSlot = SlotIndex;
		SwapStartTime = World->GetTimeSeconds();
		SwapStage = 0;
		PlaySound(TEXT("swap_servo"));
		return;
	}
	CurrentWeapon = SlotIndex;
	if (World)
	{
		NextFireTime = World->GetTimeSeconds() + 0.2f;
	}
	RefreshWeaponVisual();
	RefreshRack();
}

float AMetalBotCharacter::GetSwapProgress() const
{
	if (!IsSwappingWeapon() || GetWorld() == nullptr)
	{
		return -1.f;
	}
	const float Duration = Archetype ? FMath::Max(0.2f, Archetype->WeaponSwapDuration) : 0.8f;
	return FMath::Clamp((GetWorld()->GetTimeSeconds() - SwapStartTime) / Duration, 0.f, 1.f);
}

void AMetalBotCharacter::TickWeaponSwap(float Now)
{
	if (!IsSwappingWeapon())
	{
		return;
	}
	if (bDead || bDormant || !Weapons.IsValidIndex(SwapTargetSlot))
	{
		// Interrupted (killed, or the player tethered away): finish instantly so the robot isn't left armless.
		SwapStartTime = -1.f;
		CarriedGun->SetVisibility(false);
		CurrentWeapon = Weapons.IsValidIndex(SwapTargetSlot) ? SwapTargetSlot : CurrentWeapon;
		RefreshWeaponVisual();
		RefreshRack();
		return;
	}
	// The beats line up with the poses in UMetalBotAnimInstance: the off hand reaches the gun arm and twists
	// the gun off (0.28), swings it over the shoulder onto the back rack and pulls the new one (0.5), then
	// brings it round and snaps it onto the forearm (0.8).
	const float T = GetSwapProgress();
	if (SwapStage == 0 && T >= 0.28f)
	{
		SwapStage = 1;
		RefreshWeaponVisual(); // bare forearm mount while the gun is off
		PlaceWeaponOnBone(CarriedGun, SwapFromSlot, GetRig().HandL, FTransform::Identity);
		PlaySound(TEXT("swap_detach"));
	}
	if (SwapStage == 1 && T >= 0.5f)
	{
		SwapStage = 2;
		RackOrder.Remove(SwapFromSlot);
		RackOrder.Insert(SwapFromSlot, 0);
		PlaceWeaponOnBone(CarriedGun, SwapTargetSlot, GetRig().HandL, FTransform::Identity);
		RefreshRack();
		PlaySound(TEXT("swap_rack"), 0.7f);
	}
	if (SwapStage == 2 && T >= 0.8f)
	{
		SwapStage = 3;
		CurrentWeapon = SwapTargetSlot;
		CarriedGun->SetVisibility(false);
		RefreshWeaponVisual();
		RefreshRack();
		if (UMetalBotAnimInstance* Anim = Cast<UMetalBotAnimInstance>(GetMesh()->GetAnimInstance()))
		{
			Anim->PlayRecoil();
		}
		PlaySound(TEXT("swap_attach"));
	}
	if (T >= 1.f)
	{
		SwapStartTime = -1.f;
		NextFireTime = FMath::Max(NextFireTime, Now);
	}
}

void AMetalBotCharacter::PlaceWeaponOnBone(USkeletalMeshComponent* Component, int32 Slot, FName Bone, const FTransform& GripInBody)
{
	USkeletalMesh* Held = nullptr;
	if (Weapons.IsValidIndex(Slot) && Weapons[Slot].Data)
	{
		Held = Weapons[Slot].Data->GetLevel(Weapons[Slot].Level).HeldMeshes.FindRef(GetArchetypeId());
	}
	const USkeletalMesh* BodyAsset = GetMesh()->GetSkeletalMeshAsset();
	if (BodyAsset && (Bone.IsNone() || BodyAsset->GetRefSkeleton().FindBoneIndex(Bone) == INDEX_NONE))
	{
		Bone = Component == CarriedGun ? GetRig().ForearmL : GetRig().Pelvis;
	}
	const int32 BodyBone = BodyAsset ? BodyAsset->GetRefSkeleton().FindBoneIndex(Bone) : INDEX_NONE;
	if (Held == nullptr || BodyBone == INDEX_NONE)
	{
		Component->SetVisibility(false);
		return;
	}
	if (Component->GetSkeletalMeshAsset() != Held)
	{
		Component->SetSkeletalMeshAsset(Held);
	}
	// The held meshes are skinned to the body's skeleton, so on their own (no leader pose) they show the gun
	// where it sits on the gun arm in the bind pose. Find that grip (the weapon bone in the gun's bind pose)
	// and move the whole component so the grip lands on the wanted spot. The maths is done in body-mesh
	// space and then made relative to Bone, so the gun follows that bone's animation.
	const FReferenceSkeleton& GunRef = Held->GetRefSkeleton();
	const int32 GunBone = GunRef.FindBoneIndex(GetRig().WeaponBone);
	const FTransform GripInGun = GunBone != INDEX_NONE ? FAnimationRuntime::GetComponentSpaceTransformRefPose(GunRef, GunBone) : FTransform::Identity;
	const FTransform BoneInBody = FAnimationRuntime::GetComponentSpaceTransformRefPose(BodyAsset->GetRefSkeleton(), BodyBone);
	// Identity means "in the hand": grip on the bone itself, gun pointing the way it points on the arm.
	const FTransform Target = GripInBody.Equals(FTransform::Identity) ? FTransform(GripInGun.GetRotation(), BoneInBody.GetLocation()) : GripInBody;
	const FTransform ComponentInBody = GripInGun.Inverse() * Target;
	Component->AttachToComponent(GetMesh(), FAttachmentTransformRules::KeepRelativeTransform, Bone);
	Component->SetRelativeTransform(ComponentInBody.GetRelativeTransform(BoneInBody));
	Component->SetVisibility(true);
}

void AMetalBotCharacter::RefreshRack()
{
	if (Archetype == nullptr || !Archetype->bAnimatedWeaponSwap || bWeaponDisabled || GetMesh()->GetSkeletalMeshAsset() == nullptr)
	{
		RackGunA->SetVisibility(false);
		RackGunB->SetVisibility(false);
		return;
	}
	RackOrder.RemoveAll([this](int32 Slot) { return !Weapons.IsValidIndex(Slot); });
	for (int32 Slot = 0; Slot < Weapons.Num(); ++Slot)
	{
		RackOrder.AddUnique(Slot);
	}
	// Whatever is on the arm or in the off hand right now isn't on the rack. Mid-swap CurrentWeapon still
	// names the old gun until it is snapped on (stage 3), so work out where each gun actually is: on the
	// arm (stage 0), in the hand (stage 1), racked while the new one is in the hand (stage 2), new one on.
	int32 OnArm = CurrentWeapon;
	int32 InHand = INDEX_NONE;
	if (IsSwappingWeapon())
	{
		OnArm = SwapStage == 0 ? SwapFromSlot : (SwapStage >= 3 ? SwapTargetSlot : INDEX_NONE);
		InHand = SwapStage == 1 ? SwapFromSlot : (SwapStage == 2 ? SwapTargetSlot : INDEX_NONE);
	}
	TArray<int32, TInlineAllocator<2>> Shown;
	for (int32 Slot : RackOrder)
	{
		const FMetalWeaponSlot& Weapon = Weapons[Slot];
		if (Slot != OnArm && Slot != InHand && Shown.Num() < 2 && Weapon.Data
			&& Weapon.Data->GetLevel(Weapon.Level).HeldMeshes.FindRef(GetArchetypeId()))
		{
			Shown.Add(Slot);
		}
	}
	const FMetalBotRig& Rig = GetRig();
	const FName Bone = Rig.BackBone.IsNone() ? Rig.Pelvis : Rig.BackBone;
	// Two guns slung barrel-up behind the shoulders, crossed into a shallow X. Body space: +Y forward, +Z up;
	// tilting the forward-pointing barrel 100 degrees about X stands it up, leaning slightly back.
	for (int32 Index = 0; Index < 2; ++Index)
	{
		const float Side = Index == 0 ? 1.f : -1.f;
		const FQuat Lean = FQuat(FVector::YAxisVector, FMath::DegreesToRadians(10.f * Side)) * FQuat(FVector::XAxisVector, FMath::DegreesToRadians(100.f));
		// The grip rotation keeps the gun's own grip orientation, then leans the whole thing.
		USkeletalMeshComponent* Component = Index == 0 ? RackGunA : RackGunB;
		const int32 Slot = Shown.IsValidIndex(Index) ? Shown[Index] : INDEX_NONE;
		FQuat GripRotation = FQuat::Identity;
		if (Weapons.IsValidIndex(Slot))
		{
			if (const USkeletalMesh* Held = Weapons[Slot].Data->GetLevel(Weapons[Slot].Level).HeldMeshes.FindRef(GetArchetypeId()))
			{
				const int32 GunBone = Held->GetRefSkeleton().FindBoneIndex(Rig.WeaponBone);
				if (GunBone != INDEX_NONE)
				{
					GripRotation = FAnimationRuntime::GetComponentSpaceTransformRefPose(Held->GetRefSkeleton(), GunBone).GetRotation();
				}
			}
		}
		const FTransform Grip(Lean * GripRotation, Rig.RackOffset + FVector(6.f * Side, -3.f * Index, 0.f));
		PlaceWeaponOnBone(Component, Slot, Bone, Grip);
	}
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
	// Mid-swap the gun is in the other hand and the forearm ends in a bare mount: no gun, no hand.
	const bool bBareMount = IsSwappingWeapon() && (SwapStage == 1 || SwapStage == 2);
	WeaponMesh->SetVisibility(Held != nullptr && !bBareMount);
	UnarmedMesh->SetVisibility(Held == nullptr && !bBareMount && UnarmedMesh->GetSkeletalMeshAsset() != nullptr);
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
		PlaySound(Slot->Data->ReloadSound, IsPlayerControlled() ? 1.f : 0.5f);
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
	const FVector AimPoint = GetAimPoint();
	FVector AimDirection = (AimPoint - Muzzle).GetSafeNormal();
	// A player's hitscan shot is traced along the camera's aim ray (from level with the robot), and only
	// drawn from the muzzle. Tracing from the muzzle misses when the gun pokes into a wall or ledge the
	// player is aiming over, e.g. sniping down from a perch.
	FVector TraceStart = Muzzle;
	FVector TraceDirection = AimDirection;
	if (const APlayerController* PC = Cast<APlayerController>(GetController()); PC && Data->FireMode == EMetalFireMode::Hitscan)
	{
		FVector ViewLocation;
		FRotator ViewRotation;
		PC->GetPlayerViewPoint(ViewLocation, ViewRotation);
		const FVector ViewDirection = ViewRotation.Vector();
		TraceStart = ViewLocation + ViewDirection * FMath::Max(0.f, FVector::DotProduct(GetActorLocation() - ViewLocation, ViewDirection));
		TraceDirection = (AimPoint - TraceStart).GetSafeNormal();
	}
	// The aim point can be behind the muzzle (target right next to the robot, or the camera looking past
	// the body); shooting backwards through the robot looks wrong, so fall back to straight ahead.
	if (FVector::DotProduct(AimDirection, GetAimForward()) < 0.2f)
	{
		AimDirection = GetAimForward();
		TraceStart = Muzzle;
		TraceDirection = AimDirection;
	}
	const float AIError = (!IsPlayerControlled() && Archetype) ? Archetype->AimErrorDegrees : 0.f;
	// Precision weapons (sniper) are only accurate down the scope.
	const float Spread = (IsPlayerControlled() && !bAiming && Data->HipSpreadDegrees >= 0.f) ? Data->HipSpreadDegrees : Data->SpreadDegrees;
	const float Cone = FMath::DegreesToRadians(Spread + AIError);

	// One iteration per pellet, each randomised inside the spread cone. Hitscan weapons trace and damage
	// immediately; projectile and thrown weapons spawn an AMetalProjectile that deals damage on impact.
	bool bHitRobot = false;
	for (int32 Pellet = 0; Pellet < FMath::Max(1, Data->Pellets); ++Pellet)
	{
		const FVector Direction = FMath::VRandCone(AimDirection, Cone);
		if (Data->FireMode == EMetalFireMode::Hitscan)
		{
			// Same spread offset, applied to the trace ray (identical to Direction for AI and muzzle traces).
			const FVector TraceDir = FQuat::FindBetweenNormals(AimDirection, TraceDirection).RotateVector(Direction);
			const FVector End = TraceStart + TraceDir * Data->Range;
			FCollisionQueryParams Params(SCENE_QUERY_STAT(MetalShot), false, this);
			FHitResult Hit;
			const bool bHit = World->LineTraceSingleByChannel(Hit, TraceStart, End, ECC_Visibility, Params);
			const FVector Impact = bHit ? FVector(Hit.ImpactPoint) : End;
			AMetalFX::SpawnBeam(World, Muzzle, Impact, Data->TracerColor, 2.5f, 0.07f);
			if (bHit)
			{
				AMetalFX::SpawnBurst(World, Impact, Data->TracerColor, 10.f, 0.12f);
				if (Pellet == 0)
				{
					MetalAudio::PlayAt(this, Data->ImpactSound, Impact, 0.6f, 0.1f);
				}
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
	MetalAudio::PlayAt(this, Data->FireSound, Muzzle, IsPlayerControlled() ? 1.f : 0.7f);
	if (IsPlayerControlled())
	{
		// Gunfire carries: enemies within the weapon's noise radius come looking.
		AMetalBotAIController::ReportNoise(this, Muzzle, Data->NoiseRadius);
	}
	if (UMetalBotAnimInstance* Anim = Cast<UMetalBotAnimInstance>(GetMesh()->GetAnimInstance()))
	{
		Anim->PlayRecoil();
	}
	if (AMetalPlayerController* PC = Cast<AMetalPlayerController>(GetController()))
	{
		// Projectiles land later; only hitscan shots count towards accuracy.
		if (Data->FireMode == EMetalFireMode::Hitscan)
		{
			PC->NotifyShotFired(bHitRobot);
		}
		if (bHitRobot)
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
	const FVector Target = GetAimPoint();
	const FVector Direction = (Target - Start).GetSafeNormal();
	FVector Velocity = Direction * GrenadeWeapon->ProjectileSpeed + FVector(0.f, 0.f, 350.f);
	float Fuse = -1.f;
	// Players lob onto whatever the crosshair is on (within throwing range), arcing over cover, and the
	// fuse waits for it to land. Beyond that range, or with nothing under the crosshair, it's a long throw.
	constexpr float MaxLob = 2200.f;
	if (IsPlayerControlled() && FVector::Dist(Start, Target) < MaxLob)
	{
		const float GravityZ = GetWorld()->GetGravityZ() * GrenadeWeapon->ProjectileGravityScale;
		FVector Lob;
		if (UGameplayStatics::SuggestProjectileVelocity_CustomArc(this, Lob, Start, Target, GravityZ, 0.45f))
		{
			Velocity = Lob;
			// Time to reach the target horizontally, plus a beat to settle.
			const float Horizontal = FVector::Dist2D(Start, Target);
			Fuse = Horizontal / FMath::Max(1.f, Velocity.Size2D()) + 0.35f;
		}
	}
	FActorSpawnParameters Params;
	Params.Owner = this;
	Params.Instigator = this;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	if (AMetalProjectile* Projectile = GetWorld()->SpawnActor<AMetalProjectile>(Start, Velocity.Rotation(), Params))
	{
		Projectile->Launch(GrenadeWeapon, GrenadeWeapon->GetLevel(0).Damage, Velocity);
		if (Fuse > 0.f)
		{
			Projectile->SetFuse(Fuse);
		}
	}
	PlaySound(TEXT("grenade_throw"));
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
	if (GetWorld()->GetTimeSeconds() - LastDamageTakenTime > 0.1f)
	{
		PlaySound(IsPlayerControlled() ? TEXT("player_hit") : TEXT("robot_hit"), 0.8f);
	}
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
	// Stop firing and moving, and switch the capsule's collision off.
	StopFire();
	GetCharacterMovement()->StopMovementImmediately();
	GetCharacterMovement()->DisableMovement();
	GetCapsuleComponent()->SetCollisionEnabled(ECollisionEnabled::NoCollision);

	// Robots with a physics asset collapse as a ragdoll, nudged backwards. The others are slumped by the
	// animator instead (IsSlumped).
	if (GetMesh()->GetPhysicsAsset())
	{
		GetMesh()->SetCollisionProfileName(TEXT("Ragdoll"));
		GetMesh()->SetAllBodiesSimulatePhysics(true);
		GetMesh()->SetSimulatePhysics(true);
		GetMesh()->WakeAllRigidBodies();
		GetMesh()->AddImpulse(-GetActorForwardVector() * 150.f + FVector(0.f, 0.f, 100.f), NAME_None, true);
		bRagdoll = true;
	}

	// Kill counts, hijack endings and battle tickets all hang off this event.
	OnAnyBotDied.Broadcast(this, Killer);
	if (AMetalBotAIController* AI = Cast<AMetalBotAIController>(GetController()))
	{
		AI->NotifyPawnDied();
	}
	// The wreck lies there for a moment, then blows apart and is removed.
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
	MetalAudio::PlayAt(this, TEXT("robot_explode"), Center, FMath::Clamp(GetMesh()->GetRelativeScale3D().X, 0.8f, 2.f));
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

	// Exploring: centred behind and a little further out. Combat: over the shoulder. Aiming: zoomed in by
	// the weapon's own amount; a scoped weapon pulls the camera right into the robot's head.
	const FMetalWeaponSlot* Slot = IsSwappingWeapon() ? nullptr : GetCurrentWeapon();
	const UMetalWeaponData* Weapon = Slot ? Slot->Data.Get() : nullptr;
	const bool bScoped = bAiming && Weapon && Weapon->bScopeOverlay && IsPlayerControlled();
	const float AimArm = Weapon ? Weapon->AimArmLengthScale : 0.55f;
	const float AimFOV = Weapon ? Weapon->AimFOV : 60.f;
	const bool bRelaxed = bAction && !bCombat;
	const float ArmTarget = bAiming ? DefaultArmLength * AimArm : (bRelaxed ? DefaultArmLength * (1.1f + 0.1f * SprintAlpha) : DefaultArmLength);
	FVector OffsetTarget = bRelaxed ? FVector(0.f, 0.f, DefaultCameraOffset.Z) : DefaultCameraOffset;
	if (bScoped)
	{
		// Eye level: from the capsule centre most of the way up to the top of the head.
		OffsetTarget = FVector(0.f, 0.f, GetCapsuleComponent()->GetUnscaledCapsuleHalfHeight() * 0.45f);
	}
	const float Blend = bScoped ? 18.f : (bRelaxed ? 4.f : 10.f);
	CameraBoom->TargetArmLength = FMath::FInterpTo(CameraBoom->TargetArmLength, ArmTarget, DeltaSeconds, Blend);
	CameraBoom->SocketOffset = FMath::VInterpTo(CameraBoom->SocketOffset, OffsetTarget, DeltaSeconds, bScoped ? 18.f : 6.f);
	CameraBoom->bEnableCameraLag = !bScoped;
	const float FOVTarget = bAiming ? AimFOV : 85.f + 7.f * SprintAlpha;
	FollowCamera->SetFieldOfView(FMath::FInterpTo(FollowCamera->FieldOfView, FOVTarget, DeltaSeconds, bScoped ? 14.f : 10.f));

	ScopeAlpha = FMath::FInterpTo(ScopeAlpha, bScoped ? 1.f : 0.f, DeltaSeconds, 12.f);
	const bool bHide = ScopeAlpha > 0.6f;
	if (bHide != bHiddenForScope)
	{
		// Looking down the scope from inside the head: hide our own body from our own camera only.
		bHiddenForScope = bHide;
		TArray<USceneComponent*> Attached;
		GetMesh()->GetChildrenComponents(true, Attached);
		GetMesh()->SetOwnerNoSee(bHide);
		for (USceneComponent* Child : Attached)
		{
			if (UPrimitiveComponent* Primitive = Cast<UPrimitiveComponent>(Child))
			{
				Primitive->SetOwnerNoSee(bHide);
			}
		}
	}
}

float AMetalBotCharacter::GetLookSensitivityScale() const
{
	const FMetalWeaponSlot* Slot = GetCurrentWeapon();
	return (bAiming && Slot && Slot->Data) ? Slot->Data->AimSensitivity : 1.f;
}

bool AMetalBotCharacter::CanSprint() const
{
	return Archetype && Archetype->SprintSpeed > 0.f;
}

bool AMetalBotCharacter::CanSneak() const
{
	return Archetype && Archetype->CrouchSpeed > 0.f;
}

void AMetalBotCharacter::SetWantsSprint(bool bNewWantsSprint)
{
	bWantsSprint = bNewWantsSprint;
	if (bWantsSprint && bIsCrouched)
	{
		// Breaking into a run stands the robot up.
		UnCrouch();
	}
}

void AMetalBotCharacter::SetSneaking(bool bNewSneaking)
{
	if (!CanSneak() || bDead || bDormant)
	{
		return;
	}
	if (bNewSneaking)
	{
		bWantsSprint = false;
		Crouch();
	}
	else
	{
		UnCrouch();
	}
}

void AMetalBotCharacter::BuildJetpack()
{
	const USkeletalMesh* Body = GetMesh()->GetSkeletalMeshAsset();
	if (!JetpackParts.IsEmpty() || Archetype == nullptr || !Archetype->bHasJetpack || Body == nullptr || JetPodMesh == nullptr || JetFlameMesh == nullptr)
	{
		return;
	}
	const FMetalBotRig& Rig = GetRig();
	const int32 BoneIndex = Body->GetRefSkeleton().FindBoneIndex(Rig.Pelvis);
	if (BoneIndex == INDEX_NONE)
	{
		return;
	}
	// Everything is placed in body-mesh space of the bind pose (+Y forward, +Z up, -X right) and then made
	// relative to the pelvis, so the pack rides along with the body, the double-jump flip included.
	const FTransform BoneInBody = FAnimationRuntime::GetComponentSpaceTransformRefPose(Body->GetRefSkeleton(), BoneIndex);
	UMaterialInterface* Dark = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Metalization/Materials/MI_Env_Dark.MI_Env_Dark"), nullptr, LOAD_NoWarn | LOAD_Quiet);
	UMaterialInterface* Glow = MetalAssets::GetGlowMaterial();
	auto Attach = [this, &BoneInBody, &Rig](USceneComponent* Component, const FTransform& InBody)
	{
		Component->SetupAttachment(GetMesh(), Rig.Pelvis);
		Component->SetRelativeTransform(InBody.GetRelativeTransform(BoneInBody));
		Component->RegisterComponent();
	};
	auto Part = [this, Dark](UStaticMesh* Shape)
	{
		UStaticMeshComponent* Component = NewObject<UStaticMeshComponent>(this);
		Component->SetStaticMesh(Shape);
		Component->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		if (Dark)
		{
			Component->SetMaterial(0, Dark);
		}
		JetpackParts.Add(Component);
		return Component;
	};
	// The engine cylinder and cone are 100 cm across and 100 cm tall around their pivot; the cone's tip is up.
	for (const float Side : {1.f, -1.f})
	{
		const FVector Pod = Rig.JetpackOffset + FVector(Rig.JetpackSpread * Side, 0.f, 0.f);
		Attach(Part(JetPodMesh), FTransform(FQuat::Identity, Pod, FVector(0.09f, 0.09f, 0.24f)));
		// Strap holding the pod to the back, and a flared nozzle under it.
		Attach(Part(JetPodMesh),
			FTransform(FQuat(FVector::XAxisVector, UE_HALF_PI), Pod + FVector(0.f, 4.f, 6.f), FVector(0.04f, 0.04f, 0.08f)));
		const FVector Nozzle = Pod - FVector(0.f, 0.f, 12.f + 3.f);
		Attach(Part(JetFlameMesh), FTransform(FQuat::Identity, Nozzle, FVector(0.085f, 0.085f, 0.06f)));

		// Flame: a cone pointing down from the nozzle, hung off a pivot that is stretched while it burns.
		// Attached at unit size first to learn the relative scale that means "1" in body space: the imported
		// skeleton's bones carry a scale, so the flame can't be sized with absolute relative scales.
		USceneComponent* Pivot = NewObject<USceneComponent>(this);
		Attach(Pivot, FTransform(FQuat::Identity, Nozzle - FVector(0.f, 0.f, 3.f)));
		JetPivotUnitScale = Pivot->GetRelativeScale3D();
		Pivot->SetRelativeScale3D(JetPivotUnitScale * FVector(0.07f, 0.07f, 0.01f));
		UStaticMeshComponent* Flame = NewObject<UStaticMeshComponent>(this);
		Flame->SetStaticMesh(JetFlameMesh);
		Flame->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Flame->SetCastShadow(false);
		Flame->SetupAttachment(Pivot);
		Flame->SetRelativeLocationAndRotation(FVector(0.f, 0.f, -50.f), FQuat(FVector::XAxisVector, UE_PI));
		if (Glow)
		{
			UMaterialInstanceDynamic* MID = UMaterialInstanceDynamic::Create(Glow, this);
			MID->SetVectorParameterValue(TEXT("Color"), FLinearColor(1.f, 0.55f, 0.15f));
			MID->SetScalarParameterValue(TEXT("Intensity"), 3.f);
			Flame->SetMaterial(0, MID);
			JetFlameMaterials.Add(MID);
		}
		Flame->RegisterComponent();
		Flame->SetVisibility(false);
		Pivot->SetVisibility(false, true);
		JetFlamePivots.Add(Pivot);
	}
	JetLight = NewObject<UPointLightComponent>(this);
	JetLight->SetLightColor(FLinearColor(1.f, 0.55f, 0.2f));
	JetLight->SetAttenuationRadius(450.f);
	JetLight->SetCastShadows(false);
	JetLight->SetIntensity(0.f);
	Attach(JetLight, FTransform(Rig.JetpackOffset - FVector(0.f, 4.f, 30.f)));
}

void AMetalBotCharacter::FireJetpack()
{
	if (JetFlamePivots.IsEmpty() || bDead)
	{
		return;
	}
	JetFireEnd = GetWorld()->GetTimeSeconds() + 0.55f;
	for (USceneComponent* Pivot : JetFlamePivots)
	{
		Pivot->SetVisibility(true, true);
		AMetalFX::SpawnBurst(GetWorld(), Pivot->GetComponentLocation() - FVector(0.f, 0.f, 10.f), FLinearColor(1.f, 0.6f, 0.2f), 18.f, 0.2f);
	}
}

void AMetalBotCharacter::TickJetpack(float Now)
{
	if (JetFlamePivots.IsEmpty() || JetFireEnd <= 0.f)
	{
		return;
	}
	// A hard kick that tails off: full length at ignition, gone after ~0.55 s (or as soon as it lands).
	const float Alpha = FMath::Clamp((JetFireEnd - Now) / 0.55f, 0.f, 1.f);
	const bool bBurning = Alpha > 0.02f && !bDead && !bDormant && GetCharacterMovement()->IsFalling();
	for (int32 Index = 0; Index < JetFlamePivots.Num(); ++Index)
	{
		USceneComponent* Pivot = JetFlamePivots[Index];
		if (!bBurning)
		{
			Pivot->SetVisibility(false, true);
			continue;
		}
		const float Flicker = 0.85f + 0.15f * FMath::Sin(Now * 55.f + Index * 1.7f);
		const float Length = (8.f + 42.f * FMath::Sqrt(Alpha)) * Flicker;
		const float Width = 0.07f * (0.7f + 0.3f * Alpha);
		Pivot->SetRelativeScale3D(JetPivotUnitScale * FVector(Width, Width, Length / 100.f));
		if (JetFlameMaterials.IsValidIndex(Index))
		{
			JetFlameMaterials[Index]->SetScalarParameterValue(TEXT("Intensity"), 2.f + 4.f * Alpha);
		}
	}
	if (JetLight)
	{
		JetLight->SetIntensity(bBurning ? 600.f * Alpha : 0.f);
	}
	if (!bBurning)
	{
		JetFireEnd = 0.f;
	}
}

void AMetalBotCharacter::PlaySound(FName Name, float Volume) const
{
	MetalAudio::PlayAt(this, Name, GetActorLocation(), Volume);
}

void AMetalBotCharacter::TickLocomotion(float DeltaSeconds)
{
	if (Archetype == nullptr || bDead)
	{
		bSprinting = false;
		return;
	}
	UCharacterMovementComponent* Move = GetCharacterMovement();
	const bool bPlayer = IsPlayerControlled();
	const FVector Velocity2D(GetVelocity().X, GetVelocity().Y, 0.f);
	const float Speed = Velocity2D.Size();
	const float Now = GetWorld()->GetTimeSeconds();

	if (bPlayer)
	{
		// Sprint only while running forward-ish with nothing else going on. In the air, keep the momentum.
		const FVector ViewForward = GetController() ? FRotator(0.f, GetController()->GetControlRotation().Yaw, 0.f).Vector() : GetActorForwardVector();
		const bool bForward = Speed > 50.f && FVector::DotProduct(Velocity2D / Speed, ViewForward) > 0.3f;
		const bool bFree = !bAiming && !bFireHeld && Now - LastFireTime > 0.4f && !IsSwappingWeapon() && !IsStunned() && !bDormant;
		if (Move->IsFalling())
		{
			bSprinting = bSprinting && bWantsSprint;
		}
		else
		{
			bSprinting = bWantsSprint && CanSprint() && !bIsCrouched && bFree && bForward;
		}
		// Also restores full speed to a hijacked robot that was walking at its slow patrol pace.
		Move->MaxWalkSpeed = bSprinting ? Archetype->SprintSpeed : Archetype->WalkSpeed;
	}
	else
	{
		bSprinting = false;
	}

	CrouchAlpha = FMath::FInterpTo(CrouchAlpha, bIsCrouched ? 1.f : 0.f, DeltaSeconds, 10.f);
	const float SprintRange = FMath::Max(1.f, Archetype->SprintSpeed - Archetype->WalkSpeed);
	const float SprintTarget = bSprinting ? FMath::Clamp((Speed - Archetype->WalkSpeed * 0.8f) / SprintRange, 0.f, 1.f) : 0.f;
	SprintAlpha = FMath::FInterpTo(SprintAlpha, SprintTarget, DeltaSeconds, 6.f);

	// Footsteps every half stride. Sneaking is near silent; walking and especially sprinting carry to enemies.
	if (Move->IsMovingOnGround() && Speed > 40.f && !bDormant)
	{
		StepDistance += Speed * DeltaSeconds;
		const float Scale = GetMesh()->GetRelativeScale3D().X;
		const float StepLength = FMath::Max(40.f, GetRig().StrideLength * Scale * 0.5f * (1.f + 0.4f * SprintAlpha));
		if (StepDistance >= StepLength)
		{
			StepDistance = 0.f;
			const bool bHeavy = Scale > 1.5f || Archetype->CapsuleHalfHeight > 100.f;
			const float Volume = bIsCrouched ? 0.12f : (bPlayer ? 0.55f : 0.4f);
			const FVector Feet = GetActorLocation() - FVector(0.f, 0.f, GetCapsuleComponent()->GetScaledCapsuleHalfHeight());
			MetalAudio::PlayAt(this, bHeavy ? TEXT("footstep_heavy") : (bSprinting ? TEXT("footstep_sprint") : TEXT("footstep")), Feet, Volume, 0.12f);
			if (bPlayer && !bIsCrouched)
			{
				AMetalBotAIController::ReportNoise(this, Feet, bSprinting ? 1300.f : 450.f);
			}
		}
	}
	else
	{
		StepDistance = 0.f;
	}

	// AI marksmen show where they are aiming with a laser sight.
	const FMetalWeaponSlot* Slot = GetCurrentWeapon();
	const bool bLaser = !bPlayer && bHasAITarget && !bDormant && !IsStunned() && !bWeaponDisabled && Slot && Slot->Data && Slot->Data->bLaserSight;
	if (bLaser)
	{
		const FVector Muzzle = GetMuzzleLocation();
		const FVector Direction = (AITargetPoint - Muzzle).GetSafeNormal();
		FCollisionQueryParams Params(SCENE_QUERY_STAT(MetalLaserSight), false, this);
		FHitResult Hit;
		const FVector End = Muzzle + Direction * Slot->Data->Range;
		const FVector Impact = GetWorld()->LineTraceSingleByChannel(Hit, Muzzle, End, ECC_Visibility, Params) ? FVector(Hit.ImpactPoint) : End;
		if (LaserSight == nullptr)
		{
			LaserSight = AMetalFX::SpawnBeam(GetWorld(), Muzzle, Impact, FLinearColor(1.f, 0.05f, 0.05f), 0.6f, 0.f);
		}
		else
		{
			LaserSight->SetBeamEndpoints(Muzzle, Impact);
		}
	}
	else if (LaserSight)
	{
		LaserSight->Destroy();
		LaserSight = nullptr;
	}
}

void AMetalBotCharacter::OnJumped_Implementation()
{
	Super::OnJumped_Implementation();
	LowestFallSpeed = 0.f;
	// JumpCurrentCount is incremented before this is called, so 2+ means a mid-air jump.
	if (JumpCurrentCount >= 2)
	{
		FlipStartTime = GetWorld()->GetTimeSeconds();
		PlaySound(TEXT("double_jump"), 0.8f);
		FireJetpack();
	}
	else
	{
		PlaySound(TEXT("jump"), 0.6f);
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
	// Crouching shortens the capsule around its bottom, so its centre drops: raise the mesh by the same
	// amount to keep the feet on the floor (the animator bends the legs).
	const float CrouchLift = (bIsCrouched && Archetype) ? Archetype->CapsuleHalfHeight - GetCapsuleComponent()->GetUnscaledCapsuleHalfHeight() : 0.f;
	GetMesh()->SetRelativeLocationAndRotation(Turn.RotateVector(MeshBaseLocation + FVector(0.f, 0.f, CrouchLift)), Turn * MeshBaseRotation);
}

void AMetalBotCharacter::Landed(const FHitResult& Hit)
{
	Super::Landed(Hit);
	FlipStartTime = -1.f;
	if (UMetalBotAnimInstance* Anim = Cast<UMetalBotAnimInstance>(GetMesh()->GetAnimInstance()))
	{
		Anim->PlayLand(FMath::Clamp(-LowestFallSpeed / 1400.f, 0.25f, 1.f));
	}
	if (LowestFallSpeed < -500.f)
	{
		PlaySound(TEXT("land"), FMath::Clamp(-LowestFallSpeed / 1400.f, 0.3f, 1.f));
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
	// Called whenever a mesh changes (archetype applied, weapon swapped): the new meshes come with their
	// asset materials, so every slot on every part is given a fresh dynamic instance and the current
	// hack glow and team paint are re-applied.
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

	// Material feedback: the hit flash fades out in about 1/6 s, and a hijacked robot's hack glow pulses.
	if (HitFlash > 0.f)
	{
		HitFlash = FMath::Max(0.f, HitFlash - DeltaSeconds * 6.f);
		SetMaterialScalar(TEXT("HitFlash"), HitFlash);
	}
	if (bHijacked && !bDead)
	{
		SetMaterialScalar(TEXT("HackGlow"), 0.65f + 0.35f * FMath::Sin(Now * 6.f));
	}
	else if (bFirewallGlow && !bDead)
	{
		// A firewall after a failed hack: the hack glow stutters on and off until it drops.
		bFirewallGlow = IsFirewalled();
		SetMaterialScalar(TEXT("HackGlow"), bFirewallGlow && FMath::Frac(Now * 5.f) < 0.35f ? 0.8f : 0.f);
	}

	// Movement state first (sprint, crouch, footsteps), then the things that pose or move the body, and the
	// flip last because it sets the final mesh transform.
	TickLocomotion(DeltaSeconds);
	TickWeaponSwap(Now);
	TickJetpack(Now);
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

	// The trigger: fire whenever it is held and the weapon is ready. A player has to click again for each shot
	// of a semi-automatic weapon; AI keeps holding the trigger for its bursts.
	if (bFireHeld && !bDead && !bDormant && !IsStunned() && !IsReloading() && !IsSwappingWeapon() && Now >= NextFireTime)
	{
		FireOnce();
		const FMetalWeaponSlot* Slot = GetCurrentWeapon();
		if (Slot && Slot->Data && !Slot->Data->bAutomatic && IsPlayerControlled())
		{
			bFireHeld = false;
		}
	}
}
