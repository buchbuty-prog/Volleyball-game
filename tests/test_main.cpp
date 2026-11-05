// Minimal self-contained test runner (no third-party dependencies).
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <functional>
#include <vector>

#include "vc26/Ball.h"
#include "vc26/Career.h"
#include "vc26/MatchScore.h"
#include "vc26/RallySim.h"
#include "vc26/RoleFit.h"
#include "vc26/ServeFlight.h"
#include "vc26/TouchControls.h"
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

// --- Serve planner (court actor demo) ---------------------------------------

namespace serve_tests {
using serve::kServeSweetSpot;
using serve::landsInOpponentCourt;
using serve::planServe;
using serve::ServeOutcome;
using serve::ServePlan;
using serve::solveArc;
using serve::Trajectory;
using serve::Vec3;

namespace {
bool near(double a, double b, double tol = 1.0) { return std::fabs(a - b) <= tol; }
const Vec3 kHomeHand{-980.0, 300.0, 250.0};
}  // namespace

TEST(arc_reaches_target_and_apex) {
    const Vec3 to{500.0, -200.0, 10.5};
    const Trajectory tr = solveArc(kHomeHand, to, 450.0);
    const double t = *tr.timeDownAt(10.5);
    const Vec3 end = tr.at(t);
    CHECK(near(end.x, to.x) && near(end.y, to.y) && near(end.z, to.z));
    const double tApex = tr.velocity.z / tr.gravity;
    CHECK(near(tr.at(tApex).z, 450.0));
}

TEST(sweet_spot_serve_lands_on_target) {
    const Vec3 target{600.0, -150.0, 0.0};
    const ServePlan s = planServe(kHomeHand, target, kServeSweetSpot);
    CHECK(s.outcome == ServeOutcome::In);
    CHECK(near(s.landing.x, target.x, 2.0) && near(s.landing.y, target.y, 2.0));
    CHECK(s.flightTime > 0.5 && s.flightTime < 3.0);
}

TEST(weak_serve_hits_net) {
    // Just short of the sweet spot clips the tape...
    const ServePlan clip = planServe(kHomeHand, Vec3{500.0, 0.0, 0.0}, 0.5);
    CHECK(clip.outcome == ServeOutcome::Net);
    CHECK(near(clip.landing.x, 0.0));
    // ...and a very weak one never reaches the net.
    const ServePlan s = planServe(kHomeHand, Vec3{300.0, 0.0, 0.0}, 0.2);
    CHECK(s.outcome == ServeOutcome::Net);
    CHECK(s.landing.x < 0.0);
}

TEST(strong_deep_serve_goes_long) {
    const ServePlan s = planServe(kHomeHand, Vec3{820.0, 0.0, 0.0}, 1.0);
    CHECK(s.outcome == ServeOutcome::Out);
    CHECK(s.landing.x > 900.0);
}

TEST(wide_target_is_out) {
    const ServePlan s = planServe(kHomeHand, Vec3{500.0, 520.0, 0.0}, kServeSweetSpot);
    CHECK(s.outcome == ServeOutcome::Out);
}

TEST(away_serve_mirrors) {
    const Vec3 hand{980.0, -300.0, 250.0};
    const ServePlan s = planServe(hand, Vec3{-600.0, 100.0, 0.0}, kServeSweetSpot);
    CHECK(s.outcome == ServeOutcome::In);
    CHECK(s.landing.x < 0.0);
    CHECK(landsInOpponentCourt(Vec3{-900.0, 450.0, 0.0}, false));   // line ball is in
    CHECK(!landsInOpponentCourt(Vec3{-920.0, 0.0, 0.0}, false));
}

}  // namespace serve_tests

// --- Ball physics -----------------------------------------------------------

namespace {
constexpr float kDt = 1.f / 120.f;
bool near(float a, float b, float tol) { return std::fabs(a - b) <= tol; }
}  // namespace

TEST(ball_lands_where_predicted) {
    Ball b{{-8.f, 1.f, 2.5f}, {9.f, -1.f, 6.f}};
    const Vec3 predicted = positionAfter(b, timeToHeight(b, kBallRadius));
    BallStep s;
    for (int i = 0; i < 2000 && !s.landed; ++i) s = stepBall(b, kDt);
    CHECK(s.landed);
    CHECK(near(b.pos.x, predicted.x, 0.01f) && near(b.pos.y, predicted.y, 0.01f));
}

TEST(launch_velocity_reaches_target_on_time) {
    const Vec3 from{-1.f, 0.f, 3.f}, to{6.f, 2.f, kBallRadius};
    Ball b{from, launchVelocity(from, to, 0.6f)};
    const Vec3 p = positionAfter(b, 0.6f);
    CHECK(near(p.x, to.x, 0.001f) && near(p.y, to.y, 0.001f) && near(p.z, to.z, 0.001f));
}

TEST(apex_flight_peaks_at_the_requested_height) {
    const Vec3 from{-6.f, 0.f, 0.9f}, to{-1.5f, 0.f, 2.4f};
    const float t = flightTimeForApex(from, to, 4.3f);
    Ball b{from, launchVelocity(from, to, t)};
    CHECK(near(from.z + b.vel.z * b.vel.z / (2.f * kGravity), 4.3f, 0.01f));
    CHECK(near(positionAfter(b, t).z, to.z, 0.001f));
}

