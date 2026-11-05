// Ball flight, net collision and in/out calls.
// World axes, metres: x runs along the court (net at x = 0, Home on x < 0),
// y runs across it, z is up. Flight is drag-free, so the landing prediction
// behind the GDD 1.3 shadow reticle is exact.
#pragma once

// Defined by Unreal when this is built into the VC26Core module.
#ifndef VC26CORE_API
#define VC26CORE_API
#endif

namespace vc26 {

struct Vec3 {
    float x = 0.f;
    float y = 0.f;
    float z = 0.f;
};

constexpr Vec3 operator+(Vec3 a, Vec3 b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
constexpr Vec3 operator-(Vec3 a, Vec3 b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
constexpr Vec3 operator*(Vec3 a, float s) { return {a.x * s, a.y * s, a.z * s}; }

constexpr float kCourtHalfLength = 9.f;
constexpr float kCourtHalfWidth = 4.5f;
constexpr float kNetHeight = 2.43f;  // men's height; women's is 2.24
constexpr float kBallRadius = 0.105f;
constexpr float kGravity = 9.81f;

struct Ball {
    Vec3 pos;
    Vec3 vel;
};

struct BallStep {
    bool hitNet = false;
    bool landed = false;
    bool crossedOutsideAntenna = false;
};

// Advances the ball by dt. On landing, pos is the exact floor contact point.
VC26CORE_API BallStep stepBall(Ball& ball, float dt);

// Where the ball will be after t seconds, ignoring the net.
VC26CORE_API Vec3 positionAfter(const Ball& ball, float t);

// Seconds until the ball falls through height z, or a negative value if it never does.
VC26CORE_API float timeToHeight(const Ball& ball, float z);

// Velocity that takes the ball from `from` to `to` in exactly `flightTime` seconds.
VC26CORE_API Vec3 launchVelocity(Vec3 from, Vec3 to, float flightTime);

// Flight time of an arc from `from` that peaks at `apex` and falls onto `to`.
VC26CORE_API float flightTimeForApex(Vec3 from, Vec3 to, float apex);

// Shortest flight time at which the arc passes `margin` metres above the net
// (a negative margin aims into it). 0 if the ball never crosses the net.
VC26CORE_API float flightTimeClearingNet(Vec3 from, Vec3 to, float margin);

// A ball touching a line is in.
VC26CORE_API bool isInCourt(Vec3 pos);

}  // namespace vc26
