#include "MetalProjectile.h"

#include "MetalBotCharacter.h"
#include "MetalFX.h"
#include "MetalWeaponData.h"
#include "Components/SphereComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/OverlapResult.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "GameFramework/DamageType.h"
#include "GameFramework/ProjectileMovementComponent.h"
#include "Kismet/GameplayStatics.h"
#include "Materials/MaterialInstanceDynamic.h"

AMetalProjectile::AMetalProjectile()
{
	PrimaryActorTick.bCanEverTick = true;

	Collision = CreateDefaultSubobject<USphereComponent>(TEXT("Collision"));
	Collision->InitSphereRadius(6.f);
	Collision->SetCollisionProfileName(TEXT("BlockAllDynamic"));
	Collision->SetCollisionResponseToChannel(ECC_Camera, ECR_Ignore);
	Collision->SetCollisionResponseToChannel(ECC_Visibility, ECR_Ignore);
	Collision->SetCanEverAffectNavigation(false);
	Collision->SetNotifyRigidBodyCollision(true);
	RootComponent = Collision;

	Visual = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Visual"));
	Visual->SetupAttachment(Collision);
	Visual->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Visual->SetCastShadow(false);

	Movement = CreateDefaultSubobject<UProjectileMovementComponent>(TEXT("Movement"));
	Movement->UpdatedComponent = Collision;
	Movement->bRotationFollowsVelocity = true;
	Movement->bAutoActivate = false;

	InitialLifeSpan = 8.f;
	SetCanBeDamaged(false);
}

void AMetalProjectile::Launch(const UMetalWeaponData* Weapon, float InDamage, const FVector& Velocity)
{
	check(Weapon);
	Damage = InDamage;
	ExplosionRadius = Weapon->ExplosionRadius;
	StunDuration = Weapon->StunDuration;
	Color = Weapon->TracerColor;
	Scale = FMath::Max(0.1f, Weapon->ProjectileScale);
	Collision->SetSphereRadius(6.f * Scale);

	// Ignore each other both ways: the shooter's capsule sweep walking into a fresh shot would
	// otherwise report a hit on us and set it off point-blank.
	if (AActor* Shooter = GetInstigator())
	{
		Collision->IgnoreActorWhenMoving(Shooter, true);
		if (UPrimitiveComponent* ShooterRoot = Cast<UPrimitiveComponent>(Shooter->GetRootComponent()))
		{
			ShooterRoot->IgnoreActorWhenMoving(this, true);
		}
	}

	const bool bThrown = Weapon->FireMode == EMetalFireMode::Thrown;
	if (bThrown && Weapon->PropMesh)
	{
		Visual->SetStaticMesh(Weapon->PropMesh);
		Visual->SetRelativeScale3D(FVector(1.5f));
		FuseTime = 1.6f;
	}
	else
	{
		// Glowing slug: the engine sphere stretched along the flight direction.
		Visual->SetStaticMesh(LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Sphere.Sphere")));
		Visual->SetRelativeScale3D((ExplosionRadius > 0.f ? FVector(0.35f, 0.16f, 0.16f) : FVector(0.3f, 0.07f, 0.07f)) * Scale);
		if (UMaterialInterface* Glow = MetalAssets::GetGlowMaterial())
		{
			UMaterialInstanceDynamic* MID = UMaterialInstanceDynamic::Create(Glow, this);
			MID->SetVectorParameterValue(TEXT("Color"), Color);
			MID->SetScalarParameterValue(TEXT("Intensity"), 8.f);
			Visual->SetMaterial(0, MID);
		}
	}

	Movement->ProjectileGravityScale = Weapon->ProjectileGravityScale;
	Movement->bShouldBounce = bThrown;
	Movement->Bounciness = 0.35f;
	Movement->Friction = 0.4f;
	Movement->Velocity = Velocity;
	Movement->Activate();
	Collision->OnComponentHit.AddDynamic(this, &AMetalProjectile::OnHit);
}

void AMetalProjectile::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (FuseTime > 0.f)
	{
		FuseTime -= DeltaSeconds;
		if (FuseTime <= 0.f)
		{
			Detonate(nullptr);
		}
	}
}

void AMetalProjectile::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (AActor* Shooter = GetInstigator())
	{
		if (UPrimitiveComponent* ShooterRoot = Cast<UPrimitiveComponent>(Shooter->GetRootComponent()))
		{
			ShooterRoot->IgnoreActorWhenMoving(this, false);
		}
	}
	Super::EndPlay(EndPlayReason);
}

void AMetalProjectile::OnHit(UPrimitiveComponent* HitComponent, AActor* OtherActor, UPrimitiveComponent* OtherComp, FVector NormalImpulse, const FHitResult& Hit)
{
	if (OtherActor && OtherActor == GetInstigator())
	{
		return;
	}
	// Grenades bounce off the world and only go off early when they hit a robot.
	if (FuseTime > 0.f && Cast<AMetalBotCharacter>(OtherActor) == nullptr)
	{
		return;
	}
	Detonate(&Hit);
}

void AMetalProjectile::Detonate(const FHitResult* DirectHit)
{
	if (bDetonated)
	{
		return;
	}
	bDetonated = true;

	UWorld* World = GetWorld();
	AController* InstigatorController = GetInstigatorController();
	const FVector Location = DirectHit ? FVector(DirectHit->ImpactPoint) : GetActorLocation();

	if (ExplosionRadius > 0.f)
	{
		TArray<AActor*> Ignore;
		UGameplayStatics::ApplyRadialDamageWithFalloff(this, Damage, Damage * 0.25f, Location, ExplosionRadius * 0.3f, ExplosionRadius, 1.f,
			UDamageType::StaticClass(), Ignore, this, InstigatorController, ECC_Visibility);
		if (StunDuration > 0.f)
		{
			TArray<FOverlapResult> Overlaps;
			World->OverlapMultiByObjectType(Overlaps, Location, FQuat::Identity, FCollisionObjectQueryParams(ECC_Pawn), FCollisionShape::MakeSphere(ExplosionRadius));
			for (const FOverlapResult& Overlap : Overlaps)
			{
				if (AMetalBotCharacter* Bot = Cast<AMetalBotCharacter>(Overlap.GetActor()))
				{
					Bot->Stun(StunDuration);
				}
			}
		}
		AMetalFX::SpawnBurst(World, Location, Color, ExplosionRadius * 0.8f, 0.45f);
	}
	else
	{
		if (DirectHit && DirectHit->GetActor())
		{
			UGameplayStatics::ApplyPointDamage(DirectHit->GetActor(), Damage, GetVelocity().GetSafeNormal(), *DirectHit,
				InstigatorController, this, UDamageType::StaticClass());
		}
		AMetalFX::SpawnBurst(World, Location, Color, 18.f * Scale, 0.15f);
	}
	Destroy();
}
