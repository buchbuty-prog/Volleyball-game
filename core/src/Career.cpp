#include "vc26/Career.h"

#include <algorithm>
#include <cmath>
#include <numeric>
#include <sstream>

namespace vc26 {

namespace {

constexpr int kMaxSkill = 99;
constexpr int kPromotionInterest = 100;

const char* const kRivals[3][7] = {
    {"Riverside Tech", "North Valley U", "Lakeshore College", "Summit State", "Harbor Institute", "Pinecrest U", "Eastfield College"},
    {"Harbor City VC", "Iron Bay Spikers", "Redwood United", "Silverton Volley", "Port Mercer VC", "Ashford Rovers", "Kingsway Athletic"},
    {"Aurora Select", "Meridian Stars", "Solaris Elite", "Tempest XII", "Northern Crown", "Atlas Volley", "Vanguard National"},
};
const char* const kMyTeam[3] = {"Westbrook University", "Westbrook VC", "National Team"};
constexpr int kStrengthFloor[3] = {30, 50, 70};

// A new player's strongest areas.
struct Specialty {
    Skill first;
    Skill second;
};
constexpr Specialty kSpecialties[kArchetypes] = {
    {Skill::Attack, Skill::Serve},
    {Skill::Pass, Skill::Block},
    {Skill::Set, Skill::Pass},
    {Skill::Block, Skill::Attack},
};

}  // namespace

const char* toString(Skill s) {
    switch (s) {
        case Skill::Serve: return "Serve";
        case Skill::Pass: return "Pass";
        case Skill::Set: return "Set";
        case Skill::Attack: return "Attack";
        case Skill::Block: return "Block";
    }
    return "";
}

const char* toString(Archetype a) {
    switch (a) {
        case Archetype::PowerSpiker: return "Power Spiker";
        case Archetype::DefensiveAnchor: return "Defensive Anchor";
        case Archetype::PrecisionSetter: return "Precision Setter";
        case Archetype::BlockSpecialist: return "Block Specialist";
    }
    return "";
}

const char* toString(Tier t) {
    switch (t) {
        case Tier::University: return "University Circuit";
        case Tier::NationalLeague: return "National League";
        case Tier::WorldStage: return "World Stage";
    }
    return "";
}

void recordEvent(MatchStats& stats, const SimEvent& e) {
    switch (e.type) {
        case SimEvent::Type::Contact:
            if (e.team != Team::Home || e.kind == ContactKind::Serve) break;
            if (e.quality == ContactQuality::Perfect) ++stats.perfect;
            if (e.quality == ContactQuality::Good) ++stats.good;
            if (e.quality == ContactQuality::Shank) ++stats.shanks;
            break;
        case SimEvent::Type::Whiff:
            if (e.team == Team::Home) ++stats.whiffs;
            break;
        case SimEvent::Type::NetHit:
            break;
        case SimEvent::Type::Point:
            if (e.team == Team::Home) {
                ++stats.pointsWon;
                if (e.reason == PointReason::Kill) ++stats.kills;
                if (e.reason == PointReason::Ace) ++stats.aces;
                if (e.reason == PointReason::Block) ++stats.blocks;
                if (e.clutch) ++stats.clutchPoints;
            } else {
                ++stats.pointsLost;
                if (e.reason == PointReason::Out || e.reason == PointReason::Antenna || e.reason == PointReason::NetTouch ||
                    e.reason == PointReason::BallDown) {
                    ++stats.errors;
                }
            }
            break;
    }
}

float matchRating(const MatchStats& s, bool won) {
    const int touches = s.perfect + s.good + s.shanks + s.whiffs;
    const float quality = touches ? (static_cast<float>(s.perfect) + 0.5f * static_cast<float>(s.good)) / static_cast<float>(touches) : 0.f;
    const int points = s.pointsWon + s.pointsLost;
    const float share = points ? static_cast<float>(s.pointsWon) / static_cast<float>(points) : 0.5f;

    float rating = 4.f + 3.f * quality + 4.f * (share - 0.5f);
    rating += won ? 0.7f : -0.3f;
    rating += 0.05f * static_cast<float>(s.kills) + 0.12f * static_cast<float>(s.aces + s.blocks) + 0.1f * static_cast<float>(s.clutchPoints);
    rating -= 0.08f * static_cast<float>(s.errors + s.whiffs);
    return std::clamp(rating, 1.f, 10.f);
}

int leaguePoints(int setsFor, int setsAgainst) {
    if (setsFor > setsAgainst) return setsFor - setsAgainst >= 2 ? 3 : 2;
    return setsAgainst - setsFor == 1 ? 1 : 0;
}

Career::Career(std::string name, Archetype archetype, std::uint32_t seed)
    : name_(std::move(name)), archetype_(archetype), rng_(seed ? seed : 1u) {
    skills_.fill(50);
    const Specialty& specialty = kSpecialties[static_cast<int>(archetype)];
    skills_[static_cast<int>(specialty.first)] += 12;
    skills_[static_cast<int>(specialty.second)] += 6;
    buildLeague();
}

float Career::rand01() {
    rng_ ^= rng_ << 13;
    rng_ ^= rng_ >> 17;
    rng_ ^= rng_ << 5;
    return static_cast<float>(rng_ >> 8) / 16777216.f;
}

int Career::overall() const {
    return (std::accumulate(skills_.begin(), skills_.end(), 0) + kSkills / 2) / kSkills;
}

void Career::buildLeague() {
    const int t = static_cast<int>(tier_);
    teams_[0] = {};
    teams_[0].name = kMyTeam[t];
    teams_[0].strength = kStrengthFloor[t] + 12;
    for (int i = 1; i < kTeams; ++i) {
        teams_[i] = {};
        teams_[i].name = kRivals[t][i - 1];
        teams_[i].strength = kStrengthFloor[t] + static_cast<int>(rand01() * 25.f);
    }
    round_ = 0;
    stage_ = CareerStage::League;
    wonFinal_ = false;
    promoted_ = false;
}

int Career::opponentInRound(int round, int team) const {
    // Circle method: team 7 stays put and the others turn one place a round.
    const int turning = kTeams - 1;
    if (team == kTeams - 1) return round;
    if (team == round) return kTeams - 1;
    return ((2 * round - team) % turning + turning) % turning;
}

std::array<int, Career::kTeams> Career::standings() const {
    std::array<int, kTeams> order{};
    std::iota(order.begin(), order.end(), 0);
    std::stable_sort(order.begin(), order.end(), [this](int a, int b) {
        const LeagueTeam& x = teams_[a];
        const LeagueTeam& y = teams_[b];
        if (x.points != y.points) return x.points > y.points;
        return x.setsFor - x.setsAgainst > y.setsFor - y.setsAgainst;
    });
    return order;
}

int Career::leaguePosition() const {
    const auto order = standings();
    return static_cast<int>(std::find(order.begin(), order.end(), 0) - order.begin()) + 1;
}

int Career::nextOpponent() const {
    if (stage_ == CareerStage::Final) {
        const auto order = standings();
        return order[0] == 0 ? order[1] : order[0];
    }
    return opponentInRound(std::min(round_, kRounds - 1), 0);
}

float Career::opponentCpuSkill() const {
    return std::clamp(static_cast<float>(teams_[nextOpponent()].strength) / 100.f, 0.2f, 1.f);
}

MatchRules Career::matchRules() {
    MatchRules rules;
    rules.setsToWin = 2;
    rules.setPoints = 15;
    rules.decidingSetPoints = 15;
    rules.decidingSwitchAtLeader = 0;
    return rules;
}

void Career::applyResult(int a, int b, int setsA, int setsB) {
    LeagueTeam& x = teams_[a];
    LeagueTeam& y = teams_[b];
    ++x.played;
    ++y.played;
    x.setsFor += setsA;
    x.setsAgainst += setsB;
    y.setsFor += setsB;
    y.setsAgainst += setsA;
    x.points += leaguePoints(setsA, setsB);
    y.points += leaguePoints(setsB, setsA);
    ++(setsA > setsB ? x.won : y.won);
    ++(setsA > setsB ? y.lost : x.lost);
}

void Career::simulateMatch(int a, int b) {
    const float aWins = 1.f / (1.f + std::exp(static_cast<float>(teams_[b].strength - teams_[a].strength) / 10.f));
    int setsA = 0;
    int setsB = 0;
    while (setsA < 2 && setsB < 2) ++(rand01() < aWins ? setsA : setsB);
    applyResult(a, b, setsA, setsB);
}

MatchReport Career::recordMatch(int setsFor, int setsAgainst, const MatchStats& stats) {
    MatchReport report;
    report.won = setsFor > setsAgainst;
    report.setsFor = setsFor;
    report.setsAgainst = setsAgainst;
    report.rating = matchRating(stats, report.won);
    report.skillPoints = std::max(1, static_cast<int>(std::lround((report.rating - 5.f) / 1.5f))) + (report.won ? 1 : 0);
    report.scoutGain = std::max(0, static_cast<int>(std::lround((report.rating - 5.5f) * 5.f)));

    skillPoints_ += report.skillPoints;
    scout_ = std::min(kPromotionInterest, scout_ + report.scoutGain);
    ++matches_;
    ratingTotal_ += report.rating;

    if (stage_ == CareerStage::Final) {
        wonFinal_ = report.won;
        finishSeason();
        return report;
    }

    const int rival = nextOpponent();
    applyResult(0, rival, setsFor, setsAgainst);
    for (int team = 1; team < kTeams; ++team) {
        const int against = opponentInRound(round_, team);
        if (team != rival && team < against) simulateMatch(team, against);
    }
    ++round_;

    if (round_ == kRounds) {
        if (leaguePosition() <= 2) {
            stage_ = CareerStage::Final;
        } else {
            finishSeason();
        }
    }
    return report;
}

void Career::finishSeason() {
    stage_ = CareerStage::SeasonOver;
    if (wonFinal_) ++trophies_;
    promoted_ = tier_ != Tier::WorldStage && (wonFinal_ || scout_ >= kPromotionInterest);
}

void Career::startNextSeason() {
    if (promoted_) tier_ = static_cast<Tier>(static_cast<int>(tier_) + 1);
    ++season_;
    scout_ = 0;
    buildLeague();
}

int Career::upgradeCost(Skill s) const {
    return 1 + (skills_[static_cast<int>(s)] - 40) / 15;
}

bool Career::upgrade(Skill s) {
    int& value = skills_[static_cast<int>(s)];
    const int cost = upgradeCost(s);
    if (value >= kMaxSkill || skillPoints_ < cost) return false;
    skillPoints_ -= cost;
    ++value;
    return true;
}

std::string Career::save() const {
    std::ostringstream out;
    out.precision(9);  // enough to bring a float back exactly
    out << "vc26career 1\n" << name_ << '\n';
    out << static_cast<int>(archetype_) << ' ' << rng_ << ' ' << skillPoints_ << ' ' << static_cast<int>(tier_) << ' ' << season_ << ' '
        << round_ << ' ' << static_cast<int>(stage_) << ' ' << scout_ << ' ' << trophies_ << ' ' << matches_ << ' ' << ratingTotal_ << ' '
        << wonFinal_ << ' ' << promoted_ << '\n';
    for (int s : skills_) out << s << ' ';
    out << '\n';
    for (const LeagueTeam& t : teams_) {
        out << t.name << '\n'
            << t.strength << ' ' << t.played << ' ' << t.won << ' ' << t.lost << ' ' << t.setsFor << ' ' << t.setsAgainst << ' ' << t.points << '\n';
    }
    return out.str();
}

std::optional<Career> Career::load(const std::string& text) {
    std::istringstream in(text);
    std::string header;
    std::getline(in, header);
    if (header != "vc26career 1") return std::nullopt;

    std::string name;
    std::getline(in, name);
    int archetype = 0, tier = 0, stage = 0;
    Career c(name, Archetype::PowerSpiker, 1);
    in >> archetype >> c.rng_ >> c.skillPoints_ >> tier >> c.season_ >> c.round_ >> stage >> c.scout_ >> c.trophies_ >> c.matches_ >>
        c.ratingTotal_ >> c.wonFinal_ >> c.promoted_;
    for (int& s : c.skills_) in >> s;
    for (LeagueTeam& t : c.teams_) {
        in >> std::ws;
        std::getline(in, t.name);
        in >> t.strength >> t.played >> t.won >> t.lost >> t.setsFor >> t.setsAgainst >> t.points;
    }
    if (in.fail() || archetype < 0 || archetype >= kArchetypes || tier < 0 || tier > 2 || stage < 0 || stage > 2) return std::nullopt;
    c.archetype_ = static_cast<Archetype>(archetype);
    c.tier_ = static_cast<Tier>(tier);
    c.stage_ = static_cast<CareerStage>(stage);
    return c;
}

}  // namespace vc26
