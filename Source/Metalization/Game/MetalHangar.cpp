#include "MetalHangar.h"

#include "MetalBotArchetype.h"
#include "MetalCapitalShip.h"
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
