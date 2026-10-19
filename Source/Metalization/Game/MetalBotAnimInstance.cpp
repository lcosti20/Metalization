#include "MetalBotAnimInstance.h"

#include "MetalBotCharacter.h"
#include "MetalStalker.h"
#include "Animation/AnimNodeBase.h"
#include "GameFramework/CharacterMovementComponent.h"

namespace MetalBotAnim
{
	/** Rotation that turns a downward-hanging limb towards Direction by Degrees (character space). */
	FQuat SwingToward(const FVector& Direction, float Degrees)
	{
		const FVector Axis = FVector::CrossProduct(FVector::DownVector, Direction).GetSafeNormal();
		return Axis.IsNearlyZero() ? FQuat::Identity : FQuat(Axis, FMath::DegreesToRadians(Degrees));
	}

	/** Positive degrees tilt the forward axis upwards. */
	FQuat PitchUp(float Degrees)
	{
		return FQuat(FVector(0.f, -1.f, 0.f), FMath::DegreesToRadians(Degrees));
	}

	FQuat Yaw(float Degrees)
	{
		return FQuat(FVector::UpVector, FMath::DegreesToRadians(Degrees));
	}
}

void UMetalBotAnimInstance::PlayHitReact(const FVector& WorldDirection)
{
	if (const AActor* Owner = GetOwningActor())
	{
		HitDirection = Owner->GetActorTransform().InverseTransformVectorNoScale(WorldDirection).GetSafeNormal2D();
		HitReact = 1.f;
	}
}

void UMetalBotAnimInstance::PlayRecoil()
{
	Recoil = 1.f;
}

void UMetalBotAnimInstance::PlayLand(float Strength)
{
	LandAlpha = FMath::Max(LandAlpha, Strength);
}