TEST(clearing_flight_time_passes_over_and_into_the_net) {
    const Vec3 from{-9.7f, 0.f, 2.6f}, to{3.f, 0.f, kBallRadius};
    Ball over{from, launchVelocity(from, to, flightTimeClearingNet(from, to, 0.2f))};
    BallStep s;
    bool hit = false;
    for (int i = 0; i < 2000 && !s.landed; ++i) {
        s = stepBall(over, kDt);
        hit = hit || s.hitNet;
    }
    CHECK(!hit && over.pos.x > 0.f);

    Ball into{from, launchVelocity(from, to, flightTimeClearingNet(from, to, -0.6f))};
    s = {};
    hit = false;
    for (int i = 0; i < 2000 && !s.landed; ++i) {
        s = stepBall(into, kDt);
        hit = hit || s.hitNet;
    }
    CHECK(hit && into.pos.x < 0.f);  // stays on the hitter's side
}

TEST(ball_past_the_antenna_is_flagged) {
    Ball b{{-1.f, 5.f, 3.f}, {10.f, 0.f, 0.f}};
    bool outside = false;
    for (int i = 0; i < 60; ++i) outside = stepBall(b, kDt).crossedOutsideAntenna || outside;
    CHECK(outside);
}

TEST(line_balls_are_in) {
    CHECK(isInCourt({9.f, 4.5f, 0.f}));
    CHECK(isInCourt({9.1f, 0.f, 0.f}));
    CHECK(!isInCourt({9.2f, 0.f, 0.f}));
    CHECK(!isInCourt({0.f, 4.7f, 0.f}));
}

// --- Rally simulation -------------------------------------------------------

TEST(timing_windows_grade_early_and_late_alike) {
    CHECK(gradeTiming(0.f) == ContactQuality::Perfect);
    CHECK(gradeTiming(-0.05f) == ContactQuality::Perfect);
    CHECK(gradeTiming(0.1f) == ContactQuality::Good);
    CHECK(gradeTiming(-0.2f) == ContactQuality::Shank);
    CHECK(!gradeTiming(0.4f));
}

namespace {

struct SimTally {
    int points = 0, contacts = 0, longest = 0, aces = 0, kills = 0, faults = 0, homeWins = 0;
    std::vector<int> log;
};

// Plays until the match ends or `maxPoints` rallies have finished.
template <typename Driver>
SimTally playMatch(RallySim& sim, Driver driver, int maxPoints = 100000) {
    SimTally st;
    for (long i = 0; i < 40000000L && sim.phase() != RallyPhase::MatchOver && st.points < maxPoints; ++i) {
        const int contacts = sim.contactsThisRally();
        sim.step(kDt, driver(sim));
        for (const SimEvent& e : sim.takeEvents()) {
            if (e.type != SimEvent::Type::Point) continue;
            ++st.points;
            st.contacts += contacts;
            st.longest = std::max(st.longest, contacts);
            st.homeWins += e.team == Team::Home;
            st.aces += e.reason == PointReason::Ace;
            st.kills += e.reason == PointReason::Kill;
            st.faults += e.reason == PointReason::Out || e.reason == PointReason::BallDown || e.reason == PointReason::Antenna;
            st.log.push_back(static_cast<int>(e.team) * 10 + static_cast<int>(e.reason));
        }
    }
    return st;
}

SimInput noInput(const RallySim&) { return {}; }

// Stands in for a skilled player: runs to the ball, presses on the beat.
struct ScriptedHuman {
    float pressAt = 0.f;  // timeToContact at which to press
    bool held = false;
    float reaction = 0.f;  // seconds before it starts running to a new ball
    float waited = 0.f;
    int rallyTurn = 0;  // varies the attack, as a person would
    Vec3 lastPoint;
    SimInput operator()(const RallySim& sim) {
        SimInput in;
        const Vec3 point = sim.pending().point;
        waited = point.x == lastPoint.x && point.y == lastPoint.y ? waited + kDt : 0.f;
        lastPoint = point;
        if (sim.humanServing()) {
            if (!sim.serveCharging()) {
                held = !held;
                in.action = held;
            } else {
                in.action = sim.serveGauge() < 0.7f;
                held = in.action;
            }
            return in;
        }
        const PendingContact& p = sim.pending();
        if (p.active && p.team == Team::Home) {
            const Vec3 me = sim.players()[p.player].pos;
            const float dx = p.point.x - me.x, dy = p.point.y - me.y, d = std::hypot(dx, dy);
            if (d > 0.1f && p.touch == 1 && waited >= reaction) {
                in.moveX = dx / d;
                in.moveY = dy / d;
            }
            if (p.touch == 2) in.moveY = sim.contactsThisRally() % 4 < 2 ? 1.f : -1.f;  // moves the set around
            if (p.touch == 3) {
                in.moveY = (p.point.y > 0.f ? -1.f : 1.f) * (0.4f + 0.2f * static_cast<float>(sim.score().points(Team::Home) % 4));  // away from the block
                in.moveX = sim.score().points(Team::Away) % 2 ? 0.8f : -0.7f;
            }
            in.action = p.timeToContact - p.pressLead <= pressAt && !held;
            held = in.action;
        } else {
            held = false;
        }
        return in;
    }
};

}  // namespace

