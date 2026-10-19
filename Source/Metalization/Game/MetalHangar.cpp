#include "MetalHangar.h"

#include "MetalBotArchetype.h"
#include "MetalCapitalShip.h"
#include "MetalFlyerAIController.h"
#include "MetalStalker.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Engine/DamageEvents.h"
#include "Engine/StaticMesh.h"
#include "UObject/ConstructorHelpers.h"
#include "Engine/StaticMeshSocket.h"
#include "Engine/World.h"

namespace
{
	constexpr int32 MaxPads = 16;
	/** Seconds between checks for destroyed robots. */
	constexpr float CheckInterval = 0.5f;
}

AMetalHangar::AMetalHangar()
{
	PrimaryActorTick.bCanEverTick = true;
	RobotClass = AMetalStalker::StaticClass();

	Shell = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Shell"));
	Shell->SetCollisionProfileName(UCollisionProfile::BlockAll_ProfileName);
	RootComponent = Shell;

	PadMeshes = CreateDefaultSubobject<UInstancedStaticMeshComponent>(TEXT("Pads"));
	PadMeshes->SetupAttachment(Shell);
	PadMeshes->SetCollisionProfileName(UCollisionProfile::BlockAll_ProfileName);

	LifeSupport = CreateDefaultSubobject<UMetalShipSubsystemComponent>(TEXT("LifeSupport"));
	LifeSupport->SetupAttachment(Shell);
	LifeSupport->Type = EMetalShipSubsystem::LifeSupport;
	LifeSupport->SetStaticMesh(ConstructorHelpers::FObjectFinder<UStaticMesh>(TEXT("/Engine/BasicShapes/Cylinder.Cylinder")).Object);
	LifeSupport->SetCollisionProfileName(UCollisionProfile::BlockAll_ProfileName);
}

void AMetalHangar::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);
	BuildPads();
}

void AMetalHangar::BuildPads()
{
	Shell->SetStaticMesh(ShellMesh);
	PadMeshes->SetStaticMesh(PadMesh);
	Shell->SetCullDistance(InteriorDrawDistance);
	PadMeshes->SetCullDistance(InteriorDrawDistance);
	// The engine cylinder is 1 m across and tall, centred on its pivot.
	LifeSupport->SetRelativeLocation(LifeSupportLocation + FVector(0.f, 0.f, LifeSupportSize.Y * 0.5f));
	LifeSupport->SetRelativeScale3D(FVector(LifeSupportSize.X / 100.f, LifeSupportSize.X / 100.f, LifeSupportSize.Y / 100.f));
	LifeSupport->SetCullDistance(InteriorDrawDistance);
	if (LifeSupportMaterial)
	{
		LifeSupport->SetMaterial(0, LifeSupportMaterial);
	}
	PadMeshes->ClearInstances();
	Pads.Reset();
	for (int32 Index = 1; ShellMesh && Index <= MaxPads; ++Index)
	{
		const UStaticMeshSocket* Socket = ShellMesh->FindSocket(*FString::Printf(TEXT("pad_%d"), Index));
		if (Socket == nullptr)
		{
			break;
		}
		FPad& Pad = Pads.AddDefaulted_GetRef();
		Pad.Transform = FTransform(Socket->RelativeRotation, Socket->RelativeLocation);
		PadMeshes->AddInstance(Pad.Transform);
	}
}

void AMetalHangar::BeginPlay()
{
	Super::BeginPlay();
	BuildPads();
	// Opened on its own (testing the interior map), nobody will call InitForShip.
	if (GetLevel() == GetWorld()->PersistentLevel)
	{
		InitForShip(nullptr);
	}
}

void AMetalHangar::InitForShip(AMetalCapitalShip* Ship)
{
	OwningShip = Ship;
	if (Ship)
	{
		Team = Ship->GetTeam();
		if (const UMetalCapitalShipData* Data = Ship->ShipData)
		{
			RobotArchetype = Data->HangarRobot ? Data->HangarRobot.Get() : RobotArchetype.Get();
			RespawnDelay = Data->HangarRespawnDelay;
			Ship->RegisterSubsystem(LifeSupport, Data->LifeSupportHealth);
		}
	}
	bActive = true;
	for (FPad& Pad : Pads)
	{
		SpawnRobot(Pad);
	}
	// Pilots launch a couple of seconds in, staggered, so they don't all leave as one clump.
	PilotSlots.SetNum(Ship ? FMath::Max(0, Ship->AIPilots) : 0);
	for (int32 Slot = 0; Slot < PilotSlots.Num(); ++Slot)
	{
		PilotSlots[Slot].RespawnAt = GetWorld()->GetTimeSeconds() + 2.f + Slot * 1.5f;
	}
	UE_LOG(LogMetal, Display, TEXT("%s: team %d, %d robots parked at %s"), *GetName(), static_cast<int32>(Team), Pads.Num(), *GetActorLocation().ToCompactString());
}

float AMetalHangar::TakeDamage(float DamageAmount, FDamageEvent const& DamageEvent, AController* EventInstigator, AActor* DamageCauser)
{
	// Only the core can be hurt; it belongs to the ship, which applies the damage.
	AMetalCapitalShip* Ship = OwningShip.Get();
	if (Ship == nullptr || !DamageEvent.IsOfType(FPointDamageEvent::ClassID))
	{
		return 0.f;
	}
	if (static_cast<const FPointDamageEvent&>(DamageEvent).HitInfo.GetComponent() != LifeSupport)
	{
		return 0.f;
	}
	return Ship->ApplySubsystemDamage(LifeSupport, DamageAmount, EventInstigator, DamageCauser);
}

