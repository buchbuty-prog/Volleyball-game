// Be a Pro career (GDD 2.A): a created player climbs from the university
// circuit to the world stage. Seasons are an eight-team league and a final.
// Team names are invented.
#pragma once

#include <array>
#include <optional>
#include <string>

#include "vc26/RallySim.h"

namespace vc26 {

enum class Archetype : std::uint8_t { PowerSpiker, DefensiveAnchor, PrecisionSetter, BlockSpecialist };
constexpr int kArchetypes = 4;

enum class Tier : std::uint8_t { University, NationalLeague, WorldStage };

VC26CORE_API const char* toString(Skill s);
VC26CORE_API const char* toString(Archetype a);
VC26CORE_API const char* toString(Tier t);

struct MatchStats {
    int perfect = 0;
    int good = 0;
    int shanks = 0;
    int whiffs = 0;
    int kills = 0;
    int aces = 0;
    int blocks = 0;
    int errors = 0;
    int clutchPoints = 0;  // points won while a set was on the line
    int pointsWon = 0;
    int pointsLost = 0;
};

// Adds up the Home side's match from the sim's events.
VC26CORE_API void recordEvent(MatchStats& stats, const SimEvent& event);

// GDD 2.A: 1.0-10.0, weighing efficiency, errors and clutch points.
VC26CORE_API float matchRating(const MatchStats& stats, bool won);

// League table points. A close win or loss shares them, as in FIVB competitions.
VC26CORE_API int leaguePoints(int setsFor, int setsAgainst);

struct LeagueTeam {
    std::string name;
    int strength = 50;  // 0-100
    int played = 0;
    int won = 0;
    int lost = 0;
    int setsFor = 0;
    int setsAgainst = 0;
    int points = 0;
};

struct MatchReport {
    bool won = false;
    int setsFor = 0;
    int setsAgainst = 0;
    float rating = 6.f;
    int skillPoints = 0;
    int scoutGain = 0;
    MatchStats stats;  // a copy, so the report outlives the match
};

enum class CareerStage : std::uint8_t { League, Final, SeasonOver };

class VC26CORE_API Career {
public:
    static constexpr int kTeams = 8;
    static constexpr int kRounds = kTeams - 1;

    Career(std::string name, Archetype archetype, std::uint32_t seed);

    static SkillRatings startingSkills(Archetype archetype);

    const std::string& name() const { return name_; }
    Archetype archetype() const { return archetype_; }
    const SkillRatings& skills() const { return skills_; }
    int overall() const;
    int skillPoints() const { return skillPoints_; }
    Tier tier() const { return tier_; }
    int season() const { return season_; }
    int round() const { return round_; }  // rounds already played
    CareerStage stage() const { return stage_; }
    int scoutInterest() const { return scout_; }  // 0-100
    int trophies() const { return trophies_; }
    int matchesPlayed() const { return matches_; }
    float averageRating() const { return matches_ ? ratingTotal_ / static_cast<float>(matches_) : 0.f; }
    bool wonFinal() const { return wonFinal_; }
    bool promoted() const { return promoted_; }

    const std::array<LeagueTeam, kTeams>& teams() const { return teams_; }
    const LeagueTeam& myTeam() const { return teams_[0]; }
    std::array<int, kTeams> standings() const;  // team indices, best first
    int leaguePosition() const;                 // 1 = top

    // Index of the next opponent. Only valid before SeasonOver.
    int nextOpponent() const;
    float opponentCpuSkill() const;  // for RallySim::Config::cpuSkill
    static MatchRules matchRules();  // best of three to 15, so a match fits a sitting

    // Call after each match. Plays the rest of the round and moves the season on.
    MatchReport recordMatch(int setsFor, int setsAgainst, const MatchStats& stats);

    int upgradeCost(Skill s) const;
    bool upgrade(Skill s);

    void startNextSeason();

    std::string save() const;
    static std::optional<Career> load(const std::string& text);

private:
    void buildLeague();
    int opponentInRound(int round, int team) const;
    void simulateMatch(int a, int b);
    void applyResult(int a, int b, int setsA, int setsB);
    void finishSeason();
    float rand01() { return nextRand01(rng_); }

    std::string name_;
    Archetype archetype_;
    std::uint32_t rng_;
    SkillRatings skills_{};
    int skillPoints_ = 0;
    Tier tier_ = Tier::University;
    int season_ = 1;
    int round_ = 0;
    CareerStage stage_ = CareerStage::League;
    int scout_ = 0;
    int trophies_ = 0;
    int matches_ = 0;
    float ratingTotal_ = 0.f;
    bool wonFinal_ = false;
    bool promoted_ = false;
    std::array<LeagueTeam, kTeams> teams_{};
};

}  // namespace vc26
