// Rotation Manager (GDD 1.7): lineup legality, clockwise rotation on
// side-out, automatic libero replacement and serve-overlap validation.
#pragma once

#include <optional>
#include <string>
#include <vector>

#include "vc26/Types.h"

namespace vc26 {

// What the pre-match drag-and-drop screen produces.
struct LineupSpec {
    Court starters;                        // six regular players, index = slot
    std::optional<Player> libero;          // designated libero, if any
    std::optional<Slot> liberoStartSlot;   // slot the libero takes over before the first serve
    std::vector<int> liberoTargets;        // ids the libero may replace; empty = the player in liberoStartSlot
    std::optional<Slot> setterSlot;        // overrides the 5-1 anchor; default = first Setter starter, else P1
};

struct LineupIssue {
    Slot slot;
    Legality level;
    std::string reason;
};

struct LineupReport {
    std::array<Legality, kSlots> slots{};  // colour of each slot in the diagram
    std::vector<LineupIssue> issues;

    Legality overall() const;
    bool allowedIn(RuleMode mode) const;
};

LineupReport validateLineup(const LineupSpec& spec, bool servingFirst);

// Court position at serve contact. x: metres from the team's own LEFT
// sideline when facing the net; y: metres back from the centre line.
struct CourtPos {
    float x = 0.f;
    float y = 0.f;
};

struct OverlapFault {
    Slot a;  // must be left of / in front of b
    Slot b;
    std::string reason;
};

// FIVB 7.4.3 positional fault check. The server (P1 of the serving team) is exempt.
std::vector<OverlapFault> checkOverlap(const std::array<CourtPos, kSlots>& pos, bool teamIsServing);

struct LiberoSwap {
    Slot slot;
    int liberoId;
    int otherId;   // player who left (entry) or returned (exit)
    bool entering;
};

struct RallyResult {
    bool sideOut = false;  // we won the rally on the opponent's serve and rotated
    std::optional<LiberoSwap> liberoSwap;
};

class RotationManager {
public:
    // Returns nullopt if the lineup is not allowed in `mode`; `report`, if
    // given, receives the full validation either way.
    static std::optional<RotationManager> create(const LineupSpec& spec, RuleMode mode, bool servingFirst,
                                                 LineupReport* report = nullptr);

    // Call once per dead ball. This is the only point where rotations and
    // libero replacements happen.
    RallyResult onRallyEnd(bool wonByUs);

    const Player& at(Slot s) const { return court_[index(s)]; }
    std::optional<Slot> slotOf(int playerId) const;
    bool serving() const { return serving_; }
    const Court& court() const { return court_; }
    RuleMode mode() const { return mode_; }

    // Duty fixed by the player's starting slot for the whole set.
    std::optional<Role> dutyOf(int playerId) const;
    // Role-Fit multiplier for a player currently on court, else nullopt.
    std::optional<float> roleFitOf(int playerId) const;

private:
    RotationManager() = default;

    void rotate();
    std::optional<LiberoSwap> applyLiberoRules();
    std::optional<Role> nativeRoleOf(int playerId) const;

    Court court_{};
    RuleMode mode_ = RuleMode::Simulation;
    bool serving_ = false;

    std::optional<Player> libero_;
    std::optional<Player> liberoReplaced_;  // regular player sitting out while the libero is on court
    std::vector<int> liberoTargets_;

    struct Duty {
        int playerId;
        Role native;
        Role duty;
    };
    std::vector<Duty> duties_;
};

}  // namespace vc26
