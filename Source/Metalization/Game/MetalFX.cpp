#include "MetalFX.h"

#include "MetalTypes.h"
#include "Components/PointLightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "Materials/MaterialInstanceDynamic.h"

namespace
{
	const TCHAR* GlowMaterialPath = TEXT("/Game/Metalization/Materials/M_FX_Glow.M_FX_Glow");
	const TCHAR* CylinderPath = TEXT("/Engine/BasicShapes/Cylinder.Cylinder");
	const TCHAR* SpherePath = TEXT("/Engine/BasicShapes/Sphere.Sphere");
	const float GlowIntensity = 6.f;

	UStaticMesh* LoadMesh(const TCHAR* Path)
	{
		return LoadObject<UStaticMesh>(nullptr, Path);
	}
}

UMaterialInterface* MetalAssets::GetGlowMaterial()
{
	static TWeakObjectPtr<UMaterialInterface> Cached;
	if (!Cached.IsValid())
	{
		Cached = LoadObject<UMaterialInterface>(nullptr, GlowMaterialPath);
		if (!Cached.IsValid())
		{
			UE_LOG(LogMetal, Warning, TEXT("Missing %s - run Tools/AssetPipeline/run_pipeline.ps1"), GlowMaterialPath);
		}
	}
	return Cached.Get();
}

AMetalFX::AMetalFX()
{
	PrimaryActorTick.bCanEverTick = true;
	MeshComponent = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Mesh"));
	MeshComponent->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	MeshComponent->SetCastShadow(false);
	MeshComponent->SetGenerateOverlapEvents(false);
	RootComponent = MeshComponent;

	Light = CreateDefaultSubobject<UPointLightComponent>(TEXT("Light"));
	Light->SetupAttachment(MeshComponent);
	Light->SetCastShadows(false);
	Light->SetIntensityUnits(ELightUnits::Candelas);
	Light->SetVisibility(false);
	SetCanBeDamaged(false);
}

void AMetalFX::Setup(UStaticMesh* Mesh, const FLinearColor& Color, float InLifetime, float LightRadius)
{
	Lifetime = InLifetime;
	MeshComponent->SetStaticMesh(Mesh);
	if (UMaterialInterface* Glow = MetalAssets::GetGlowMaterial())
	{
		Material = UMaterialInstanceDynamic::Create(Glow, this);
		Material->SetVectorParameterValue(TEXT("Color"), Color);
		Material->SetScalarParameterValue(TEXT("Intensity"), GlowIntensity);
		MeshComponent->SetMaterial(0, Material);
	}
	StartGlow = GlowIntensity;
	if (LightRadius > 0.f)
	{
		StartLightIntensity = 40.f * (LightRadius / 100.f);
		Light->SetLightColor(Color);
		Light->SetAttenuationRadius(LightRadius);
		Light->SetIntensity(StartLightIntensity);
		Light->SetVisibility(true);
	}
}

AMetalFX* AMetalFX::SpawnBeam(UWorld* World, const FVector& Start, const FVector& End, const FLinearColor& Color, float Width, float Lifetime)
{
	if (World == nullptr)
	{
		return nullptr;
	}
	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	AMetalFX* FX = World->SpawnActor<AMetalFX>(AMetalFX::StaticClass(), FTransform(Start), Params);
	if (FX)
	{
		FX->BeamWidth = Width;
		FX->Setup(LoadMesh(CylinderPath), Color, Lifetime, 0.f);
		FX->SetBeamEndpoints(Start, End);
		FX->StartScale = FX->GetActorScale3D();
		FX->EndScale = FVector(0.f, 0.f, FX->StartScale.Z);
	}
	return FX;
}

AMetalFX* AMetalFX::SpawnBurst(UWorld* World, const FVector& Location, const FLinearColor& Color, float Radius, float Lifetime)
{
	if (World == nullptr)
	{
		return nullptr;
	}
	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	AMetalFX* FX = World->SpawnActor<AMetalFX>(AMetalFX::StaticClass(), FTransform(Location), Params);
	if (FX)
	{
		FX->Setup(LoadMesh(SpherePath), Color, Lifetime, Radius * 4.f);
		FX->StartScale = FVector(Radius / 50.f * 0.3f);
		FX->EndScale = FVector(Radius / 50.f);
		FX->SetActorScale3D(FX->StartScale);
	}
	return FX;
}

void AMetalFX::SetBeamEndpoints(const FVector& Start, const FVector& End)
{
	const FVector Delta = End - Start;
	const float Length = FMath::Max(1.f, Delta.Size());
	// The engine cylinder is 100 units tall along Z, 100 wide, centred on its origin.
	SetActorLocationAndRotation((Start + End) * 0.5f, FRotationMatrix::MakeFromZ(Delta / Length).Rotator());
	SetActorScale3D(FVector(BeamWidth / 100.f, BeamWidth / 100.f, Length / 100.f));
}

void AMetalFX::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (Lifetime <= 0.f)
	{
		return; // persistent; owner destroys it
	}
	Age += DeltaSeconds;
	const float Alpha = FMath::Clamp(Age / Lifetime, 0.f, 1.f);
	SetActorScale3D(FMath::Lerp(StartScale, EndScale, FMath::Sqrt(Alpha)));
	if (Material)
	{
		Material->SetScalarParameterValue(TEXT("Intensity"), StartGlow * (1.f - Alpha));
	}
	Light->SetIntensity(StartLightIntensity * (1.f - Alpha));
	if (Alpha >= 1.f)
	{
		Destroy();
	}
}
