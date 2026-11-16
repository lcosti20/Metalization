#include "MetalRobotDispenser.h"

#include "EngineUtils.h"
#include "MetalAudio.h"
#include "MetalBotAIController.h"
#include "MetalBotArchetype.h"
#include "MetalBotCharacter.h"
#include "MetalFX.h"
#include "MetalGate.h"
#include "Components/CapsuleComponent.h"
#include "Components/PointLightComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "Materials/MaterialInstanceDynamic.h"

namespace
{
	const FLinearColor SparkColor(1.f, 0.65f, 0.25f);
	const FLinearColor ArcColor(0.55f, 0.8f, 1.f);
	const FLinearColor WorkingColor(1.f, 0.15f, 0.05f);
	/** Seconds between the "what next?" checks. */
	constexpr float CheckInterval = 0.25f;
	/** A standing robot this close to the pad (2D, cm) blocks the next build. */
	constexpr float PadClearRadius = 110.f;

	/** @return True if the bone name contains Part, ignoring case. */
	bool NameHas(FName Name, const TCHAR* Part)
	{
		return Name.ToString().Contains(Part);
	}
}

AMetalRobotDispenser::AMetalRobotDispenser()
{
	PrimaryActorTick.bCanEverTick = true;
	DispenserMesh = TSoftObjectPtr<UStaticMesh>(FSoftObjectPath(TEXT("/Game/Metalization/Ships/SM_RobotDispenser.SM_RobotDispenser")));

	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	Body = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Body"));
	Body->SetupAttachment(RootComponent);
	Body->SetCollisionProfileName(UCollisionProfile::BlockAll_ProfileName);
	Body->SetCanEverAffectNavigation(true);

	Light = CreateDefaultSubobject<UPointLightComponent>(TEXT("Light"));
	Light->SetupAttachment(Body);
	Light->SetRelativeLocation(FVector(90.f, 0.f, 260.f));
	Light->SetLightColor(WorkingColor);
	Light->SetIntensity(2500.f);
	Light->SetAttenuationRadius(500.f);
	Light->SetCastShadows(false);
}

void AMetalRobotDispenser::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);
	Body->SetStaticMesh(DispenserMesh.LoadSynchronous());
}

void AMetalRobotDispenser::RecordAssignedUnits()
{
	UnitRecords.Reset();
	for (const AMetalBotCharacter* Unit : AssignedUnits)
	{
		FMetalDispenserUnit& Record = UnitRecords.AddDefaulted_GetRef();
		if (Unit)
		{
			Record.Archetype = Unit->Archetype;
			Record.Tags = Unit->Tags;
			Record.Post = Unit->GetActorLocation();
			Record.PatrolRadius = Unit->PatrolRadius;
			Record.bOverrideTeam = Unit->bOverrideTeam;
			Record.TeamOverride = Unit->TeamOverride;
		}
	}
}

void AMetalRobotDispenser::BeginPlay()
{
	Super::BeginPlay();
	Body->SetStaticMesh(DispenserMesh.LoadSynchronous());
	if (const FVector LightAt = SocketLocation(TEXT("light"), FVector::ZeroVector); Body->DoesSocketExist(TEXT("light")))
	{
		Light->SetWorldLocation(LightAt);
	}
	for (int32 Index = 0; Index < Body->GetNumMaterials(); ++Index)
	{
		UMaterialInterface* Material = Body->GetMaterial(Index);
		if (Material && Material->GetName().Contains(TEXT("Glow")))
		{
			UMaterialInstanceDynamic* Dynamic = Body->CreateDynamicMaterialInstance(Index, Material);
			GlowMaterials.Add(Dynamic);
		}
	}

	if (UnitRecords.Num() != AssignedUnits.Num())
	{
		RecordAssignedUnits();
	}
	// A checkpoint restore removes units that were already destroyed before this runs; their slots then
	// start empty and get rebuilt as soon as the dispenser is working.
	Slots.SetNum(UnitRecords.Num());
	for (int32 Index = 0; Index < Slots.Num(); ++Index)
	{
		AMetalBotCharacter* Unit = AssignedUnits.IsValidIndex(Index) ? AssignedUnits[Index].Get() : nullptr;
		Slots[Index].Robot = IsValid(Unit) && !Unit->IsActorBeingDestroyed() ? Unit : nullptr;
	}
	bActive = ActivationGate == nullptr && bStartsActive;
	NextSpawnTime = GetWorld()->GetTimeSeconds() + 1.f;
	UpdateGlow();
}

