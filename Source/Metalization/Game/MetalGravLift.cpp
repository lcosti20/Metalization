#include "MetalGravLift.h"

#include "MetalAudio.h"
#include "MetalBotCharacter.h"
#include "MetalFX.h"
#include "MetalStalker.h"
#include "Components/AudioComponent.h"
#include "Components/BoxComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/PointLightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "UObject/ConstructorHelpers.h"

namespace
{
	constexpr int32 RingCount = 4;
	/** The engine cylinder is 100 cm across and 100 cm tall, centred on its pivot. */
	constexpr float CylinderSize = 100.f;
}

AMetalGravLift::AMetalGravLift()
{
	PrimaryActorTick.bCanEverTick = true;

	// The root is the centre of the floor pad; the trigger box is raised to span the column.
	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	Trigger = CreateDefaultSubobject<UBoxComponent>(TEXT("Trigger"));
	Trigger->SetupAttachment(RootComponent);
	Trigger->SetCollisionProfileName(TEXT("Trigger"));
	Trigger->SetGenerateOverlapEvents(true);

	static ConstructorHelpers::FObjectFinder<UStaticMesh> Cylinder(TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
	auto MakeMesh = [this](const TCHAR* Name) {
		UStaticMeshComponent* Mesh = CreateDefaultSubobject<UStaticMeshComponent>(Name);
		Mesh->SetupAttachment(RootComponent);
		Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Mesh->SetCastShadow(false);
		return Mesh;
	};
	Pad = MakeMesh(TEXT("Pad"));
	Beam = MakeMesh(TEXT("Beam"));
	for (int32 Index = 0; Index < RingCount; ++Index)
	{
		Rings.Add(MakeMesh(*FString::Printf(TEXT("Ring%d"), Index)));
	}
	if (Cylinder.Succeeded())
	{
		Pad->SetStaticMesh(Cylinder.Object);
		Beam->SetStaticMesh(Cylinder.Object);
		for (UStaticMeshComponent* Ring : Rings)
		{
			Ring->SetStaticMesh(Cylinder.Object);
		}
	}

	UPointLightComponent* Light = CreateDefaultSubobject<UPointLightComponent>(TEXT("Light"));
	Light->SetupAttachment(RootComponent);
	Light->SetIntensity(6000.f);
	Light->SetAttenuationRadius(900.f);
	Light->SetCastShadows(false);
	Light->SetLightColor(FLinearColor(0.25f, 0.85f, 1.f));
}

void AMetalGravLift::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);
	// Everything is relative to the floor pad at the origin. The box reaches a little below the floor and
	// above the top so riders are released inside it.
	const float Half = LiftHeight * 0.5f;
	Trigger->SetRelativeLocation(FVector(0.f, 0.f, Half));
	Trigger->SetBoxExtent(FVector(Radius, Radius, Half + 40.f));
	const float PadZ = 0.f;
	const float Diameter = Radius * 2.f / CylinderSize;
	Pad->SetRelativeLocation(FVector(0.f, 0.f, PadZ + 4.f));
	Pad->SetRelativeScale3D(FVector(Diameter * 1.15f, Diameter * 1.15f, 0.08f));
	Beam->SetRelativeLocation(FVector(0.f, 0.f, PadZ + Half));
	Beam->SetRelativeScale3D(FVector(Diameter * 0.8f, Diameter * 0.8f, LiftHeight / CylinderSize));
	for (UStaticMeshComponent* Ring : Rings)
	{
		Ring->SetRelativeScale3D(FVector(Diameter, Diameter, 0.04f));
	}
	if (UPointLightComponent* Light = FindComponentByClass<UPointLightComponent>())
	{
		Light->SetRelativeLocation(FVector(0.f, 0.f, PadZ + 60.f));
		Light->SetLightColor(Color);
	}
}

void AMetalGravLift::BeginPlay()
{
	Super::BeginPlay();
	Trigger->OnComponentEndOverlap.AddDynamic(this, &AMetalGravLift::OnEndOverlap);
	if (UMaterialInterface* Glow = MetalAssets::GetGlowMaterial())
	{
		auto Paint = [this, Glow](UStaticMeshComponent* Mesh, float Intensity) {
			UMaterialInstanceDynamic* MID = UMaterialInstanceDynamic::Create(Glow, this);
			MID->SetVectorParameterValue(TEXT("Color"), Color);
			MID->SetScalarParameterValue(TEXT("Intensity"), Intensity);
			Mesh->SetMaterial(0, MID);
			Materials.Add(MID);
		};
		Paint(Pad, 4.f);
		Paint(Beam, 0.12f);
		for (UStaticMeshComponent* Ring : Rings)
		{
			Paint(Ring, 1.5f);
		}
	}
	Hum = MetalAudio::PlayLoop(Pad, TEXT("lift_hum_loop"), 0.45f);
}

