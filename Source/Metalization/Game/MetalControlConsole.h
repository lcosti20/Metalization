// Enemy-only control console. The player can't use it directly - they have to hijack an enemy robot
// with the Hack Tether and operate it with that body. Activating it opens the linked gates.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "MetalTypes.h"
#include "MetalControlConsole.generated.h"

class AMetalBotCharacter;
class UStaticMeshComponent;
class UPointLightComponent;
class UMaterialInstanceDynamic;

UCLASS()
class AMetalControlConsole : public AActor
{
	GENERATED_BODY()

public:
	AMetalControlConsole();

	/** Robots on this team can operate the console. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Console")
	EMetalTeam RequiredFaction = EMetalTeam::Villains;

	/** Gates (AMetalGate) opened on activation. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Console")
	TArray<TObjectPtr<AActor>> LinkedGates;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Console")
	float InteractRadius = 220.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Console")
	FText PromptText = NSLOCTEXT("Metalization", "ConsolePrompt", "[E] Operate gate console");

	bool IsActivated() const { return bActivated; }
	/** Returns the message to show the player. */
	FText TryActivate(AMetalBotCharacter* User);

protected:
	virtual void BeginPlay() override;
	void SetScreenColor(const FLinearColor& Color);

	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UStaticMeshComponent> Base;

	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UStaticMeshComponent> Screen;

	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UPointLightComponent> Light;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> ScreenMaterial;

private:
	bool bActivated = false;
};
