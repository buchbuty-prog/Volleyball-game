#include "vc26/Types.h"

namespace vc26 {

const char* toString(Slot s) {
    static const char* const kNames[kSlots] = {"P1", "P2", "P3", "P4", "P5", "P6"};
    return kNames[index(s)];
}

const char* toString(Role r) {
    switch (r) {
        case Role::Setter: return "Setter";
        case Role::OutsideHitter: return "Outside Hitter";
        case Role::MiddleBlocker: return "Middle Blocker";
        case Role::Opposite: return "Opposite";
        case Role::Libero: return "Libero";
    }
    return "?";
}

const char* toString(Legality l) {
    switch (l) {
        case Legality::Legal: return "Legal";
        case Legality::ArcadeOnly: return "ArcadeOnly";
        case Legality::Illegal: return "Illegal";
    }
    return "?";
}

}  // namespace vc26
