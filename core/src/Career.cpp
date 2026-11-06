#include "vc26/Career.h"

#include <algorithm>
#include <cmath>
#include <numeric>
#include <sstream>

namespace vc26 {

namespace {

constexpr int kPromotionInterest = 100;
constexpr int kRivalTeam = 1;  // the rival plays for this club at every level below the World Stage
constexpr int kRivalEdge = 8;
constexpr float kInjuryChance = 0.08f;
constexpr float kTapedAnkleRisk = 0.4f;  // playing through it can make it worse
constexpr int kTapedAnkleMatches = 2;
constexpr int kBenchTrust = 25;          // below this the coach sits you down
constexpr int kBenchedTrustGain = 15;    // the point has been made
constexpr int kSitOutPenalty = 6;        // the squad is weaker without you
constexpr int kCaptainTrust = 65;
constexpr float kSlumpRating = 5.5f;
constexpr float kMvpRating = 7.5f;
constexpr float kTwoOffersRating = 7.f;
constexpr float kOneOfferRating = 6.f;
constexpr int kSignedTrust = 55;      // a new coach starts on your side
constexpr int kSignedChemistry = 40;  // a new locker room doesn't yet
constexpr int kLoyaltyChemistry = 6;
constexpr std::size_t kMaxOffers = 2;
constexpr std::size_t kMaxSeasons = 1000;  // a save claiming more is damaged

enum StoryId { PressCredit, CoachFilm, RivalBeaten, RivalLost, SoreAnkle, Captain, Mentor, Rumour, Slump, StoryCount };

const StoryEvent kStories[] = {
    {"Press Conference", "A reporter asks who deserves the credit for your form.",
     {"\"My teammates make it easy.\"", "\"I've been carrying this team.\""}, {3, -3}, {10, -8}, {0, 10}, {0, 0}},
    {"Film Session", "Coach wants you in early tomorrow to study film. It's your only day off.",
     {"Be there first.", "Skip it and train alone."}, {10, -8}, {2, 0}, {0, 0}, {0, 2}},
    {"Rivalry", "You beat Niko Vance's team. Niko tells the cameras you got lucky.",
     {"Let the game do the talking.", "Fire back in the interview."}, {5, -5}, {5, 0}, {0, 8}, {0, 0}},
    {"Rivalry", "Niko Vance's team beat you. Your phone buzzes: \"Better luck next time.\"",
     {"Screenshot it and train harder.", "Shrug it off."}, {0, 5}, {0, 3}, {0, 0}, {2, 0}},
    {"Sore Ankle", "You rolled your ankle late on. The physio wants you to rest it.",
     {"Listen to the physio.", "Tape it and tell no one."}, {8, -4}, {0, 4}, {0, 8}, {0, 0}, {1, 0}},
    {"The Armband", "Coach offers you the captaincy for the rest of the season.",
     {"Accept it.", "\"Give it to someone senior.\""}, {5, -2}, {4, 10}, {6, 0}, {0, 0}},
    {"Young Prospect", "A rookie asks you to stay after practice and work on their game.",
     {"Stay as long as it takes.", "Keep the time for your own work."}, {2, 0}, {8, -3}, {0, 0}, {0, 2}},
    {"Transfer Rumour", "The papers say a bigger club is watching you. The locker room has seen it.",
     {"Deny it. You're focused here.", "Smile and say never say never."}, {5, -6}, {3, -4}, {0, 12}, {0, 0}},
    {"Slump", "Nothing is dropping for you. The coach asks what you need.",
     {"Extra reps, every morning.", "A match off to clear my head."}, {3, -4}, {0, 2}, {0, 0}, {2, 0}, {0, 1}},
};

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
    {Skill::Attack, Skill::Serve},  // order matches Archetype
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

void recordEvent(MatchStats& stats, const SimEvent& e, int lockedPlayer) {
    const bool mine = lockedPlayer < 0 || e.player == lockedPlayer;
    switch (e.type) {
        case SimEvent::Type::Contact:
            if (e.team != Team::Home || e.kind == ContactKind::Serve || !mine) break;
            if (e.quality == ContactQuality::Perfect) ++stats.perfect;
            if (e.quality == ContactQuality::Good) ++stats.good;
            if (e.quality == ContactQuality::Shank) ++stats.shanks;
            break;
        case SimEvent::Type::Whiff:
            if (e.team == Team::Home && mine) ++stats.whiffs;
            break;
        case SimEvent::Type::NetHit:
        case SimEvent::Type::ZoneStart:
            break;
        case SimEvent::Type::Point:
            if (e.team == Team::Home) {
                ++stats.pointsWon;
                if (mine && e.reason == PointReason::Kill) ++stats.kills;
                if (mine && e.reason == PointReason::Ace) ++stats.aces;
                if (mine && e.reason == PointReason::Block) ++stats.blocks;
                if (e.clutch) ++stats.clutchPoints;
            } else {
                ++stats.pointsLost;
                if (mine && (e.reason == PointReason::Out || e.reason == PointReason::Antenna || e.reason == PointReason::NetTouch ||
                             e.reason == PointReason::BallDown)) {
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
    skills_ = startingSkills(archetype);
    look_ = makeAppearance(position(), rng_);
    buildLeague();
}

Career::Career(std::string name, Archetype archetype, std::uint32_t seed, const Appearance& look) : Career(std::move(name), archetype, seed) {
    look_ = look;
    const SkillRatings edge = physiqueEdge(look, position());
    for (int i = 0; i < kSkills; ++i) {
        skills_[i] = std::clamp(skills_[i] + edge[i], kMinSkill, kMaxSkill);
    }
}

void Career::restyle(const Appearance& look) {
    if (!isValid(look)) return;
    const Appearance body = look_;
    look_ = look;
    look_.heightCm = body.heightCm;
    look_.massKg = body.massKg;
    look_.arms = body.arms;
}
SkillRatings Career::startingSkills(Archetype archetype) {
    SkillRatings skills;
    skills.fill(50);
    const Specialty& specialty = kSpecialties[static_cast<int>(archetype)];
    skills[static_cast<int>(specialty.first)] += 12;
    skills[static_cast<int>(specialty.second)] += 6;
    return skills;
}

const char* Career::rivalName() { return "Niko Vance"; }

bool Career::isRivalMatch() const { return tier_ != Tier::WorldStage && stage_ != CareerStage::SeasonOver && nextOpponent() == kRivalTeam; }

const StoryEvent* Career::pendingStory() const { return story_ >= 0 ? &kStories[story_] : nullptr; }

void Career::chooseStory(int option) {
    const StoryEvent* story = pendingStory();
    if (!story || option < 0 || option > 1) return;
    trust_ = std::clamp(trust_ + story->trust[option], 0, 100);
    chemistry_ = std::clamp(chemistry_ + story->chemistry[option], 0, 100);
    scout_ = std::clamp(scout_ + story->scout[option], 0, kPromotionInterest);
    skillPoints_ += story->skillPoints[option];
    // Rest only means something with league matches left to miss.
    if (stage_ == CareerStage::League) {
        injured_ += story->rest[option];
        if (story_ == SoreAnkle && option == 1 && rand01() < kTapedAnkleRisk) injured_ += kTapedAnkleMatches;
    }
    story_ = -1;
}

bool Career::benched() const { return stage_ == CareerStage::League && trust_ < kBenchTrust; }

// Each archetype's position. None is a middle, because the libero swaps with those.
int Career::lockedPlayer() const {
    switch (archetype_) {
        case Archetype::PowerSpiker: return 1;      // outside hitter
        case Archetype::DefensiveAnchor: return 4;  // outside hitter, passing first
        case Archetype::PrecisionSetter: return 0;  // setter
        case Archetype::BlockSpecialist: return 3;  // opposite
    }
    return 1;
}

float Career::teammateSkill() const {
    const float level = 0.45f + 0.2f * static_cast<float>(tier_);
    const float squad = static_cast<float>(clubEdge_) / 100.f;  // a stronger club plays better around you
    return std::clamp(level + squad + static_cast<float>(chemistry_ - 50) / 250.f + static_cast<float>(trust_ - 50) / 400.f, 0.25f, 1.f);
}

int Career::overall() const {
    return (std::accumulate(skills_.begin(), skills_.end(), 0) + kSkills / 2) / kSkills;
}

void Career::buildLeague() {
    const int t = static_cast<int>(tier_);
    teams_[0] = {};
    teams_[0].name = club_.empty() ? kMyTeam[t] : club_;
    teams_[0].strength = kStrengthFloor[t] + 12 + clubEdge_;
    for (int i = 1; i < kTeams; ++i) {
        teams_[i] = {};
        // The club you joined is out of the league, so your old one takes its place.
        teams_[i].name = kRivals[t][i - 1] == club_ ? kMyTeam[t] : kRivals[t][i - 1];
        teams_[i].strength = kStrengthFloor[t] + static_cast<int>(rand01() * 25.f);
    }
    if (tier_ != Tier::WorldStage) teams_[kRivalTeam].strength += kRivalEdge;
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

void Career::playSets(int strengthA, int strengthB, int& setsA, int& setsB) {
    const float aWins = 1.f / (1.f + std::exp(static_cast<float>(strengthB - strengthA) / 10.f));
    while (setsA < 2 && setsB < 2) ++(rand01() < aWins ? setsA : setsB);
}

void Career::simulateMatch(int a, int b) {
    int setsA = 0;
    int setsB = 0;
    playSets(teams_[a].strength, teams_[b].strength, setsA, setsB);
    applyResult(a, b, setsA, setsB);
}

MatchReport Career::recordMatch(int setsFor, int setsAgainst, const MatchStats& stats) {
    MatchReport report;
    report.won = setsFor > setsAgainst;
    report.setsFor = setsFor;
    report.setsAgainst = setsAgainst;
    report.stats = stats;
    report.rating = matchRating(stats, report.won);
    report.skillPoints = std::max(1, static_cast<int>(std::lround((report.rating - 5.f) / 1.5f))) + (report.won ? 1 : 0);
    report.scoutGain = std::max(0, static_cast<int>(std::lround((report.rating - 5.5f) * 5.f)));

    skillPoints_ += report.skillPoints;
    scout_ = std::min(kPromotionInterest, scout_ + report.scoutGain);
    ++matches_;
    ratingTotal_ += report.rating;
    ++seasonMatches_;
    seasonRatingTotal_ += report.rating;
    lastRating_ = report.rating;
    bestRating_ = std::max(bestRating_, report.rating);
    kills_ += stats.kills;
    aces_ += stats.aces;
    blocks_ += stats.blocks;

    const bool rivalMatch = isRivalMatch();
    if (rivalMatch) ++(report.won ? rivalWins_ : rivalLosses_);
    trust_ = std::clamp(trust_ + static_cast<int>(std::lround((report.rating - 6.5f) * 2.f)), 0, 100);

    if (stage_ == CareerStage::Final) {
        wonFinal_ = report.won;
        finishSeason();
        return report;
    }

    finishRound(nextOpponent(), setsFor, setsAgainst);

    if (rivalMatch) {
        story_ = report.won ? RivalBeaten : RivalLost;
    } else if (rand01() < kInjuryChance) {
        story_ = SoreAnkle;
    } else if (round_ == 2) {
        story_ = PressCredit;
    } else if (round_ == 3 && trust_ >= kCaptainTrust) {
        story_ = Captain;
    } else if (round_ == 4) {
        story_ = Mentor;
    } else if (round_ == 5) {
        story_ = CoachFilm;
    } else if (round_ == 6) {
        story_ = Rumour;
    } else if (lastRating_ < kSlumpRating) {
        story_ = Slump;
    }
    return report;
}

void Career::finishRound(int opponent, int setsFor, int setsAgainst) {
    applyResult(0, opponent, setsFor, setsAgainst);
    for (int team = 1; team < kTeams; ++team) {
        const int against = opponentInRound(round_, team);
        if (team != opponent && team < against) simulateMatch(team, against);
    }
    ++round_;

    if (round_ == kRounds) {
        if (leaguePosition() <= 2) {
            stage_ = CareerStage::Final;
        } else {
            finishSeason();
        }
    }
}

MatchReport Career::sitOut() {
    MatchReport report;
    report.satOut = true;
    report.rating = 0.f;
    report.skillPoints = 1;  // film and gym work
    skillPoints_ += report.skillPoints;

    const int opponent = nextOpponent();
    playSets(teams_[0].strength - kSitOutPenalty, teams_[opponent].strength, report.setsFor, report.setsAgainst);
    report.won = report.setsFor > report.setsAgainst;

    if (benched()) trust_ = std::min(100, trust_ + kBenchedTrustGain);
    injured_ = std::max(0, injured_ - 1);

    if (stage_ == CareerStage::Final) {
        wonFinal_ = report.won;
        finishSeason();
    } else {
        finishRound(opponent, report.setsFor, report.setsAgainst);
    }
    return report;
}

void Career::finishSeason() {
    stage_ = CareerStage::SeasonOver;
    if (wonFinal_) ++trophies_;
    promoted_ = tier_ != Tier::WorldStage && (wonFinal_ || scout_ >= kPromotionInterest);

    SeasonRecord record;
    record.season = season_;
    record.tier = tier_;
    record.club = myTeam().name;
    record.position = leaguePosition();
    record.won = myTeam().won;
    record.lost = myTeam().lost;
    record.rating = seasonMatches_ ? seasonRatingTotal_ / static_cast<float>(seasonMatches_) : 0.f;
    record.champion = wonFinal_;
    record.promoted = promoted_;
    record.mvp = record.rating >= kMvpRating && record.position <= 2;
    history_.push_back(record);

    // The national team doesn't have transfers, so offers stop at the league below it.
    offers_.clear();
    const int next = static_cast<int>(tier_) + (promoted_ ? 1 : 0);
    const int wanted = record.rating >= kTwoOffersRating ? 2 : record.rating >= kOneOfferRating ? 1 : 0;
    if (next == static_cast<int>(Tier::WorldStage)) return;
    std::array<int, 6> clubs = {1, 2, 3, 4, 5, 6};  // never the rival's club, which is index 0
    for (int i = 0; i < wanted; ++i) {
        std::swap(clubs[static_cast<std::size_t>(i)], clubs[static_cast<std::size_t>(i) + static_cast<std::size_t>(rand01() * 5.99f) % (clubs.size() - static_cast<std::size_t>(i))]);
        TransferOffer offer;
        offer.club = kRivals[next][clubs[static_cast<std::size_t>(i)]];
        offer.strength = 5 + static_cast<int>(rand01() * 8.f);
        offer.signingPoints = 2 + static_cast<int>(rand01() * 5.f);
        if (offer.club != club_) offers_.push_back(offer);
    }
}

std::string Career::nextClub() const {
    if (promoted_) return kMyTeam[static_cast<int>(tier_) + 1];
    return myTeam().name;
}

void Career::startNextSeason(int offer) {
    const bool signing = offer >= 0 && offer < static_cast<int>(offers_.size());
    const bool stayedLoyal = !signing && !offers_.empty();
    if (promoted_) {
        tier_ = static_cast<Tier>(static_cast<int>(tier_) + 1);
        club_.clear();
        clubEdge_ = 0;
    }
    if (signing) {
        const TransferOffer& move = offers_[static_cast<std::size_t>(offer)];
        club_ = move.club;
        clubEdge_ = move.strength;
        skillPoints_ += move.signingPoints;
        trust_ = kSignedTrust;
        chemistry_ = kSignedChemistry;
    }
    if (stayedLoyal) chemistry_ = std::min(100, chemistry_ + kLoyaltyChemistry);
    offers_.clear();
    injured_ = 0;  // the summer heals everything
    ++season_;
    scout_ = 0;
    seasonRatingTotal_ = 0.f;
    seasonMatches_ = 0;
    buildLeague();
}

int Career::upgradeCost(Skill s) const {
    return 1 + (skills_[static_cast<int>(s)] - kMinSkill) / 15;
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
    out << "vc26career 4\n" << name_ << '\n';
    out << static_cast<int>(archetype_) << ' ' << rng_ << ' ' << skillPoints_ << ' ' << static_cast<int>(tier_) << ' ' << season_ << ' '
        << round_ << ' ' << static_cast<int>(stage_) << ' ' << scout_ << ' ' << trophies_ << ' ' << matches_ << ' ' << ratingTotal_ << ' '
        << wonFinal_ << ' ' << promoted_ << '\n';
    out << trust_ << ' ' << chemistry_ << ' ' << rivalWins_ << ' ' << rivalLosses_ << ' ' << story_ << '\n';
    for (int s : skills_) out << s << ' ';
    out << '\n';
    for (const LeagueTeam& t : teams_) {
        out << t.name << '\n'
            << t.strength << ' ' << t.played << ' ' << t.won << ' ' << t.lost << ' ' << t.setsFor << ' ' << t.setsAgainst << ' ' << t.points << '\n';
    }
    out << club_ << '\n';
    out << injured_ << ' ' << clubEdge_ << ' ' << seasonRatingTotal_ << ' ' << seasonMatches_ << ' ' << lastRating_ << ' ' << kills_ << ' ' << aces_ << ' '
        << blocks_ << ' ' << bestRating_ << '\n';
    out << offers_.size() << '\n';
    for (const TransferOffer& o : offers_) out << o.club << '\n' << o.strength << ' ' << o.signingPoints << '\n';
    out << history_.size() << '\n';
    for (const SeasonRecord& r : history_) {
        out << r.club << '\n'
            << r.season << ' ' << static_cast<int>(r.tier) << ' ' << r.position << ' ' << r.won << ' ' << r.lost << ' ' << r.rating << ' ' << r.champion << ' '
            << r.promoted << ' ' << r.mvp << '\n';
    }
    out << encode(look_) << '\n';
    return out.str();
}

std::optional<Career> Career::load(const std::string& text) {
    std::istringstream in(text);
    std::string header;
    std::getline(in, header);
    const bool version4 = header == "vc26career 4";
    const bool version3 = version4 || header == "vc26career 3";
    const bool version2 = version3 || header == "vc26career 2";
    if (!version2 && header != "vc26career 1") return std::nullopt;

    std::string name;
    std::getline(in, name);
    int archetype = 0, tier = 0, stage = 0;
    Career c(name, Archetype::PowerSpiker, 1);
    in >> archetype >> c.rng_ >> c.skillPoints_ >> tier >> c.season_ >> c.round_ >> stage >> c.scout_ >> c.trophies_ >> c.matches_ >>
        c.ratingTotal_ >> c.wonFinal_ >> c.promoted_;
    if (version2) in >> c.trust_ >> c.chemistry_ >> c.rivalWins_ >> c.rivalLosses_ >> c.story_;
    for (int& s : c.skills_) in >> s;
    for (LeagueTeam& t : c.teams_) {
        in >> std::ws;
        std::getline(in, t.name);
        in >> t.strength >> t.played >> t.won >> t.lost >> t.setsFor >> t.setsAgainst >> t.points;
    }
    if (version3) {
        std::size_t offers = 0, seasons = 0;
        in.ignore(1);  // the end of the last line, as the club name may be empty
        std::getline(in, c.club_);
        in >> c.injured_ >> c.clubEdge_ >> c.seasonRatingTotal_ >> c.seasonMatches_ >> c.lastRating_ >> c.kills_ >> c.aces_ >> c.blocks_ >> c.bestRating_;
        in >> offers;
        if (in.fail() || offers > kMaxOffers) return std::nullopt;
        c.offers_.resize(offers);
        for (TransferOffer& o : c.offers_) {
            in >> std::ws;
            std::getline(in, o.club);
            in >> o.strength >> o.signingPoints;
        }
        in >> seasons;
        if (in.fail() || seasons > kMaxSeasons) return std::nullopt;
        c.history_.resize(seasons);
        for (SeasonRecord& r : c.history_) {
            int recordTier = 0;
            in >> std::ws;
            std::getline(in, r.club);
            in >> r.season >> recordTier >> r.position >> r.won >> r.lost >> r.rating >> r.champion >> r.promoted >> r.mvp;
            if (recordTier < 0 || recordTier > 2) return std::nullopt;
            r.tier = static_cast<Tier>(recordTier);
        }
    }
    if (in.fail() || archetype < 0 || archetype >= kArchetypes || tier < 0 || tier > 2 || stage < 0 || stage > 2) return std::nullopt;
    const bool skillsOk = std::all_of(c.skills_.begin(), c.skills_.end(), [](int s) { return s >= kMinSkill && s <= kMaxSkill; });
    if (!skillsOk || c.rng_ == 0 || c.round_ < 0 || c.round_ > kRounds || c.season_ < 1 || c.scout_ < 0 || c.scout_ > kPromotionInterest ||
        c.skillPoints_ < 0 || c.trophies_ < 0 || c.matches_ < 0 || c.trust_ < 0 || c.trust_ > 100 || c.chemistry_ < 0 ||
        c.chemistry_ > 100 || c.story_ < -1 || c.story_ >= StoryCount || c.injured_ < 0 || c.seasonMatches_ < 0) {
        return std::nullopt;
    }
    c.archetype_ = static_cast<Archetype>(archetype);
    c.look_ = makeAppearance(c.position(), c.rng_);  // saves from before looks get one
    if (version4) {
        std::string line;
        in >> std::ws;
        std::getline(in, line);
        if (!decode(line, c.look_)) return std::nullopt;
    }
    c.tier_ = static_cast<Tier>(tier);
    c.stage_ = static_cast<CareerStage>(stage);
    return c;
}

}  // namespace vc26
