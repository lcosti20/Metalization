#pragma once

#include "CoreMinimal.h"

struct FPlanetCell
{
	FVector Direction = FVector::UpVector;
	TArray<int32> Neighbors;
	float Height = 0.f;
	float MoveCost = 1.f;
	bool bBlocked = false;
};

class FPlanetNavGraph
{
public:
	void Build(int32 Subdivisions = 6);

	bool IsValidIndex(int32 Cell) const
	{
		return Cells.IsValidIndex(Cell);
	}

	const FPlanetCell& GetCell(int32 Index) const
	{
		return Cells[Index];
	}

	int32 FindCellFromDirection(const FVector& Direction, int32 HintCell = 0) const;
	int32 FindNearestOpenCell(int32 FromCell) const;
	bool FindPath(int32 StartCell, int32 GoalCell, TArray<int32>& OutPath) const;
	void ApplyTerrain(TFunctionRef<float(const FVector&)> HeightFn,
		float PlanetRadius,
		float SeaLevel,
		float MaxSlope,
		float SlopeCostScale);

	int32 Num() const
	{
		return Cells.Num();
	}

private:
	TArray<FPlanetCell> Cells;
};