FVector AMetalRobotDispenser::SocketLocation(FName Socket, const FVector& Fallback) const
{
	return Body->DoesSocketExist(Socket) ? Body->GetSocketLocation(Socket) : GetActorTransform().TransformPosition(Fallback);
}

void AMetalRobotDispenser::UpdateGlow()
{
	const bool bWorking = IsWorking();
	Light->SetIntensity(bDisabled ? 0.f : bWorking ? 2500.f : 600.f);
	for (UMaterialInstanceDynamic* Dynamic : GlowMaterials)
	{
		if (Dynamic)
		{
			// The glowing strips use M_MZ_Light (unlit; Color x Intensity, default 2).
			Dynamic->SetScalarParameterValue(TEXT("Intensity"), bDisabled ? 0.03f : bWorking ? 2.5f : 0.6f);
		}
	}
}

void AMetalRobotDispenser::Disable(bool bQuiet)
{
	if (bDisabled)
	{
		return;
	}
	bDisabled = true;
	if (AMetalBotCharacter* Robot = Building.Get())
	{
		// The half-built robot falls apart on the pad.
		if (!bQuiet)
		{
			const FVector At = Robot->GetActorLocation();
			AMetalFX::SpawnBurst(GetWorld(), At, SparkColor, 90.f, 0.4f);
			Weld(At, 10);
			MetalAudio::PlayAt(this, TEXT("robot_explode"), At, 0.7f);
		}
		Robot->Destroy();
	}
	Building = nullptr;
	if (!bQuiet)
	{
		const FVector Nozzle = SocketLocation(TEXT("nozzle"), FVector(15.f, 0.f, 260.f));
		AMetalFX::SpawnBurst(GetWorld(), Nozzle, WorkingColor, 120.f, 0.5f);
		MetalAudio::PlayAt(this, TEXT("dispenser_shutdown"), Nozzle, 1.f, 0.f);
	}
	UpdateGlow();
	UE_LOG(LogMetal, Display, TEXT("[Dispenser] %s disabled"), *GetName());
}

bool AMetalRobotDispenser::IsPadClear() const
{
	const FVector Pad = SocketLocation(TEXT("assembly"), FVector(25.f, 0.f, 30.f));
	for (TActorIterator<AMetalBotCharacter> It(GetWorld()); It; ++It)
	{
		if (It->IsAlive() && *It != Building.Get() && FVector::DistSquared2D(It->GetActorLocation(), Pad) < FMath::Square(PadClearRadius + It->GetCapsuleComponent()->GetScaledCapsuleRadius())
			&& FMath::Abs(It->GetActorLocation().Z - Pad.Z) < 300.f)
		{
			return false;
		}
	}
	return true;
}

UMetalBotArchetype* AMetalRobotDispenser::PickChoice() const
{
	float Total = 0.f;
	for (const FMetalDispenserChoice& Choice : SpawnChoices)
	{
		Total += Choice.Archetype ? FMath::Max(0.f, Choice.Weight) : 0.f;
	}
	float Roll = FMath::FRandRange(0.f, Total);
	for (const FMetalDispenserChoice& Choice : SpawnChoices)
	{
		if (Choice.Archetype && Choice.Weight > 0.f)
		{
			Roll -= Choice.Weight;
			if (Roll <= 0.f)
			{
				return Choice.Archetype;
			}
		}
	}
	return nullptr;
}

