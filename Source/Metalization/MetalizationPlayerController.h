// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "MetalizationPlayerController.generated.h"

class UInputMappingContext;
class UUserWidget;

DECLARE_DYNAMIC_MULTICAST_DELEGATE(FRTSSelectionChanged);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FRTSMoveOrderIssued, FVector, Destination);

UCLASS()
class AMetalizationPlayerController : public APlayerController
{
	GENERATED_BODY()

protected:
	UPROPERTY(EditAnywhere, Category = "Input|Input Mappings")
	TArray<UInputMappingContext*> DefaultMappingContexts;

	UPROPERTY(EditAnywhere, Category = "Input|Input Mappings")
	TArray<UInputMappingContext*> MobileExcludedMappingContexts;

	UPROPERTY(EditAnywhere, Category = "Input|Touch Controls")
	TSubclassOf<UUserWidget> MobileControlsWidgetClass;

	UPROPERTY()
	TObjectPtr<UUserWidget> MobileControlsWidget;

	UPROPERTY(EditAnywhere, Config, Category = "Input|Touch Controls")
	bool bForceTouchControls = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RTS Camera")
	float EdgePanMargin = 18.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RTS Selection")
	FName SelectableActorTag = TEXT("Selectable");

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RTS Selection")
	int32 TeamId = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RTS Movement")
	float FormationSpacing = 60.f;

	UPROPERTY(BlueprintReadOnly, Category = "RTS Selection")
	TArray<AActor*> SelectedActors;

	UPROPERTY(BlueprintAssignable, Category = "RTS Selection")
	FRTSSelectionChanged OnSelectionChanged;

	UPROPERTY(BlueprintAssignable, Category = "RTS Orders")
	FRTSMoveOrderIssued OnMoveOrderIssued;

	FVector2D SelectionStart = FVector2D::ZeroVector;
	FVector2D SelectionCurrent = FVector2D::ZeroVector;
	bool bSelectionBoxActive = false;

	virtual void BeginPlay() override;
	virtual void SetupInputComponent() override;
	virtual void Tick(float DeltaSeconds) override;

	void BeginSelection();
	void EndSelection();
	void IssueMoveOrder();
	void IssueStopOrder();
	void ZoomCamera(float Amount);
	void SelectActor(AActor* Actor, bool bAddToSelection);
	void SelectActorsInBox();
	void ClearSelection();
	bool ShouldUseTouchControls() const;

public:
	UFUNCTION(BlueprintPure, Category = "RTS Selection")
	const TArray<AActor*>& GetSelectedActors() const { return SelectedActors; }

	UFUNCTION(BlueprintPure, Category = "RTS Selection")
	bool IsSelectionBoxActive() const { return bSelectionBoxActive; }

	UFUNCTION(BlueprintPure, Category = "RTS Selection")
	FVector2D GetSelectionStart() const { return SelectionStart; }

	UFUNCTION(BlueprintPure, Category = "RTS Selection")
	FVector2D GetSelectionCurrent() const { return SelectionCurrent; }

	UFUNCTION(BlueprintCallable, Category = "RTS Selection")
	void SetTeamId(int32 NewTeamId) { TeamId = NewTeamId; }

	UFUNCTION(BlueprintPure, Category = "RTS Selection")
	int32 GetTeamId() const { return TeamId; }

private:
	bool IsSelectableActor(AActor* Actor) const;
};
