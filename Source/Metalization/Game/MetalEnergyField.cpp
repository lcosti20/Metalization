#include "MetalEnergyField.h"

#include "MetalTypes.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/DamageEvents.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "UObject/ConstructorHelpers.h"

AMetalEnergyField::AMetalEnergyField()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = false;

	static ConstructorHelpers::FObjectFinder<UStaticMesh> Cube(TEXT("/Engine/BasicShapes/Cube.Cube"));
	Field = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Field"));
	Field->SetStaticMesh(Cube.Object);
	// Blocks robots, shots (Visibility), projectiles and blast damage. The camera passes through, so it
	// doesn't snap in when the player backs up against the field.
	Field->SetCollisionProfileName(UCollisionProfile::BlockAll_ProfileName);
	Field->SetCollisionResponseToChannel(ECC_Camera, ECR_Ignore);
	Field->SetCastShadow(false);
	Field->SetMobility(EComponentMobility::Static);
	RootComponent = Field;

	for (FVector& Center : RippleCenters)
	{
		Center = FVector::ZeroVector;
	}
}

void AMetalEnergyField::BeginPlay()
{
	Super::BeginPlay();
	if (UMaterialInterface* Base = Field->GetMaterial(0))
	{
		Material = UMaterialInstanceDynamic::Create(Base, this);
		Material->SetVectorParameterValue(TEXT("Color"), FieldColor);
		Material->SetScalarParameterValue(TEXT("RippleLife"), RippleLife);
		Field->SetMaterial(0, Material);
	}
	PushRipples();
}

void AMetalEnergyField::IgnoreAll(const UWorld* World, FCollisionQueryParams& Params)
{
	if (World == nullptr)
	{
		return;
	}
	for (TActorIterator<AMetalEnergyField> It(World); It; ++It)
	{
		Params.AddIgnoredActor(*It);
	}
}

void AMetalEnergyField::AddRipple(const FVector& Location)
{
	const float Now = GetWorld()->GetTimeSeconds();
	// A free (or finished) slot, else the oldest ripple makes way.
	int32 Slot = 0;
	for (int32 i = 0; i < MaxRipples; ++i)
	{
		if (RippleStarts[i] < 0.f || Now - RippleStarts[i] >= RippleLife)
		{
			Slot = i;
			break;
		}
		if (RippleStarts[i] < RippleStarts[Slot])
		{
			Slot = i;
		}
	}
	RippleCenters[Slot] = Location;
	RippleStarts[Slot] = Now;
	SetActorTickEnabled(true);
	PushRipples();
	UE_LOG(LogMetal, Log, TEXT("[EnergyField] %s rippled at %s"), *GetActorNameOrLabel(), *Location.ToCompactString());
}

void AMetalEnergyField::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	PushRipples();
}

void AMetalEnergyField::PushRipples()
{
	const float Now = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.f;
	bool bAnyActive = false;
	for (int32 i = 0; i < MaxRipples; ++i)
	{
		float Age = RippleStarts[i] < 0.f ? -1.f : Now - RippleStarts[i];
		if (Age >= RippleLife)
		{
			RippleStarts[i] = -1.f;
			Age = -1.f;
		}
		bAnyActive |= Age >= 0.f;
		if (Material)
		{
			const FVector& C = RippleCenters[i];
			Material->SetVectorParameterValue(FName(*FString::Printf(TEXT("Hit%d"), i)), FLinearColor(C.X, C.Y, C.Z, Age));
		}
	}
	if (!bAnyActive)
	{
		SetActorTickEnabled(false);
	}
}

float AMetalEnergyField::TakeDamage(float DamageAmount, const FDamageEvent& DamageEvent, AController* EventInstigator, AActor* DamageCauser)
{
	FVector Where = GetActorLocation();
	if (DamageEvent.IsOfType(FPointDamageEvent::ClassID))
	{
		Where = static_cast<const FPointDamageEvent&>(DamageEvent).HitInfo.ImpactPoint;
	}
	else if (DamageEvent.IsOfType(FRadialDamageEvent::ClassID))
	{
		Where = Field->Bounds.GetBox().GetClosestPointTo(static_cast<const FRadialDamageEvent&>(DamageEvent).Origin);
	}
	AddRipple(Where);
	return 0.f;
}
