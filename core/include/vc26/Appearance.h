// What a player looks like. Bodies follow the real averages for each position,
// from Palao, Manzanares and Valades (2014): 1,440 men at the Olympics and World
// Championships from 2000 to 2012. Faces are invented, never anyone real.
#pragma once

#include <array>
#include <cstdint>
#include <string>

#include "vc26/RallySim.h"

namespace vc26 {

enum class Position : std::uint8_t { Setter, Outside, Middle, Opposite, Libero };
constexpr int kPositions = 5;

// Averages and standard deviations. Reach is measured from the floor.
struct PositionProfile {
    float heightCm;
    float heightSpread;
    float massKg;
    float massSpread;
    float spikeReachCm;
    float blockReachCm;
};

VC26CORE_API const PositionProfile& positionProfile(Position position);
VC26CORE_API const char* toString(Position position);
VC26CORE_API Position positionOfShirt(int shirt);  // 1-6 as the lineup has them, 7 the libero

enum class FaceShape : std::uint8_t { Nose, Jaw, Chin, Cheeks, Brow, Forehead, Lips };
constexpr int kFaceShapes = 7;
constexpr int kFaceRange = 5;  // each runs from -5 to 5
constexpr int kFacePresets = 12;

enum class HairStyle : std::uint8_t { Flow, Crop, Buzz, Bald, Mohawk, Curls, Undercut };
constexpr int kHairStyles = 7;
enum class FacialHair : std::uint8_t { None, Stubble, Moustache, Goatee, Beard };
constexpr int kFacialHairs = 5;

constexpr int kSkinTones = 8;
constexpr int kHairColours = 7;
constexpr int kShoes = 6;
constexpr int kKneeSleeves = 4;  // none, black, white, team
constexpr int kArmSleeves = 4;   // none, hitting arm, other arm, both
constexpr int kTattoos = 3;      // none, bands, forearm
constexpr int kHeadbands = 4;    // none, black, white, team
constexpr int kMinHeight = 170;
constexpr int kMaxHeight = 218;
constexpr int kMinMass = 62;
constexpr int kMaxMass = 118;
constexpr int kArmRange = 3;     // arm length, from -3 to 3

VC26CORE_API const char* toString(HairStyle style);
VC26CORE_API const char* toString(FacialHair style);

struct Appearance {
    int heightCm = 190;
    int massKg = 84;
    int arms = 0;
    std::array<int, kFaceShapes> face{};
    int skin = 0;
    int hairColour = 0;
    HairStyle hair = HairStyle::Crop;
    FacialHair facialHair = FacialHair::None;
    int shoes = 0;
    int kneeSleeve = 1;
    int armSleeve = 0;
    int tattoo = 0;
    int headband = 0;
    bool highSocks = false;
    bool fingerTape = false;
};

// How the one base body is reshaped. Scales multiply the body's own proportions,
// and the muscle values are centimetres added to or taken from that part.
struct BodyShape {
    float scale = 1.f;         // the whole body
    float legs = 1.f;
    float arms = 1.f;
    float shoulderWidth = 1.f;
    float head = 1.f;
    float shoulders = 0.f;
    float upperArms = 0.f;
    float chest = 0.f;
    float waist = 0.f;
    float thighs = 0.f;
    float calves = 0.f;
};

// A player from that position's real spread.
VC26CORE_API Appearance makeAppearance(Position position, std::uint32_t seed);
// The same player every time for a team and shirt. Team-mates tend to share a background.
VC26CORE_API Appearance makeTeamAppearance(const std::string& teamName, int shirt);
VC26CORE_API std::array<int, kFaceShapes> facePreset(int preset);

VC26CORE_API BodyShape bodyShape(const Appearance& look);
VC26CORE_API int spikeReachCm(const Appearance& look, Position position);
VC26CORE_API int blockReachCm(const Appearance& look, Position position);

// What the body gives and takes against the position's average: height helps at
// the net and costs low on the floor. Applied once, when a player is created.
VC26CORE_API SkillRatings physiqueEdge(const Appearance& look, Position position);

VC26CORE_API bool isValid(const Appearance& look);
VC26CORE_API std::string encode(const Appearance& look);
VC26CORE_API bool decode(const std::string& text, Appearance& look);

}  // namespace vc26
