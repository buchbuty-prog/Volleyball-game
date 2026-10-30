#include "VC26TeamComponent.h"

namespace
{
vc26::Player ToCore(const FVC26Player& P)
{
	return vc26::Player{P.Id, static_cast<vc26::Role>(P.Role)};
}

vc26::LineupSpec ToCore(const FVC26Lineup& L)
{
	vc26::LineupSpec Spec;
	for (int32 i = 0; i < vc26::kSlots && i < L.Starters.Num(); ++i)
	{
		Spec.starters[i] = ToCore(L.Starters[i]);
	}
	if (L.bHasLibero)
	{
		Spec.libero = ToCore(L.Libero);
		if (L.bLiberoStarts)
		{
			Spec.liberoStartSlot = static_cast<vc26::Slot>(L.LiberoStartSlot);
		}
		Spec.liberoTargets.assign(L.LiberoTargets.GetData(), L.LiberoTargets.GetData() + L.LiberoTargets.Num());
	}
	Spec.system = static_cast<vc26::System>(L.System);
	return Spec;
}
}  // namespace

EVC26Legality UVC26TeamComponent::ValidateLineup(const FVC26Lineup& Lineup, bool bServingFirst, TArray<EVC26Legality>& OutSlotColors, TArray<FString>& OutIssues)
{
	const vc26::LineupReport Report = vc26::validateLineup(ToCore(Lineup), bServingFirst);
	OutSlotColors.Reset(vc26::kSlots);
	for (vc26::Legality L : Report.slots)
	{
		OutSlotColors.Add(static_cast<EVC26Legality>(L));
	}
	OutIssues.Reset(Report.issues.size());
	for (const vc26::LineupIssue& Issue : Report.issues)
	{
		OutIssues.Add(FString::Printf(TEXT("%s: %s"), UTF8_TO_TCHAR(vc26::toString(Issue.slot)), UTF8_TO_TCHAR(Issue.reason.c_str())));
	}
	return static_cast<EVC26Legality>(Report.overall());
}

bool UVC26TeamComponent::StartSet(const FVC26Lineup& Lineup, EVC26RuleMode Mode, bool bServingFirst)
{
	auto Created = vc26::RotationManager::create(ToCore(Lineup), static_cast<vc26::RuleMode>(Mode), bServingFirst);
	if (!Created)
	{
		return false;
	}
	Rotation = std::move(Created);
	return true;
}

void UVC26TeamComponent::RallyEnd(bool bWonByUs)
{
	if (!Rotation)
	{
		return;
	}
	const vc26::RallyResult Result = Rotation->onRallyEnd(bWonByUs);
	if (Result.sideOut)
	{
		OnSideOut.Broadcast();
	}
	if (Result.liberoSwap)
	{
		const vc26::LiberoSwap& Swap = *Result.liberoSwap;
		OnLiberoSwap.Broadcast(static_cast<EVC26Slot>(Swap.slot), Swap.liberoId, Swap.otherId, Swap.entering);
	}
}

bool UVC26TeamComponent::IsServing() const
{
	return Rotation && Rotation->serving();
}

int32 UVC26TeamComponent::GetPlayerAt(EVC26Slot Slot) const
{
	return Rotation ? Rotation->at(static_cast<vc26::Slot>(Slot)).id : -1;
}

bool UVC26TeamComponent::GetSlotOf(int32 PlayerId, EVC26Slot& OutSlot) const
{
	if (!Rotation)
	{
		return false;
	}
	const auto Slot = Rotation->slotOf(PlayerId);
	if (!Slot)
	{
		return false;
	}
	OutSlot = static_cast<EVC26Slot>(*Slot);
	return true;
}

float UVC26TeamComponent::GetRoleFit(int32 PlayerId) const
{
	return Rotation ? Rotation->roleFitOf(PlayerId).value_or(1.0f) : 1.0f;
}
