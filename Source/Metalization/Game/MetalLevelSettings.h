// The levels offered by the pause menu's Level Select, in order. Edit them in Project Settings > Game >
// Metalization Levels (saved to Config/DefaultGame.ini). Every map listed here must also be in the
// packaging settings' MapsToCook, or it won't be in packaged builds.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "MetalLevelSettings.generated.h"

USTRUCT(BlueprintType)
struct FMetalLevelEntry
{
	GENERATED_BODY()

	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category="Level")
	FText DisplayName;

	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category="Level")
	TSoftObjectPtr<UWorld> Map;
};

UCLASS(Config=Game, DefaultConfig, meta=(DisplayName="Metalization Levels"))
class UMetalLevelSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category="Levels")
	TArray<FMetalLevelEntry> Levels;

	virtual FName GetCategoryName() const override { return TEXT("Game"); }
};
