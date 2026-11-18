// Radio dialogue: lines queued by level events and played one after another during gameplay.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "MetalDialogue.generated.h"

class UAudioComponent;

/** One line of radio dialogue. */
USTRUCT(BlueprintType)
struct FMetalDialogueLine
{
	GENERATED_BODY()

	/** Name shown above the line (e.g. "Guide"). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Dialogue")
	FText Speaker;

	/** The words spoken. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Dialogue", meta=(MultiLine=true))
	FText Line;

	/** Seconds the line stays on screen before the next one. Its text types out one character at a time over all but the last second. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Dialogue", meta=(ClampMin="0.5"))
	float Duration = 4.f;
};

/**
 * Plays dialogue during gameplay. Anything can queue lines (mission objectives, AMetalTriggerZone,
 * level Blueprints); they play in order, each for its Duration, and can't be skipped. There are no
 * cutscenes or choices: the game keeps running underneath. AMetalHUD draws the current line in the
 * dialogue box and a radio icon over the player while an NPC is talking. A radio whistle (a tone sweeping
 * between random pitches) loops quietly underneath while each line is spoken. The last second of every line
 * is a pause: the text has finished typing and the whistle and radio icon are off.
 */
UCLASS()
class UMetalDialogueSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	/**
	 * Queues lines after whatever is already playing. Lines with no text are skipped.
	 *
	 * @param Lines Dialogue to play, in order.
	 */
	UFUNCTION(BlueprintCallable, Category="Metalization|Dialogue")
	void PlayDialogue(const TArray<FMetalDialogueLine>& Lines);

	/**
	 * Queues one line after whatever is already playing.
	 *
	 * @param Line Line to play. Ignored if it has no text.
	 */
	void PlayLine(const FMetalDialogueLine& Line);

	/** @return True while a line is on screen. */
	UFUNCTION(BlueprintPure, Category="Metalization|Dialogue")
	bool IsPlaying() const { return Queue.Num() > 0; }

	/** @return The line on screen, or null when nobody is talking. */
	const FMetalDialogueLine* GetCurrentLine() const { return Queue.Num() > 0 ? &Queue[0] : nullptr; }

	/** @return Seconds the current line has been on screen. */
	float GetLineTime() const { return LineTime; }

	/**
	 * Seconds a line is "on air": its text types out, the radio whistle plays and the radio icon shows. This is
	 * a second less than its Duration (at least 0.25 s), leaving a pause before the next line.
	 *
	 * @param Line Line to time.
	 * @return Talk time in seconds.
	 */
	static float GetTalkTime(const FMetalDialogueLine& Line);

	/** @return True while the current line is still being spoken (not in its closing pause). */
	bool IsTalking() const { return Queue.Num() > 0 && LineTime < GetTalkTime(Queue[0]); }

	/** @return Seconds since the dialogue box opened; it stays open across back-to-back lines. */
	float GetOpenTime() const { return OpenTime; }

	/** Advances to the next line once the current one has shown for its Duration. */
	virtual void Tick(float DeltaTime) override;

	/** Stat id required of tickable objects. */
	virtual TStatId GetStatId() const override;

protected:
	/** Only game worlds play dialogue. */
	virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;

	/** Stops the radio whistle with the world. */
	virtual void Deinitialize() override;

private:
	/** Lines waiting to be shown; the front entry is on screen. */
	TArray<FMetalDialogueLine> Queue;
	/** Seconds the front line has been on screen. */
	float LineTime = 0.f;
	/** Seconds since the dialogue box opened (reset when the queue empties). */
	float OpenTime = 0.f;
	/** Looping radio whistle ("radio_whistle_loop") playing while the dialogue box is open; created on the first line. */
	UPROPERTY(Transient)
	TObjectPtr<UAudioComponent> Whistle;
	/** True between fading the whistle in and fading it out. */
	bool bWhistleOn = false;

	/**
	 * Fades the radio whistle in (dialogue box opening), from a random point in the loop and at a slightly
	 * random pitch so it never sounds the same twice, or out (dialogue box closing).
	 */
	void SetWhistlePlaying(bool bPlaying);
};
