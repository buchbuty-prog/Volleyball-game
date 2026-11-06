// Be a Pro career (GDD 2.A): a created player climbs from the university
// circuit to the world stage. Seasons are an eight-team league and a final.
// Team names are invented.
#pragma once

#include <array>
#include <optional>
#include <string>
#include <vector>

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

// Adds up the Home side's match from the sim's events. With a locked player,
// touches, kills and errors count only when that player made them.
VC26CORE_API void recordEvent(MatchStats& stats, const SimEvent& event, int lockedPlayer = -1);

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
    bool satOut = false;  // the team played without you
    MatchStats stats;     // a copy, so the report outlives the match
};

// A club that wants you next season. Clubs at the next level count, if you're going up.
struct TransferOffer {
    std::string club;
    int strength = 0;       // how much better the squad is than your usual one
    int signingPoints = 0;  // skill points up front
};

struct SeasonRecord {
    int season = 1;
    Tier tier = Tier::University;
    std::string club;
    int position = 0;
    int won = 0;
    int lost = 0;
    float rating = 0.f;  // 0 if you never played
    bool champion = false;
    bool promoted = false;
    bool mvp = false;
};

enum class CareerStage : std::uint8_t { League, Final, SeasonOver };

// A decision between matches, with what each answer does.
struct StoryEvent {
    const char* title;
    const char* text;
    const char* options[2];
    int trust[2];       // the coach
    int chemistry[2];   // the locker room
    int scout[2];
    int skillPoints[2];
    int rest[2];  // matches missed because of the answer
};

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
    int coachTrust() const { return trust_; }   // 0-100
    int chemistry() const { return chemistry_; }  // 0-100
    static const char* rivalName();
    bool isRivalMatch() const;
    int rivalWins() const { return rivalWins_; }
    int rivalLosses() const { return rivalLosses_; }
    const StoryEvent* pendingStory() const;
    void chooseStory(int option);

    // Injured or benched players sit the next match out. Benching follows low coach trust.
    int injuredMatches() const { return injured_; }
    bool benched() const;
    bool canPlay() const { return injured_ == 0 && !benched(); }
    MatchReport sitOut();  // the team plays on, and the season moves as it would after a match

    // Be a Pro: which Home body is this player, and how well the team plays around them.
    int lockedPlayer() const;
    float teammateSkill() const;

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

    // Offers wait at SeasonOver. Pass an index to sign, or -1 to stay.
    const std::vector<TransferOffer>& offers() const { return offers_; }
    void startNextSeason(int offer = -1);
    std::string nextClub() const;  // who you play for next season without signing

    const std::vector<SeasonRecord>& history() const { return history_; }
    int careerKills() const { return kills_; }
    int careerAces() const { return aces_; }
    int careerBlocks() const { return blocks_; }
    float bestRating() const { return bestRating_; }

    std::string save() const;
    static std::optional<Career> load(const std::string& text);

private:
    void buildLeague();
    int opponentInRound(int round, int team) const;
    void playSets(int strengthA, int strengthB, int& setsA, int& setsB);
    void simulateMatch(int a, int b);
    void finishRound(int opponent, int setsFor, int setsAgainst);
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
    int trust_ = 50;
    int chemistry_ = 50;
    int rivalWins_ = 0;
    int rivalLosses_ = 0;
    int story_ = -1;
    int injured_ = 0;       // matches left to miss
    std::string club_;      // empty: the usual club at this level
    int clubEdge_ = 0;      // squad strength a signing brought
    float seasonRatingTotal_ = 0.f;
    int seasonMatches_ = 0;
    float lastRating_ = 6.f;
    int kills_ = 0;
    int aces_ = 0;
    int blocks_ = 0;
    float bestRating_ = 0.f;
    std::vector<TransferOffer> offers_;
    std::vector<SeasonRecord> history_;
    bool wonFinal_ = false;
    bool promoted_ = false;
    std::array<LeagueTeam, kTeams> teams_{};
};

}  // namespace vc26
