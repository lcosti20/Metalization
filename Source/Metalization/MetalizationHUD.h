#pragma once

#include "CoreMinimal.h"
#include "GameFramework/HUD.h"
#include "MetalizationHUD.generated.h"

UCLASS()
class AMetalizationHUD : public AHUD
{
	GENERATED_BODY()

public:
	virtual void DrawHUD() override;

private:
	void DrawTeamStats() const;
};
