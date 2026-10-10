#include "MetalizationStructure.h"

#include "Components/StaticMeshComponent.h"
#include "Kismet/GameplayStatics.h"
#include "MetalizationGameState.h"
#include "MetalizationGroundUnit.h"
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
	CurrentHealth = MaxHealth;
	UpdateSurfaceTransform();
}

void AMetalizationStructure::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	UpdateSurfaceTransform();
	HandleStructureDefense(DeltaSeconds);
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
	if (!PlanetActor.IsValid())
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

float AMetalizationStructure::ApplyDamage(float Amount, int32 DamageSourceTeamId)
{
	if (CurrentHealth <= 0.f)
	{
		return 0.f;
	}

	const float ActualDamage = FMath::Min(Amount, CurrentHealth);
	CurrentHealth = FMath::Max(0.f, CurrentHealth - Amount);
	if (CurrentHealth <= 0.f)
	{
		HandleStructureDestruction();
	}
	return ActualDamage;
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
		const int32 OldTeamId = TeamId;
		TeamId = CapturingTeamId;
		CaptureProgress = 0.f;

		if (UWorld* World = GetWorld())
		{
			if (AMetalizationGameState* GameState = World->GetGameState<AMetalizationGameState>())
			{
				GameState->RegisterTeam(CapturingTeamId, 0.f);
				GameState->AddTeamEnergy(CapturingTeamId, 100.f);
				if (OldTeamId != CapturingTeamId)
				{
					GameState->RegisterTeam(OldTeamId, 0.f);
				}
			}
		}

		TArray<AActor*> ColonyActors;
		UGameplayStatics::GetAllActorsOfClass(GetWorld(), AMetalizationColony::StaticClass(), ColonyActors);
		if (ColonyActors.Num() > 0)
		{
			int32 DominantTeamId = -1;
			bool bAllOwnedBySameTeam = true;
			for (AActor* ColonyActor : ColonyActors)
			{
				if (AMetalizationColony* Colony = Cast<AMetalizationColony>(ColonyActor))
				{
					if (DominantTeamId == -1)
					{
						DominantTeamId = Colony->GetTeamId();
					}
					else if (Colony->GetTeamId() != DominantTeamId)
					{
						bAllOwnedBySameTeam = false;
						break;
					}
				}
			}

			if (bAllOwnedBySameTeam && DominantTeamId >= 0 && GetWorld())
			{
				if (AMetalizationGameState* GameState = GetWorld()->GetGameState<AMetalizationGameState>())
				{
					GameState->DeclareWinner(DominantTeamId);
				}
			}
		}
	}
}

void AMetalizationStructure::HandleStructureDefense(float DeltaSeconds)
{
	if (!IsAlive() || DefenseCooldownRemaining > 0.f)
	{
		DefenseCooldownRemaining = FMath::Max(0.f, DefenseCooldownRemaining - DeltaSeconds);
		return;
	}

	TArray<AActor*> FoundUnits;
	UGameplayStatics::GetAllActorsOfClass(GetWorld(), AMetalizationGroundUnit::StaticClass(), FoundUnits);
	for (AActor* Actor : FoundUnits)
	{
		if (AMetalizationGroundUnit* Unit = Cast<AMetalizationGroundUnit>(Actor))
		{
			if (Unit->GetTeamId() == TeamId || !Unit->IsAlive())
			{
				continue;
			}

			if (FVector::DistSquared(GetActorLocation(), Unit->GetActorLocation()) <= DefenseRange * DefenseRange)
			{
				Unit->ApplyDamage(DefenseDamage, TeamId);
				DefenseCooldownRemaining = DefenseCooldown;
				return;
			}
		}
	}
}

void AMetalizationStructure::HandleStructureDestruction()
{
	if (!IsAlive())
	{
		if (AMetalizationGameState* GameState = GetWorld() ? GetWorld()->GetGameState<AMetalizationGameState>() : nullptr)
		{
			GameState->RegisterTeam(TeamId, 0.f);
		}
		Destroy();
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
