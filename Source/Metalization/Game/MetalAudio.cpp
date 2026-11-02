#include "MetalAudio.h"

#include "MetalTypes.h"
#include "Components/AudioComponent.h"
#include "Kismet/GameplayStatics.h"
#include "Sound/SoundAttenuation.h"
#include "Sound/SoundBase.h"

namespace
{
	/** Loaded on first use and rooted (it is tiny and used for the whole session). */
	UMetalSoundLibrary* Library = nullptr;
	bool bTriedLoad = false;
	USoundAttenuation* Attenuation = nullptr;

	UMetalSoundLibrary* GetLibrary()
	{
		if (!bTriedLoad)
		{
			bTriedLoad = true;
			Library = LoadObject<UMetalSoundLibrary>(nullptr, TEXT("/Game/Metalization/Data/DA_Sounds.DA_Sounds"), nullptr, LOAD_NoWarn | LOAD_Quiet);
			if (Library)
			{
				Library->AddToRoot();
				// Shared falloff for every positional sound: full volume up close, silent past ~45 m.
				// Only this static points at it, so it must be rooted too or the next GC (level travel) frees it.
				Attenuation = NewObject<USoundAttenuation>(GetTransientPackage(), TEXT("MetalAttenuation"));
				Attenuation->AddToRoot();
				Attenuation->Attenuation.bAttenuate = true;
				Attenuation->Attenuation.bSpatialize = true;
				Attenuation->Attenuation.AttenuationShape = EAttenuationShape::Sphere;
				Attenuation->Attenuation.AttenuationShapeExtents = FVector(400.f);
				Attenuation->Attenuation.FalloffDistance = 4500.f;
				Attenuation->Attenuation.DistanceAlgorithm = EAttenuationDistanceModel::NaturalSound;
			}
			else
			{
				UE_LOG(LogMetal, Warning, TEXT("DA_Sounds not found - the game runs silent. Run Tools/AssetPipeline/run_pipeline.ps1."));
			}
		}
		return Library;
	}

	USoundBase* Find(FName Name, float& InOutVolume)
	{
		const UMetalSoundLibrary* Lib = GetLibrary();
		if (Lib == nullptr || Name.IsNone())
		{
			return nullptr;
		}
		if (const float* Scale = Lib->Volumes.Find(Name))
		{
			InOutVolume *= *Scale;
		}
		const TObjectPtr<USoundBase>* Sound = Lib->Sounds.Find(Name);
		return Sound ? Sound->Get() : nullptr;
	}

	float Pitch(float Jitter)
	{
		return Jitter > 0.f ? FMath::FRandRange(1.f - Jitter, 1.f + Jitter) : 1.f;
	}
}

namespace MetalAudio
{
	void PlayAt(const UObject* WorldContext, FName Name, const FVector& Location, float Volume, float PitchJitter)
	{
		if (USoundBase* Sound = Find(Name, Volume))
		{
			UGameplayStatics::PlaySoundAtLocation(WorldContext, Sound, Location, Volume, Pitch(PitchJitter), 0.f, Attenuation);
		}
	}

	void Play2D(const UObject* WorldContext, FName Name, float Volume, float PitchJitter)
	{
		if (USoundBase* Sound = Find(Name, Volume))
		{
			UGameplayStatics::PlaySound2D(WorldContext, Sound, Volume, Pitch(PitchJitter));
		}
	}

	UAudioComponent* PlayLoop(USceneComponent* AttachTo, FName Name, float Volume)
	{
		if (AttachTo == nullptr)
		{
			return nullptr;
		}
		if (USoundBase* Sound = Find(Name, Volume))
		{
			return UGameplayStatics::SpawnSoundAttached(Sound, AttachTo, NAME_None, FVector::ZeroVector, EAttachLocation::KeepRelativeOffset,
				true, Volume, 1.f, 0.f, Attenuation);
		}
		return nullptr;
	}

	bool HasSound(FName Name)
	{
		float Unused = 1.f;
		return Find(Name, Unused) != nullptr;
	}
}
