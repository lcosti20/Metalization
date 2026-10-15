#include "MetalGate.h"

#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "UObject/ConstructorHelpers.h"

AMetalGate::AMetalGate()
{
	PrimaryActorTick.bCanEverTick = true;
	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));

	static ConstructorHelpers::FObjectFinder<UStaticMesh> Cube(TEXT("/Engine/BasicShapes/Cube.Cube"));
	Door = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Door"));
	Door->SetupAttachment(RootComponent);
	Door->SetStaticMesh(Cube.Object);
	// 6m wide, 4.5m tall, 40cm thick slab standing on the actor's origin.
	Door->SetRelativeScale3D(FVector(0.4f, 6.f, 4.5f));
	Door->SetRelativeLocation(FVector(0.f, 0.f, 225.f));
	Door->SetMobility(EComponentMobility::Movable);
}

void AMetalGate::BeginPlay()
{
	Super::BeginPlay();
	ClosedLocation = Door->GetRelativeLocation();
}

void AMetalGate::Open()
{
	bOpening = true;
}

void AMetalGate::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (bOpening && OpenAlpha < 1.f)
	{
		OpenAlpha = FMath::Min(1.f, OpenAlpha + DeltaSeconds / FMath::Max(0.1f, OpenTime));
		Door->SetRelativeLocation(ClosedLocation + OpenOffset * FMath::InterpEaseInOut(0.f, 1.f, OpenAlpha, 2.f));
	}
}
