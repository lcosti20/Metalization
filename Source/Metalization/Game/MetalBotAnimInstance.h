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
	FMetalBotRig Rig;
	FQuat MeshToCharacter = FQuat::Identity;
	FVector MoveDirection = FVector::ForwardVector;
	float MoveAlpha = 0.f;
	float GaitPhase = 0.f;
	float InAirAlpha = 0.f;
	float AimPitch = 0.f;
	float Recoil = 0.f;
	float SlumpAlpha = 0.f;
	float StunAlpha = 0.f;
	float HitReact = 0.f;
	FVector HitDirection = FVector::ForwardVector;
	float Time = 0.f;
	/** 1 = gun up tracking the aim, 0 = running freely (arms pump, no aim). */
	float CombatAlpha = 1.f;
	float LandAlpha = 0.f;
	float FlipTuck = 0.f;
	/** Forward lean from acceleration and speed, degrees. */
	float Lean = 0.f;
};

struct FMetalBotAnimProxy : public FAnimInstanceProxy
{
	FMetalBotAnimProxy() = default;
	explicit FMetalBotAnimProxy(UAnimInstance* InAnimInstance) : FAnimInstanceProxy(InAnimInstance) {}

	virtual bool Evaluate(FPoseContext& Output) override;

	FMetalBotAnimState State;
};

UCLASS(Transient)
class UMetalBotAnimInstance : public UAnimInstance
{
	GENERATED_BODY()

public:
	/** Kicks the body back from a hit coming from WorldDirection. */
	void PlayHitReact(const FVector& WorldDirection);

	/** Weapon kick for one shot. */
	void PlayRecoil();

	/** Knee-bend absorb on touchdown, Strength 0..1. */
	void PlayLand(float Strength);

protected:
	virtual void NativeUpdateAnimation(float DeltaSeconds) override;
	virtual FAnimInstanceProxy* CreateAnimInstanceProxy() override;
	virtual void DestroyAnimInstanceProxy(FAnimInstanceProxy* InProxy) override;

private:
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
	FVector MoveDirection = FVector::ForwardVector;
	FVector HitDirection = FVector::ForwardVector;
};
