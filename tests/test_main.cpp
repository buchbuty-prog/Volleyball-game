// Minimal self-contained test runner (no third-party dependencies).
#include <cmath>
#include <cstdio>
#include <functional>
#include <vector>

#include "vc26/MatchScore.h"
#include "vc26/RoleFit.h"
#include "vc26/RotationManager.h"

using namespace vc26;

namespace {

int g_failures = 0;
std::vector<std::pair<const char*, std::function<void()>>>& registry() {
    static std::vector<std::pair<const char*, std::function<void()>>> r;
    return r;
}
struct Register {
    Register(const char* name, std::function<void()> fn) { registry().emplace_back(name, std::move(fn)); }
};

#define TEST(name)                                  \
    static void name();                             \
    static Register reg_##name(#name, name);        \
    static void name()

#define CHECK(cond)                                                            \
    do {                                                                       \
        if (!(cond)) {                                                         \
            std::printf("  FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);      \
            ++g_failures;                                                      \
        }                                                                      \
    } while (0)

// Standard 5-1, setter in P1: S, OH, MB, OPP, OH, MB.
constexpr int S = 1, OH1 = 2, MB1 = 3, OPP = 4, OH2 = 5, MB2 = 6, LIB = 7;

LineupSpec standardLineup() {
    LineupSpec spec;
    spec.starters = {Player{S, Role::Setter},   Player{OH1, Role::OutsideHitter}, Player{MB1, Role::MiddleBlocker},
                     Player{OPP, Role::Opposite}, Player{OH2, Role::OutsideHitter}, Player{MB2, Role::MiddleBlocker}};
    return spec;
}

std::array<CourtPos, kSlots> legalFormation() {
    // index = slot (P1..P6); x from left sideline, y from centre line.
    return {CourtPos{8.f, 7.f}, CourtPos{7.f, 2.f}, CourtPos{4.5f, 2.f},
            CourtPos{2.f, 2.f}, CourtPos{1.5f, 7.f}, CourtPos{4.5f, 7.f}};
}

}  // namespace

// --- Lineup legality --------------------------------------------------------

TEST(standard_lineup_is_green) {
    const LineupReport r = validateLineup(standardLineup(), true);
    CHECK(r.overall() == Legality::Legal);
    CHECK(r.allowedIn(RuleMode::Simulation));
    CHECK(r.issues.empty());
}

TEST(empty_or_duplicate_slot_is_red) {
    LineupSpec spec = standardLineup();
    spec.starters[index(Slot::P3)] = Player{};
    spec.starters[index(Slot::P5)] = Player{S, Role::Setter};
    const LineupReport r = validateLineup(spec, true);
    CHECK(r.slots[index(Slot::P3)] == Legality::Illegal);
    CHECK(r.slots[index(Slot::P5)] == Legality::Illegal);
    CHECK(!r.allowedIn(RuleMode::Arcade));
}

TEST(libero_in_front_row_is_amber) {
    LineupSpec spec = standardLineup();
    spec.libero = Player{LIB, Role::Libero};
    spec.liberoStartSlot = Slot::P2;
    const LineupReport r = validateLineup(spec, false);
    CHECK(r.slots[index(Slot::P2)] == Legality::ArcadeOnly);
    CHECK(!r.allowedIn(RuleMode::Simulation));
    CHECK(r.allowedIn(RuleMode::Arcade));
    CHECK(!RotationManager::create(spec, RuleMode::Simulation, false));
    CHECK(RotationManager::create(spec, RuleMode::Arcade, false));
}

TEST(libero_cannot_start_serving) {
    LineupSpec spec = standardLineup();
    spec.libero = Player{LIB, Role::Libero};
    spec.liberoStartSlot = Slot::P1;
    CHECK(validateLineup(spec, true).slots[index(Slot::P1)] == Legality::ArcadeOnly);
    CHECK(validateLineup(spec, false).slots[index(Slot::P1)] == Legality::Legal);
}

TEST(registered_libero_as_starter_is_amber) {
    LineupSpec spec = standardLineup();
    spec.starters[index(Slot::P2)] = Player{LIB, Role::Libero};
    CHECK(validateLineup(spec, true).slots[index(Slot::P2)] == Legality::ArcadeOnly);
}

// --- Rotation ---------------------------------------------------------------

TEST(rotates_clockwise_only_on_side_out) {
    auto rm = RotationManager::create(standardLineup(), RuleMode::Simulation, false);
    CHECK(rm.has_value());

    RallyResult r = rm->onRallyEnd(false);  // opponent holds serve
    CHECK(!r.sideOut);
    CHECK(rm->at(Slot::P1).id == S);

    r = rm->onRallyEnd(true);  // side-out
    CHECK(r.sideOut);
    CHECK(rm->serving());
    CHECK(rm->at(Slot::P1).id == OH1);
    CHECK(rm->at(Slot::P6).id == S);
    CHECK(rm->at(Slot::P4).id == OH2);

    r = rm->onRallyEnd(true);  // point on own serve: no rotation
    CHECK(!r.sideOut);
    CHECK(rm->at(Slot::P1).id == OH1);
}

TEST(six_side_outs_return_to_start) {
    auto rm = RotationManager::create(standardLineup(), RuleMode::Simulation, false);
    const Court start = rm->court();
    for (int i = 0; i < kSlots; ++i) {
        rm->onRallyEnd(true);
        rm->onRallyEnd(false);
    }
    for (int i = 0; i < kSlots; ++i) CHECK(rm->court()[i].id == start[i].id);
}

// --- Libero automation (Simulation) -----------------------------------------

TEST(libero_follows_both_middles) {
    LineupSpec spec = standardLineup();
    spec.libero = Player{LIB, Role::Libero};
    spec.liberoStartSlot = Slot::P6;  // replaces MB2
    spec.liberoTargets = {MB1, MB2};
    auto rm = RotationManager::create(spec, RuleMode::Simulation, false);
    CHECK(rm.has_value());
    CHECK(rm->at(Slot::P6).id == LIB);

    int liberoOnCourtRallies = 0;
    for (int i = 0; i < 24; ++i) {
        const bool win = i % 2 == 0;  // alternate side-outs
        rm->onRallyEnd(win);
        const auto ls = rm->slotOf(LIB);
        if (ls) {
            ++liberoOnCourtRallies;
            CHECK(!isFrontRow(*ls));
            CHECK(!(rm->serving() && *ls == Slot::P1));
            CHECK(!rm->slotOf(MB1) || !rm->slotOf(MB2));  // libero is replacing one of them
        }
        int count = 0;
        for (const Player& p : rm->court()) count += p.valid() ? 1 : 0;
        CHECK(count == kSlots);
    }
    CHECK(liberoOnCourtRallies > 0);
}

TEST(libero_exits_when_rotating_to_front) {
    LineupSpec spec = standardLineup();
    spec.libero = Player{LIB, Role::Libero};
    spec.liberoStartSlot = Slot::P5;  // replaces OH2, next stop P4
    auto rm = RotationManager::create(spec, RuleMode::Simulation, false);
    const RallyResult r = rm->onRallyEnd(true);
    CHECK(r.liberoSwap.has_value());
    CHECK(!r.liberoSwap->entering);
    CHECK(r.liberoSwap->slot == Slot::P4);
    CHECK(rm->at(Slot::P4).id == OH2);
    CHECK(!rm->slotOf(LIB));
}

TEST(arcade_libero_stays_in_rotation) {
    LineupSpec spec = standardLineup();
    spec.libero = Player{LIB, Role::Libero};
    spec.liberoStartSlot = Slot::P2;  // libero as a right-side hitter
    auto rm = RotationManager::create(spec, RuleMode::Arcade, false);
    CHECK(rm.has_value());
    rm->onRallyEnd(true);
    CHECK(rm->at(Slot::P1).id == LIB);  // and serves
    CHECK(!rm->slotOf(OH1));
    CHECK(rm->dutyOf(LIB) == Role::OutsideHitter);
    const auto fit = rm->roleFitOf(LIB);
    CHECK(fit && std::fabs(*fit - roleFit(Role::Libero, Role::OutsideHitter)) < 1e-6f);
}

// --- Role-Fit ---------------------------------------------------------------

TEST(role_fit_bounds_and_native) {
    for (int n = 0; n < 5; ++n) {
        for (int d = 0; d < 5; ++d) {
            const float f = roleFit(static_cast<Role>(n), static_cast<Role>(d));
            CHECK(f >= kMinRoleFit && f <= kMaxRoleFit);
            if (n == d) CHECK(f == 1.0f);
        }
    }
    CHECK(roleFitPercent(Role::Libero, Role::MiddleBlocker) == 60);
}

TEST(five_one_duties_follow_setter) {
    CHECK(fiveOneDuty(Slot::P1, Slot::P1) == Role::Setter);
    CHECK(fiveOneDuty(Slot::P4, Slot::P1) == Role::Opposite);
    CHECK(fiveOneDuty(Slot::P3, Slot::P3) == Role::Setter);
    CHECK(fiveOneDuty(Slot::P6, Slot::P3) == Role::Opposite);
    CHECK(fiveOneDuty(Slot::P2, Slot::P3) == Role::MiddleBlocker);
}

TEST(off_role_player_gets_fit_penalty) {
    LineupSpec spec = standardLineup();
    spec.starters[index(Slot::P4)] = Player{OPP, Role::MiddleBlocker};  // a middle playing opposite
    auto rm = RotationManager::create(spec, RuleMode::Simulation, true);
    CHECK(rm->dutyOf(OPP) == Role::Opposite);
    CHECK(rm->roleFitOf(OPP) == roleFit(Role::MiddleBlocker, Role::Opposite));
    CHECK(rm->roleFitOf(S) == 1.0f);
    CHECK(!rm->roleFitOf(LIB));
}

// --- Overlap ----------------------------------------------------------------

TEST(legal_formation_has_no_faults) {
    CHECK(checkOverlap(legalFormation(), false).empty());
    CHECK(checkOverlap(legalFormation(), true).empty());
}

TEST(front_back_overlap_detected) {
    auto pos = legalFormation();
    pos[index(Slot::P6)].y = 1.5f;  // middle back ahead of middle front
    const auto faults = checkOverlap(pos, false);
    CHECK(faults.size() == 1);
    CHECK(faults[0].a == Slot::P3 && faults[0].b == Slot::P6);
}

TEST(server_is_exempt) {
    auto pos = legalFormation();
    pos[index(Slot::P1)] = CourtPos{0.5f, 1.0f};  // anywhere
    CHECK(checkOverlap(pos, true).empty());
    CHECK(checkOverlap(pos, false).size() == 2);  // receiving P1 is not exempt
}

// --- Systems ----------------------------------------------------------------

TEST(six_two_has_two_setters) {
    CHECK(systemDuty(System::SixTwo, Slot::P1, Slot::P1) == Role::Setter);
    CHECK(systemDuty(System::SixTwo, Slot::P4, Slot::P1) == Role::Setter);
    CHECK(systemDuty(System::FourTwo, Slot::P6, Slot::P3) == Role::Setter);
    CHECK(systemDuty(System::SixTwo, Slot::P3, Slot::P1) == Role::MiddleBlocker);
    for (int i = 0; i < kSlots; ++i) {
        CHECK(systemDuty(System::FiveOne, slotAt(i), Slot::P2) == fiveOneDuty(slotAt(i), Slot::P2));
    }
}

TEST(six_two_second_setter_has_full_fit) {
    LineupSpec spec = standardLineup();
    spec.system = System::SixTwo;
    spec.starters[index(Slot::P4)] = Player{OPP, Role::Setter};
    auto rm = RotationManager::create(spec, RuleMode::Simulation, true);
    CHECK(rm->dutyOf(OPP) == Role::Setter);
    CHECK(rm->roleFitOf(OPP) == 1.0f);
    CHECK(rm->dutyOf(S) == Role::Setter);  // first Setter starter anchors the system
}

// --- Match scoring ----------------------------------------------------------

namespace {
void score(MatchScore& m, Team t, int n) {
    for (int i = 0; i < n; ++i) m.awardPoint(t);
}
}  // namespace

TEST(indoor_set_needs_25_and_two_clear) {
    MatchScore m;
    score(m, Team::Home, 24);
    CHECK(m.setPointFor() == Team::Home);
    score(m, Team::Away, 24);
    CHECK(!m.setPointFor());  // 24-24
    PointResult r = m.awardPoint(Team::Home);
    CHECK(!r.setWon && r.setPoint);  // 25-24 is not enough
    r = m.awardPoint(Team::Home);
    CHECK(r.setWon && !r.matchWon);
    CHECK(m.sets(Team::Home) == 1 && m.points(Team::Home) == 0 && m.setNumber() == 2);
}

TEST(indoor_deciding_set_to_15_with_switch_at_8) {
    MatchScore m;
    for (int s = 0; s < 2; ++s) {
        score(m, Team::Home, 25);
        score(m, Team::Away, 25);
    }
    CHECK(m.isDecidingSet() && m.setNumber() == 5);
    int switches = 0;
    for (int i = 0; i < 7; ++i) switches += m.awardPoint(Team::Away).switchSides;
    CHECK(switches == 0);
    CHECK(m.awardPoint(Team::Away).switchSides);  // 0-8
    CHECK(!m.awardPoint(Team::Home).switchSides);
    score(m, Team::Away, 5);  // 1-13
    CHECK(!m.isMatchPoint());
    PointResult r = m.awardPoint(Team::Away);  // 1-14
    CHECK(r.matchPoint && m.isMatchPoint());
    r = m.awardPoint(Team::Away);
    CHECK(r.matchWon && m.winner() == Team::Away && m.finished());
    CHECK(!m.awardPoint(Team::Home).setWon);  // ignored after the match
    CHECK(m.sets(Team::Away) == 3);
}

TEST(beach_best_of_three_switches_every_seven) {
    MatchScore m(MatchRules::beach());
    int switches = 0;
    for (int i = 0; i < 14; ++i) switches += m.awardPoint(i % 2 ? Team::Home : Team::Away).switchSides;
    CHECK(switches == 2);  // at 7 and 14 combined points
    score(m, Team::Home, 14);  // 21-7
    CHECK(m.sets(Team::Home) == 1);
    score(m, Team::Home, 20);
    CHECK(m.isMatchPoint());
    CHECK(m.awardPoint(Team::Home).matchWon);
}

int main() {
    for (auto& [name, fn] : registry()) {
        const int before = g_failures;
        fn();
        std::printf("%s %s\n", g_failures == before ? "PASS" : "FAIL", name);
    }
    std::printf("%zu tests, %d failed checks\n", registry().size(), g_failures);
    return g_failures == 0 ? 0 : 1;
}
