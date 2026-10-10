#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "PlanetNavGraph.h"
#include "PlanetSurfaceSubsystem.generated.h"

UCLASS()
class METALIZATION_API UPlanetSurfaceSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintCallable, Category = "Planet")
	void RegisterPlanet(AActor* InPlanet, float InRadius, int32 NavSubdivisions = 6);

	UFUNCTION(BlueprintPure, Category = "Planet")
	bool HasPlanet() const { return NavGraph.IsValid(); }

	UFUNCTION(BlueprintPure, Category = "Planet")
	FVector GetCenter() const { return Center; }

	UFUNCTION(BlueprintPure, Category = "Planet")
	float GetRadius() const { return Radius; }

	FVector DirectionFromWorld(const FVector& WorldPos) const;
	float GetSurfaceHeight(const FVector& Direction) const;
	FVector SurfacePoint(const FVector& Direction, float Offset = 0.f) const;
	bool StepToward(FVector& InOutDir, const FVector& TargetDir, float ArcDistance) const;
	int32 CellFromDirection(const FVector& Direction);
	void RequestPath(const FVector& FromDir, const FVector& ToDir, TFunction<void(TArray<FVector>)> OnComplete);

	const FPlanetNavGraph* GetNavGraph() const { return NavGraph.Get(); }

private:
	TWeakObjectPtr<AActor> Planet;
	FVector Center = FVector::ZeroVector;
	float Radius = 1000.f;
	TSharedPtr<FPlanetNavGraph, ESPMode::ThreadSafe> NavGraph;
	int32 LastCellHint = 0;
};