TEST(cpu_matches_finish_with_a_legal_result) {
    SimTally total;
    for (std::uint32_t seed = 1; seed <= 30; ++seed) {
        RallySim::Config c;
        c.seed = seed;
        c.humanHome = false;
        RallySim sim(c);
        const SimTally st = playMatch(sim, noInput);
        CHECK(sim.phase() == RallyPhase::MatchOver);
        CHECK(sim.score().finished());
        const Team w = *sim.score().winner();
        CHECK(sim.score().sets(w) == 3 && sim.score().sets(other(w)) < 3);
        total.points += st.points;
        total.contacts += st.contacts;
        total.longest = std::max(total.longest, st.longest);
        total.aces += st.aces;
        total.kills += st.kills;
        total.faults += st.faults;
        total.homeWins += st.homeWins;
    }
    const float avg = static_cast<float>(total.contacts) / static_cast<float>(total.points);
    std::printf("  %d rallies: %.2f contacts avg, longest %d, aces %d, kills %d, faults %d, home won %d\n", total.points,
                static_cast<double>(avg), total.longest, total.aces, total.kills, total.faults, total.homeWins);
    CHECK(avg >= 3.5f && avg <= 12.f);  // real rallies, not serve-and-done
    CHECK(total.longest >= 8);
    CHECK(total.aces < total.points / 5);
    CHECK(total.kills > total.points / 4);
    const float homeShare = static_cast<float>(total.homeWins) / static_cast<float>(total.points);
    CHECK(homeShare > 0.45f && homeShare < 0.55f);  // same skill, no side advantage
}

TEST(same_seed_replays_the_same_match) {
    RallySim::Config c;
    c.seed = 42;
    c.humanHome = false;
    RallySim a(c), b(c);
    CHECK(playMatch(a, noInput).log == playMatch(b, noInput).log);
    c.seed = 43;
    RallySim d(c);
    RallySim e(RallySim::Config{MatchRules::indoor(), 42, false, 0.7f});
    CHECK(playMatch(d, noInput).log != playMatch(e, noInput).log);
}

TEST(idle_human_gives_the_points_away) {
    RallySim::Config c;
    c.seed = 7;
    RallySim sim(c);
    // Serves when it has to, then never touches the ball.
    bool held = false;
    const SimTally st = playMatch(sim, [&held](const RallySim& s) {
        SimInput in;
        if (s.humanServing()) {
            held = !held;
            in.action = held;
        }
        return in;
    }, 200);
    CHECK(sim.score().winner() == Team::Away);
    CHECK(st.homeWins * 5 < st.points);  // only CPU serving errors
}

TEST(well_timed_human_beats_the_cpu) {
    int won = 0;
    for (std::uint32_t seed = 1; seed <= 10; ++seed) {
        RallySim::Config c;
        c.seed = seed;
        RallySim sim(c);
        const SimTally st = playMatch(sim, ScriptedHuman{});
        CHECK(sim.phase() == RallyPhase::MatchOver);
        CHECK(st.longest >= 5);
        won += sim.score().winner() == Team::Home;
    }
    std::printf("  scripted human won %d of 10\n", won);
    CHECK(won >= 7);
}

TEST(late_human_plays_worse_than_on_time_human) {
    int onTime = 0, late = 0, onPoints = 0, latePoints = 0;
    for (std::uint32_t seed = 1; seed <= 10; ++seed) {
        RallySim::Config c;
        c.seed = seed;
        RallySim good(c), bad(c);
        const SimTally g = playMatch(good, ScriptedHuman{0.f}, 150);
        const SimTally b = playMatch(bad, ScriptedHuman{-0.25f}, 150);
        onTime += g.homeWins;
        onPoints += g.points;
        late += b.homeWins;
        latePoints += b.points;
    }
    CHECK(static_cast<float>(onTime) / static_cast<float>(onPoints) >
          static_cast<float>(late) / static_cast<float>(latePoints) + 0.1f);
}

TEST(cpu_blocks_some_attacks_and_serves_score_aces) {
    int attacks = 0, blocks = 0, blockPoints = 0, netTouches = 0, aces = 0, points = 0;
    for (std::uint32_t seed = 1; seed <= 30; ++seed) {
        RallySim::Config c;
        c.seed = seed;
        c.humanHome = false;
        RallySim sim(c);
        for (long i = 0; i < 40000000L && sim.phase() != RallyPhase::MatchOver; ++i) {
            sim.step(kDt, {});
            for (const SimEvent& e : sim.takeEvents()) {
                attacks += e.type == SimEvent::Type::Contact && e.kind == ContactKind::Attack;
                blocks += e.type == SimEvent::Type::Contact && e.kind == ContactKind::Block;
                if (e.type != SimEvent::Type::Point) continue;
                ++points;
                aces += e.reason == PointReason::Ace;
                blockPoints += e.reason == PointReason::Block;
                netTouches += e.reason == PointReason::NetTouch;
            }
        }
    }
    std::printf("  %d attacks, %d blocks, %d block points, %d net touches, %d aces in %d points\n", attacks, blocks,
                blockPoints, netTouches, aces, points);
    CHECK(blocks > attacks / 20 && blocks < attacks / 3);
    CHECK(blockPoints > 0 && blockPoints < blocks);
    CHECK(netTouches > 0 && netTouches < blocks);
    CHECK(aces > points / 50 && aces < points / 6);
}

