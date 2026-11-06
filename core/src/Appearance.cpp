#include "vc26/Appearance.h"

#include <algorithm>
#include <cmath>
#include <sstream>

#include "vc26/Commentary.h"

namespace vc26 {

namespace {

constexpr float kBaseHeight = 178.f;  // the base body, in cm
constexpr float kBaseBmi = 22.9f;     // the study's average, which the base body matches
constexpr float kBmiSpread = 1.6f;
constexpr float kHipShare = 0.54f;    // how much of a body's height is legs
constexpr float kEdgePerSpread = 1.5f;
constexpr int kMaxEdge = 3;

// Height and mass in Tables 5 and 6 of the study, and reach in Tables 8 and 9.
constexpr PositionProfile kProfiles[kPositions] = {
    {192.f, 6.f, 84.8f, 7.1f, 335.f, 318.f},  // setter
    {197.f, 5.f, 89.0f, 7.0f, 345.f, 327.f},  // outside
    {202.f, 5.f, 92.1f, 7.8f, 348.f, 330.f},  // middle
    {199.f, 6.f, 91.1f, 7.8f, 348.f, 329.f},  // opposite
    {186.f, 6.f, 81.2f, 6.6f, 326.f, 310.f},  // libero
};

// About normal, with the far tails cut off.
float bell(std::uint32_t& rng) {
    const float sum = nextRand01(rng) + nextRand01(rng) + nextRand01(rng) + nextRand01(rng);
    return std::clamp((sum - 2.f) * 1.732f, -2.5f, 2.5f);
}

int pick(std::uint32_t& rng, int count) {
    return std::min(static_cast<int>(nextRand01(rng) * static_cast<float>(count)), count - 1);
}

bool chance(std::uint32_t& rng, float odds) {
    return nextRand01(rng) < odds;
}

template <std::size_t N>
int weighted(std::uint32_t& rng, const int (&weights)[N]) {
    int total = 0;
    for (const int w : weights) total += w;
    int roll = pick(rng, total);
    for (std::size_t i = 0; i < N; ++i) {
        roll -= weights[i];
        if (roll < 0) return static_cast<int>(i);
    }
    return 0;
}

std::uint32_t nonZero(std::uint32_t seed) {
    return seed ? seed : 0x9E3779B9u;
}

Appearance randomLook(Position position, std::uint32_t& rng) {
    const PositionProfile& profile = positionProfile(position);
    Appearance look;
    look.heightCm = std::clamp(static_cast<int>(std::lround(profile.heightCm + bell(rng) * profile.heightSpread)), kMinHeight, kMaxHeight);
    // Tall players are heavier, so only what's left over is left to chance.
    const float forHeight = profile.massKg + (static_cast<float>(look.heightCm) - profile.heightCm) * 0.9f;
    look.massKg = std::clamp(static_cast<int>(std::lround(forHeight + bell(rng) * profile.massSpread * 0.7f)), kMinMass, kMaxMass);
    look.arms = std::clamp(static_cast<int>(std::lround(bell(rng))), -kArmRange, kArmRange);
    for (int& shape : look.face) {
        shape = std::clamp(static_cast<int>(std::lround(bell(rng) * 2.f)), -kFaceRange, kFaceRange);
    }
    look.skin = pick(rng, kSkinTones);
    look.hairColour = pick(rng, kHairColours);

    // Short hair is what most of these players wear on court.
    static const int kStyles[kHairStyles] = {10, 35, 15, 5, 5, 10, 20};
    look.hair = static_cast<HairStyle>(weighted(rng, kStyles));
    static const int kBeards[kFacialHairs] = {45, 25, 5, 10, 15};
    look.facialHair = static_cast<FacialHair>(weighted(rng, kBeards));

    look.shoes = pick(rng, kShoes);
    // Liberos spend the match on the floor, and blockers tape their fingers.
    look.kneeSleeve = position == Position::Libero || chance(rng, 0.7f) ? 1 + pick(rng, kKneeSleeves - 1) : 0;
    look.armSleeve = chance(rng, 0.2f) ? 1 + pick(rng, kArmSleeves - 1) : 0;
    look.tattoo = chance(rng, 0.25f) ? 1 + pick(rng, kTattoos - 1) : 0;
    look.headband = chance(rng, 0.08f) ? 1 + pick(rng, kHeadbands - 1) : 0;
    look.highSocks = chance(rng, 0.12f);
    look.fingerTape = chance(rng, position == Position::Middle || position == Position::Opposite ? 0.55f : 0.25f);
    return look;
}

}  // namespace

const PositionProfile& positionProfile(Position position) {
    return kProfiles[static_cast<int>(position)];
}

const char* toString(Position position) {
    switch (position) {
        case Position::Setter: return "Setter";
        case Position::Outside: return "Outside Hitter";
        case Position::Middle: return "Middle Blocker";
        case Position::Opposite: return "Opposite";
        case Position::Libero: return "Libero";
    }
    return "";
}

const char* toString(HairStyle style) {
    switch (style) {
        case HairStyle::Flow: return "Flow";
        case HairStyle::Crop: return "Crop";
        case HairStyle::Buzz: return "Buzz Cut";
        case HairStyle::Bald: return "Shaved";
        case HairStyle::Mohawk: return "Mohawk";
        case HairStyle::Curls: return "Curls";
        case HairStyle::Undercut: return "Undercut";
    }
    return "";
}

const char* toString(FacialHair style) {
    switch (style) {
        case FacialHair::None: return "Clean Shaven";
        case FacialHair::Stubble: return "Stubble";
        case FacialHair::Moustache: return "Moustache";
        case FacialHair::Goatee: return "Goatee";
        case FacialHair::Beard: return "Beard";
    }
    return "";
}

Position positionOfShirt(int shirt) {
    switch (shirt) {
        case 1: return Position::Setter;
        case 2: case 5: return Position::Outside;
        case 3: case 6: return Position::Middle;
        case 4: return Position::Opposite;
        default: return Position::Libero;
    }
}

Appearance makeAppearance(Position position, std::uint32_t seed) {
    std::uint32_t rng = nonZero(seed);
    return randomLook(position, rng);
}

Appearance makeTeamAppearance(const std::string& teamName, int shirt) {
    const std::uint32_t team = nameHash(teamName);
    std::uint32_t rng = nonZero(team ^ (static_cast<std::uint32_t>(shirt) * 2654435761u));
    nextRand01(rng);
    Appearance look = randomLook(positionOfShirt(shirt), rng);
    // Most of a squad comes from one part of the world, with players from elsewhere.
    if (chance(rng, 0.6f)) {
        look.skin = std::clamp(static_cast<int>(team % kSkinTones) + pick(rng, 3) - 1, 0, kSkinTones - 1);
    }
    return look;
}

std::array<int, kFaceShapes> facePreset(int preset) {
    std::uint32_t rng = nonZero(static_cast<std::uint32_t>(preset + 1) * 40503u);
    nextRand01(rng);
    std::array<int, kFaceShapes> face{};
    for (int& shape : face) {
        shape = std::clamp(static_cast<int>(std::lround(bell(rng) * 2.4f)), -kFaceRange, kFaceRange);
    }
    return face;
}

BodyShape bodyShape(const Appearance& look) {
    const float height = static_cast<float>(look.heightCm);
    const float metres = height / 100.f;
    const float bmi = static_cast<float>(look.massKg) / (metres * metres);
    const float build = std::clamp((bmi - kBaseBmi) / kBmiSpread, -2.5f, 3.f);
    // At the same BMI a taller body is a narrower one.
    const float slim = (height - kBaseHeight) * 0.02f;

    BodyShape shape;
    shape.legs = std::clamp(1.f + (height - 190.f) * 0.0016f, 0.97f, 1.05f);
    shape.arms = shape.legs + static_cast<float>(look.arms) * 0.012f;
    shape.scale = height / kBaseHeight / (1.f + kHipShare * (shape.legs - 1.f));
    shape.shoulderWidth = 1.f + build * 0.05f;
    shape.head = std::pow(kBaseHeight / height, 0.7f);  // heads grow less than bodies do
    shape.shoulders = build * 0.8f - slim * 0.3f;
    shape.upperArms = build * 0.5f - slim * 0.3f;
    shape.chest = build * 0.8f - slim * 0.5f;
    shape.waist = std::max(build, 0.f) * 0.9f + std::min(build, 0.f) * 0.5f - slim * 0.6f;
    shape.thighs = build * 0.7f - slim * 0.4f;
    shape.calves = build * 0.35f - slim * 0.2f;
    return shape;
}

int spikeReachCm(const Appearance& look, Position position) {
    const PositionProfile& profile = positionProfile(position);
    const float extra = (static_cast<float>(look.heightCm) - profile.heightCm) * 1.3f + static_cast<float>(look.arms) * 2.f;
    return static_cast<int>(std::lround(profile.spikeReachCm + extra));
}

int blockReachCm(const Appearance& look, Position position) {
    const PositionProfile& profile = positionProfile(position);
    const float extra = (static_cast<float>(look.heightCm) - profile.heightCm) * 1.3f + static_cast<float>(look.arms) * 2.f;
    return static_cast<int>(std::lround(profile.blockReachCm + extra));
}

SkillRatings physiqueEdge(const Appearance& look, Position position) {
    const PositionProfile& profile = positionProfile(position);
    const float spreads = (static_cast<float>(look.heightCm) - profile.heightCm) / profile.heightSpread;
    const int edge = std::clamp(static_cast<int>(std::lround(spreads * kEdgePerSpread)), -kMaxEdge, kMaxEdge);
    SkillRatings ratings{};
    ratings[static_cast<int>(Skill::Block)] = edge;
    ratings[static_cast<int>(Skill::Attack)] = edge;
    ratings[static_cast<int>(Skill::Pass)] = -edge;
    return ratings;
}

bool isValid(const Appearance& look) {
    const auto inRange = [](int value, int low, int high) { return value >= low && value <= high; };
    const bool face = std::all_of(look.face.begin(), look.face.end(), [&](int shape) { return inRange(shape, -kFaceRange, kFaceRange); });
    return face && inRange(look.heightCm, kMinHeight, kMaxHeight) && inRange(look.massKg, kMinMass, kMaxMass) && inRange(look.arms, -kArmRange, kArmRange) &&
           inRange(look.skin, 0, kSkinTones - 1) && inRange(look.hairColour, 0, kHairColours - 1) && inRange(static_cast<int>(look.hair), 0, kHairStyles - 1) &&
           inRange(static_cast<int>(look.facialHair), 0, kFacialHairs - 1) && inRange(look.shoes, 0, kShoes - 1) && inRange(look.kneeSleeve, 0, kKneeSleeves - 1) &&
           inRange(look.armSleeve, 0, kArmSleeves - 1) && inRange(look.tattoo, 0, kTattoos - 1) && inRange(look.headband, 0, kHeadbands - 1);
}

std::string encode(const Appearance& look) {
    std::ostringstream out;
    out << look.heightCm << ' ' << look.massKg << ' ' << look.arms;
    for (const int shape : look.face) out << ' ' << shape;
    out << ' ' << look.skin << ' ' << look.hairColour << ' ' << static_cast<int>(look.hair) << ' ' << static_cast<int>(look.facialHair) << ' ' << look.shoes << ' '
        << look.kneeSleeve << ' ' << look.armSleeve << ' ' << look.tattoo << ' ' << look.headband << ' ' << look.highSocks << ' ' << look.fingerTape;
    return out.str();
}

bool decode(const std::string& text, Appearance& look) {
    std::istringstream in(text);
    Appearance read;
    int hair = 0, facialHair = 0;
    in >> read.heightCm >> read.massKg >> read.arms;
    for (int& shape : read.face) in >> shape;
    in >> read.skin >> read.hairColour >> hair >> facialHair >> read.shoes >> read.kneeSleeve >> read.armSleeve >> read.tattoo >> read.headband >> read.highSocks >> read.fingerTape;
    if (in.fail() || hair < 0 || hair >= kHairStyles || facialHair < 0 || facialHair >= kFacialHairs) return false;
    read.hair = static_cast<HairStyle>(hair);
    read.facialHair = static_cast<FacialHair>(facialHair);
    if (!isValid(read)) return false;
    look = read;
    return true;
}

}  // namespace vc26
