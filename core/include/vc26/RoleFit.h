// Role-Fit attribute modifier (GDD 1.7).
//
// P1-P6 are rotational slots, not roles: every player passes through all six
// during a set. What a player actually does (set, hit middle, pass...) is the
// "duty" their starting slot carries in the team's system. Role-Fit compares a
// player's native role with that duty.
#pragma once

#include "vc26/Types.h"

namespace vc26 {

constexpr float kMinRoleFit = 0.6f;
constexpr float kMaxRoleFit = 1.0f;

// Multiplier applied to context actions, always within [kMinRoleFit, kMaxRoleFit].
float roleFit(Role native, Role duty);

// Duty carried by `slot` in a 5-1 system whose setter starts in `setterSlot`.
// Order going P1 -> P6 from the setter: S, OH, MB, OPP, OH, MB.
Role fiveOneDuty(Slot slot, Slot setterSlot);

// Rounded percentage for the HUD "Fit %" badge.
int roleFitPercent(Role native, Role duty);

}  // namespace vc26
