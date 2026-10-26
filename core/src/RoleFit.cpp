#include "vc26/RoleFit.h"

#include <algorithm>
#include <cmath>

namespace vc26 {

namespace {

// Rows: native role. Columns: duty. Order matches the Role enum.
// Starting values for balancing; tune from Role-Fit telemetry (GDD 6).
constexpr float kFit[5][5] = {
    //  S      OH     MB     OPP    L
    {1.00f, 0.75f, 0.70f, 0.80f, 0.80f},  // Setter
    {0.70f, 1.00f, 0.80f, 0.90f, 0.85f},  // Outside hitter
    {0.65f, 0.80f, 1.00f, 0.80f, 0.65f},  // Middle blocker
    {0.70f, 0.90f, 0.80f, 1.00f, 0.70f},  // Opposite
    {0.75f, 0.60f, 0.60f, 0.60f, 1.00f},  // Libero
};

constexpr Role kFiveOneOrder[kSlots] = {
    Role::Setter,        Role::OutsideHitter, Role::MiddleBlocker,
    Role::Opposite,      Role::OutsideHitter, Role::MiddleBlocker,
};

}  // namespace

float roleFit(Role native, Role duty) {
    const float f = kFit[static_cast<int>(native)][static_cast<int>(duty)];
    return std::clamp(f, kMinRoleFit, kMaxRoleFit);
}

Role fiveOneDuty(Slot slot, Slot setterSlot) {
    const int offset = index(slotAt(index(slot) - index(setterSlot)));
    return kFiveOneOrder[offset];
}

int roleFitPercent(Role native, Role duty) {
    return static_cast<int>(std::lround(roleFit(native, duty) * 100.0f));
}

}  // namespace vc26
