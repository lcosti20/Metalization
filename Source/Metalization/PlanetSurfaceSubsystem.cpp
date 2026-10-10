#include "PlanetSurfaceSubsystem.h"

#include "Async/Async.h"
#include "GameFramework/Actor.h"

void UPlanetSurfaceSubsystem::RegisterPlanet(AActor* InPlanet, float InRadius, int32 NavSubdivisions)
{
	if (!InPlanet)
	{
		return;
	}

	Planet = InPlanet;
	Center = InPlanet->GetActorLocation();
	Radius = FMath::Max(InRadius, 1.f);

	TSharedPtr<FPlanetNavGraph, ESPMode::ThreadSafe> NewGraph = MakeShared<FPlanetNavGraph, ESPMode::ThreadSafe>();
	NewGraph->Build(FMath::Clamp(NavSubdivisions, 1, 8));
	NavGraph = NewGraph;
	LastCellHint = 0;
}

FVector UPlanetSurfaceSubsystem::DirectionFromWorld(const FVector& WorldPos) const
{
	const FVector Dir = (WorldPos - Center).GetSafeNormal();
	return Dir.IsNearlyZero() ? FVector::UpVector : Dir;
}

float UPlanetSurfaceSubsystem::GetSurfaceHeight(const FVector& Direction) const
{
	return 0.f;
}

FVector UPlanetSurfaceSubsystem::SurfacePoint(const FVector& Direction, float Offset) const
{
	return Center + Direction * (Radius + GetSurfaceHeight(Direction) + Offset);
}

bool UPlanetSurfaceSubsystem::StepToward(FVector& InOutDir, const FVector& TargetDir, float ArcDistance) const
{
	const double Dot = FMath::Clamp(FVector::DotProduct(InOutDir, TargetDir), -1.0, 1.0);
	const double Angle = FMath::Acos(Dot);
	const double StepAngle = ArcDistance / Radius;

	if (StepAngle >= Angle)
	{
		InOutDir = TargetDir;
		return true;
	}

	FVector Axis = FVector::CrossProduct(InOutDir, TargetDir).GetSafeNormal();
	if (Axis.IsNearlyZero())
	{
		Axis = FVector::CrossProduct(InOutDir, FVector::ForwardVector).GetSafeNormal();
		if (Axis.IsNearlyZero())
		{
			Axis = FVector::CrossProduct(InOutDir, FVector::RightVector).GetSafeNormal();
		}
	}

	InOutDir = FQuat(Axis, StepAngle).RotateVector(InOutDir).GetSafeNormal();
	return false;
}

int32 UPlanetSurfaceSubsystem::CellFromDirection(const FVector& Direction)
{
	if (!NavGraph.IsValid())
	{
		return INDEX_NONE;
	}
	LastCellHint = NavGraph->FindCellFromDirection(Direction, LastCellHint);
	return LastCellHint;
}

void UPlanetSurfaceSubsystem::RequestPath(const FVector& FromDir, const FVector& ToDir, TFunction<void(TArray<FVector>)> OnComplete)
{
	if (!NavGraph.IsValid())
	{
		OnComplete(TArray<FVector>());
		return;
	}

	const int32 StartCell = CellFromDirection(FromDir);
	int32 GoalCell = CellFromDirection(ToDir);

	bool bGoalAdjusted = false;
	if (!NavGraph->IsValidIndex(GoalCell) || NavGraph->GetCell(GoalCell).bBlocked)
	{
		GoalCell = NavGraph->FindNearestOpenCell(GoalCell);
		bGoalAdjusted = true;
	}
	if (GoalCell == INDEX_NONE)
	{
		OnComplete(TArray<FVector>());
		return;
	}
	if (!NavGraph->IsValidIndex(StartCell))
	{
		OnComplete(TArray<FVector>());
		return;
	}

	TSharedPtr<FPlanetNavGraph, ESPMode::ThreadSafe> Graph = NavGraph;
	TWeakObjectPtr<UPlanetSurfaceSubsystem> WeakThis(this);

	Async(EAsyncExecution::ThreadPool, [Graph, WeakThis, StartCell, GoalCell, ToDir, bGoalAdjusted, OnComplete]()
	{
		TArray<int32> CellPath;
		TArray<FVector> Waypoints;

		if (Graph->FindPath(StartCell, GoalCell, CellPath))
		{
			for (int32 i = 1; i < CellPath.Num(); ++i)
			{
				Waypoints.Add(Graph->GetCell(CellPath[i]).Direction);
			}

			if (!bGoalAdjusted)
			{
				if (Waypoints.Num() > 0)
				{
					Waypoints.Last() = ToDir;
				}
				else
				{
					Waypoints.Add(ToDir);
				}
			}
		}

		AsyncTask(ENamedThreads::GameThread, [WeakThis, Waypoints = MoveTemp(Waypoints), OnComplete]()
		{
			if (WeakThis.IsValid())
			{
				OnComplete(Waypoints);
			}
		});
	});
}
