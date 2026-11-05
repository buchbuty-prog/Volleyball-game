#include "vc26/Ball.h"

#include <algorithm>
#include <cmath>

namespace vc26 {

BallStep stepBall(Ball& ball, float dt) {
    BallStep result;
    const Vec3 prev = ball.pos;

    ball.pos = positionAfter(ball, dt);
    ball.vel.z -= kGravity * dt;

    if ((prev.x < 0.f) != (ball.pos.x < 0.f)) {
        const float f = prev.x / (prev.x - ball.pos.x);
        const Vec3 crossing = prev + (ball.pos - prev) * f;
        if (std::fabs(crossing.y) > kCourtHalfWidth) {
            result.crossedOutsideAntenna = true;
        } else if (crossing.z < kNetHeight + kBallRadius) {
            // Dropped back on the side it came from, with most of its speed gone.
            ball.pos = crossing;
            ball.pos.x = prev.x < 0.f ? -(kBallRadius + 0.01f) : kBallRadius + 0.01f;
            ball.vel.x *= -0.2f;
            ball.vel.y *= 0.5f;
            ball.vel.z = std::min(ball.vel.z, 0.f) * 0.5f;
            result.hitNet = true;
        }
    }

    if (ball.pos.z <= kBallRadius) {
        const float f = prev.z > ball.pos.z ? (prev.z - kBallRadius) / (prev.z - ball.pos.z) : 1.f;
        ball.pos = prev + (ball.pos - prev) * f;
        ball.pos.z = kBallRadius;
        result.landed = true;
    }
    return result;
}

Vec3 positionAfter(const Ball& ball, float t) {
    Vec3 p = ball.pos + ball.vel * t;
    p.z -= 0.5f * kGravity * t * t;
    return p;
}

float timeToHeight(const Ball& ball, float z) {
    const float disc = ball.vel.z * ball.vel.z + 2.f * kGravity * (ball.pos.z - z);
    if (disc < 0.f) return -1.f;
    return (ball.vel.z + std::sqrt(disc)) / kGravity;
}

Vec3 launchVelocity(Vec3 from, Vec3 to, float flightTime) {
    Vec3 v = (to - from) * (1.f / flightTime);
    v.z += 0.5f * kGravity * flightTime;
    return v;
}

float flightTimeForApex(Vec3 from, Vec3 to, float apex) {
    const float peak = std::max(apex, std::max(from.z, to.z) + 0.05f);
    return std::sqrt(2.f * (peak - from.z) / kGravity) + std::sqrt(2.f * (peak - to.z) / kGravity);
}

float flightTimeClearingNet(Vec3 from, Vec3 to, float margin) {
    if ((from.x < 0.f) == (to.x < 0.f)) return 0.f;
    const float f = from.x / (from.x - to.x);
    const float straightLine = from.z + f * (to.z - from.z);
    const float needed = kNetHeight + kBallRadius + margin - straightLine;
    if (needed <= 0.f) return 0.f;
    return std::sqrt(2.f * needed / (kGravity * f * (1.f - f)));
}

bool isInCourt(Vec3 pos) {
    return std::fabs(pos.x) <= kCourtHalfLength + kBallRadius && std::fabs(pos.y) <= kCourtHalfWidth + kBallRadius;
}

}  // namespace vc26