TEST(a_block_is_not_one_of_the_three_touches) {
    int checked = 0;
    for (std::uint32_t seed = 1; seed <= 5; ++seed) {
        RallySim::Config c;
        c.seed = seed;
        c.humanHome = false;
        RallySim sim(c);
        for (long i = 0; i < 40000000L && sim.phase() != RallyPhase::MatchOver; ++i) {
            sim.step(kDt, {});
            for (const SimEvent& e : sim.takeEvents()) {
                if (e.type != SimEvent::Type::Contact || e.kind != ContactKind::Block || !sim.pending().active) continue;
                CHECK(sim.pending().touch == 1);
                ++checked;
            }
        }
    }
    CHECK(checked > 0);
}

namespace {

// Passes, sets and attacks like ScriptedHuman, and also blocks at `blockAt`.
struct ScriptedBlocker {
    ScriptedHuman play;
    float blockAt = 0.f;
    SimInput operator()(const RallySim& sim) {
        if (!sim.humanBlocking()) return play(sim);
        const BlockAttempt& b = sim.block();
        SimInput in;
        const float gap = b.attackPoint.y - sim.players()[b.player].pos.y;
        if (std::fabs(gap) > 0.05f) in.moveY = gap > 0.f ? 1.f : -1.f;
        in.action = b.timeToContact - b.pressLead <= blockAt;
        return in;
    }
};

int humanBlocks(float blockAt, ContactQuality wanted) {
    int count = 0;
    for (std::uint32_t seed = 1; seed <= 5; ++seed) {
        RallySim::Config c;
        c.seed = seed;
        RallySim sim(c);
        ScriptedBlocker driver{ScriptedHuman{}, blockAt};
        for (long i = 0; i < 40000000L && sim.phase() != RallyPhase::MatchOver; ++i) {
            sim.step(kDt, driver(sim));
            for (const SimEvent& e : sim.takeEvents()) {
                count += e.type == SimEvent::Type::Contact && e.kind == ContactKind::Block && e.team == Team::Home &&
                         e.quality == wanted;
            }
        }
    }
    return count;
}

}  // namespace

TEST(higher_cpu_skill_is_harder_to_beat) {
    auto homeShare = [](float skill) {
        int won = 0, points = 0;
        for (std::uint32_t seed = 1; seed <= 10; ++seed) {
            RallySim::Config c;
            c.seed = seed;
            c.cpuSkill = skill;
            RallySim sim(c);
            ScriptedHuman player;  // human-like: slightly off, and slow off the mark
            player.pressAt = 0.1f;
            player.reaction = 0.35f;
            const SimTally st = playMatch(sim, player, 150);
            won += st.homeWins;
            points += st.points;
        }
        return static_cast<float>(won) / static_cast<float>(points);
    };
    const float easy = homeShare(0.35f), normal = homeShare(0.7f), hard = homeShare(1.f);
    std::printf("  average-timed human wins %.0f%% easy, %.0f%% normal, %.0f%% hard\n", static_cast<double>(easy * 100.f),
                static_cast<double>(normal * 100.f), static_cast<double>(hard * 100.f));
    CHECK(easy > normal + 0.03f);
    CHECK(normal > hard + 0.03f);
}

TEST(slightly_off_timing_stays_playable) {
    auto homeShare = [](float pressAt) {
        int won = 0, points = 0;
        for (std::uint32_t seed = 1; seed <= 10; ++seed) {
            RallySim::Config c;
            c.seed = seed;
            RallySim sim(c);
            const SimTally st = playMatch(sim, ScriptedHuman{pressAt}, 150);
            won += st.homeWins;
            points += st.points;
        }
        return static_cast<float>(won) / static_cast<float>(points);
    };
    const float early = homeShare(0.1f), late = homeShare(-0.1f);
    std::printf("  0.1 s early wins %.0f%% of points, 0.1 s late wins %.0f%%\n", static_cast<double>(early * 100.f),
                static_cast<double>(late * 100.f));
    CHECK(early > 0.5f && late > 0.5f);  // a Good-window press must not be a disguised miss
}

