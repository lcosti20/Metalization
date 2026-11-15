#include "MetalDialogue.h"

#include "MetalAudio.h"
#include "MetalTypes.h"
#include "Components/AudioComponent.h"
#include "Sound/SoundBase.h"

namespace
{
	/** Radio whistle volume: a bed under the dialogue, below the blips (a pure tone carries, so keep it low). */
	constexpr float WhistleVolume = 0.3f;
	/** Seconds the whistle takes to fade in when the box opens and out when it closes. */
	constexpr float WhistleFadeIn = 0.15f;
	constexpr float WhistleFadeOut = 0.4f;
	/** Random pitch range applied each time the box opens. */
	constexpr float WhistlePitchMin = 0.85f;
	constexpr float WhistlePitchMax = 1.15f;
}

void UMetalDialogueSubsystem::PlayDialogue(const TArray<FMetalDialogueLine>& Lines)
{
	for (const FMetalDialogueLine& Line : Lines)
	{
		PlayLine(Line);
	}
}

void UMetalDialogueSubsystem::PlayLine(const FMetalDialogueLine& Line)
{
	if (Line.Line.IsEmpty())
	{
		return;
	}
	// Nothing playing: this line opens the dialogue box, with a radio blip.
	if (Queue.Num() == 0)
	{
		LineTime = 0.f;
		OpenTime = 0.f;
		MetalAudio::Play2D(GetWorld(), TEXT("radio_blip"), 0.6f);
		SetWhistlePlaying(true);
	}
	Queue.Add(Line);
	UE_LOG(LogMetal, Log, TEXT("Dialogue queued: %s: %s"), *Line.Speaker.ToString(), *Line.Line.ToString());
}

void UMetalDialogueSubsystem::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
	if (Queue.Num() == 0)
	{
		return;
	}
	LineTime += DeltaTime;
	OpenTime += DeltaTime;
	// The line has been spoken: the whistle stops for the pause before the next line.
	if (bWhistleOn && !IsTalking())
	{
		SetWhistlePlaying(false);
	}
	// The front line has had its time: drop it. The box stays open (OpenTime keeps counting) while more lines
	// are queued; the next one starts with a quieter blip and the whistle back on.
	if (LineTime >= FMath::Max(0.5f, Queue[0].Duration))
	{
		Queue.RemoveAt(0);
		LineTime = 0.f;
		if (Queue.Num() > 0)
		{
			MetalAudio::Play2D(GetWorld(), TEXT("radio_blip"), 0.4f);
			SetWhistlePlaying(true);
		}
		else if (bWhistleOn)
		{
			SetWhistlePlaying(false);
		}
	}
}

float UMetalDialogueSubsystem::GetTalkTime(const FMetalDialogueLine& Line)
{
	// The 0.5 s minimum duration matches Tick.
	return FMath::Max(FMath::Max(0.5f, Line.Duration) - 1.f, 0.25f);
}

void UMetalDialogueSubsystem::SetWhistlePlaying(bool bPlaying)
{
	bWhistleOn = bPlaying;
	if (!bPlaying)
	{
		UE_LOG(LogMetal, Log, TEXT("Dialogue radio whistle off"));
		if (IsValid(Whistle))
		{
			Whistle->FadeOut(WhistleFadeOut, 0.f);
		}
		return;
	}
	// A different stretch of the loop, slightly higher or lower, every time the box opens.
	const float Pitch = FMath::FRandRange(WhistlePitchMin, WhistlePitchMax);
	if (!IsValid(Whistle))
	{
		Whistle = MetalAudio::CreateLoop2D(GetWorld(), TEXT("radio_whistle_loop"), WhistleVolume);
		if (!IsValid(Whistle))
		{
			return;
		}
	}
	// GetDuration() reports looping sounds as endless, so read the wave's real length.
	const float Length = Whistle->Sound ? Whistle->Sound->Duration : 0.f;
	const float StartTime = Length > 0.f ? FMath::FRandRange(0.f, Length) : 0.f;
	Whistle->SetPitchMultiplier(Pitch);
	Whistle->FadeIn(WhistleFadeIn, 1.f, StartTime);
	UE_LOG(LogMetal, Log, TEXT("Dialogue radio whistle on (pitch %.2f, from %.1fs)"), Pitch, StartTime);
}

void UMetalDialogueSubsystem::Deinitialize()
{
	if (IsValid(Whistle))
	{
		Whistle->Stop();
	}
	Whistle = nullptr;
	bWhistleOn = false;
	Super::Deinitialize();
}

TStatId UMetalDialogueSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UMetalDialogueSubsystem, STATGROUP_Tickables);
}

bool UMetalDialogueSubsystem::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}
