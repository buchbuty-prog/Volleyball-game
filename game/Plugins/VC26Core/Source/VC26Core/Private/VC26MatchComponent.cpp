#include "VC26MatchComponent.h"

void UVC26MatchComponent::StartMatch(bool bBeach)
{
	Score = vc26::MatchScore(bBeach ? vc26::MatchRules::beach() : vc26::MatchRules::indoor());
}

void UVC26MatchComponent::AwardPoint(EVC26Team Scorer)
{
	const vc26::PointResult Result = Score.awardPoint(ToCore(Scorer));
	if (Result.matchWon)
	{
		OnSetWon.Broadcast(Scorer);
		OnMatchWon.Broadcast(Scorer);
		return;
	}
	if (Result.setWon)
	{
		OnSetWon.Broadcast(Scorer);
		return;
	}
	if (Result.switchSides)
	{
		OnSwitchSides.Broadcast();
	}
	if (const auto Leader = Score.setPointFor())
	{
		(Result.matchPoint ? OnMatchPoint : OnSetPoint).Broadcast(FromCore(*Leader));
	}
}
