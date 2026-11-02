#include "MetalDialogue.h"

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
	if (Queue.Num() == 0)
	{
		LineTime = 0.f;
		OpenTime = 0.f;
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
	if (LineTime >= FMath::Max(0.5f, Queue[0].Duration))
	{
		Queue.RemoveAt(0);
		LineTime = 0.f;
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
