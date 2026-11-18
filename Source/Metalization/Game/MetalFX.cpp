#include "MetalFX.h"

#include "MetalTypes.h"
#include "Components/PointLightComponent.h"
#include "Engine/World.h"
#include "NiagaraComponent.h"
#include "NiagaraSystem.h"

namespace
{
	const TCHAR* GlowMaterialPath = TEXT("/Game/Metalization/Materials/M_FX_Glow.M_FX_Glow");
	const TCHAR* EffectFolder = TEXT("/Game/Metalization/FX");
	const FName ColorParam(TEXT("User.Color"));

	/** Reference sizes the systems are authored at (see UMetalFXAuthoring::BuildEffectSystems). */
	const float MuzzleReferenceSize = 10.f;
	const float ImpactReferenceSize = 10.f;
	const float BurstReferenceRadius = 100.f;
	/** Beams are a unit cylinder: 100 cm long and 100 cm across. */
	const float BeamReferenceLength = 100.f;
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

UNiagaraSystem* MetalAssets::GetEffect(const TCHAR* Name)
{
	// Rooted so the systems aren't collected and reloaded (a hitch) between shots or across level travel.
	static TMap<FName, UNiagaraSystem*> Loaded;
	const FName Key(Name);
	if (UNiagaraSystem** Found = Loaded.Find(Key))
	{
		return *Found;
	}
	const FString Path = FString::Printf(TEXT("%s/%s.%s"), EffectFolder, Name, Name);
	UNiagaraSystem* System = LoadObject<UNiagaraSystem>(nullptr, *Path);
	if (System)
	{
		System->AddToRoot();
	}
	else
	{
		UE_LOG(LogMetal, Warning, TEXT("Missing %s - run Tools/AssetPipeline/run_pipeline.ps1"), *Path);
	}
	Loaded.Add(Key, System);
	return System;
}

AMetalFX::AMetalFX()
{
	PrimaryActorTick.bCanEverTick = true;
	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	RootComponent = Root;

	Effect = CreateDefaultSubobject<UNiagaraComponent>(TEXT("Effect"));
	Effect->SetupAttachment(Root);
	Effect->SetAutoActivate(false);
	Effect->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Effect->SetCastShadow(false);

	Light = CreateDefaultSubobject<UPointLightComponent>(TEXT("Light"));
	Light->SetupAttachment(Root);
	Light->SetCastShadows(false);
	Light->SetIntensityUnits(ELightUnits::Candelas);
	Light->SetVisibility(false);
	SetCanBeDamaged(false);
}

AMetalFX* AMetalFX::SpawnEffect(UWorld* World, const TCHAR* SystemName, const FTransform& Transform, const FLinearColor& Color, float LightRadius, float InLifetime)
{
	UNiagaraSystem* System = MetalAssets::GetEffect(SystemName);
	if (World == nullptr || System == nullptr)
	{
		return nullptr;
	}
	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	AMetalFX* FX = World->SpawnActor<AMetalFX>(AMetalFX::StaticClass(), FTransform(Transform.GetRotation(), Transform.GetLocation()), Params);
	if (FX == nullptr)
	{
		return nullptr;
	}
	FX->Lifetime = InLifetime;
	FX->Effect->SetRelativeScale3D(Transform.GetScale3D());
	FX->Effect->SetAsset(System);
	FX->Effect->SetVariableLinearColor(ColorParam, Color);
	FX->Effect->Activate(true);
	if (LightRadius > 0.f)
	{
		FX->StartLightIntensity = 40.f * (LightRadius / 100.f);
		FX->Light->SetLightColor(Color);
		FX->Light->SetAttenuationRadius(LightRadius);
		FX->Light->SetIntensity(FX->StartLightIntensity);
		FX->Light->SetVisibility(true);
	}
	return FX;
}

AMetalFX* AMetalFX::SpawnBeam(UWorld* World, const FVector& Start, const FVector& End, const FLinearColor& Color, float Width, float Lifetime)
{
	AMetalFX* FX = SpawnEffect(World, Lifetime > 0.f ? TEXT("NS_Tracer") : TEXT("NS_Beam"), FTransform(Start), Color, 0.f, Lifetime);
	if (FX)
	{
		FX->BeamWidth = Width;
		FX->SetBeamEndpoints(Start, End);
	}
	return FX;
}

AMetalFX* AMetalFX::SpawnBurst(UWorld* World, const FVector& Location, const FLinearColor& Color, float Radius, float Lifetime)
{
	const FTransform Transform(FQuat::Identity, Location, FVector(Radius / BurstReferenceRadius));
	return SpawnEffect(World, TEXT("NS_Burst"), Transform, Color, Radius * 4.f, FMath::Max(Lifetime, 0.01f));
}

AMetalFX* AMetalFX::SpawnMuzzleFlash(UWorld* World, const FVector& Location, const FVector& Direction, const FLinearColor& Color, float Size)
{
	const FTransform Transform(FRotationMatrix::MakeFromX(Direction.GetSafeNormal(UE_SMALL_NUMBER, FVector::ForwardVector)).ToQuat(), Location, FVector(Size / MuzzleReferenceSize));
	return SpawnEffect(World, TEXT("NS_MuzzleFlash"), Transform, Color, Size * 8.f, 0.08f);
}

AMetalFX* AMetalFX::SpawnImpact(UWorld* World, const FVector& Location, const FVector& Normal, const FLinearColor& Color, float Size)
{
	const FTransform Transform(FRotationMatrix::MakeFromX(Normal.GetSafeNormal(UE_SMALL_NUMBER, FVector::UpVector)).ToQuat(), Location, FVector(Size / ImpactReferenceSize));
	return SpawnEffect(World, TEXT("NS_Impact"), Transform, Color, Size * 5.f, 0.1f);
}

AMetalFX* AMetalFX::SpawnExplosion(UWorld* World, const FVector& Location, const FLinearColor& Color, float Radius, float Lifetime)
{
	const FTransform Transform(FQuat::Identity, Location, FVector(Radius / BurstReferenceRadius));
	return SpawnEffect(World, TEXT("NS_Explosion"), Transform, Color, Radius * 4.f, FMath::Max(Lifetime, 0.01f));
}

void AMetalFX::SetBeamEndpoints(const FVector& Start, const FVector& End)
{
	const FVector Delta = End - Start;
	const float Length = FMath::Max(1.f, Delta.Size());
	// The unit beam runs along local Z, centred on the component.
	SetActorLocationAndRotation((Start + End) * 0.5f, FRotationMatrix::MakeFromZ(Delta / Length).Rotator());
	Effect->SetRelativeScale3D(FVector(BeamWidth / BeamReferenceLength, BeamWidth / BeamReferenceLength, Length / BeamReferenceLength));
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
	if (Light->IsVisible())
	{
		// Squared so the light drops quickly, like the flash it comes from.
		Light->SetIntensity(StartLightIntensity * FMath::Square(1.f - Alpha));
		if (Alpha >= 1.f)
		{
			Light->SetVisibility(false);
		}
	}
	if (Alpha >= 1.f && !Effect->IsActive())
	{
		Destroy();
	}
}
