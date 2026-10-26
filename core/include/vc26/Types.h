// Volleyball Career 26 - engine-agnostic gameplay core.
// Plain C++17, no engine headers, no RNG: identical inputs give identical
// results, so the same code can run on the client and the ranked server.
#pragma once

#include <array>
#include <cstdint>

namespace vc26 {

enum class Role : std::uint8_t { Setter, OutsideHitter, MiddleBlocker, Opposite, Libero };

// Simulation = strict FIVB rules. Arcade = "house rules" (GDD 1.7).
enum class RuleMode : std::uint8_t { Simulation, Arcade };

// Team rotation system (GDD 2.B tactical board). In 4-2 and 6-2 the second
// setter starts opposite the first, in the slot a 5-1 gives the opposite.
enum class System : std::uint8_t { FiveOne, SixTwo, FourTwo };

// Rotational slots, numbered as on the official score sheet.
// P1 = right back (server), P2 = right front, P3 = middle front,
// P4 = left front, P5 = left back, P6 = middle back.
enum class Slot : std::uint8_t { P1, P2, P3, P4, P5, P6 };

constexpr int kSlots = 6;

constexpr int index(Slot s) { return static_cast<int>(s); }
constexpr Slot slotAt(int i) { return static_cast<Slot>(((i % kSlots) + kSlots) % kSlots); }
constexpr bool isFrontRow(Slot s) { return s == Slot::P2 || s == Slot::P3 || s == Slot::P4; }

// Maps directly onto the pre-match court diagram colours.
enum class Legality : std::uint8_t {
    Legal,       // green: allowed in Simulation and Arcade
    ArcadeOnly,  // amber: allowed only with house rules
    Illegal      // red: never allowed
};

struct Player {
    int id = -1;
    Role role = Role::OutsideHitter;

    bool valid() const { return id >= 0; }
};

using Court = std::array<Player, kSlots>;

const char* toString(Slot s);
const char* toString(Role r);
const char* toString(Legality l);
const char* toString(System s);

}  // namespace vc26