TEST(dead_ball_bounces_and_comes_to_rest_on_the_floor) {
    RallySim::Config c;
    c.humanHome = false;
    RallySim sim(c);
    float peak = 0.f;
    bool sawPointOver = false;
    for (int i = 0; i < 200000 && !(sawPointOver && sim.phase() == RallyPhase::PreServe); ++i) {
        sim.step(kDt, {});
        if (sim.phase() != RallyPhase::PointOver) continue;
        sawPointOver = true;
        peak = std::max(peak, sim.ball().pos.z);
        CHECK(sim.ball().pos.z >= kBallRadius);
    }
    CHECK(sawPointOver && peak > 0.2f);
}

// --- Moments ----------------------------------------------------------------

TEST(a_run_of_points_builds_momentum_into_the_zone) {
    RallySim::Config c;
    c.seed = 4;
    c.humanHome = false;
    RallySim sim(c);
    int zones = 0;
    float strongest = 0.f;
    for (long i = 0; i < 40000000L && sim.phase() != RallyPhase::MatchOver; ++i) {
        sim.step(kDt, {});
        strongest = std::max(strongest, std::fabs(sim.momentum()));
        CHECK(std::fabs(sim.momentum()) <= 1.f);
        for (const SimEvent& e : sim.takeEvents()) zones += e.type == SimEvent::Type::ZoneStart;
    }
    CHECK(zones > 0 && strongest >= 0.75f);
    std::printf("  %d zones in a match\n", zones);
}

TEST(a_copied_match_replays_exactly_with_the_same_input) {
    RallySim::Config c;
    c.seed = 21;
    RallySim live(c);
    ScriptedHuman player;
    for (int i = 0; i < 4000; ++i) live.step(kDt, player(live));
    live.takeEvents();

    RallySim replay(live);
    std::vector<SimInput> log;
    for (int i = 0; i < 2000; ++i) {
        log.push_back(player(live));
        live.step(kDt, log.back());
    }
    for (const SimInput& in : log) replay.step(kDt, in);
    CHECK(replay.ball().pos.x == live.ball().pos.x && replay.ball().pos.z == live.ball().pos.z);
    CHECK(replay.players()[3].pos.x == live.players()[3].pos.x);
    CHECK(replay.score().points(Team::Home) == live.score().points(Team::Home));
    CHECK(replay.score().points(Team::Away) == live.score().points(Team::Away));
}

TEST(set_point_is_flagged_and_speeds_the_gauge) {
    RallySim::Config c;
    c.rules.setPoints = 2;
    c.rules.winBy = 1;
    c.seed = 8;
    c.humanHome = false;
    RallySim sim(c);
    bool saw = false;
    for (long i = 0; i < 400000L && !saw; ++i) {
        sim.step(kDt, {});
        saw = sim.pressurePoint();
    }
    CHECK(saw);
}

// --- Team play --------------------------------------------------------------

TEST(the_libero_swaps_in_and_never_serves_or_plays_front_row) {
    RallySim::Config c;
    c.seed = 13;
    c.humanHome = false;
    RallySim sim(c);
    bool sawLibero = false, sawMiddleBack = false;
    for (long i = 0; i < 2000000L && sim.phase() != RallyPhase::MatchOver; ++i) {
        sim.step(kDt, {});
        sim.takeEvents();
        int liberos = 0;
        for (int p = 0; p < 6; ++p) {
            liberos += sim.players()[p].id == 7;
            sawMiddleBack = sawMiddleBack || sim.players()[p].id == 6;
        }
        sawLibero = sawLibero || liberos == 1;
        CHECK(liberos <= 1);
        if (sim.phase() == RallyPhase::PreServe) CHECK(sim.players()[sim.server()].id != 7);
    }
    CHECK(sawLibero && sawMiddleBack);  // both on and off the court during the match
}

TEST(setter_plays_follow_the_stick) {
    auto setHeight = [](float forward) {
        RallySim::Config c;
        c.seed = 3;
        RallySim sim(c);
        ScriptedHuman player;
        float peak = 0.f;
        int free = 0;
        bool inSet = false;
        for (long i = 0; i < 2000000L && sim.phase() != RallyPhase::MatchOver; ++i) {
            SimInput in = player(sim);
            if (sim.pending().active && sim.pending().team == Team::Home && sim.pending().touch == 2) in.moveX = forward;
            sim.step(kDt, in);
            for (const SimEvent& e : sim.takeEvents()) {
                if (e.type != SimEvent::Type::Contact) continue;
                inSet = e.kind == ContactKind::Set && e.team == Team::Home;
                free += e.kind == ContactKind::FreeBall && e.team == Team::Home;
            }
            if (inSet) peak = std::max(peak, sim.ball().pos.z);
        }
        return std::make_pair(peak, free);
    };
    const auto high = setHeight(0.f), quick = setHeight(1.f), dump = setHeight(-1.f);
    CHECK(high.first > 4.3f);
    CHECK(quick.first > 0.f && quick.first < 3.9f);
    CHECK(dump.second > high.second);
}

// --- Career -----------------------------------------------------------------

