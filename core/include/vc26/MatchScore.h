// Set / match scoring with indoor (FIVB) and beach rules. Emits the events the
// camera and commentary systems key on: set point, match point, side switch
// (GDD 1.1, 4).
#pragma once

#include <array>
#include <cstdint>
#include <optional>

namespace vc26 {

enum class Team : std::uint8_t { Home, Away };

constexpr Team other(Team t) { return t == Team::Home ? Team::Away : Team::Home; }

struct MatchRules {
    int setsToWin = 3;
    int setPoints = 25;
    int decidingSetPoints = 15;
    int winBy = 2;
    int switchEvery = 0;               // combined points between side switches in regular sets (0 = never)
    int decidingSwitchEvery = 0;       // same, deciding set
    int decidingSwitchAtLeader = 8;    // one switch when the leader first reaches this in the deciding set (0 = never)

    static MatchRules indoor() { return {}; }
    static MatchRules beach() { return {2, 21, 15, 2, 7, 5, 0}; }
};

struct PointResult {
    bool setWon = false;      // teams change ends before the next set
    bool matchWon = false;
    bool switchSides = false; // mid-set side switch
    bool setPoint = false;    // after this point, someone can win the set with the next one
    bool matchPoint = false;  // ...and that would win the match
};

class MatchScore {
public:
    explicit MatchScore(MatchRules rules = MatchRules::indoor()) : rules_(rules) {}

    // Ignored once the match is over.
    PointResult awardPoint(Team scorer);

    int points(Team t) const { return points_[idx(t)]; }
    int sets(Team t) const { return sets_[idx(t)]; }
    int setNumber() const { return sets_[0] + sets_[1] + (finished() ? 0 : 1); }
    bool isDecidingSet() const;
    bool finished() const { return winner_.has_value(); }
    std::optional<Team> winner() const { return winner_; }

    // Team that wins the current set with the next point, if any.
    std::optional<Team> setPointFor() const;
    bool isMatchPoint() const;

    const MatchRules& rules() const { return rules_; }

private:
    static int idx(Team t) { return static_cast<int>(t); }
    int target() const { return isDecidingSet() ? rules_.decidingSetPoints : rules_.setPoints; }
    bool wins(int own, int opp) const { return own >= target() && own - opp >= rules_.winBy; }

    MatchRules rules_;
    std::array<int, 2> points_{};
    std::array<int, 2> sets_{};
    std::optional<Team> winner_;
    bool decidingSwitchDone_ = false;
};

}  // namespace vc26
