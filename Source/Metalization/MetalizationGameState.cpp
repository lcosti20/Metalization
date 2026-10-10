#include "MetalizationGameState.h"

AMetalizationGameState::AMetalizationGameState()
{
	RegisterTeam(0, 250.f);
	RegisterTeam(1, 200.f);
}

void AMetalizationGameState::RegisterTeam(int32 TeamId, float StartingEnergy)
{
	if (!TeamEnergy.Contains(TeamId))
	{
		TeamEnergy.Add(TeamId, StartingEnergy);
	}
	if (!TeamUnitCounts.Contains(TeamId))
	{
		TeamUnitCounts.Add(TeamId, 0);
	}
}

void AMetalizationGameState::AddTeamEnergy(int32 TeamId, float Amount)
{
	RegisterTeam(TeamId, 0.f);
	TeamEnergy.FindOrAdd(TeamId) += Amount;
}

void AMetalizationGameState::SpendTeamEnergy(int32 TeamId, float Amount)
{
	RegisterTeam(TeamId, 0.f);
	TeamEnergy.FindOrAdd(TeamId) = FMath::Max(0.f, TeamEnergy[TeamId] - Amount);
}

float AMetalizationGameState::GetTeamEnergy(int32 TeamId) const
{
	return TeamEnergy.Contains(TeamId) ? TeamEnergy[TeamId] : 0.f;
}

void AMetalizationGameState::SetTeamEnergy(int32 TeamId, float NewEnergy)
{
	RegisterTeam(TeamId, NewEnergy);
	TeamEnergy[TeamId] = NewEnergy;
}

void AMetalizationGameState::SetTeamUnitCount(int32 TeamId, int32 NewCount)
{
	RegisterTeam(TeamId, 0.f);
	TeamUnitCounts[TeamId] = FMath::Max(0, NewCount);
}

int32 AMetalizationGameState::GetTeamUnitCount(int32 TeamId) const
{
	return TeamUnitCounts.Contains(TeamId) ? TeamUnitCounts[TeamId] : 0;
}

void AMetalizationGameState::IncrementUnitCount(int32 TeamId, int32 Delta)
{
	RegisterTeam(TeamId, 0.f);
	TeamUnitCounts[TeamId] = FMath::Max(0, TeamUnitCounts[TeamId] + Delta);
}

bool AMetalizationGameState::HasTeam(int32 TeamId) const
{
	return TeamEnergy.Contains(TeamId) || TeamUnitCounts.Contains(TeamId);
}
