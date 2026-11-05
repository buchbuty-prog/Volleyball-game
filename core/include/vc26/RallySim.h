// Playable 6v6 rally: serve, pass, set, attack, referee calls and CPU players.
// The engine only supplies input and draws the result, so whole matches can
// run in the unit tests. Randomness comes from a seeded generator, so the same
// seed and inputs always give the same match.
#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <vector>

#include "vc26/Ball.h"
#include "vc26/MatchScore.h"
#include "vc26/RotationManager.h"

namespace vc26 {

enum class RallyPhase : std::uint8_t { PreServe, InPlay, PointOver, MatchOver };
enum class ContactQuality : std::uint8_t { Perfect, Good, Shank };  // GDD 1.3 timing bands
enum class ContactKind : std::uint8_t { Serve, Pass, Set, Attack, FreeBall, Block };
enum class PointReason : std::uint8_t { Ace, Kill, BallDown, Out, Antenna, Block, NetTouch };

// `error` is seconds before (+) or after (-) the ideal contact.
// Empty when the press is too far off to touch the ball.
VC26CORE_API std::optional<ContactQuality> gradeTiming(float error);

struct SimInput {
    float moveX = 0.f;    // towards the opponent, -1..1
    float moveY = 0.f;    // towards Home's left, -1..1
    bool action = false;  // held state of the action button
};

struct SimPlayer {
    Team team = Team::Home;
    int id = -1;
    Vec3 pos;  // z is jump height
};

struct SimEvent {
    enum class Type : std::uint8_t { Contact, Whiff, NetHit, Point };
    Type type = Type::Contact;
    Team team = Team::Home;  // who touched the ball, or who won the point
    int player = -1;         // index into players(), for contacts
    ContactKind kind = ContactKind::Serve;
    ContactQuality quality = ContactQuality::Good;
    PointReason reason = PointReason::Kill;
};

// The next touch the sim expects.
struct PendingContact {
    bool active = false;
    Team team = Team::Home;
    int player = -1;           // index into players()
    int touch = 1;             // 1 pass, 2 set, 3 attack
    float timeToContact = 0.f; // negative once the ideal moment has passed
    Vec3 point;
};

// A front-row player meeting an attack at the net (GDD 1.6). Timing is graded
// against the hitter's contact: Perfect is a roof block, Good a recoverable
// touch, Shank lets the ball through and risks a net touch.
struct BlockAttempt {
    bool active = false;
    Team team = Team::Home;
    int player = -1;            // index into players()
    float timeToContact = 0.f;  // until the hitter strikes the ball
    Vec3 attackPoint;
    float shade = 0.f;          // CPU blockers guess this far to one side
    bool committed = false;     // has jumped
    ContactQuality quality = ContactQuality::Good;
    float jumpTime = 0.f;
};

class VC26CORE_API RallySim {
public:
    struct Config {
        MatchRules rules = MatchRules::indoor();
        std::uint32_t seed = 1;
        bool humanHome = true;  // false: the CPU plays both teams
        float cpuSkill = 0.7f;  // 0..1
    };

    explicit RallySim(const Config& config);

    // Call at a fixed rate. Input is read for the Home player being controlled.
    void step(float dt, const SimInput& input);

    RallyPhase phase() const { return phase_; }
    const Ball& ball() const { return ball_; }
    const std::array<SimPlayer, 12>& players() const { return players_; }
    const MatchScore& score() const { return score_; }
    const PendingContact& pending() const { return pending_; }
    const BlockAttempt& block() const { return block_; }
    bool humanBlocking() const { return block_.active && !block_.committed && isHumanTeam(block_.team); }
    Team servingTeam() const { return serving_; }
    int controlled() const { return controlled_; }
    int server() const { return serverIndex(); }
    bool humanHome() const { return config_.humanHome; }
    void setCpuSkill(float skill) { config_.cpuSkill = skill; }

    // Where the ball will land; only meaningful while hasLanding() is true.
    bool hasLanding() const { return hasLanding_; }
    Vec3 landing() const { return landing_; }

    bool humanServing() const { return phase_ == RallyPhase::PreServe && config_.humanHome && serving_ == Team::Home; }
    bool serveCharging() const { return charging_; }
    float serveGauge() const;  // 0..1, sweeps while charging
    float serveAim() const { return serveAim_; }

    int contactsThisRally() const { return rallyContacts_; }

    // Returns the events since the last call.
    std::vector<SimEvent> takeEvents();

private:
    void startRally();
    void updatePreServe(float dt, const SimInput& input, bool pressed);
    void updateInPlay(float dt, const SimInput& input, bool pressed);
    void movePlayers(float dt, const SimInput& input);
    void serveHuman(float power);
    void serveCpu();
    void serveAt(Vec3 target, float netMargin);
    void playContact(ContactQuality quality, const SimInput& aim);
    void launch(Team team, int player, ContactKind kind, ContactQuality quality, Vec3 velocity);
    void assignNext();
    void startBlock(Team team, Vec3 attackPoint, float timeToContact);
    bool updateBlock(float dt, bool pressed);  // true if it used the press
    void resolveBlock(Vec3 crossing);
    void endRally(Team winner, PointReason reason);

    int serverIndex() const;
    int chooseReceiver(Team team, int touch, Vec3 point, bool spikeable) const;
    Vec3 basePosition(int player) const;
    bool isHumanTeam(Team team) const { return config_.humanHome && team == Team::Home; }
    ContactQuality cpuQuality();
    float rand01();
    Vec3 scatter(float radius);

    Config config_;
    std::uint32_t rng_;
    MatchScore score_;
    std::array<std::optional<RotationManager>, 2> rotation_;
    std::array<SimPlayer, 12> players_{};

    RallyPhase phase_ = RallyPhase::PreServe;
    Ball ball_;
    Team serving_ = Team::Home;
    float timer_ = 0.f;
    bool prevAction_ = false;

    bool charging_ = false;
    float chargeTime_ = 0.f;
    float serveAim_ = 0.f;

    PendingContact pending_;
    BlockAttempt block_;
    bool hasLanding_ = false;
    Vec3 landing_;

    Team lastTeam_ = Team::Home;
    ContactKind lastKind_ = ContactKind::Serve;
    int lastToucher_ = -1;
    int touches_ = 0;
    bool outsideAntenna_ = false;
    float sinceContact_ = 0.f;
    int rallyContacts_ = 0;
    int controlled_ = 0;

    std::vector<SimEvent> events_;
};

}  // namespace vc26
