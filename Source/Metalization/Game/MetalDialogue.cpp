#include "MetalDialogue.h"

#include "MetalAudio.h"
#include "MetalTypes.h"

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
	}
}

TStatId UMetalDialogueSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UMetalDialogueSubsystem, STATGROUP_Tickables);
}

bool UMetalDialogueSubsystem::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}
