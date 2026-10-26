#include "vc26/MatchScore.h"

#include <algorithm>

namespace vc26 {

bool MatchScore::isDecidingSet() const {
    return sets_[0] == rules_.setsToWin - 1 && sets_[1] == rules_.setsToWin - 1;
}

std::optional<Team> MatchScore::setPointFor() const {
    if (finished()) return std::nullopt;
    for (Team t : {Team::Home, Team::Away}) {
        if (wins(points(t) + 1, points(other(t)))) return t;
    }
    return std::nullopt;
}

bool MatchScore::isMatchPoint() const {
    const auto t = setPointFor();
    return t && sets(*t) == rules_.setsToWin - 1;
}

PointResult MatchScore::awardPoint(Team scorer) {
    PointResult r;
    if (finished()) return r;

    const bool deciding = isDecidingSet();
    ++points_[idx(scorer)];

    if (wins(points(scorer), points(other(scorer)))) {
        r.setWon = true;
        ++sets_[idx(scorer)];
        points_ = {0, 0};
        decidingSwitchDone_ = false;
        if (sets(scorer) == rules_.setsToWin) {
            winner_ = scorer;
            r.matchWon = true;
        }
        return r;
    }

    const int combined = points_[0] + points_[1];
    const int every = deciding ? rules_.decidingSwitchEvery : rules_.switchEvery;
    if (every > 0 && combined % every == 0) r.switchSides = true;
    if (deciding && !decidingSwitchDone_ && rules_.decidingSwitchAtLeader > 0 &&
        std::max(points_[0], points_[1]) == rules_.decidingSwitchAtLeader) {
        r.switchSides = true;
        decidingSwitchDone_ = true;
    }

    r.setPoint = setPointFor().has_value();
    r.matchPoint = isMatchPoint();
    return r;
}

}  // namespace vc26