void AMetalRobotDispenser::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	const float Now = GetWorld()->GetTimeSeconds();
	if (bDisabled)
	{
		return;
	}
	if (Building.IsValid())
	{
		TickBuild(Now);
	}
	else if (BuildingSlot != INDEX_NONE || !Parts.IsEmpty())
	{
		// The robot on the pad vanished (killed by a fall, a level reset...): forget it.
		Parts.Reset();
		BuildingSlot = INDEX_NONE;
	}
	if (Now < NextCheckTime)
	{
		return;
	}
	NextCheckTime = Now + CheckInterval;

	if (!bActive)
	{
		const AMetalGate* Gate = Cast<AMetalGate>(ActivationGate);
		if (Gate && Gate->IsOpen())
		{
			bActive = true;
			NextSpawnTime = Now + 0.5f;
			UE_LOG(LogMetal, Display, TEXT("[Dispenser] %s powered on by %s"), *GetName(), *Gate->GetName());
			UpdateGlow();
			MetalAudio::PlayAt(this, TEXT("dispenser_hammer"), GetActorLocation(), 0.8f);
		}
		return;
	}

	if (Mode == EMetalDispenserMode::Respawn)
	{
		int32 Ready = INDEX_NONE;
		for (int32 Index = 0; Index < Slots.Num(); ++Index)
		{
			FSlot& Slot = Slots[Index];
			if (Slot.Robot.IsValid() && Slot.Robot->IsAlive())
			{
				Slot.RebuildAt = -1.f;
				continue;
			}
			if (Slot.RebuildAt < 0.f)
			{
				Slot.RebuildAt = Now + RespawnDelay;
			}
			else if (Now >= Slot.RebuildAt && Ready == INDEX_NONE)
			{
				Ready = Index;
			}
		}
		if (Ready != INDEX_NONE && !Building.IsValid() && IsPadClear())
		{
			StartBuild(UnitRecords[Ready].Archetype, Ready);
		}
	}
	else
	{
		Spawned.RemoveAll([](const TWeakObjectPtr<AMetalBotCharacter>& Robot) { return !Robot.IsValid() || !Robot->IsAlive(); });
		if (Now >= NextSpawnTime && !Building.IsValid() && Spawned.Num() < MaxAlive && IsPadClear())
		{
			if (StartBuild(PickChoice(), INDEX_NONE))
			{
				NextSpawnTime = Now + SpawnInterval;
			}
		}
	}
}

bool AMetalRobotDispenser::StartBuild(UMetalBotArchetype* Archetype, int32 Slot)
{
	if (Archetype == nullptr)
	{
		return false;
	}
	const FVector Pad = SocketLocation(TEXT("assembly"), FVector(25.f, 0.f, 30.f));
	const FVector Location = Pad + FVector(0.f, 0.f, Archetype->CapsuleHalfHeight + 3.f);
	const FRotator Facing(0.f, GetActorRotation().Yaw, 0.f);
	AMetalBotCharacter* Robot = SpawnRobot(Archetype, Slot, FTransform(Facing, Location), true);
	if (Robot == nullptr)
	{
		return false;
	}

	// Nothing shows until the first part is welded on. The body appears part by part through hidden bones;
	// the gun and the extra parts (all following the body's pose) wait for the end.
	HiddenComponents.Reset();
	TArray<UPrimitiveComponent*> Components;
	Robot->GetComponents<UPrimitiveComponent>(Components);
	for (UPrimitiveComponent* Component : Components)
	{
		if (Component != Robot->GetCapsuleComponent() && !Component->bHiddenInGame)
		{
			Component->SetHiddenInGame(true);
			HiddenComponents.Add(Component);
		}
	}

	// Sort the bones hanging off the pelvis into limbs: legs come with the torso, then each arm, then the
	// head with anything else (wings, antennae).
	USkeletalMeshComponent* Mesh = Robot->GetMesh();
	const FName Pelvis = Mesh->GetBoneIndex(TEXT("bip_pelvis")) != INDEX_NONE ? FName(TEXT("bip_pelvis")) : Mesh->GetBoneName(0);
	FBuildPart Torso, ArmL, ArmR, Head, Final;
	Torso.WeldBone = Pelvis;
	for (int32 Bone = 0; Bone < Mesh->GetNumBones(); ++Bone)
	{
		const FName Name = Mesh->GetBoneName(Bone);
		if (Mesh->GetParentBone(Name) != Pelvis)
		{
			continue;
		}
		const bool bArm = NameHas(Name, TEXT("arm")) || NameHas(Name, TEXT("shoulder"));
		if (NameHas(Name, TEXT("hip")) || NameHas(Name, TEXT("leg")) || NameHas(Name, TEXT("thigh")))
		{
			continue;
		}
		FBuildPart& Part = bArm && Name.ToString().EndsWith(TEXT("_L")) ? ArmL : bArm ? ArmR : Head;
		Part.Bones.Add(Name);
		if (Part.WeldBone.IsNone() || NameHas(Name, TEXT("head")))
		{
			Part.WeldBone = Name;
		}
	}
	Final.bFinal = true;
	Final.WeldBone = Mesh->GetBoneIndex(TEXT("weapon_bone")) != INDEX_NONE ? FName(TEXT("weapon_bone")) : Pelvis;
	Parts.Reset();
	for (FBuildPart* Part : {&Torso, &ArmL, &ArmR, &Head, &Final})
	{
		if (Part == &Torso || Part->bFinal || !Part->Bones.IsEmpty())
		{
			Parts.Add(*Part);
		}
	}
	for (const FBuildPart& Part : Parts)
	{
		for (const FName& Bone : Part.Bones)
		{
			Mesh->HideBoneByName(Bone, PBO_None);
		}
	}

	Building = Robot;
	BuildingSlot = Slot;
	BuildStartTime = GetWorld()->GetTimeSeconds();
	UE_LOG(LogMetal, Display, TEXT("[Dispenser] %s building %s (%d parts)"), *GetName(), *Archetype->ArchetypeId.ToString(), Parts.Num());
	NextPart = 0;
	DrilledPart = INDEX_NONE;
	NextSparkTime = 0.f;
	return true;
}

