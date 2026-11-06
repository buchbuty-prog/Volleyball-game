// The broadcast layer: who is on the court, and what the commentator says.
// Names are invented. Everything here is text, so it can be tested without a screen.
#pragma once

#include <array>
#include <cstdint>
#include <string>

#include "vc26/RallySim.h"

namespace vc26 {

constexpr int kRosterSize = 7;  // six on court and the libero

// Surnames by shirt number minus one, the same for a team every time it's seen.
using Roster = std::array<std::string, kRosterSize>;
VC26CORE_API std::uint32_t nameHash(const std::string& text);
VC26CORE_API Roster makeRoster(const std::string& teamName);

struct PointCall {
    PointReason reason = PointReason::Kill;
    std::string scorer;   // last touch by the winning team, if any
    std::string blunder;  // last touch by the losing team, if any
    std::string team;     // who won the point
    int contacts = 0;     // touches in the rally
    int run = 1;          // points in a row for the winning team
    bool setOnTheLine = false;
};

// Says what happened. The pick chooses between ways of saying it, so the same
// point can sound different without the text depending on the sim's random numbers.
VC26CORE_API std::string callPoint(const PointCall& call, std::uint32_t pick);

// Tracks points in a row, for the call above.
class VC26CORE_API PointRun {
public:
    int add(Team winner);
    void reset() { length_ = 0; }

private:
    Team last_ = Team::Home;
    int length_ = 0;
};

}  // namespace vc26
