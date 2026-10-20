// Procedural animation for the Metalization robots.
//
// The graybox robots ship as bind poses only (no animation rig), and their skeletons differ
// wildly (the Player has no spine, Shooters have reverse knees, Brutes carry guns as forearms). Instead of retargeting humanoid clips, this anim instance builds the pose natively
// from the bind pose plus a handful of character-space rotations driven by gameplay state, using
// the per-archetype FMetalBotRig. Authored animation can replace it later by giving the mesh an
// Anim Blueprint instead.

#pragma once

#include "CoreMinimal.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimInstanceProxy.h"
#include "MetalTypes.h"
#include "MetalBotAnimInstance.generated.h"

/** Snapshot of gameplay state consumed on the animation worker thread. */
struct FMetalBotAnimState
{
	/** Bone names and pose tuning of the robot's archetype. */
	FMetalBotRig Rig;
	/** Mesh rotation relative to the capsule, to convert character-space deltas to component space. */
	FQuat MeshToCharacter = FQuat::Identity;
	/** Horizontal movement direction in character space. */
	FVector MoveDirection = FVector::ForwardVector;
	/** 0 standing still .. 1 at walking speed. */
	float MoveAlpha = 0.f;
	/** Position in the walk cycle, 0..1. */
	float GaitPhase = 0.f;
	/** 1 while airborne (jumping or falling). */
	float InAirAlpha = 0.f;
	/** Smoothed aim pitch, degrees. */
	float AimPitch = 0.f;
	/** 1 right after a shot, decaying. */
	float Recoil = 0.f;
	/** 1 when powered down or dead without a ragdoll. */
	float SlumpAlpha = 0.f;
	/** 1 while stunned, decaying after. */
	float StunAlpha = 0.f;
	/** 1 right after a hit, decaying. */
	float HitReact = 0.f;
	/** Direction the last hit travelled, character space. */
	FVector HitDirection = FVector::ForwardVector;
	/** Running time in seconds, for idle motion. */
	float Time = 0.f;
	/** 1 = gun up tracking the aim, 0 = running freely (arms pump, no aim). */
	float CombatAlpha = 1.f;
	/** Landing absorb strength, decaying. */
	float LandAlpha = 0.f;
	/** Tuck amount during the double-jump flip, 0..1. */
	float FlipTuck = 0.f;
	/** Forward lean from acceleration and speed, degrees. */
	float Lean = 0.f;
	/** Flying robots: arms sweep back with boost and fold while parked. */
	float BoostAlpha = 0.f;
	float ParkedAlpha = 0.f;
};

/**
 * Animation proxy that does the actual posing. UE evaluates proxies on a worker thread, so it only reads
 * the State snapshot copied over by UMetalBotAnimInstance::NativeUpdateAnimation on the game thread.
 */
struct FMetalBotAnimProxy : public FAnimInstanceProxy
{
	FMetalBotAnimProxy() = default;

	/** @param InAnimInstance The owning UMetalBotAnimInstance. */
	explicit FMetalBotAnimProxy(UAnimInstance* InAnimInstance) : FAnimInstanceProxy(InAnimInstance) {}

	/**
	 * Builds the output pose: starts from the reference (bind) pose, accumulates character-space rotations
	 * for each animated bone from State, then applies them down the bone hierarchy.
	 *
	 * @param Output Pose to fill.
	 * @return Always true (the pose is always valid).
	 */
	virtual bool Evaluate(FPoseContext& Output) override;

	/** Latest gameplay snapshot, written on the game thread before each evaluation. */
	FMetalBotAnimState State;
};

/**
 * Anim instance assigned to every robot mesh by AMetalBotCharacter. Each frame it smooths gameplay state
 * (speed, aim, stun, boost...) into 0-1 blend weights and hands them to FMetalBotAnimProxy, which poses the
 * skeleton procedurally. One-shot reactions (hit, recoil, landing) are triggered by the Play* functions.
 */
UCLASS(Transient)
class UMetalBotAnimInstance : public UAnimInstance
{
	GENERATED_BODY()

public:
	/**
	 * Kicks the body back from a hit coming from WorldDirection.
	 *
	 * @param WorldDirection Direction the hit travels in, world space (only the horizontal part is used).
	 */
	void PlayHitReact(const FVector& WorldDirection);

	/** Weapon kick for one shot. */
	void PlayRecoil();

	/**
	 * Knee-bend absorb on touchdown.
	 *
	 * @param Strength 0..1; a stronger landing overrides a weaker one still playing.
	 */
	void PlayLand(float Strength);

protected:
	/**
	 * Reads the owning robot's state, eases each blend weight towards its target, advances the gait phase
	 * by distance travelled, and copies everything into the proxy's State.
	 *
	 * @param DeltaSeconds Frame time.
	 */
	virtual void NativeUpdateAnimation(float DeltaSeconds) override;

	/** @return A new FMetalBotAnimProxy (owned by the engine until DestroyAnimInstanceProxy). */
	virtual FAnimInstanceProxy* CreateAnimInstanceProxy() override;

	/** Deletes the proxy made by CreateAnimInstanceProxy. */
	virtual void DestroyAnimInstanceProxy(FAnimInstanceProxy* InProxy) override;

private:
	// Smoothed game-thread copies of the FMetalBotAnimState fields (see that struct for their meaning).
	float GaitPhase = 0.f;
	float MoveAlpha = 0.f;
	float InAirAlpha = 0.f;
	float SlumpAlpha = 0.f;
	float StunAlpha = 0.f;
	float Recoil = 0.f;
	float HitReact = 0.f;
	float AimPitch = 0.f;
	float Time = 0.f;
	float CombatAlpha = 1.f;
	float LandAlpha = 0.f;
	float Lean = 0.f;
	float BoostAlpha = 0.f;
	float ParkedAlpha = 0.f;
	FVector MoveDirection = FVector::ForwardVector;
	FVector HitDirection = FVector::ForwardVector;
};
