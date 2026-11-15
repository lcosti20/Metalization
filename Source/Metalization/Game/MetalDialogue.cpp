#include "MetalDialogue.h"

#include "MetalAudio.h"
#include "MetalTypes.h"
#include "Components/AudioComponent.h"

namespace
{
	/** Radio static volume: a bed under the dialogue, well below the blips. */
	constexpr float StaticVolume = 0.45f;
	/** Seconds the static takes to fade in when the box opens and out when it closes. */
	constexpr float StaticFadeIn = 0.15f;
	constexpr float StaticFadeOut = 0.4f;
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
		SetStaticPlaying(true);
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
	// The front line has had its time: drop it. The box stays open (OpenTime keeps counting) while more lines
	// are queued, with a quieter blip between speakers.
	if (LineTime >= FMath::Max(0.5f, Queue[0].Duration))
	{
		Queue.RemoveAt(0);
		LineTime = 0.f;
		if (Queue.Num() > 0)
		{
			MetalAudio::Play2D(GetWorld(), TEXT("radio_blip"), 0.4f);
		}
		else
		{
			SetStaticPlaying(false);
		}
	}
}

void UMetalDialogueSubsystem::SetStaticPlaying(bool bPlaying)
{
	UE_LOG(LogMetal, Log, TEXT("Dialogue radio static %s"), bPlaying ? TEXT("on") : TEXT("off"));
	if (!IsValid(Static))
	{
		if (bPlaying)
		{
			Static = MetalAudio::PlayLoop2D(GetWorld(), TEXT("radio_static_loop"), StaticVolume, StaticFadeIn);
		}
		return;
	}
	if (bPlaying)
	{
		Static->FadeIn(StaticFadeIn);
	}
	else
	{
		Static->FadeOut(StaticFadeOut, 0.f);
	}
}

void UMetalDialogueSubsystem::Deinitialize()
{
	if (IsValid(Static))
	{
		Static->Stop();
	}
	Static = nullptr;
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