AMetalBotCharacter* AMetalRobotDispenser::SpawnRobot(UMetalBotArchetype* Archetype, int32 Slot, const FTransform& Transform, bool bAssembling)
{
	FActorSpawnParameters Params;
	Params.OverrideLevel = GetLevel();
	Params.SpawnCollisionHandlingOverride = bAssembling ? ESpawnActorCollisionHandlingMethod::AlwaysSpawn
		: ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn;
	Params.bDeferConstruction = true;
	AMetalBotCharacter* Robot = GetWorld()->SpawnActor<AMetalBotCharacter>(AMetalBotCharacter::StaticClass(), Transform, Params);
	if (Robot == nullptr)
	{
		return nullptr;
	}
	Robot->Archetype = Archetype;
	if (UnitRecords.IsValidIndex(Slot))
	{
		const FMetalDispenserUnit& Record = UnitRecords[Slot];
		Robot->Tags = Record.Tags;
		Robot->PatrolRadius = Record.PatrolRadius;
		Robot->bOverrideTeam = Record.bOverrideTeam;
		Robot->TeamOverride = Record.TeamOverride;
	}
	else
	{
		Robot->Tags = SpawnTags;
		Robot->PatrolRadius = SpawnPatrolRadius;
	}
	if (bAssembling)
	{
		Robot->SetAssembling(true);
	}
	Robot->FinishSpawning(Transform);
	if (Slots.IsValidIndex(Slot))
	{
		Slots[Slot].Robot = Robot;
		Slots[Slot].RebuildAt = -1.f;
	}
	else
	{
		Spawned.Add(Robot);
	}
	return Robot;
}

FVector AMetalRobotDispenser::PostFor(int32 Slot) const
{
	return UnitRecords.IsValidIndex(Slot) ? UnitRecords[Slot].Post : GetActorLocation() + GetActorForwardVector() * LaunchDistance;
}

void AMetalRobotDispenser::ForEachBuiltRobot(TFunctionRef<void(AMetalBotCharacter* Robot, int32 Slot)> Visit) const
{
	// A robot still on the pad isn't saved: the dispenser simply builds it again after the retry.
	auto Standing = [this](const AMetalBotCharacter* Robot)
	{
		return Robot && Robot->IsAlive() && !Robot->IsActorBeingDestroyed() && Robot != Building.Get() && !Robot->IsAssembling();
	};
	for (int32 Index = 0; Index < Slots.Num(); ++Index)
	{
		AMetalBotCharacter* Robot = Slots[Index].Robot.Get();
		const bool bOriginal = AssignedUnits.IsValidIndex(Index) && AssignedUnits[Index].Get() == Robot;
		if (Standing(Robot) && !bOriginal)
		{
			Visit(Robot, Index);
		}
	}
	for (const TWeakObjectPtr<AMetalBotCharacter>& Weak : Spawned)
	{
		if (AMetalBotCharacter* Robot = Weak.Get(); Standing(Robot))
		{
			Visit(Robot, INDEX_NONE);
		}
	}
}

AMetalBotCharacter* AMetalRobotDispenser::RestoreBuiltRobot(UMetalBotArchetype* Archetype, int32 Slot, const FTransform& Transform)
{
	if (Archetype == nullptr || (Slot != INDEX_NONE && !Slots.IsValidIndex(Slot)))
	{
		return nullptr;
	}
	AMetalBotCharacter* Robot = SpawnRobot(Archetype, Slot, Transform, false);
	if (Robot)
	{
		if (AMetalBotAIController* AI = Cast<AMetalBotAIController>(Robot->GetController()))
		{
			AI->SetHome(PostFor(Slot));
		}
	}
	return Robot;
}

