#include "MetalAudio.h"

#include "MetalTypes.h"
#include "Components/AudioComponent.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/ConfigCacheIni.h"
#include "Sound/SoundClass.h"
#include "Sound/SoundMix.h"
#include "Sound/SoundAttenuation.h"
#include "Sound/SoundBase.h"

namespace
{
	/** Loaded on first use and rooted (it is tiny and used for the whole session). */
	UMetalSoundLibrary* Library = nullptr;
	/** Only try to load DA_Sounds once, so a missing library warns once instead of every sound. */
	bool bTriedLoad = false;
	/** Falloff shared by every positional sound. Created with the library. */
	USoundAttenuation* Attenuation = nullptr;

	/** @return The sound library, loading it (and creating Attenuation) on first use; null if it doesn't exist. */
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

	/**
	 * Looks a sound up by name and applies its volume scale from the library.
	 *
	 * @param Name        Library name ("laser", "footstep"...).
	 * @param InOutVolume Requested volume, multiplied by the library's scale for this sound.
	 * @return The sound, or null if the library or the entry is missing.
	 */
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

	/** GameUserSettings.ini section holding the volume settings. */
	const TCHAR* VolumeSection = TEXT("/Script/Metalization.MetalAudio");

	/** @return The ini key of a volume setting. */
	const TCHAR* VolumeKey(EMetalVolume Group)
	{
		switch (Group)
		{
		case EMetalVolume::Music:  return TEXT("MusicVolume");
		case EMetalVolume::Voices: return TEXT("VoicesVolume");
		default:                   return TEXT("EffectsVolume");
		}
	}

	/**
	 * @return The sound class a volume setting scales (SC_Music / SC_Effects / SC_Voices, made by the pipeline), loaded and
	 *         rooted on first use; null if it doesn't exist yet.
	 */
	USoundClass* VolumeClass(EMetalVolume Group)
	{
		static USoundClass* Classes[3] = {nullptr, nullptr, nullptr};
		static bool bTried[3] = {false, false, false};
		const int32 Index = static_cast<int32>(Group);
		if (!bTried[Index])
		{
			bTried[Index] = true;
			const TCHAR* Path = Group == EMetalVolume::Music ? TEXT("/Game/Metalization/Audio/SC_Music.SC_Music")
				: Group == EMetalVolume::Voices ? TEXT("/Game/Metalization/Audio/SC_Voices.SC_Voices")
				: TEXT("/Game/Metalization/Audio/SC_Effects.SC_Effects");
			Classes[Index] = LoadObject<USoundClass>(nullptr, Path, nullptr, LOAD_NoWarn | LOAD_Quiet);
			if (Classes[Index])
			{
				Classes[Index]->AddToRoot();
			}
		}
		return Classes[Index];
	}

	/** The base sound mix carrying the two volume settings as class overrides. Transient and rooted. */
	USoundMix* VolumeMix()
	{
		static USoundMix* Mix = nullptr;
		if (Mix == nullptr)
		{
			Mix = NewObject<USoundMix>(GetTransientPackage(), TEXT("MetalVolumeMix"));
			Mix->AddToRoot();
		}
		return Mix;
	}

	/**
	 * @param Jitter Maximum pitch change either way (0.1 = +/-10%).
	 * @return A random pitch multiplier, so repeated sounds don't sound identical.
	 */
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

	UAudioComponent* PlayAttached(USceneComponent* AttachTo, FName Name, float Volume, float Pitch)
	{
		if (AttachTo == nullptr)
		{
			return nullptr;
		}
		if (USoundBase* Sound = Find(Name, Volume))
		{
			return UGameplayStatics::SpawnSoundAttached(Sound, AttachTo, NAME_None, FVector::ZeroVector, EAttachLocation::KeepRelativeOffset,
				true, Volume, Pitch, 0.f, Attenuation);
		}
		return nullptr;
	}

	FName PickVariant(FName Base)
	{
		// Variant counts per base name. The library never changes during a session, so they are counted once.
		static TMap<FName, int32> Counts;
		const int32* Known = Counts.Find(Base);
		int32 Count = Known ? *Known : 0;
		if (Known == nullptr)
		{
			if (GetLibrary() == nullptr)
			{
				return NAME_None;
			}
			while (HasSound(FName(*FString::Printf(TEXT("%s%d"), *Base.ToString(), Count + 1))))
			{
				++Count;
			}
			Counts.Add(Base, Count);
		}
		return Count > 0 ? FName(*FString::Printf(TEXT("%s%d"), *Base.ToString(), FMath::RandRange(1, Count))) : FName();
	}

	UAudioComponent* CreateLoop2D(const UObject* WorldContext, FName Name, float Volume)
	{
		if (USoundBase* Sound = Find(Name, Volume))
		{
			return UGameplayStatics::CreateSound2D(WorldContext, Sound, Volume, 1.f, 0.f, nullptr, false, false);
		}
		return nullptr;
	}

	bool HasSound(FName Name)
	{
		float Unused = 1.f;
		return Find(Name, Unused) != nullptr;
	}

	float GetVolume(EMetalVolume Group)
	{
		float Volume = 1.f;
		if (GConfig)
		{
			GConfig->GetFloat(VolumeSection, VolumeKey(Group), Volume, GGameUserSettingsIni);
		}
		return FMath::Clamp(Volume, 0.f, 1.f);
	}

	void SetVolume(const UObject* WorldContext, EMetalVolume Group, float Volume)
	{
		if (GConfig)
		{
			GConfig->SetFloat(VolumeSection, VolumeKey(Group), FMath::Clamp(Volume, 0.f, 1.f), GGameUserSettingsIni);
			GConfig->Flush(false, GGameUserSettingsIni);
		}
		ApplyVolumes(WorldContext);
	}

	void ApplyVolumes(const UObject* WorldContext)
	{
		USoundMix* Mix = VolumeMix();
		for (const EMetalVolume Group : {EMetalVolume::Music, EMetalVolume::Effects, EMetalVolume::Voices})
		{
			if (USoundClass* Class = VolumeClass(Group))
			{
				UGameplayStatics::SetSoundMixClassOverride(WorldContext, Mix, Class, GetVolume(Group), 1.f, 0.f, true);
			}
		}
		UGameplayStatics::SetBaseSoundMix(WorldContext, Mix);
		UE_LOG(LogMetal, Log, TEXT("Volumes: music %.2f, effects %.2f, voices %.2f%s"), GetVolume(EMetalVolume::Music), GetVolume(EMetalVolume::Effects),
			GetVolume(EMetalVolume::Voices),
			VolumeClass(EMetalVolume::Music) && VolumeClass(EMetalVolume::Effects) && VolumeClass(EMetalVolume::Voices) ? TEXT("") : TEXT(" (sound classes missing - run the pipeline)"));
	}
}
