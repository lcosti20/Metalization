// The levels offered by the pause menu's Level Select, in order. Edit them in Project Settings > Game >
// Metalization Levels (saved to Config/DefaultGame.ini). Every map listed here must also be in the
// packaging settings' MapsToCook, or it won't be in packaged builds.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "MetalLevelSettings.generated.h"

/** One row of the Level Select list: a name to show and the map it opens. */
USTRUCT(BlueprintType)
struct FMetalLevelEntry
{
	GENERATED_BODY()

	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category="Level")
	FText DisplayName;

	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category="Level")
	TSoftObjectPtr<UWorld> Map;
};

/**
 * Project-wide list of playable levels, stored in Config/DefaultGame.ini under
 * [/Script/Metalization.MetalLevelSettings]. Read it with GetDefault<UMetalLevelSettings>().
 *
 * @see UMetalPauseMenu, which builds its Level Select buttons from Levels.
 */
UCLASS(Config=Game, DefaultConfig, meta=(DisplayName="Metalization Levels"))
class UMetalLevelSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	/** Levels in the order the menu lists them. */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category="Levels")
	TArray<FMetalLevelEntry> Levels;

	/** @return "Game", so the settings appear under Project Settings > Game. */
	virtual FName GetCategoryName() const override { return TEXT("Game"); }
};
