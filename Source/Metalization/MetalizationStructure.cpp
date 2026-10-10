#include "MetalizationStructure.h"

#include "Components/StaticMeshComponent.h"
#include "MetalizationGameState.h"
#include "PlanetSurfaceSubsystem.h"

AMetalizationStructure::AMetalizationStructure()
{
	PrimaryActorTick.bCanEverTick = true;
	Tags.Add(TEXT("Selectable"));
	Tags.Add(TEXT("Structure"));

	RootScene = CreateDefaultSubobject<USceneComponent>(TEXT("RootScene"));
	RootComponent = RootScene;

	VisualMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("VisualMesh"));
	VisualMesh->SetupAttachment(RootScene);
	VisualMesh->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	VisualMesh->SetCollisionResponseToAllChannels(ECR_Ignore);
	VisualMesh->SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);
	VisualMesh->SetCanEverAffectNavigation(false);
}

void AMetalizationStructure::BeginPlay()
{
	Super::BeginPlay();
	UpdateSurfaceTransform();
}

void AMetalizationStructure::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	UpdateSurfaceTransform();
}

void AMetalizationStructure::SetSurfaceDetails(AActor* InPlanet, const FVector& InDirection, float InRadius)
{
	PlanetActor = InPlanet;
	SurfaceDirection = InDirection.GetSafeNormal();
	PlanetRadius = FMath::Max(InRadius, 1.f);
	UpdateSurfaceTransform();
}

void AMetalizationStructure::UpdateSurfaceTransform()
{
	if (!PlanetActor)
	{
		return;
	}

	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	UPlanetSurfaceSubsystem* SurfaceSubsystem = World->GetSubsystem<UPlanetSurfaceSubsystem>();
	if (!SurfaceSubsystem)
	{
		return;
	}

	const FVector PlanetCenter = PlanetActor->GetActorLocation();
	const FVector WorldLocation = PlanetCenter + SurfaceDirection * (PlanetRadius + 40.f);
	SetActorLocation(WorldLocation, false);

	const FVector Up = SurfaceSubsystem->DirectionFromWorld(WorldLocation);
	const FVector Forward = (SurfaceDirection - Up * FVector::DotProduct(Up, SurfaceDirection)).GetSafeNormal();
	SetActorRotation(FRotationMatrix::MakeFromXZ(Forward, Up).Rotator());
}

void AMetalizationStructure::ApplyCaptureProgress(float Amount, int32 CapturingTeamId)
{
	if (CapturingTeamId == TeamId)
	{
		return;
	}

	CaptureProgress += Amount;
	if (CaptureProgress >= CaptureProgressRequired)
	{
		TeamId = CapturingTeamId;
		CaptureProgress = 0.f;
	}
}

bool AMetalizationStructure::IsStructureNear(const FVector& WorldLocation, float DistanceThreshold) const
{
	return FVector::DistSquared(GetActorLocation(), WorldLocation) <= DistanceThreshold * DistanceThreshold;
}

AMetalizationEnergyVent::AMetalizationEnergyVent()
{
	Tags.Add(TEXT("Vent"));
	StoredEnergy = 1500.f;
}

void AMetalizationEnergyVent::BeginPlay()
{
	Super::BeginPlay();
	StoredEnergy = FMath::Clamp(StoredEnergy, 0.f, MaxEnergy);
}

float AMetalizationEnergyVent::HarvestEnergy(float Amount)
{
	const float Actual = FMath::Min(Amount, StoredEnergy);
	StoredEnergy -= Actual;
	return Actual;
}

AMetalizationColony::AMetalizationColony()
{
	Tags.Add(TEXT("Colony"));
	StoredEnergy = 200.f;
}

void AMetalizationColony::BeginPlay()
{
	Super::BeginPlay();
	StoredEnergy = FMath::Clamp(StoredEnergy, 0.f, MaxStoredEnergy);
}

void AMetalizationColony::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	const float GeneratedEnergy = ProductionPerSecond * DeltaSeconds;
	StoredEnergy = FMath::Clamp(StoredEnergy + GeneratedEnergy, 0.f, MaxStoredEnergy);

	if (UWorld* World = GetWorld())
	{
		if (AMetalizationGameState* GameState = World->GetGameState<AMetalizationGameState>())
		{
			GameState->RegisterTeam(TeamId, 0.f);
			GameState->AddTeamEnergy(TeamId, GeneratedEnergy);
		}
	}
}

float AMetalizationColony::DepositEnergy(float Amount)
{
	const float Actual = FMath::Min(Amount, MaxStoredEnergy - StoredEnergy);
	StoredEnergy += Actual;
	return Actual;
}