bool AMetalGravLift::IsCarrying(const AActor* Actor) const
{
	return Riders.Contains(TWeakObjectPtr<AMetalBotCharacter>(const_cast<AMetalBotCharacter*>(Cast<AMetalBotCharacter>(Actor))));
}

void AMetalGravLift::OnEndOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor, UPrimitiveComponent* OtherComp, int32 OtherBodyIndex)
{
	if (AMetalBotCharacter* Bot = Cast<AMetalBotCharacter>(OtherActor); Bot && Riders.Contains(Bot))
	{
		Release(Bot, false);
	}
}

void AMetalGravLift::Release(AMetalBotCharacter* Bot, bool bAtTop)
{
	Riders.Remove(Bot);
	UCharacterMovementComponent* Move = Bot->GetCharacterMovement();
	if (Move->MovementMode == MOVE_Flying)
	{
		Move->SetMovementMode(MOVE_Falling);
	}
	Move->MaxFlySpeed = 600.f;
	if (bAtTop)
	{
		// Toss the rider onto the landing and give back a mid-air jump for anything just out of reach.
		const FVector Out = GetActorRotation().RotateVector(ExitDirection).GetSafeNormal2D();
		Move->Velocity = Out * ExitSpeed + FVector(0.f, 0.f, 320.f);
		Bot->JumpCurrentCount = 0;
		MetalAudio::PlayAt(this, TEXT("lift_exit"), Bot->GetActorLocation(), 0.7f);
	}
}

void AMetalGravLift::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	const FVector Center = GetActorLocation();
	const float TopZ = Center.Z + LiftHeight;

	// Rings drift up the column and fade in at the bottom, out at the top.
	RingPhase = FMath::Fmod(RingPhase + DeltaSeconds * 0.35f, 1.f);
	for (int32 Index = 0; Index < Rings.Num(); ++Index)
	{
		const float T = FMath::Fmod(RingPhase + static_cast<float>(Index) / Rings.Num(), 1.f);
		Rings[Index]->SetRelativeLocation(FVector(0.f, 0.f, T * LiftHeight));
		if (Materials.IsValidIndex(Index + 2))
		{
			Materials[Index + 2]->SetScalarParameterValue(TEXT("Intensity"), 1.5f * FMath::Sin(T * UE_PI));
		}
	}

	for (auto It = Riders.CreateIterator(); It; ++It)
	{
		if (!It->IsValid() || !(*It)->IsAlive())
		{
			It.RemoveCurrent();
		}
	}

	TArray<AActor*> Overlapping;
	Trigger->GetOverlappingActors(Overlapping, AMetalBotCharacter::StaticClass());
	for (AActor* Actor : Overlapping)
	{
		AMetalBotCharacter* Bot = Cast<AMetalBotCharacter>(Actor);
		// Flyers have their own engines; dead and powered-down robots just stand (or fall) where they are.
		if (Bot == nullptr || Cast<AMetalStalker>(Bot) || !Bot->IsAlive() || Bot->IsDormant())
		{
			continue;
		}
		UCharacterMovementComponent* Move = Bot->GetCharacterMovement();
		const float Feet = Bot->GetActorLocation().Z - Bot->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
		const bool bRiding = Riders.Contains(Bot);
		if (Feet >= TopZ - 25.f)
		{
			if (bRiding)
			{
				Release(Bot, true);
			}
			continue;
		}
		if (!bRiding)
		{
			Riders.Add(Bot);
			Bot->UnCrouch();
			Move->SetMovementMode(MOVE_Flying);
			Move->MaxFlySpeed = LiftSpeed + 300.f;
			MetalAudio::PlayAt(this, TEXT("lift_enter"), Bot->GetActorLocation(), 0.7f);
		}
		// Rise, slowing over the last couple of metres; pull gently towards the axis and damp sideways drift
		// so riders stay in the column unless they steer out on purpose.
		FVector Velocity = Move->Velocity;
		const float Target = LiftSpeed * FMath::Clamp((TopZ - Feet) / 250.f, 0.35f, 1.f);
		Velocity.Z = FMath::FInterpTo(Velocity.Z, Target, DeltaSeconds, 5.f);
		const FVector ToAxis(Center.X - Bot->GetActorLocation().X, Center.Y - Bot->GetActorLocation().Y, 0.f);
		Velocity += ToAxis * 2.5f * DeltaSeconds;
		const float Damp = FMath::Clamp(1.f - 1.5f * DeltaSeconds, 0.f, 1.f);
		Velocity.X *= Damp;
		Velocity.Y *= Damp;
		Move->Velocity = Velocity;
	}
}