namespace {

MatchStats strongGame() {
    MatchStats s;
    s.perfect = 30;
    s.good = 10;
    s.kills = 12;
    s.aces = 2;
    s.pointsWon = 30;
    s.pointsLost = 12;
    s.clutchPoints = 3;
    return s;
}

MatchStats poorGame() {
    MatchStats s;
    s.good = 5;
    s.shanks = 12;
    s.whiffs = 6;
    s.errors = 8;
    s.pointsWon = 12;
    s.pointsLost = 30;
    return s;
}

// Wins every match; the final counts too.
void winSeason(Career& c) {
    while (c.stage() != CareerStage::SeasonOver) c.recordMatch(2, 0, strongGame());
}

}  // namespace

TEST(league_points_follow_the_set_score) {
    CHECK(leaguePoints(2, 0) == 3);
    CHECK(leaguePoints(2, 1) == 2);
    CHECK(leaguePoints(1, 2) == 1);
    CHECK(leaguePoints(0, 2) == 0);
}

TEST(better_games_rate_higher_within_the_scale) {
    const float good = matchRating(strongGame(), true), bad = matchRating(poorGame(), false);
    std::printf("  strong game %.1f, poor game %.1f\n", static_cast<double>(good), static_cast<double>(bad));
    CHECK(good > 8.f && good <= 10.f);
    CHECK(bad >= 1.f && bad < 5.f);
    CHECK(matchRating(strongGame(), true) > matchRating(strongGame(), false));
}

TEST(archetypes_start_strong_in_their_specialty) {
    Career spiker("A", Archetype::PowerSpiker, 1), anchor("B", Archetype::DefensiveAnchor, 1);
    CHECK(spiker.skills()[static_cast<int>(Skill::Attack)] > spiker.skills()[static_cast<int>(Skill::Pass)]);
    CHECK(anchor.skills()[static_cast<int>(Skill::Pass)] > anchor.skills()[static_cast<int>(Skill::Attack)]);
    CHECK(spiker.overall() == anchor.overall());
}

TEST(everyone_plays_everyone_once_a_season) {
    Career c("A", Archetype::PowerSpiker, 3);
    std::vector<int> seen;
    for (int r = 0; r < Career::kRounds; ++r) {
        seen.push_back(c.nextOpponent());
        c.recordMatch(r % 2 ? 2 : 0, r % 2 ? 1 : 2, strongGame());
    }
    std::sort(seen.begin(), seen.end());
    CHECK((seen == std::vector<int>{1, 2, 3, 4, 5, 6, 7}));

    int wins = 0, losses = 0, setsFor = 0, setsAgainst = 0;
    for (const LeagueTeam& t : c.teams()) {
        CHECK(t.played == Career::kRounds);
        wins += t.won;
        losses += t.lost;
        setsFor += t.setsFor;
        setsAgainst += t.setsAgainst;
    }
    CHECK(wins == losses && wins == 28);
    CHECK(setsFor == setsAgainst);
}

TEST(winning_the_final_promotes_up_to_the_world_stage) {
    Career c("A", Archetype::PowerSpiker, 5);
    winSeason(c);
    CHECK(c.wonFinal() && c.promoted() && c.trophies() == 1);
    c.startNextSeason();
    CHECK(c.tier() == Tier::NationalLeague && c.season() == 2 && c.round() == 0 && c.scoutInterest() == 0);
    CHECK(c.myTeam().played == 0);
    winSeason(c);
    c.startNextSeason();
    CHECK(c.tier() == Tier::WorldStage);
    winSeason(c);
    CHECK(c.trophies() == 3 && !c.promoted());
    c.startNextSeason();
    CHECK(c.tier() == Tier::WorldStage);
}

TEST(a_bad_season_ends_without_a_final_or_promotion) {
    Career c("A", Archetype::PowerSpiker, 5);
    for (int r = 0; r < Career::kRounds; ++r) c.recordMatch(0, 2, poorGame());
    CHECK(c.stage() == CareerStage::SeasonOver && c.leaguePosition() > 2);
    CHECK(!c.promoted() && c.trophies() == 0);
    c.startNextSeason();
    CHECK(c.tier() == Tier::University);
}

TEST(great_form_can_earn_promotion_without_the_title) {
    Career c("A", Archetype::PowerSpiker, 5);
    MatchStats s = strongGame();
    while (c.stage() == CareerStage::League) c.recordMatch(2, 0, s);
    CHECK(c.stage() == CareerStage::Final && c.scoutInterest() == 100);
    c.recordMatch(0, 2, s);  // loses the final, but the scouts have seen enough
    CHECK(!c.wonFinal() && c.promoted());
}

TEST(upgrades_spend_points_and_get_dearer) {
    Career c("A", Archetype::PowerSpiker, 5);
    CHECK(!c.upgrade(Skill::Serve));  // nothing to spend yet
    while (c.skillPoints() < 20 && c.stage() != CareerStage::SeasonOver) c.recordMatch(2, 0, strongGame());
    const int before = c.skillPoints(), cost = c.upgradeCost(Skill::Serve), value = c.skills()[0];
    CHECK(c.upgrade(Skill::Serve));
    CHECK(c.skillPoints() == before - cost && c.skills()[0] == value + 1);
    CHECK(c.upgradeCost(Skill::Attack) >= c.upgradeCost(Skill::Serve));
}