void AMetalRobotDispenser::TickBuild(float Now)
{
	AMetalBotCharacter* Robot = Building.Get();
	USkeletalMeshComponent* Mesh = Robot->GetMesh();
	const float PartTime = AssemblyTime / FMath::Max(1, Parts.Num());
	const float Elapsed = Now - BuildStartTime;

	// Each part is drilled for PartTime, then hammered on.
	const int32 Working = FMath::Min(Parts.Num() - 1, FMath::FloorToInt(Elapsed / PartTime));
	if (Working > DrilledPart && NextPart < Parts.Num())
	{
		DrilledPart = Working;
		MetalAudio::PlayAt(this, TEXT("dispenser_drill"), Robot->GetActorLocation(), 0.75f, 0.08f);
	}
	if (NextPart < Parts.Num())
	{
		const FBuildPart& Part = Parts[NextPart];
		const FVector WeldAt = Mesh->GetBoneLocation(Part.WeldBone);
		if (Now >= NextSparkTime)
		{
			NextSparkTime = Now + 0.07f;
			Weld(WeldAt, 4);
		}
		if (Elapsed >= PartTime * (NextPart + 1))
		{
			if (NextPart == 0)
			{
				Mesh->SetHiddenInGame(false);
				HiddenComponents.Remove(Mesh);
			}
			for (const FName& Bone : Part.Bones)
			{
				Mesh->UnHideBoneByName(Bone);
			}
			AMetalFX::SpawnBurst(GetWorld(), WeldAt, SparkColor, 28.f, 0.18f);
			Weld(WeldAt, 6);
			MetalAudio::PlayAt(this, TEXT("dispenser_hammer"), WeldAt, 0.9f, 0.1f);
			++NextPart;
			if (Part.bFinal || NextPart >= Parts.Num())
			{
				FinishBuild();
			}
		}
	}
}

void AMetalRobotDispenser::FinishBuild()
{
	AMetalBotCharacter* Robot = Building.Get();
	Building = nullptr;
	Parts.Reset();
	for (const TWeakObjectPtr<UPrimitiveComponent>& Component : HiddenComponents)
	{
		if (Component.IsValid())
		{
			Component->SetHiddenInGame(false);
		}
	}
	HiddenComponents.Reset();
	const int32 Slot = BuildingSlot;
	BuildingSlot = INDEX_NONE;
	if (Robot == nullptr)
	{
		return;
	}
	Robot->SetAssembling(false);
	Robot->Speak(EMetalBark::Chatter);
	// Off the pad and on to its post: the original's for a rebuilt unit, a spot in front of the machine otherwise.
	const FVector Post = PostFor(Slot);
	if (AMetalBotAIController* AI = Cast<AMetalBotAIController>(Robot->GetController()))
	{
		AI->SetHome(Post);
		AI->MoveToLocation(Post, 80.f);
	}
	UE_LOG(LogMetal, Display, TEXT("[Dispenser] %s built %s (%s) in %.2f s"), *GetName(), *Robot->GetName(),
		*Robot->GetArchetypeId().ToString(), GetWorld()->GetTimeSeconds() - BuildStartTime);
}

void AMetalRobotDispenser::Weld(const FVector& At, int32 Sparks)
{
	UWorld* World = GetWorld();
	for (int32 Index = 0; Index < Sparks; ++Index)
	{
		// Short bright streaks flying out and mostly down, like sparks off a weld.
		FVector Direction = FMath::VRand();
		Direction.Z = Direction.Z * 0.6f - 0.35f;
		const FVector From = At + FMath::VRand() * 6.f;
		AMetalFX::SpawnBeam(World, From, From + Direction.GetSafeNormal() * FMath::FRandRange(25.f, 70.f), SparkColor, 2.5f, FMath::FRandRange(0.08f, 0.16f));
	}
	// The hot spot itself.
	AMetalFX::SpawnBurst(World, At, FLinearColor(1.f, 0.85f, 0.6f), 9.f, 0.1f);
	// Welding arcs from the nozzle overhead and from one of the torch arms.
	AMetalFX::SpawnBeam(World, SocketLocation(TEXT("nozzle"), FVector(15.f, 0.f, 260.f)), At, ArcColor, 2.f, 0.06f);
	const FName Torch = FMath::RandBool() ? FName(TEXT("torch_L")) : FName(TEXT("torch_R"));
	if (Body->DoesSocketExist(Torch))
	{
		AMetalFX::SpawnBeam(World, Body->GetSocketLocation(Torch), At, ArcColor, 1.5f, 0.05f);
	}
}
