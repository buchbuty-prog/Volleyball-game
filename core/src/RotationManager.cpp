#include "vc26/RotationManager.h"

#include <algorithm>

#include "vc26/RoleFit.h"

namespace vc26 {

namespace {

void raise(LineupReport& report, Slot slot, Legality level, std::string reason) {
    auto& current = report.slots[index(slot)];
    current = std::max(current, level);
    report.issues.push_back({slot, level, std::move(reason)});
}

}  // namespace

// ---------------------------------------------------------------------------
// Lineup validation
// ---------------------------------------------------------------------------

Legality LineupReport::overall() const {
    Legality worst = Legality::Legal;
    for (Legality l : slots) worst = std::max(worst, l);
    return worst;
}

bool LineupReport::allowedIn(RuleMode mode) const {
    const Legality worst = overall();
    return mode == RuleMode::Simulation ? worst == Legality::Legal : worst != Legality::Illegal;
}

LineupReport validateLineup(const LineupSpec& spec, bool servingFirst) {
    LineupReport report;
    report.slots.fill(Legality::Legal);

    std::vector<int> seen;
    for (int i = 0; i < kSlots; ++i) {
        const Slot slot = slotAt(i);
        const Player& p = spec.starters[i];
        if (!p.valid()) {
            raise(report, slot, Legality::Illegal, "slot is empty");
            continue;
        }
        if (std::find(seen.begin(), seen.end(), p.id) != seen.end()) {
            raise(report, slot, Legality::Illegal, "player is already on court");
            continue;
        }
        seen.push_back(p.id);
        if (p.role == Role::Libero) {
            raise(report, slot, Legality::ArcadeOnly, "registered libero playing as a regular player");
        }
    }

    if (spec.liberoStartSlot && !spec.libero) {
        raise(report, *spec.liberoStartSlot, Legality::Illegal, "no libero designated");
    }

    if (spec.libero) {
        const Slot anchor = spec.liberoStartSlot.value_or(Slot::P1);
        if (!spec.libero->valid()) {
            raise(report, anchor, Legality::Illegal, "libero is not a valid player");
        } else if (std::find(seen.begin(), seen.end(), spec.libero->id) != seen.end()) {
            raise(report, anchor, Legality::Illegal, "libero is also listed as a starter");
        }
        if (spec.liberoStartSlot) {
            const Slot s = *spec.liberoStartSlot;
            if (isFrontRow(s)) {
                raise(report, s, Legality::ArcadeOnly, "libero may only replace back-row players");
            } else if (s == Slot::P1 && servingFirst) {
                raise(report, s, Legality::ArcadeOnly, "libero may not serve");
            }
        }
    }

    return report;
}

// ---------------------------------------------------------------------------
// Serve overlap (FIVB 7.4.3)
// ---------------------------------------------------------------------------

std::vector<OverlapFault> checkOverlap(const std::array<CourtPos, kSlots>& pos, bool teamIsServing) {
    std::vector<OverlapFault> faults;
    const auto& at = [&](Slot s) { return pos[index(s)]; };
    const auto exempt = [&](Slot s) { return teamIsServing && s == Slot::P1; };

    struct Pair {
        Slot a, b;
        bool lateral;  // true: a left of b; false: a closer to the net than b
    };
    static const Pair kPairs[] = {
        {Slot::P4, Slot::P3, true},  {Slot::P3, Slot::P2, true},   // front row, left to right
        {Slot::P5, Slot::P6, true},  {Slot::P6, Slot::P1, true},   // back row, left to right
        {Slot::P4, Slot::P5, false}, {Slot::P3, Slot::P6, false},  // front player ahead of back player
        {Slot::P2, Slot::P1, false},
    };

    for (const Pair& pr : kPairs) {
        if (exempt(pr.a) || exempt(pr.b)) continue;
        const bool ok = pr.lateral ? at(pr.a).x < at(pr.b).x : at(pr.a).y < at(pr.b).y;
        if (!ok) {
            std::string reason = std::string(toString(pr.a)) + (pr.lateral ? " must be left of " : " must be in front of ") +
                                 toString(pr.b);
            faults.push_back({pr.a, pr.b, std::move(reason)});
        }
    }
    return faults;
}

// ---------------------------------------------------------------------------
// RotationManager
// ---------------------------------------------------------------------------

std::optional<RotationManager> RotationManager::create(const LineupSpec& spec, RuleMode mode, bool servingFirst,
                                                       LineupReport* report) {
    LineupReport r = validateLineup(spec, servingFirst);
    const bool allowed = r.allowedIn(mode);
    if (report) *report = std::move(r);
    if (!allowed) return std::nullopt;

    RotationManager rm;
    rm.court_ = spec.starters;
    rm.mode_ = mode;
    rm.serving_ = servingFirst;
    rm.libero_ = spec.libero;

    Slot setter = Slot::P1;
    if (spec.setterSlot) {
        setter = *spec.setterSlot;
    } else {
        for (int i = 0; i < kSlots; ++i) {
            if (spec.starters[i].role == Role::Setter) {
                setter = slotAt(i);
                break;
            }
        }
    }
    for (int i = 0; i < kSlots; ++i) {
        const Player& p = spec.starters[i];
        rm.duties_.push_back({p.id, p.role, fiveOneDuty(slotAt(i), setter)});
    }

    if (spec.libero) {
        const Role liberoDuty = (mode == RuleMode::Arcade && spec.liberoStartSlot)
                                    ? fiveOneDuty(*spec.liberoStartSlot, setter)
                                    : Role::Libero;
        rm.duties_.push_back({spec.libero->id, spec.libero->role, liberoDuty});

        rm.liberoTargets_ = spec.liberoTargets;
        if (spec.liberoStartSlot) {
            Player& replaced = rm.court_[index(*spec.liberoStartSlot)];
            if (rm.liberoTargets_.empty()) rm.liberoTargets_.push_back(replaced.id);
            rm.liberoReplaced_ = replaced;
            replaced = *spec.libero;
        }
    }
    return rm;
}

RallyResult RotationManager::onRallyEnd(bool wonByUs) {
    RallyResult result;
    if (wonByUs && !serving_) {
        rotate();
        serving_ = true;
        result.sideOut = true;
    } else if (!wonByUs) {
        serving_ = false;
    }
    // Arcade keeps the libero wherever the player put them (house rules).
    if (mode_ == RuleMode::Simulation) result.liberoSwap = applyLiberoRules();
    return result;
}

void RotationManager::rotate() {
    // Clockwise: P2 -> P1, P3 -> P2, ..., P1 -> P6.
    Court next;
    for (int i = 0; i < kSlots; ++i) next[i] = court_[index(slotAt(i + 1))];
    court_ = next;
}

std::optional<LiberoSwap> RotationManager::applyLiberoRules() {
    if (!libero_) return std::nullopt;

    // At most one libero replacement per dead ball, which also satisfies the
    // "completed rally between two libero replacements" rule.
    if (const auto ls = slotOf(libero_->id)) {
        const bool mustLeave = isFrontRow(*ls) || (serving_ && *ls == Slot::P1);
        if (!mustLeave || !liberoReplaced_) return std::nullopt;
        const Player back = *liberoReplaced_;
        court_[index(*ls)] = back;
        liberoReplaced_.reset();
        return LiberoSwap{*ls, libero_->id, back.id, false};
    }

    for (int id : liberoTargets_) {
        const auto s = slotOf(id);
        if (!s || isFrontRow(*s) || (serving_ && *s == Slot::P1)) continue;
        liberoReplaced_ = court_[index(*s)];
        court_[index(*s)] = *libero_;
        return LiberoSwap{*s, libero_->id, id, true};
    }
    return std::nullopt;
}

std::optional<Slot> RotationManager::slotOf(int playerId) const {
    for (int i = 0; i < kSlots; ++i) {
        if (court_[i].id == playerId) return slotAt(i);
    }
    return std::nullopt;
}

std::optional<Role> RotationManager::dutyOf(int playerId) const {
    for (const Duty& d : duties_) {
        if (d.playerId == playerId) return d.duty;
    }
    return std::nullopt;
}

std::optional<Role> RotationManager::nativeRoleOf(int playerId) const {
    for (const Duty& d : duties_) {
        if (d.playerId == playerId) return d.native;
    }
    return std::nullopt;
}

std::optional<float> RotationManager::roleFitOf(int playerId) const {
    if (!slotOf(playerId)) return std::nullopt;
    const auto duty = dutyOf(playerId);
    const auto native = nativeRoleOf(playerId);
    if (!duty || !native) return std::nullopt;
    return roleFit(*native, *duty);
}

}  // namespace vc26