TEST(a_saved_career_comes_back_identical) {
    Career c("Dana Levi", Archetype::BlockSpecialist, 9);
    for (int r = 0; r < 3; ++r) c.recordMatch(2, 1, strongGame());
    c.upgrade(Skill::Block);
    const std::optional<Career> back = Career::load(c.save());
    CHECK(back.has_value());
    if (!back) return;
    CHECK(back->name() == "Dana Levi" && back->archetype() == Archetype::BlockSpecialist);
    CHECK(back->skills() == c.skills() && back->round() == 3 && back->skillPoints() == c.skillPoints());
    CHECK(back->teams()[3].name == c.teams()[3].name && back->teams()[3].points == c.teams()[3].points);
    CHECK(back->save() == c.save());

    Career a = *back, b = c;  // same future too
    a.recordMatch(2, 0, strongGame());
    b.recordMatch(2, 0, strongGame());
    CHECK(a.save() == b.save());

    std::string tampered = c.save();
    const std::size_t skillLine = tampered.find("\n50 ");  // Serve, the first skill
    CHECK(skillLine != std::string::npos);
    if (skillLine == std::string::npos) return;
    tampered.replace(skillLine, 4, "\n999 ");  // far past the cap
    CHECK(!Career::load(tampered));
    CHECK(!Career::load("not a save"));
    CHECK(!Career::load(c.save().substr(0, 60)));
}

TEST(career_tracks_a_real_match_from_sim_events) {
    RallySim::Config cfg;
    cfg.rules = Career::matchRules();
    cfg.seed = 11;
    RallySim sim(cfg);
    MatchStats stats;
    ScriptedHuman player;
    for (long i = 0; i < 40000000L && sim.phase() != RallyPhase::MatchOver; ++i) {
        sim.step(kDt, player(sim));
        for (const SimEvent& e : sim.takeEvents()) recordEvent(stats, e);
    }
    CHECK(sim.phase() == RallyPhase::MatchOver);
    const int setPoints = sim.score().sets(Team::Home) + sim.score().sets(Team::Away);
    CHECK(setPoints >= 2 && setPoints <= 3);
    CHECK(stats.pointsWon >= 30 && stats.perfect > 0 && stats.clutchPoints > 0);
    CHECK(stats.kills + stats.aces + stats.blocks <= stats.pointsWon);
}

TEST(better_skills_win_more) {
    auto homeShare = [](int rating) {
        int won = 0, points = 0;
        for (std::uint32_t seed = 1; seed <= 12; ++seed) {
            RallySim::Config c;
            c.seed = seed;
            c.humanSkills.fill(rating);
            RallySim sim(c);
            ScriptedHuman player;
            player.pressAt = 0.12f;
            player.reaction = 0.35f;
            const SimTally st = playMatch(sim, player, 150);
            won += st.homeWins;
            points += st.points;
        }
        return static_cast<float>(won) / static_cast<float>(points);
    };
    const float rookie = homeShare(45), star = homeShare(95);
    std::printf("  rookie wins %.0f%% of points, star %.0f%%\n", static_cast<double>(rookie * 100.f), static_cast<double>(star * 100.f));
    CHECK(star > rookie + 0.04f);
}

TEST(be_a_pro_controls_one_player_and_rates_only_their_touches) {
    Career career("A", Archetype::PowerSpiker, 2);
    RallySim::Config c;
    c.rules = Career::matchRules();
    c.seed = 6;
    c.lockedPlayer = career.lockedPlayer();
    c.teammateSkill = career.teammateSkill();
    RallySim sim(c);
    ScriptedHuman player;
    MatchStats mine, team;
    int cpuHomeTouches = 0;
    for (long i = 0; i < 4000000L && sim.phase() != RallyPhase::MatchOver; ++i) {
        CHECK(sim.controlled() == c.lockedPlayer);
        sim.step(kDt, player(sim));
        for (const SimEvent& e : sim.takeEvents()) {
            recordEvent(mine, e, c.lockedPlayer);
            recordEvent(team, e);
            cpuHomeTouches += e.type == SimEvent::Type::Contact && e.team == Team::Home && e.player != c.lockedPlayer;
        }
    }
    CHECK(sim.phase() == RallyPhase::MatchOver);
    CHECK(cpuHomeTouches > 0);  // teammates played the rest
    CHECK(mine.perfect + mine.good + mine.shanks > 0);
    CHECK(mine.perfect + mine.good + mine.shanks < team.perfect + team.good + team.shanks);
    CHECK(mine.pointsWon == team.pointsWon);
    std::printf("  Be a Pro match: %d of %d team touches, %d kills\n", mine.perfect + mine.good + mine.shanks,
                team.perfect + team.good + team.shanks, mine.kills);
}

TEST(no_position_ever_turns_into_the_libero) {
    for (int a = 0; a < kArchetypes; ++a) {
        const int body = Career("A", static_cast<Archetype>(a), 1).lockedPlayer() + 1;
        CHECK(body != 3 && body != 6);  // the libero swaps with the middles
    }
}

