// Shared types for the Metalization gameplay prototype.

#pragma once

#include "CoreMinimal.h"
#include "MetalTypes.generated.h"

DECLARE_LOG_CATEGORY_EXTERN(LogMetal, Log, All);

/** Which side of the war a robot fights for. Possessed robots temporarily switch to the player's team. */
UENUM(BlueprintType)
enum class EMetalTeam : uint8
{
	Heroes,
	Villains,
	Neutral
};

/**
 * Bone names and tuning for the procedural animator. The graybox rigs differ per robot
 * (the Player has no spine, Shooters have digitigrade legs, Brutes have built-in guns), so every
 * archetype describes its own rig instead of relying on a shared skeleton.
 *
 * All rotations are in character space: X forward, Y right, Z up.
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
};

/** A region of a robot that reacts differently to damage (weak points, destructible weapon arms). */
USTRUCT(BlueprintType)
struct FMetalHitZone
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Hit Zone")
	FName ZoneName = NAME_None;

	/** Bone the zone is centred on. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Hit Zone")
	FName Bone = NAME_None;

	/** Radius around the bone (cm, before actor scale) that counts as a hit on this zone. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Hit Zone")
	float Radius = 20.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Hit Zone")
	float DamageMultiplier = 1.f;

	/** Damage this zone can absorb before it breaks. 0 = unbreakable. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Hit Zone")
	float Durability = 0.f;

	/** When the zone breaks the robot loses its gun and switches to melee. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Hit Zone")
	bool bDisablesWeaponWhenBroken = false;
};
