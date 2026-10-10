#include "PlanetNavGraph.h"

#include "Algo/Reverse.h"

void FPlanetNavGraph::Build(int32 Subdivisions)
{
	Cells.Reset();

	const float T = (1.f + FMath::Sqrt(5.f)) * 0.5f;
	TArray<FVector> Verts;
	Verts.Reserve(12);
	Verts.Add(FVector(-1.f, T, 0.f).GetSafeNormal());
	Verts.Add(FVector(1.f, T, 0.f).GetSafeNormal());
	Verts.Add(FVector(-1.f, -T, 0.f).GetSafeNormal());
	Verts.Add(FVector(1.f, -T, 0.f).GetSafeNormal());
	Verts.Add(FVector(0.f, -1.f, T).GetSafeNormal());
	Verts.Add(FVector(0.f, 1.f, T).GetSafeNormal());
	Verts.Add(FVector(0.f, -1.f, -T).GetSafeNormal());
	Verts.Add(FVector(0.f, 1.f, -T).GetSafeNormal());
	Verts.Add(FVector(T, 0.f, -1.f).GetSafeNormal());
	Verts.Add(FVector(T, 0.f, 1.f).GetSafeNormal());
	Verts.Add(FVector(-T, 0.f, -1.f).GetSafeNormal());
	Verts.Add(FVector(-T, 0.f, 1.f).GetSafeNormal());

	TArray<FIntVector> Tris;
	Tris = {
		FIntVector(0, 11, 5),
		FIntVector(0, 5, 1),
		FIntVector(0, 1, 7),
		FIntVector(0, 7, 10),
		FIntVector(0, 10, 11),
		FIntVector(1, 5, 9),
		FIntVector(5, 11, 4),
		FIntVector(11, 10, 2),
		FIntVector(10, 7, 6),
		FIntVector(7, 1, 8),
		FIntVector(3, 9, 4),
		FIntVector(3, 4, 2),
		FIntVector(3, 2, 6),
		FIntVector(3, 6, 8),
		FIntVector(3, 8, 9),
		FIntVector(4, 9, 5),
		FIntVector(2, 4, 11),
		FIntVector(6, 2, 10),
		FIntVector(8, 6, 7),
		FIntVector(9, 8, 1)
	};

	for (int32 Iteration = 0; Iteration < FMath::Max(Subdivisions, 1); ++Iteration)
	{
		TMap<uint64, int32> MidCache;
		TArray<FIntVector> NewTris;
		NewTris.Reserve(Tris.Num() * 4);

		auto GetMid = [&](int32 A, int32 B) -> int32
		{
			const uint64 Key = (static_cast<uint64>(FMath::Min(A, B)) << 32) | static_cast<uint64>(FMath::Max(A, B));
			if (const int32* Found = MidCache.Find(Key))
			{
				return *Found;
			}
			const FVector Mid = ((Verts[A] + Verts[B]) * 0.5f).GetSafeNormal();
			const int32 Index = Verts.Add(Mid);
			MidCache.Add(Key, Index);
			return Index;
		};

		for (const FIntVector& Tri : Tris)
		{
			const int32 A = Tri.X, B = Tri.Y, C = Tri.Z;
			const int32 AB = GetMid(A, B);
			const int32 BC = GetMid(B, C);
			const int32 CA = GetMid(C, A);
			NewTris.Add(FIntVector(A, AB, CA));
			NewTris.Add(FIntVector(B, BC, AB));
			NewTris.Add(FIntVector(C, CA, BC));
			NewTris.Add(FIntVector(AB, BC, CA));
		}
		Tris = MoveTemp(NewTris);
	}

	Cells.SetNum(Verts.Num());
	for (int32 i = 0; i < Verts.Num(); ++i)
	{
		Cells[i].Direction = Verts[i];
	}

	auto Link = [this](int32 A, int32 B)
	{
		Cells[A].Neighbors.AddUnique(B);
		Cells[B].Neighbors.AddUnique(A);
	};
	for (const FIntVector& Tri : Tris)
	{
		Link(Tri.X, Tri.Y);
		Link(Tri.Y, Tri.Z);
		Link(Tri.Z, Tri.X);
	}
}

int32 FPlanetNavGraph::FindCellFromDirection(const FVector& Direction, int32 HintCell) const
{
	if (Cells.Num() == 0)
	{
		return INDEX_NONE;
	}

	int32 Current = Cells.IsValidIndex(HintCell) ? HintCell : 0;
	double Best = FVector::DotProduct(Cells[Current].Direction, Direction);

	bool bMoved = true;
	while (bMoved)
	{
		bMoved = false;
		for (const int32 N : Cells[Current].Neighbors)
		{
			const double D = FVector::DotProduct(Cells[N].Direction, Direction);
			if (D > Best)
			{
				Best = D;
				Current = N;
				bMoved = true;
			}
		}
	}
	return Current;
}