FTransform AMetalHangar::GetMouthTransform() const
{
	return Shell->DoesSocketExist(TEXT("hangar_portal")) ? Shell->GetSocketTransform(TEXT("hangar_portal")) : GetActorTransform();
}

int32 AMetalHangar::GetActivePilots() const
{
	int32 Count = 0;
	for (const FPilotSlot& Slot : PilotSlots)
	{
		Count += Slot.Robot.IsValid() && Slot.Robot->IsAlive() ? 1 : 0;
	}
	return Count;
}

void AMetalHangar::LaunchPilot(int32 Slot)
{
	FPilotSlot& Pilot = PilotSlots[Slot];
	Pilot.RespawnAt = -1.f;
	if (RobotArchetype == nullptr || RobotClass == nullptr || Pads.Num() == 0)
	{
		return;
	}
	// Spawn hovering 15 m above a pad (clear of the parked robot), already flying.
	const FTransform World = Pads[Slot % Pads.Num()].Transform * GetActorTransform();
	const FVector Location = World.GetLocation() + FVector(0.f, 0.f, 1500.f + 400.f * (Slot / Pads.Num()));
	const FRotator Facing(0.f, World.Rotator().Yaw, 0.f);
	FActorSpawnParameters Params;
	Params.OverrideLevel = GetLevel();
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn;
	Params.bDeferConstruction = true;
	AMetalBotCharacter* Robot = GetWorld()->SpawnActor<AMetalBotCharacter>(RobotClass, Location, Facing, Params);
	if (Robot == nullptr)
	{
		return;
	}
	Robot->Archetype = RobotArchetype;
	Robot->bOverrideTeam = true;
	Robot->TeamOverride = Team;
	if (AMetalStalker* Stalker = Cast<AMetalStalker>(Robot))
	{
		Stalker->bStartParked = false;
	}
	Robot->FinishSpawning(FTransform(Facing, Location));
	Robot->AIControllerClass = AMetalFlyerAIController::StaticClass();
	Robot->SpawnDefaultController();
	if (AMetalFlyerAIController* Brain = Cast<AMetalFlyerAIController>(Robot->GetController()))
	{
		// Alternate jobs: odd slots raid the enemy ship, even slots fly cover.
		Brain->PilotRole = Slot % 2 == 1 ? EMetalPilotRole::Raider : EMetalPilotRole::Fighter;
	}
	Pilot.Robot = Robot;
}

AMetalBotCharacter* AMetalHangar::GetParkedRobot(int32 PadIndex) const
{
	return Pads.IsValidIndex(PadIndex) ? Pads[PadIndex].Robot.Get() : nullptr;
}

void AMetalHangar::SpawnRobot(FPad& Pad)
{
	Pad.RespawnAt = -1.f;
	if (RobotArchetype == nullptr || RobotClass == nullptr)
	{
		return;
	}
	// Stand on the pad: its top plus the robot's capsule.
	const float PadTop = PadMesh ? PadMesh->GetBoundingBox().Max.Z : 0.f;
	const FTransform World = Pad.Transform * GetActorTransform();
	const FVector Location = World.GetLocation() + FVector(0.f, 0.f, PadTop + RobotArchetype->CapsuleHalfHeight + 5.f);
	const FRotator Facing(0.f, World.Rotator().Yaw, 0.f);

	FActorSpawnParameters Params;
	Params.OverrideLevel = GetLevel();
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn;
	Params.bDeferConstruction = true;
	AMetalBotCharacter* Robot = GetWorld()->SpawnActor<AMetalBotCharacter>(RobotClass, Location, Facing, Params);
	if (Robot == nullptr)
	{
		return;
	}
	Robot->Archetype = RobotArchetype;
	Robot->bOverrideTeam = true;
	Robot->TeamOverride = Team;
	Robot->PatrolRadius = 0.f;
	if (AMetalStalker* Stalker = Cast<AMetalStalker>(Robot))
	{
		Stalker->bStartParked = true;
	}
	Robot->FinishSpawning(FTransform(Facing, Location));
	Pad.Robot = Robot;
}

void AMetalHangar::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	const float Now = GetWorld()->GetTimeSeconds();
	if (!bActive || Now < NextCheckTime)
	{
		return;
	}
	NextCheckTime = Now + CheckInterval;
	for (int32 Slot = 0; Slot < PilotSlots.Num(); ++Slot)
	{
		FPilotSlot& Pilot = PilotSlots[Slot];
		if (Pilot.Robot.IsValid() && Pilot.Robot->IsAlive())
		{
			continue;
		}
		if (Pilot.RespawnAt < 0.f)
		{
			Pilot.RespawnAt = Now + RespawnDelay;
		}
		else if (Now >= Pilot.RespawnAt)
		{
			LaunchPilot(Slot);
		}
	}
	for (FPad& Pad : Pads)
	{
		const AMetalBotCharacter* Robot = Pad.Robot.Get();
		if (Robot && Robot->IsAlive())
		{
			continue;
		}
		if (Pad.RespawnAt < 0.f)
		{
			Pad.RespawnAt = Now + RespawnDelay;
		}
		else if (Now >= Pad.RespawnAt)
		{
			SpawnRobot(Pad);
		}
	}
}