void UMetalBotAnimInstance::NativeUpdateAnimation(float DeltaSeconds)
{
	Super::NativeUpdateAnimation(DeltaSeconds);

	const AMetalBotCharacter* Bot = Cast<AMetalBotCharacter>(TryGetPawnOwner());
	if (Bot == nullptr)
	{
		return;
	}

	Time += DeltaSeconds;
	const FMetalBotRig& Rig = Bot->GetRig();
	const float Scale = FMath::Max(0.1f, Bot->GetMesh()->GetRelativeScale3D().Z);

	const FVector LocalVelocity = Bot->GetActorTransform().InverseTransformVectorNoScale(Bot->GetVelocity());
	const float GroundSpeed = LocalVelocity.Size2D();
	// Flyers (Stalkers) have no legs to walk or fall with; their arms get the flight poses instead.
	const AMetalStalker* Flyer = Cast<AMetalStalker>(Bot);
	const bool bFlyer = !Rig.FlyerArmUpperL.IsNone();
	const bool bFalling = !bFlyer && Bot->GetCharacterMovement() && Bot->GetCharacterMovement()->IsFalling();
	const bool bCanMove = Bot->IsAlive() && !Bot->IsSlumped() && !bFlyer;

	if (GroundSpeed > 10.f)
	{
		MoveDirection = FVector(LocalVelocity.X, LocalVelocity.Y, 0.f).GetSafeNormal();
	}
	const float TargetMoveAlpha = (bCanMove && !bFalling) ? FMath::Clamp(GroundSpeed / 200.f, 0.f, 1.f) : 0.f;
	MoveAlpha = FMath::FInterpTo(MoveAlpha, TargetMoveAlpha, DeltaSeconds, 10.f);
	InAirAlpha = FMath::FInterpTo(InAirAlpha, bFalling && bCanMove ? 1.f : 0.f, DeltaSeconds, 8.f);
	SlumpAlpha = FMath::FInterpTo(SlumpAlpha, Bot->IsSlumped() && !bFlyer ? 1.f : 0.f, DeltaSeconds, 5.f);
	BoostAlpha = FMath::FInterpTo(BoostAlpha, Flyer && Flyer->IsBoosting() ? 1.f : 0.f, DeltaSeconds, 5.f);
	ParkedAlpha = FMath::FInterpTo(ParkedAlpha, Flyer && (Flyer->IsParked() || Flyer->IsDormant()) ? 1.f : 0.f, DeltaSeconds, 3.f);
	StunAlpha = Bot->IsStunned() ? 1.f : FMath::FInterpTo(StunAlpha, 0.f, DeltaSeconds, 6.f);
	Recoil = FMath::FInterpTo(Recoil, 0.f, DeltaSeconds, 14.f);
	HitReact = FMath::FInterpTo(HitReact, 0.f, DeltaSeconds, 7.f);
	AimPitch = FMath::FInterpTo(AimPitch, Bot->GetAimPitch(), DeltaSeconds, 18.f);
	CombatAlpha = FMath::FInterpTo(CombatAlpha, Bot->IsInCombatStance() ? 1.f : 0.f, DeltaSeconds, 8.f);
	LandAlpha = FMath::FInterpTo(LandAlpha, 0.f, DeltaSeconds, 7.f);
	// Running freely the robot leans into its stride.
	Lean = FMath::FInterpTo(Lean, 9.f * MoveAlpha * (1.f - CombatAlpha), DeltaSeconds, 6.f);

	const float Stride = FMath::Max(10.f, Rig.StrideLength * Scale);
	GaitPhase = FMath::Fmod(GaitPhase + GroundSpeed * DeltaSeconds / Stride, 1.f);

	FMetalBotAnimProxy& Proxy = GetProxyOnGameThread<FMetalBotAnimProxy>();
	FMetalBotAnimState& State = Proxy.State;
	State.Rig = Rig;
	State.MeshToCharacter = Bot->GetMeshBaseRotation();
	State.MoveDirection = MoveDirection;
	State.MoveAlpha = MoveAlpha;
	State.GaitPhase = GaitPhase;
	State.InAirAlpha = InAirAlpha;
	State.AimPitch = AimPitch;
	State.Recoil = Recoil;
	State.SlumpAlpha = SlumpAlpha;
	State.StunAlpha = StunAlpha;
	State.HitReact = HitReact;
	State.HitDirection = HitDirection;
	State.Time = Time;
	State.CombatAlpha = CombatAlpha;
	State.LandAlpha = LandAlpha;
	State.FlipTuck = Bot->GetFlipTuck();
	State.Lean = Lean;
	State.BoostAlpha = BoostAlpha;
	State.ParkedAlpha = ParkedAlpha;
}

FAnimInstanceProxy* UMetalBotAnimInstance::CreateAnimInstanceProxy()
{
	return new FMetalBotAnimProxy(this);
}

void UMetalBotAnimInstance::DestroyAnimInstanceProxy(FAnimInstanceProxy* InProxy)
{
	delete static_cast<FMetalBotAnimProxy*>(InProxy);
}