TEST(story_choices_change_the_career) {
    Career c("A", Archetype::PowerSpiker, 5);
    bool sawStory = false;
    while (c.stage() != CareerStage::SeasonOver) {
        c.recordMatch(2, 0, strongGame());
        if (const StoryEvent* story = c.pendingStory()) {
            sawStory = true;
            const int before = c.chemistry() + c.coachTrust() + c.scoutInterest() + c.skillPoints();
            c.chooseStory(0);
            CHECK(!c.pendingStory());
            CHECK(c.chemistry() + c.coachTrust() + c.scoutInterest() + c.skillPoints() != before || story->trust[0] == 0);
        }
    }
    CHECK(sawStory);
    CHECK(c.rivalWins() == 1);  // they meet once in the league (the final can add one more)
}

TEST(better_chemistry_means_better_teammates) {
    Career low("A", Archetype::PowerSpiker, 5), high("A", Archetype::PowerSpiker, 5);
    while (low.round() < 2) {
        low.recordMatch(2, 0, strongGame());
        high.recordMatch(2, 0, strongGame());
        if (low.pendingStory()) {
            low.chooseStory(1);
            high.chooseStory(0);
        }
    }
    CHECK(high.teammateSkill() > low.teammateSkill());
}

TEST(old_saves_still_load) {
    Career c("A", Archetype::PowerSpiker, 5);
    std::string text = c.save();
    text.replace(0, 12, "vc26career 1");
    const std::size_t extras = text.find("50 50 0 0 -1\n");
    CHECK(extras != std::string::npos);
    if (extras == std::string::npos) return;
    text.erase(extras, 13);
    const std::optional<Career> old = Career::load(text);
    CHECK(old.has_value() && old->coachTrust() == 50);
}

// --- Touch controls ---------------------------------------------------------

namespace {
constexpr float kW = 2000.f, kH = 1000.f;
void touch(TouchControls& c, std::vector<TouchPoint> fingers) {
    c.update(fingers.data(), static_cast<int>(fingers.size()), kW, kH);
}
}  // namespace

TEST(left_thumb_steers_from_where_it_lands) {
    TouchControls c;
    touch(c, {{1, 400.f, 600.f}});
    CHECK(c.stickActive() && c.stickOriginX() == 400.f);
    CHECK(c.input().moveX == 0.f && c.input().moveY == 0.f);

    touch(c, {{1, 400.f, 490.f}});  // a full stick radius up the screen
    CHECK(near(c.input().moveX, 1.f, 0.001f) && near(c.input().moveY, 0.f, 0.001f));
    touch(c, {{1, 290.f, 600.f}});  // left of the screen is Home's left
    CHECK(near(c.input().moveY, 1.f, 0.001f));
    touch(c, {{1, 900.f, 600.f}});  // past the rim stays at full tilt
    CHECK(near(c.input().moveY, -1.f, 0.001f));

    touch(c, {});
    CHECK(!c.stickActive() && c.input().moveY == 0.f);
}

TEST(tiny_thumb_movements_are_ignored) {
    TouchControls c;
    touch(c, {{1, 400.f, 600.f}});
    touch(c, {{1, 410.f, 600.f}});
    CHECK(c.input().moveY == 0.f);
}

TEST(both_thumbs_work_at_once) {
    TouchControls c;
    touch(c, {{1, 400.f, 600.f}});
    touch(c, {{1, 400.f, 490.f}, {2, 1600.f, 600.f}});
    CHECK(c.input().action && near(c.input().moveX, 1.f, 0.001f));
    touch(c, {{1, 400.f, 490.f}});  // lifting one leaves the other alone
    CHECK(!c.input().action && near(c.input().moveX, 1.f, 0.001f));
}

TEST(a_finger_keeps_its_job_across_the_screen) {
    TouchControls c;
    touch(c, {{1, 1600.f, 600.f}});
    touch(c, {{1, 300.f, 600.f}});  // action finger slides left
    CHECK(c.input().action && !c.stickActive());
    touch(c, {});
    touch(c, {{2, 400.f, 600.f}});
    touch(c, {{2, 1800.f, 600.f}});  // stick finger slides right
    CHECK(!c.input().action && c.stickActive());
}

TEST(second_finger_on_the_left_is_an_action) {
    TouchControls c;
    touch(c, {{1, 400.f, 600.f}});
    touch(c, {{1, 400.f, 600.f}, {2, 700.f, 700.f}});
    CHECK(c.input().action && c.stickOriginX() == 400.f);
}

TEST(corner_tap_changes_difficulty_once) {
    TouchControls c;
    touch(c, {{1, 100.f, 50.f}});
    CHECK(c.difficultyTapped() && !c.stickActive() && !c.input().action);
    touch(c, {{1, 100.f, 50.f}});
    CHECK(!c.difficultyTapped());
}

TEST(human_block_quality_follows_the_timing) {
    CHECK(humanBlocks(0.f, ContactQuality::Perfect) > 0);
    CHECK(humanBlocks(0.f, ContactQuality::Good) == 0);
    CHECK(humanBlocks(0.1f, ContactQuality::Good) > 0);
    CHECK(humanBlocks(0.1f, ContactQuality::Perfect) == 0);
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
