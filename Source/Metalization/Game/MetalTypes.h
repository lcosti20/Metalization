// Shared types for the Metalization gameplay prototype.

#pragma once

#include "CoreMinimal.h"
#include "MetalTypes.generated.h"

/** Log category for all Metalization gameplay code. Defined in MetalBotCharacter.cpp. */
DECLARE_LOG_CATEGORY_EXTERN(LogMetal, Log, All);

/**
 * Which side of the war a robot fights for. Possessed robots temporarily switch to the player's team.
 *
 * @see AMetalBotCharacter::GetTeam
 * @see AMetalBotCharacter::IsHostileTo
 */
UENUM(BlueprintType)
enum class EMetalTeam : uint8
{
	/** The player's side. */
	Heroes,
	/** The enemy side. */
	Villains,
	/** Hostile to nobody and ignored by AI targeting. */
	Neutral
};

/**
 * Bone names and tuning for the procedural animator. The graybox rigs differ per robot
 * (the Player has no spine, Shooters have digitigrade legs, Brutes have built-in guns), so every
 * archetype describes its own rig instead of relying on a shared skeleton.
 *
 * All rotations are in character space: X forward, Y right, Z up.
 *
 * @see UMetalBotAnimInstance, which reads this struct every frame to pose the bones.
 * @see UMetalBotArchetype::Rig
 */
USTRUCT(BlueprintType)
struct FMetalBotRig
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Bones")
	FName Pelvis = TEXT("bip_pelvis");

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Bones")
	FName Head = TEXT("bip_head");

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Bones")
	FName ThighL = TEXT("bip_hip_L");

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Bones")
	FName ThighR = TEXT("bip_hip_R");

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Bones")
	FName KneeL = TEXT("bip_knee_L");

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Bones")
	FName KneeR = TEXT("bip_knee_R");

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Bones")
	FName UpperArmL = TEXT("bip_upperArm_L");

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Bones")
	FName UpperArmR = TEXT("bip_upperArm_R");

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Bones")
	FName ForearmL = TEXT("bip_lowerArm_L");

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Bones")
	FName ForearmR = TEXT("bip_lowerArm_R");

	/** Bone that weapons are bonemerged onto, and where shots originate. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Bones")
	FName WeaponBone = TEXT("weapon_bone");

	/** Character-space offsets that turn the bind pose into a combat-ready stance (e.g. lower T-posed arms). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Stance")
	FRotator UpperArmLReady = FRotator::ZeroRotator;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Stance")
	FRotator UpperArmRReady = FRotator::ZeroRotator;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Stance")
	FRotator ForearmLReady = FRotator::ZeroRotator;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Stance")
	FRotator ForearmRReady = FRotator::ZeroRotator;

	/** Distance in cm covered by one full gait cycle (two steps). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Gait")
	float StrideLength = 120.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Gait")
	float LegSwingDegrees = 28.f;

	/** Positive bends the lower leg backwards (human knee); negative for reverse-jointed legs. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Gait")
	float KneeBendDegrees = 35.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Gait")
	float BobHeight = 3.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Gait")
	float ArmSwingDegrees = 14.f;

	/** How strongly the weapon arm follows the camera/AI aim pitch. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aim")
	float AimPitchWeight = 1.f;

	/** Pitch the off-hand arm with the aim as well (two-armed gunners like the Brute). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aim")
	bool bAimWithBothArms = false;

	/** Rotate the upper arms (shoulder) for aiming instead of the forearms (elbow). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aim")
	bool bAimFromShoulder = false;

	/** Flying robots (Stalkers): roots of the four gun arms. Setting FlyerArmUpperL switches on the flight poses. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Flight")
	FName FlyerArmUpperL = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Flight")
	FName FlyerArmUpperR = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Flight")
	FName FlyerArmLowerL = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Flight")
	FName FlyerArmLowerR = NAME_None;

	/** Upper arms tilt up and lower arms down by this much, making the X seen from the front. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Flight")
	float XSpreadDegrees = 28.f;

	/** Arms sweep back by this much at full boost. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Flight")
	float BoostSweepDegrees = 40.f;

	/** Arms fold back by this much while parked. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Flight")
	float ParkedFoldDegrees = 55.f;
};

/**
 * Flight handling for robots that fly instead of walk (Stalkers). Speeds in cm/s.
 *
 * @see AMetalStalker, which applies these values to its flying CharacterMovementComponent.
 */
USTRUCT(BlueprintType)
struct FMetalFlightTuning
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Flight")
	float MaxSpeed = 4500.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Flight")
	float Acceleration = 6000.f;

	/** Deceleration with no input: the craft coasts to a hover rather than drifting forever. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Flight")
	float Braking = 4000.f;

	/** How quickly the velocity swings round to a new stick direction. Higher = less drift, tighter corners. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Flight")
	float Grip = 3.f;

	/** Yaw speed of the body following the camera, degrees per second. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Flight")
	float TurnRate = 140.f;

	/** Bank angle at full strafe speed or a hard turn. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Flight")
	float MaxBank = 35.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Boost")
	float BoostSpeed = 10000.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Boost")
	float BoostAcceleration = 14000.f;

	/** Seconds of continuous boost from a full meter. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Boost")
	float BoostDuration = 3.f;

	/** Seconds to refill an empty meter once the boost is released. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Boost")
	float BoostRechargeTime = 5.f;

	/** Sideways burst of a dodge roll. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Boost")
	float DodgeSpeed = 4500.f;

	/** Boost meter fraction one dodge costs. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Boost")
	float DodgeCost = 0.25f;
};

/**
 * Per-team paint for robots that fight on both sides (Stalkers): applied to the Body / Accent material slots.
 *
 * @see UMetalBotArchetype::TeamColors
 */
USTRUCT(BlueprintType)
struct FMetalTeamColors
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Colors")
	FLinearColor Body = FLinearColor::Gray;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Colors")
	FLinearColor Accent = FLinearColor::White;
};

/**
 * A region of a robot that reacts differently to damage (weak points, destructible weapon arms).
 * A hit counts against the zone whose bone lies closest to the impact point, within Radius.
 *
 * @see AMetalBotCharacter::TakeDamage
 */
USTRUCT(BlueprintType)
struct FMetalHitZone
{
	GENERATED_BODY()

	/** Identifier used in logs and to look the zone up (e.g. "WeaponArm"). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Hit Zone")
	FName ZoneName = NAME_None;

	/** Bone the zone is centred on. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Hit Zone")
	FName Bone = NAME_None;

	/** Radius around the bone (cm, before actor scale) that counts as a hit on this zone. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Hit Zone")
	float Radius = 20.f;

	/** Scales damage landing in this zone (above 1 for weak points). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Hit Zone")
	float DamageMultiplier = 1.f;

	/** Damage this zone can absorb before it breaks. 0 = unbreakable. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Hit Zone")
	float Durability = 0.f;

	/** When the zone breaks the robot loses its gun and switches to melee. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Hit Zone")
	bool bDisablesWeaponWhenBroken = false;

	/**
	 * When the zone breaks, this built-in gun (one of the archetype's MuzzleBones) stops firing and the
	 * zone's bone is hidden, while the other guns keep going. Losing every gun disables the weapon.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Hit Zone")
	FName DisablesMuzzleBone = NAME_None;
};