bool FMetalBotAnimProxy::Evaluate(FPoseContext& Output)
{
	using namespace MetalBotAnim;

	FCompactPose& Pose = Output.Pose;
	Pose.ResetToRefPose();
	const FBoneContainer& Bones = Pose.GetBoneContainer();
	const FMetalBotAnimState& S = State;
	const FMetalBotRig& Rig = S.Rig;

	// Character-space rotation (and translation for the pelvis) to apply to each bone, accumulated below.
	TMap<int32, FQuat> Rotations;
	FVector PelvisOffset = FVector::ZeroVector;

	auto Find = [&Bones](FName Name) -> int32
	{
		if (Name.IsNone())
		{
			return INDEX_NONE;
		}
		const int32 MeshIndex = Bones.GetPoseBoneIndexForBoneName(Name);
		if (MeshIndex == INDEX_NONE)
		{
			return INDEX_NONE;
		}
		return Bones.MakeCompactPoseIndex(FMeshPoseBoneIndex(MeshIndex)).GetInt();
	};
	auto Add = [&Rotations](int32 Index, const FQuat& Delta)
	{
		if (Index != INDEX_NONE)
		{
			FQuat& Existing = Rotations.FindOrAdd(Index, FQuat::Identity);
			Existing = Delta * Existing;
		}
	};

	const int32 Pelvis = Find(Rig.Pelvis);
	const int32 Head = Find(Rig.Head);
	const int32 ThighL = Find(Rig.ThighL);
	const int32 ThighR = Find(Rig.ThighR);
	const int32 KneeL = Find(Rig.KneeL);
	const int32 KneeR = Find(Rig.KneeR);
	const int32 UpperArmL = Find(Rig.UpperArmL);
	const int32 UpperArmR = Find(Rig.UpperArmR);
	const int32 ForearmL = Find(Rig.ForearmL);
	const int32 ForearmR = Find(Rig.ForearmR);

	const float Cycle = S.GaitPhase * UE_TWO_PI;
	const float Sin = FMath::Sin(Cycle);
	const float Cos = FMath::Cos(Cycle);
	const float Upright = 1.f - S.SlumpAlpha;
	const float Relaxed = (1.f - S.CombatAlpha) * Upright;
	// Robots with built-in or fixed gun arms (ArmSwingDegrees 0) keep them still.
	const float ArmsRelaxed = Rig.ArmSwingDegrees > 0.f ? Relaxed : 0.f;

	// Combat-ready stance on top of the bind pose.
	Add(UpperArmL, FQuat::Slerp(FQuat::Identity, Rig.UpperArmLReady.Quaternion(), Upright));
	Add(UpperArmR, FQuat::Slerp(FQuat::Identity, Rig.UpperArmRReady.Quaternion(), Upright));
	Add(ForearmL, FQuat::Slerp(FQuat::Identity, Rig.ForearmLReady.Quaternion(), Upright));
	Add(ForearmR, FQuat::Slerp(FQuat::Identity, Rig.ForearmRReady.Quaternion(), Upright));

	// Walk cycle: legs swing along the movement direction, knees fold while the leg travels forward.
	const float Swing = Rig.LegSwingDegrees * S.MoveAlpha;
	Add(ThighL, SwingToward(S.MoveDirection, Swing * Sin));
	Add(ThighR, SwingToward(S.MoveDirection, -Swing * Sin));
	Add(KneeL, SwingToward(S.MoveDirection, -Rig.KneeBendDegrees * S.MoveAlpha * FMath::Max(0.f, Cos)));
	Add(KneeR, SwingToward(S.MoveDirection, -Rig.KneeBendDegrees * S.MoveAlpha * FMath::Max(0.f, -Cos)));
	PelvisOffset.Z += Rig.BobHeight * S.MoveAlpha * (FMath::Abs(Sin) - 0.5f);
	Add(Pelvis, FQuat(FVector::ForwardVector, FMath::DegreesToRadians(3.f * S.MoveAlpha * Sin)));

	// Idle "breathing" so the robots never freeze completely.
	const float Idle = (1.f - S.MoveAlpha) * Upright;
	PelvisOffset.Z += Idle * 0.6f * FMath::Sin(S.Time * 2.2f);
	Add(Head, Yaw(Idle * 12.f * FMath::Sin(S.Time * 0.45f)));

	// Free running: lean into the stride (legs counter-rotated so the feet stay under the body),
	// both arms pump against the legs with the elbows bent.
	Add(Pelvis, PitchUp(-S.Lean));
	Add(ThighL, SwingToward(FVector::ForwardVector, S.Lean));
	Add(ThighR, SwingToward(FVector::ForwardVector, S.Lean));
	const float Pump = Rig.ArmSwingDegrees * 1.6f * S.MoveAlpha * ArmsRelaxed;
	Add(UpperArmL, SwingToward(FVector::ForwardVector, -Pump * Sin));
	Add(UpperArmR, SwingToward(FVector::ForwardVector, (Rig.ArmSwingDegrees * S.MoveAlpha * ArmsRelaxed + Pump) * Sin));
	Add(ForearmL, SwingToward(FVector::ForwardVector, 45.f * S.MoveAlpha * ArmsRelaxed));
	Add(ForearmR, SwingToward(FVector::ForwardVector, 45.f * S.MoveAlpha * ArmsRelaxed));

	// Jumping / falling: tuck one leg, extend the other; arms fling out for balance when not shooting.
	Add(ThighL, SwingToward(FVector::ForwardVector, 28.f * S.InAirAlpha));
	Add(ThighR, SwingToward(FVector::ForwardVector, -12.f * S.InAirAlpha));
	Add(KneeL, SwingToward(FVector::ForwardVector, -Rig.KneeBendDegrees * 1.4f * S.InAirAlpha));
	Add(KneeR, SwingToward(FVector::ForwardVector, -Rig.KneeBendDegrees * 0.6f * S.InAirAlpha));
	const float Fling = 50.f * S.InAirAlpha * ArmsRelaxed * (1.f - S.FlipTuck);
	Add(UpperArmL, SwingToward(FVector(0.2f, -1.f, 0.f).GetSafeNormal(), Fling));
	Add(UpperArmR, SwingToward(FVector(0.2f, 1.f, 0.f).GetSafeNormal(), Fling));

	// Double-jump flip: curl into a ball.
	if (S.FlipTuck > KINDA_SMALL_NUMBER)
	{
		Add(ThighL, SwingToward(FVector::ForwardVector, 55.f * S.FlipTuck));
		Add(ThighR, SwingToward(FVector::ForwardVector, 70.f * S.FlipTuck));
		Add(KneeL, SwingToward(FVector::ForwardVector, -FMath::Abs(Rig.KneeBendDegrees) * 1.6f * S.FlipTuck));
		Add(KneeR, SwingToward(FVector::ForwardVector, -FMath::Abs(Rig.KneeBendDegrees) * 2.f * S.FlipTuck));
		Add(UpperArmL, SwingToward(FVector::ForwardVector, 35.f * S.FlipTuck));
		Add(UpperArmR, SwingToward(FVector::ForwardVector, 35.f * S.FlipTuck));
		Add(Head, PitchUp(-25.f * S.FlipTuck));
	}

	// Landing: absorb the impact through the knees.
	if (S.LandAlpha > KINDA_SMALL_NUMBER)
	{
		Add(ThighL, SwingToward(FVector::ForwardVector, 22.f * S.LandAlpha));
		Add(ThighR, SwingToward(FVector::ForwardVector, 22.f * S.LandAlpha));
		Add(KneeL, SwingToward(FVector::ForwardVector, -Rig.KneeBendDegrees * 1.2f * S.LandAlpha));
		Add(KneeR, SwingToward(FVector::ForwardVector, -Rig.KneeBendDegrees * 1.2f * S.LandAlpha));
		PelvisOffset.Z -= 9.f * S.LandAlpha;
	}

	// Aim: the weapon arm follows the camera (or AI) pitch.
	const float Aim = (S.AimPitch * Rig.AimPitchWeight * S.CombatAlpha + 9.f * S.Recoil) * Upright;
	const int32 AimR = Rig.bAimFromShoulder ? UpperArmR : ForearmR;
	const int32 AimL = Rig.bAimFromShoulder ? UpperArmL : ForearmL;
	Add(AimR, PitchUp(Aim));
	if (Rig.bAimWithBothArms)
	{
		Add(AimL, PitchUp(Aim));
	}
	else
	{
		Add(UpperArmL, SwingToward(FVector::ForwardVector, -Rig.ArmSwingDegrees * S.MoveAlpha * Sin));
	}
	Add(Head, PitchUp(S.AimPitch * 0.35f * S.CombatAlpha * Upright));
	PelvisOffset.X -= 1.5f * S.Recoil;

	// Hit reaction: the torso snaps away from the impact.
	if (S.HitReact > KINDA_SMALL_NUMBER)
	{
		const FVector Away = -S.HitDirection;
		Add(Pelvis, FQuat(FVector::CrossProduct(FVector::UpVector, Away).GetSafeNormal(), FMath::DegreesToRadians(10.f * S.HitReact)));
	}

	// Powered down (the player's body while they control another robot, or EMP stun): sag, head down, arms limp.
	if (S.SlumpAlpha > KINDA_SMALL_NUMBER || S.StunAlpha > KINDA_SMALL_NUMBER)
	{
		const float Twitch = S.StunAlpha * 6.f * FMath::Sin(S.Time * 47.f);
		const float Sag = FMath::Max(S.SlumpAlpha, S.StunAlpha * 0.6f);
		Add(Head, PitchUp(-35.f * Sag + Twitch));
		Add(ThighL, SwingToward(FVector::ForwardVector, 25.f * Sag));
		Add(ThighR, SwingToward(FVector::ForwardVector, 25.f * Sag));
		Add(KneeL, SwingToward(FVector::ForwardVector, -45.f * Sag));
		Add(KneeR, SwingToward(FVector::ForwardVector, -45.f * Sag));
		Add(Pelvis, PitchUp(-12.f * Sag) * Yaw(Twitch));
		PelvisOffset.Z -= 6.f * Sag;
	}

	// Flying: the four gun arms open into an X (upper pair up, lower pair down), close up and sweep back
	// under boost, kick back on each shot, and fold back flat while parked. The body bobs as it hovers.
	if (!Rig.FlyerArmUpperL.IsNone())
	{
		// Character space: left arms point along -Y, right arms along +Y. Positive Raise lifts the arm,
		// positive Sweep swings it back.
		auto Raise = [](float Side, float Degrees) { return FQuat(FVector::ForwardVector, FMath::DegreesToRadians(Side * Degrees)); };
		auto Sweep = [](float Side, float Degrees) { return FQuat(FVector::UpVector, FMath::DegreesToRadians(Side * Degrees)); };
		const float Active = 1.f - S.ParkedAlpha;
		const float Spread = Rig.XSpreadDegrees * FMath::Lerp(0.75f, 1.f, S.CombatAlpha) * (1.f - 0.6f * S.BoostAlpha) * Active;
		const float Flex = 3.f * FMath::Sin(S.Time * 1.7f) * Active;
		const float Back = Rig.BoostSweepDegrees * S.BoostAlpha + Rig.ParkedFoldDegrees * S.ParkedAlpha + 4.f * S.Recoil;
		const struct { FName Bone; float Side; float Lift; } Arms[] = {
			{Rig.FlyerArmUpperL, -1.f, 1.f}, {Rig.FlyerArmUpperR, 1.f, 1.f},
			{Rig.FlyerArmLowerL, -1.f, -1.f}, {Rig.FlyerArmLowerR, 1.f, -1.f},
		};
		for (const auto& Arm : Arms)
		{
			Add(Find(Arm.Bone), Sweep(Arm.Side, Back) * Raise(Arm.Side, Arm.Lift * (Spread + Flex)));
		}
		PelvisOffset.Z += 8.f * Active * FMath::Sin(S.Time * 2.1f);
	}

	// Apply the character-space deltas while walking the hierarchy (parents always precede children).
	const FQuat ToComponent = S.MeshToCharacter.Inverse();
	TArray<FTransform> ComponentSpace;
	ComponentSpace.SetNum(Pose.GetNumBones());
	for (FCompactPoseBoneIndex Index : Pose.ForEachBoneIndex())
	{
		const FCompactPoseBoneIndex Parent = Pose.GetParentBoneIndex(Index);
		const bool bHasParent = Parent.GetInt() != INDEX_NONE;
		FTransform Bone = bHasParent ? Pose[Index] * ComponentSpace[Parent.GetInt()] : Pose[Index];

		if (const FQuat* Delta = Rotations.Find(Index.GetInt()))
		{
			const FQuat ComponentDelta = ToComponent * (*Delta) * S.MeshToCharacter;
			Bone.SetRotation((ComponentDelta * Bone.GetRotation()).GetNormalized());
		}
		if (Index.GetInt() == Pelvis)
		{
			Bone.AddToTranslation(ToComponent.RotateVector(PelvisOffset));
		}

		ComponentSpace[Index.GetInt()] = Bone;
		Pose[Index] = bHasParent ? Bone.GetRelativeTransform(ComponentSpace[Parent.GetInt()]) : Bone;
	}
	return true;
}
