#include "VC26Types.h"

#include "vc26/Types.h"

static_assert(static_cast<int>(EVC26Role::Libero) == static_cast<int>(vc26::Role::Libero), "EVC26Role out of sync");
static_assert(static_cast<int>(EVC26Slot::P6) == static_cast<int>(vc26::Slot::P6), "EVC26Slot out of sync");
static_assert(static_cast<int>(EVC26RuleMode::Arcade) == static_cast<int>(vc26::RuleMode::Arcade), "EVC26RuleMode out of sync");
static_assert(static_cast<int>(EVC26Legality::Illegal) == static_cast<int>(vc26::Legality::Illegal), "EVC26Legality out of sync");
static_assert(static_cast<int>(EVC26System::FourTwo) == static_cast<int>(vc26::System::FourTwo), "EVC26System out of sync");
