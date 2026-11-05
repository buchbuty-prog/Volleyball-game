#include "vc26/RallySim.h"

#include <algorithm>
#include <cmath>
#include <utility>

namespace vc26 {

namespace {

// Feel tuning. Timing windows are seconds either side of the ideal contact.
constexpr float kPerfectWindow = 0.06f;
constexpr float kGoodWindow = 0.15f;
constexpr float kShankWindow = 0.30f;

constexpr float kPlayerSpeed = 6.f;
constexpr float kHumanAcceleration = 26.f;  // m/s^2: full speed in about a quarter of a second
constexpr float kCpuAcceleration = 18.f;
constexpr float kBraking = 16.f;            // how late a player can leave stopping
constexpr float kFormationSpeed = 5.5f;
constexpr float kJogSpeed = 4.5f;           // between rallies
constexpr float kWalkSpeed = 2.5f;
constexpr float kPassReach = 1.8f;     // the furthest dive
constexpr float kCpuPassReach = 1.4f;
constexpr float kCpuDigReach = 1.1f;   // hard-driven balls leave less time to stretch
constexpr float kCleanReach = 1.0f;    // beyond this a pass is a stretch and loses a grade
constexpr float kAssistDelay = 0.12f;  // an idle player starts running to the ball after this
constexpr float kCpuDefenceSpeed = 5.5f;
constexpr float kCpuReaction = 0.18f;  // seconds before a CPU defender starts moving
constexpr float kCpuServeReaction = 0.4f;
constexpr float kPlayReach = 1.5f;
constexpr float kPassHeight = 0.9f;
constexpr float kPassLead = 0.15f;  // passes happen this long before landing, so a late press still beats the floor
constexpr float kSetHeight = 2.4f;
constexpr float kAttackHeight = 3.0f;
constexpr float kMinSpikeHeight = 2.5f;
constexpr float kMaxSpikeDistance = 4.5f;  // from the net
constexpr float kJumpHeight = 0.9f;
constexpr float kJumpLead = 0.45f;       // seconds before contact the hitter leaves the floor
constexpr float kApproachTime = 1.0f;    // the hitter starts the run-up this long before contact
constexpr float kApproachBack = 2.6f;    // metres behind the contact point the run-up starts
constexpr float kTakeoffBack = 0.5f;
constexpr float kApproachSpeed = 6.8f;
constexpr float kMissedJumpGrace = 0.2f; // a hitter who never jumped can still bump it over

constexpr float kBlockReach = 0.5f;    // either side of the blocker
constexpr float kBlockTop = kNetHeight + 0.6f;
constexpr float kBlockerDepth = 0.35f;  // from the net
constexpr float kBlockJumpHeight = 0.7f;
constexpr float kBlockLead = 0.3f;      // blockers jump this long before the hitter strikes
constexpr float kNetTouchChance = 0.06f;  // on a mistimed block

constexpr float kServeHeight = 2.6f;
constexpr float kServeX = 9.7f;
constexpr float kServeDelay = 1.0f;     // before a human can start charging
constexpr float kCpuServeDelay = 2.0f;
constexpr float kTossTime = 0.4f;
constexpr float kGaugeHalfPeriod = 0.8f;
constexpr float kDeepestGoodPower = 0.85f;  // at this power the serve lands on the end line
constexpr float kNettedPower = 0.2f;        // below this the serve doesn't clear the net
constexpr float kServeHop = 0.35f;
constexpr float kPointPause = 2.6f;

float sideSign(Team t) { return t == Team::Home ? -1.f : 1.f; }
float leftSign(Team t) { return t == Team::Home ? 1.f : -1.f; }
Team sideOf(float x) { return x < 0.f ? Team::Home : Team::Away; }

float flatDistance(Vec3 a, Vec3 b) { return std::hypot(a.x - b.x, a.y - b.y); }

// Metres to the team's left for a rotation slot.
float slotAcross(Slot s) {
    if (s == Slot::P1 || s == Slot::P2) return -3.f;
    if (s == Slot::P4 || s == Slot::P5) return 3.f;
    return 0.f;
}

float errorRadius(ContactQuality q, float perfect, float good, float shank) {
    return q == ContactQuality::Perfect ? perfect : q == ContactQuality::Good ? good : shank;
}

// Changes velocity towards the wanted one, no faster than rate allows.
void accelerate(SimPlayer& p, float wantX, float wantY, float rate, float dt) {
    const float dx = wantX - p.vel.x;
    const float dy = wantY - p.vel.y;
    const float gap = std::hypot(dx, dy);
    const float step = rate * dt;
    if (gap <= step) {
        p.vel.x = wantX;
        p.vel.y = wantY;
    } else {
        p.vel.x += dx / gap * step;
        p.vel.y += dy / gap * step;
    }
}

// Runs to a spot and slows down in time to stop on it.
void steer(SimPlayer& p, Vec3 goal, float topSpeed, float rate, float dt) {
    const float d = flatDistance(p.pos, goal);
    if (d < 0.02f) {
        accelerate(p, 0.f, 0.f, rate, dt);
        return;
    }
    const float speed = std::min(topSpeed, std::sqrt(2.f * kBraking * d));
    accelerate(p, (goal.x - p.pos.x) / d * speed, (goal.y - p.pos.y) / d * speed, rate, dt);
}

void jump(SimPlayer& p, float height) {
    if (p.pos.z > 0.f) return;
    p.vz = std::sqrt(2.f * kGravity * height);
    p.vel = p.vel * 0.25f;  // the run-up goes into height, not distance
}

}  // namespace

std::optional<ContactQuality> gradeTiming(float error) {
    const float e = std::fabs(error);
    if (e <= kPerfectWindow) return ContactQuality::Perfect;
    if (e <= kGoodWindow) return ContactQuality::Good;
    if (e <= kShankWindow) return ContactQuality::Shank;
    return std::nullopt;
}

bool inCleanReach(Vec3 feet, Vec3 point) { return flatDistance(feet, point) <= kCleanReach; }

RallySim::RallySim(const Config& config)
    : config_(config), rng_(config.seed ? config.seed : 1u), score_(config.rules) {
    LineupSpec spec;
    spec.starters = {Player{1, Role::Setter},   Player{2, Role::OutsideHitter}, Player{3, Role::MiddleBlocker},
                     Player{4, Role::Opposite}, Player{5, Role::OutsideHitter}, Player{6, Role::MiddleBlocker}};
    rotation_[0] = RotationManager::create(spec, RuleMode::Simulation, true);
    rotation_[1] = RotationManager::create(spec, RuleMode::Simulation, false);

    for (int i = 0; i < 12; ++i) {
        players_[i].team = i < 6 ? Team::Home : Team::Away;
        players_[i].id = i % 6 + 1;
    }
    for (int i = 0; i < 12; ++i) players_[i].pos = basePosition(i);
    startRally();
}

std::vector<SimEvent> RallySim::takeEvents() { return std::exchange(events_, {}); }

Vec3 RallySim::scatter(float radius) {
    const float angle = rand01() * 6.2831853f;
    const float r = radius * std::sqrt(rand01());
    return {std::cos(angle) * r, std::sin(angle) * r, 0.f};
}

float RallySim::skillBoost(Team team, Skill skill) const {
    if (!isHumanTeam(team)) return 0.f;
    return static_cast<float>(config_.humanSkills[static_cast<int>(skill)] - kNeutralSkill) / static_cast<float>(kMaxSkill - kNeutralSkill);
}

float RallySim::serveGauge() const {
    if (!charging_) return 0.f;  // chargeTime_ stops during the toss, which holds the value
    const float t = std::fmod(chargeTime_ / kGaugeHalfPeriod, 2.f);
    return t <= 1.f ? t : 2.f - t;
}

int RallySim::serverIndex() const {
    const int team = static_cast<int>(serving_);
    return team * 6 + rotation_[team]->at(Slot::P1).id - 1;
}

Vec3 RallySim::basePosition(int player) const {
    const SimPlayer& p = players_[player];
    const Slot slot = *rotation_[static_cast<int>(p.team)]->slotOf(p.id);
    const float depth = isFrontRow(slot) ? 2.5f : 6.5f;
    return {sideSign(p.team) * depth, slotAcross(slot) * leftSign(p.team), 0.f};
}

Vec3 RallySim::formationPosition(int player) const {
    const SimPlayer& p = players_[player];
    const RotationManager& rm = *rotation_[static_cast<int>(p.team)];
    const Slot slot = *rm.slotOf(p.id);
    const float own = sideSign(p.team);
    const float left = leftSign(p.team);
    const bool front = isFrontRow(slot);
    const float across = slotAcross(slot);

    if (pending_.active && pending_.team == p.team) {
        // Our ball: the setter goes to the net and hitters back off to approach.
        if (rm.at(slot).role == Role::Setter) return {own * 1.2f, -1.f * left, 0.f};
        return {own * (front ? 3.6f : 6.8f), across * left, 0.f};
    }
    // Their ball: the front row closes the net and the back row digs deep.
    return {own * (front ? 0.9f : 6.3f), (front ? across : across * 0.9f) * left, 0.f};
}

void RallySim::startRally() {
    phase_ = RallyPhase::PreServe;
    timer_ = 0.f;
    charging_ = false;
    tossing_ = false;
    tossTime_ = 0.f;
    chargeTime_ = 0.f;
    pending_ = {};
    block_ = {};
    hasLanding_ = false;
    outsideAntenna_ = false;
    rallyContacts_ = 0;
    touches_ = 0;
    lastToucher_ = -1;

    serving_ = rotation_[0]->serving() ? Team::Home : Team::Away;
    lastTeam_ = serving_;
    lastKind_ = ContactKind::Serve;
    controlled_ = serving_ == Team::Home ? serverIndex() : rotation_[0]->at(Slot::P6).id - 1;
}

void RallySim::step(float dt, const SimInput& input) {
    const bool pressed = input.action && !prevAction_;
    prevAction_ = input.action;

    switch (phase_) {
        case RallyPhase::PreServe:
            updatePreServe(dt, input, pressed);
            break;
        case RallyPhase::InPlay:
            updateInPlay(dt, input, pressed);
            break;
        case RallyPhase::PointOver:
            timer_ += dt;
            // The dead ball bounces away. Nothing depends on where it goes.
            ball_.pos = positionAfter(ball_, dt);
            ball_.vel.z -= kGravity * dt;
            if (ball_.pos.z < kBallRadius) {
                ball_.pos.z = kBallRadius;
                ball_.vel = {ball_.vel.x * 0.8f, ball_.vel.y * 0.8f, std::fabs(ball_.vel.z) * 0.55f};
            }
            if (timer_ >= kPointPause) {
                if (score_.finished()) {
                    phase_ = RallyPhase::MatchOver;
                } else {
                    startRally();
                }
            }
            break;
        case RallyPhase::MatchOver:
            break;
    }
    movePlayers(dt, input);
}

void RallySim::updatePreServe(float dt, const SimInput& input, bool pressed) {
    timer_ += dt;
    SimPlayer& server = players_[serverIndex()];
    const bool human = isHumanTeam(serving_);

    if (human) {
        if (!tossing_) serveAim_ = std::clamp(serveAim_ + input.moveY * 4.f * dt, -kCourtHalfWidth + 0.1f, kCourtHalfWidth - 0.1f);
        if (!charging_ && pressed && timer_ >= kServeDelay) charging_ = true;
    } else if (!charging_ && timer_ >= kCpuServeDelay) {
        charging_ = true;
    }

    if (charging_ && !tossing_) {
        chargeTime_ += dt;
        // The ball stays in the hands until the power is locked in.
        if (!human || !input.action) {
            tossing_ = true;
        }
    }

    float height = 1.1f;
    if (tossing_) {
        tossTime_ += dt;
        const float u = std::min(tossTime_ / kTossTime, 1.f);
        height += (kServeHeight - 1.1f) * std::sin(u * 1.5707963f);
        if (tossTime_ >= 0.12f) jump(server, kServeHop);
    }
    ball_.pos = {server.pos.x - sideSign(serving_) * 0.3f, server.pos.y, height};
    ball_.vel = {};

    if (tossing_ && tossTime_ >= kTossTime) {
        if (human) {
            serveHuman(serveGauge());
        } else {
            serveCpu();
        }
    }
}

void RallySim::serveHuman(float power) {
    const float depth = 2.f + 7.f * power / kDeepestGoodPower;
    serveAt({-sideSign(serving_) * depth, serveAim_, kBallRadius}, power < kNettedPower ? -0.6f : 0.25f);
}

void RallySim::serveCpu() {
    const float across = (rand01() * 2.f - 1.f) * 3.8f;
    const bool fault = rand01() < 0.06f;
    const float depth = fault ? kCourtHalfLength + 0.8f : 3.f + rand01() * 5.5f;
    serveAt({-sideSign(serving_) * depth, across, kBallRadius}, 0.25f);
}

void RallySim::serveAt(Vec3 target, float netMargin) {
    charging_ = false;
    tossing_ = false;
    phase_ = RallyPhase::InPlay;
    const float fastest = 0.95f - 0.12f * skillBoost(serving_, Skill::Serve);
    const float t = std::max(flightTimeClearingNet(ball_.pos, target, netMargin), fastest);
    launch(serving_, serverIndex(), ContactKind::Serve, ContactQuality::Good, launchVelocity(ball_.pos, target, t));
}

void RallySim::launch(Team team, int player, ContactKind kind, ContactQuality quality, Vec3 velocity) {
    if (kind == ContactKind::Block) {
        touches_ = 0;  // a block is not one of the team's three touches
    } else {
        touches_ = team == lastTeam_ && kind != ContactKind::Serve ? touches_ + 1 : 1;
    }
    lastTeam_ = team;
    lastKind_ = kind;
    lastToucher_ = player;
    outsideAntenna_ = false;
    sinceContact_ = 0.f;
    ball_.vel = velocity;
    ++rallyContacts_;

    SimEvent e;
    e.type = SimEvent::Type::Contact;
    e.team = team;
    e.player = player;
    e.kind = kind;
    e.quality = quality;
    e.where = ball_.pos;
    e.stretched = stretching_;
    events_.push_back(e);

    assignNext();
}

int RallySim::chooseReceiver(Team team, int touch, Vec3 point, bool spikeable) const {
    const int first = static_cast<int>(team) * 6;
    int best = -1;
    float bestCost = 1e9f;
    for (int i = first; i < first + 6; ++i) {
        if (i == lastToucher_ && lastTeam_ == team) continue;  // no double contact
        const RotationManager& rm = *rotation_[static_cast<int>(team)];
        const int id = players_[i].id;
        if (touch == 3 && spikeable && !isFrontRow(*rm.slotOf(id))) continue;

        float cost = flatDistance(players_[i].pos, point);
        const bool setter = rm.at(*rm.slotOf(id)).role == Role::Setter;
        if (touch == 2 && setter) cost -= 2.f;
        if (touch == 1 && setter) cost += 1.5f;
        if (cost < bestCost) {
            bestCost = cost;
            best = i;
        }
    }
    return best;
}

void RallySim::assignNext() {
    pending_ = {};
    const float landTime = timeToHeight(ball_, kBallRadius);
    hasLanding_ = landTime >= 0.f;
    if (!hasLanding_) return;
    landing_ = positionAfter(ball_, landTime);

    const Team side = sideOf(landing_.x);
    if (lastKind_ == ContactKind::Serve && side == lastTeam_) return;  // netted serve is dead
    const int touch = side == lastTeam_ ? touches_ + 1 : 1;
    if (touch > 3) return;
    if (touch == 1 && !isHumanTeam(side) && !isInCourt(landing_)) {
        // CPU players leave balls that are clearly going out.
        const float over = std::max(std::fabs(landing_.x) - kCourtHalfLength, std::fabs(landing_.y) - kCourtHalfWidth);
        if (over > 0.25f) return;
    }

    float contactTime = touch == 1 ? std::max(landTime - kPassLead, 0.02f)
                                   : timeToHeight(ball_, touch == 2 ? kSetHeight : kAttackHeight);
    if (contactTime < 0.f || sideOf(positionAfter(ball_, contactTime).x) != side) {
        contactTime = timeToHeight(ball_, kPassHeight);
    }
    if (contactTime < 0.f) contactTime = std::max(landTime - 0.05f, 0.f);

    const Vec3 point = positionAfter(ball_, contactTime);
    if (sideOf(point.x) != side) return;

    const bool spikeable = point.z >= kMinSpikeHeight && std::fabs(point.x) <= kMaxSpikeDistance;
    const int receiver = chooseReceiver(side, touch, point, spikeable);
    if (receiver < 0) return;

    pending_.active = true;
    pending_.team = side;
    pending_.player = receiver;
    pending_.touch = touch;
    pending_.timeToContact = contactTime;
    pending_.point = point;
    pending_.spike = touch == 3 && spikeable;
    pending_.pressLead = pending_.spike ? kJumpLead : 0.f;
    if (isHumanTeam(side)) controlled_ = receiver;
    if (touch == 3 && spikeable && !block_.active) startBlock(other(side), point, contactTime);
}

void RallySim::startBlock(Team team, Vec3 attackPoint, float timeToContact) {
    const RotationManager& rm = *rotation_[static_cast<int>(team)];
    const int first = static_cast<int>(team) * 6;
    int best = -1;
    float bestGap = 1e9f;
    for (int i = first; i < first + 6; ++i) {
        if (!isFrontRow(*rm.slotOf(players_[i].id))) continue;
        const float gap = std::fabs(players_[i].pos.y - attackPoint.y);
        if (gap < bestGap) {
            bestGap = gap;
            best = i;
        }
    }

    block_ = {};
    block_.active = true;
    block_.team = team;
    block_.player = best;
    block_.timeToContact = timeToContact;
    block_.pressLead = kBlockLead;
    block_.attackPoint = attackPoint;
    block_.shade = (rand01() * 2.f - 1.f) * 0.8f;
    if (isHumanTeam(team)) controlled_ = best;
}

bool RallySim::updateBlock(float dt, bool pressed) {
    if (!block_.active) return false;
    block_.timeToContact -= dt;

    bool usedPress = false;
    if (!block_.committed) {
        const float error = block_.timeToContact - kBlockLead;
        if (isHumanTeam(block_.team)) {
            if (pressed) {
                if (const auto quality = gradeTiming(error)) {
                    block_.committed = true;
                    block_.quality = *quality;
                    usedPress = true;
                }
            }
        } else if (error <= 0.05f) {
            const float r = rand01();
            block_.committed = true;
            block_.quality = r < 0.1f + 0.2f * config_.cpuSkill ? ContactQuality::Perfect
                             : r < 0.3f + 0.4f * config_.cpuSkill ? ContactQuality::Good
                                                                  : ContactQuality::Shank;
        }
        if (block_.committed) jump(players_[block_.player], kBlockJumpHeight);
    }
    if (block_.timeToContact < -0.5f) block_.active = false;  // the attack never came
    return usedPress;
}

float RallySim::blockReach(Team team) const {
    return kBlockReach * (1.f + 0.3f * skillBoost(team, Skill::Block));
}

void RallySim::resolveBlock(Vec3 crossing) {
    const BlockAttempt attempt = block_;
    block_.active = false;
    if (!attempt.committed || lastKind_ != ContactKind::Attack) return;
    if (std::fabs(crossing.y - players_[attempt.player].pos.y) > blockReach(attempt.team) || crossing.z > kBlockTop) return;

    const float own = sideSign(attempt.team);
    if (attempt.quality == ContactQuality::Shank) {
        if (rand01() < kNetTouchChance) endRally(other(attempt.team), PointReason::NetTouch);
        return;  // through the hands
    }

    Vec3 from = crossing;
    if (attempt.quality == ContactQuality::Perfect) {
        // Roofed straight down on the hitter's side.
        from.x = -own * 0.15f;
        ball_.pos = from;
        const Vec3 target{-own * (1.5f + rand01() * 3.f), std::clamp(crossing.y, -4.f, 4.f), kBallRadius};
        launch(attempt.team, attempt.player, ContactKind::Block, attempt.quality, launchVelocity(from, target, 0.75f));
    } else {
        // Soft touch up on the blocker's side, playable by their team.
        from.x = own * 0.15f;
        ball_.pos = from;
        const Vec3 target{own * 4.5f, crossing.y * 0.7f, kBallRadius};
        launch(attempt.team, attempt.player, ContactKind::Block, attempt.quality, launchVelocity(from, target, flightTimeForApex(from, target, 4.2f)));
    }
}

ContactQuality RallySim::cpuQuality() {
    const float speed = std::sqrt(ball_.vel.x * ball_.vel.x + ball_.vel.y * ball_.vel.y + ball_.vel.z * ball_.vel.z);
    const float hard = pending_.touch == 1 ? std::clamp((speed - 10.f) / 8.f, 0.f, 1.f) : 0.f;
    const float perfect = (0.25f + 0.45f * config_.cpuSkill) * (1.f - 0.7f * hard);
    const float serveBonus = lastKind_ == ContactKind::Serve ? 0.3f : 0.f;
    const float shank = 0.25f - 0.2f * config_.cpuSkill + 0.35f * hard + serveBonus;
    const float r = rand01();
    if (r < perfect) return ContactQuality::Perfect;
    return r < 1.f - shank ? ContactQuality::Good : ContactQuality::Shank;
}

void RallySim::playContact(ContactQuality quality, const SimInput& aim) {
    const PendingContact contact = pending_;
    const Team team = contact.team;
    const float own = sideSign(team);
    const bool human = isHumanTeam(team);
    const Vec3 from = ball_.pos;

    if (contact.touch == 1) {
        const float accuracy = 1.f - 0.45f * skillBoost(team, Skill::Pass);
        const Vec3 target = Vec3{own * 1.5f, 0.f, kSetHeight} + scatter(errorRadius(quality, 0.2f, 1.f, 3.2f) * accuracy);
        const float apex = quality == ContactQuality::Shank ? 3.f + rand01() * 2.5f : 4.3f;
        launch(team, contact.player, ContactKind::Pass, quality, launchVelocity(from, target, flightTimeForApex(from, target, apex)));
        return;
    }

    if (contact.touch == 2) {
        float across = (static_cast<int>(rand01() * 3.f) - 1) * 3.2f;
        if (human) across = std::fabs(aim.moveY) > 0.3f ? (aim.moveY > 0.f ? 3.2f : -3.2f) : 0.f;
        const float accuracy = 1.f - 0.45f * skillBoost(team, Skill::Set);
        const Vec3 target = Vec3{own * 1.f, across, kAttackHeight} + scatter(errorRadius(quality, 0.1f, 0.5f, 1.8f) * accuracy);
        launch(team, contact.player, ContactKind::Set, quality, launchVelocity(from, target, flightTimeForApex(from, target, 4.6f)));
        return;
    }

    float across = (rand01() * 2.f - 1.f) * 3.8f;
    float depth = 3.f + rand01() * 5.2f;
    if (human) {
        across = aim.moveY * 3.8f;
        depth = 6.f + aim.moveX * 2.3f;
    } else {
        // Better CPU hitters look at more shots and take the one furthest from a defender.
        const int looks = config_.cpuSkill < 0.5f ? 1 : config_.cpuSkill < 0.9f ? 2 : 3;
        float bestGap = -1.f;
        for (int look = 0; look < looks; ++look) {
            const float y = (rand01() * 2.f - 1.f) * 3.8f;
            const float d = 3.f + rand01() * 5.2f;
            float gap = 1e9f;
            const int firstDefender = static_cast<int>(other(team)) * 6;
            for (int i = firstDefender; i < firstDefender + 6; ++i) {
                gap = std::min(gap, flatDistance(players_[i].pos, Vec3{-own * d, y, 0.f}));
            }
            if (gap > bestGap) {
                bestGap = gap;
                across = y;
                depth = d;
            }
        }
    }

    if (!contact.spike) {
        const Vec3 target{-own * 5.f, across, kBallRadius};
        launch(team, contact.player, ContactKind::FreeBall, quality, launchVelocity(from, target, flightTimeForApex(from, target, 4.5f)));
        return;
    }

    const Vec3 target = Vec3{-own * depth, across, kBallRadius} + scatter(errorRadius(quality, 0.2f, 0.7f, 2.2f));
    float speed = quality == ContactQuality::Perfect ? 18.f : quality == ContactQuality::Good ? 14.f : 10.f;
    speed *= human ? 1.f + 0.2f * skillBoost(team, Skill::Attack) : 1.f + (config_.cpuSkill - 0.7f) * 0.9f;
    const float margin = quality == ContactQuality::Shank && rand01() < 0.4f ? -0.4f : 0.08f;
    const float t = std::max(flatDistance(from, target) / speed, flightTimeClearingNet(from, target, margin));
    launch(team, contact.player, ContactKind::Attack, quality, launchVelocity(from, target, t));
}

void RallySim::updateInPlay(float dt, const SimInput& input, bool pressed) {
    sinceContact_ += dt;
    if (updateBlock(dt, pressed)) pressed = false;
    if (pending_.active) {
        pending_.timeToContact -= dt;
        const bool human = isHumanTeam(pending_.team);

        std::optional<ContactQuality> quality;
        if (pending_.spike) {
            // The press is the jump. The hit itself comes at the top of it.
            const float error = pending_.timeToContact - kJumpLead;
            if (!pending_.jumped) {
                if (human) {
                    if (pressed) {
                        if (const auto graded = gradeTiming(error)) {
                            pending_.jumped = true;
                            pending_.jumpQuality = *graded;
                        }
                    }
                } else if (error <= 0.f) {
                    pending_.jumped = true;
                    pending_.jumpQuality = cpuQuality();
                }
                if (pending_.jumped) jump(players_[pending_.player], kJumpHeight);
            }
            if (pending_.jumped && pending_.timeToContact <= 0.f) {
                quality = pending_.jumpQuality;
            } else if (!pending_.jumped && pending_.timeToContact < -kMissedJumpGrace) {
                pending_.spike = false;  // never left the floor: bump it over instead
                quality = ContactQuality::Shank;
            }
        } else if (human) {
            if (pressed && pending_.timeToContact <= kShankWindow) {
                quality = gradeTiming(pending_.timeToContact);
            } else if (pending_.timeToContact < -kShankWindow) {
                pending_.active = false;
            }
        } else if (pending_.timeToContact <= 0.f) {
            quality = cpuQuality();
        }

        if (quality) {
            const float reach = pending_.touch > 1 ? kPlayReach : human ? kPassReach : lastKind_ == ContactKind::Attack ? kCpuDigReach : kCpuPassReach;
            // Standing on the ring counts, even if a late press lets a fast ball go past.
            const Vec3 feet = players_[pending_.player].pos;
            const float gap = std::min(flatDistance(feet, ball_.pos), flatDistance(feet, pending_.point));
            if (gap <= reach) {
                stretching_ = pending_.touch == 1 && gap > kCleanReach;
                if (stretching_) quality = *quality == ContactQuality::Perfect ? ContactQuality::Good : ContactQuality::Shank;
                playContact(*quality, input);
                stretching_ = false;
            } else if (human && pending_.touch > 1 && !pending_.spike) {
                // Pressed before arriving under the ball: wait and let them try again.
            } else {
                SimEvent e;
                e.type = SimEvent::Type::Whiff;
                e.team = pending_.team;
                events_.push_back(e);
                pending_.active = false;
            }
        }
    }

    const Vec3 before = ball_.pos;
    const BallStep result = stepBall(ball_, dt);
    outsideAntenna_ = outsideAntenna_ || result.crossedOutsideAntenna;

    if (block_.active && !result.hitNet && (before.x < 0.f) != (ball_.pos.x < 0.f)) {
        const float f = before.x / (before.x - ball_.pos.x);
        resolveBlock(before + (ball_.pos - before) * f);
        if (phase_ != RallyPhase::InPlay) return;  // net touch ended the rally
    }

    if (result.hitNet) {
        SimEvent e;
        e.type = SimEvent::Type::NetHit;
        e.team = lastTeam_;
        events_.push_back(e);
        assignNext();
    }

    if (result.landed) {
        const Team side = sideOf(ball_.pos.x);
        // A serve that aces the passer counts even if the pass reached the floor first.
        const bool shankedServe = lastKind_ == ContactKind::Pass && rallyContacts_ == 2;
        if (outsideAntenna_) {
            endRally(other(lastTeam_), PointReason::Antenna);
        } else if (!isInCourt(ball_.pos)) {
            endRally(other(lastTeam_), shankedServe ? PointReason::Ace : PointReason::Out);
        } else if (side == lastTeam_) {
            endRally(other(side), shankedServe ? PointReason::Ace : PointReason::BallDown);
        } else {
            endRally(other(side), lastKind_ == ContactKind::Serve ? PointReason::Ace
                                  : lastKind_ == ContactKind::Block ? PointReason::Block
                                                                    : PointReason::Kill);
        }
    }
}

void RallySim::endRally(Team winner, PointReason reason) {
    phase_ = RallyPhase::PointOver;
    timer_ = 0.f;
    pending_ = {};
    block_ = {};
    hasLanding_ = false;
    ball_.vel = {ball_.vel.x * 0.5f, ball_.vel.y * 0.5f, ball_.pos.z <= kBallRadius ? std::fabs(ball_.vel.z) * 0.55f : ball_.vel.z};

    pointWinner_ = winner;
    const bool clutch = score_.setPointFor().has_value();
    score_.awardPoint(winner);
    rotation_[0]->onRallyEnd(winner == Team::Home);
    rotation_[1]->onRallyEnd(winner == Team::Away);

    SimEvent e;
    e.type = SimEvent::Type::Point;
    e.team = winner;
    e.reason = reason;
    e.clutch = clutch;
    events_.push_back(e);
}

void RallySim::movePlayers(float dt, const SimInput& input) {
    for (int i = 0; i < 12; ++i) {
        SimPlayer& p = players_[i];
        const float own = sideSign(p.team);
        const bool human = isHumanTeam(p.team);
        const float rate = human ? kHumanAcceleration : kCpuAcceleration;

        // Nobody changes direction in the air.
        if (p.pos.z <= 0.f && p.vz <= 0.f) {
            const bool live = phase_ == RallyPhase::InPlay;
            const bool chosen = live && pending_.active && pending_.player == i;
            const bool server = phase_ == RallyPhase::PreServe && i == serverIndex();
            const bool blocking = block_.active && block_.player == i;

            if (blocking) {
                float y = p.pos.y;
                if (block_.committed) {
                    accelerate(p, 0.f, 0.f, rate, dt);
                } else {
                    if (!human) y = block_.attackPoint.y + block_.shade;
                    else if (controlled_ == i) y = p.pos.y + input.moveY * 1.5f;
                    y = std::clamp(y, -kCourtHalfWidth, kCourtHalfWidth);
                    steer(p, Vec3{own * kBlockerDepth, y, 0.f}, kPlayerSpeed, kHumanAcceleration, dt);
                }
            } else if (chosen && pending_.spike) {
                // Hold the run-up spot, then attack the ball.
                const Vec3 point = pending_.point;
                if (pending_.timeToContact > kApproachTime) {
                    steer(p, Vec3{point.x + own * kApproachBack, point.y, 0.f}, kFormationSpeed, rate, dt);
                } else {
                    steer(p, Vec3{point.x + own * kTakeoffBack, point.y, 0.f}, kApproachSpeed, kHumanAcceleration, dt);
                }
            } else if (chosen && pending_.touch > 1) {
                steer(p, pending_.point, kPlayerSpeed + 0.5f, kHumanAcceleration, dt);
            } else if (human && i == controlled_ && !server && (live || phase_ == RallyPhase::PreServe)) {
                float x = input.moveX;
                float y = input.moveY;
                const float len = std::hypot(x, y);
                if (len > 1.f) {
                    x /= len;
                    y /= len;
                }
                if (chosen && len < 0.1f && sinceContact_ >= kAssistDelay) {
                    steer(p, pending_.point, kPlayerSpeed, kHumanAcceleration, dt);  // assist until the player steers
                } else {
                    accelerate(p, x * kPlayerSpeed, y * kPlayerSpeed, kHumanAcceleration, dt);
                }
            } else if (chosen) {
                const float skillShift = config_.cpuSkill - 0.7f;  // 0.7 is the tuned baseline
                const float reaction = (lastKind_ == ContactKind::Serve ? kCpuServeReaction : kCpuReaction) - skillShift * 0.4f;
                if (sinceContact_ >= reaction) {
                    steer(p, pending_.point, kCpuDefenceSpeed + skillShift * 2.f, rate, dt);
                }
            } else if (server) {
                steer(p, Vec3{own * kServeX, basePosition(i).y, 0.f}, kJogSpeed, rate, dt);
            } else if (live) {
                steer(p, formationPosition(i), kFormationSpeed, rate, dt);
            } else if (phase_ == RallyPhase::PreServe) {
                steer(p, basePosition(i), kJogSpeed, rate, dt);
            } else if (p.team == pointWinner_) {
                // Winners come together for a moment.
                const float angle = static_cast<float>(i % 6) * 1.0472f;
                const Vec3 huddle{own * 4.f + std::cos(angle) * 0.9f, std::sin(angle) * 0.9f, 0.f};
                steer(p, huddle, kJogSpeed, rate, dt);
            } else {
                steer(p, basePosition(i), kWalkSpeed, rate, dt);
            }
        }

        p.pos.x += p.vel.x * dt;
        p.pos.y += p.vel.y * dt;
        if (p.pos.z > 0.f || p.vz > 0.f) {
            p.pos.z += p.vz * dt;
            p.vz -= kGravity * dt;
            if (p.pos.z <= 0.f) {
                p.pos.z = 0.f;
                p.vz = 0.f;
                p.vel = p.vel * 0.35f;  // absorbing the landing
            }
        }

        // Stay on your own side of the net and inside the hall.
        if (p.pos.x * own < 0.2f) {
            p.pos.x = own * 0.2f;
            p.vel.x = 0.f;
        }
        p.pos.x = std::clamp(p.pos.x, -13.f, 13.f);
        p.pos.y = std::clamp(p.pos.y, -8.f, 8.f);
    }
}

}  // namespace vc26