int32 FPlanetNavGraph::FindNearestOpenCell(int32 FromCell) const
{
	if (!Cells.IsValidIndex(FromCell))
	{
		return INDEX_NONE;
	}
	if (!Cells[FromCell].bBlocked)
	{
		return FromCell;
	}

	TArray<bool> Visited;
	Visited.Init(false, Cells.Num());
	TArray<int32> Queue;
	Queue.Add(FromCell);
	Visited[FromCell] = true;

	for (int32 Head = 0; Head < Queue.Num(); ++Head)
	{
		for (const int32 N : Cells[Queue[Head]].Neighbors)
		{
			if (Visited[N])
			{
				continue;
			}
			if (!Cells[N].bBlocked)
			{
				return N;
			}
			Visited[N] = true;
			Queue.Add(N);
		}
	}
	return INDEX_NONE;
}

bool FPlanetNavGraph::FindPath(int32 StartCell, int32 GoalCell, TArray<int32>& OutPath) const
{
	OutPath.Reset();
	if (!Cells.IsValidIndex(StartCell) || !Cells.IsValidIndex(GoalCell) || Cells[GoalCell].bBlocked)
	{
		return false;
	}
	if (StartCell == GoalCell)
	{
		OutPath.Add(GoalCell);
		return true;
	}

	const int32 Num = Cells.Num();
	TArray<float> GScore;
	GScore.Init(TNumericLimits<float>::Max(), Num);
	TArray<int32> Parent;
	Parent.Init(INDEX_NONE, Num);
	TArray<bool> Closed;
	Closed.Init(false, Num);

	struct FNode
	{
		int32 Cell;
		float F;
	};
	auto Pred = [](const FNode& A, const FNode& B) { return A.F < B.F; };
	TArray<FNode> Open;

	const FVector GoalDir = Cells[GoalCell].Direction;
	auto Arc = [](const FVector& A, const FVector& B) -> float
	{
		return FMath::Acos(FMath::Clamp(static_cast<float>(FVector::DotProduct(A, B)), -1.f, 1.f));
	};
	auto Heuristic = [&](int32 Cell) -> float { return Arc(Cells[Cell].Direction, GoalDir); };

	GScore[StartCell] = 0.f;
	Open.HeapPush(FNode{ StartCell, Heuristic(StartCell) }, Pred);

	while (Open.Num() > 0)
	{
		FNode Current;
		Open.HeapPop(Current, Pred);

		if (Closed[Current.Cell])
		{
			continue;
		}
		if (Current.Cell == GoalCell)
		{
			break;
		}
		Closed[Current.Cell] = true;

		const FPlanetCell& C = Cells[Current.Cell];
		for (const int32 N : C.Neighbors)
		{
			const FPlanetCell& NC = Cells[N];
			if (NC.bBlocked || Closed[N])
			{
				continue;
			}

			const float Step = Arc(C.Direction, NC.Direction) * 0.5f * (C.MoveCost + NC.MoveCost);
			const float NewG = GScore[Current.Cell] + Step;
			if (NewG < GScore[N])
			{
				GScore[N] = NewG;
				Parent[N] = Current.Cell;
				Open.HeapPush(FNode{ N, NewG + Heuristic(N) }, Pred);
			}
		}
	}

	if (Parent[GoalCell] == INDEX_NONE)
	{
		return false;
	}

	for (int32 Cell = GoalCell; Cell != INDEX_NONE; Cell = Parent[Cell])
	{
		OutPath.Add(Cell);
	}
	Algo::Reverse(OutPath);
	return true;
}

void FPlanetNavGraph::ApplyTerrain(TFunctionRef<float(const FVector&)> HeightFn,
	float PlanetRadius,
	float SeaLevel,
	float MaxSlope,
	float SlopeCostScale)
{
	for (FPlanetCell& Cell : Cells)
	{
		Cell.Height = HeightFn(Cell.Direction);
	}

	for (FPlanetCell& Cell : Cells)
	{
		float MaxCellSlope = 0.f;
		for (const int32 N : Cell.Neighbors)
		{
			const float Angle = FMath::Acos(FMath::Clamp(static_cast<float>(FVector::DotProduct(Cell.Direction, Cells[N].Direction)), -1.f, 1.f));
			const float Run = Angle * PlanetRadius;
			if (Run > KINDA_SMALL_NUMBER)
			{
				MaxCellSlope = FMath::Max(MaxCellSlope, FMath::Abs(Cells[N].Height - Cell.Height) / Run);
			}
		}

		Cell.bBlocked = (Cell.Height < SeaLevel) || (MaxCellSlope > MaxSlope);
		Cell.MoveCost = 1.f + MaxCellSlope * SlopeCostScale;
	}
}
