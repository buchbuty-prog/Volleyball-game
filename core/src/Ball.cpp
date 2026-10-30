#include "vc26/Ball.h"

#include <algorithm>
#include <cmath>

namespace vc26 {

Vec3 Trajectory::at(double t) const {
    return {start.x + velocity.x * t, start.y + velocity.y * t,
            start.z + velocity.z * t - 0.5 * gravity * t * t};
}

std::optional<double> Trajectory::timeDownAt(double z) const {
    // z(t) = start.z + vz t - g/2 t^2 ; take the later root.
    const double a = -0.5 * gravity;
    const double b = velocity.z;
    const double c = start.z - z;
    const double disc = b * b - 4.0 * a * c;
    if (disc < 0.0) return std::nullopt;
    const double t = (-b - std::sqrt(disc)) / (2.0 * a);
    if (t < 0.0) return std::nullopt;
    return t;
}

std::optional<double> Trajectory::timeAtX(double x) const {
    if (velocity.x == 0.0) return std::nullopt;
    const double t = (x - start.x) / velocity.x;
    if (t < 0.0) return std::nullopt;
    return t;
}

Trajectory solveArc(const Vec3& from, const Vec3& to, double apexZ, double gravity) {
    apexZ = std::max(apexZ, std::max(from.z, to.z) + 1.0);
    const double up = std::sqrt(2.0 * gravity * (apexZ - from.z));
    const double tUp = up / gravity;
    const double tDown = std::sqrt(2.0 * (apexZ - to.z) / gravity);
    const double total = tUp + tDown;
    Trajectory tr;
    tr.start = from;
    tr.gravity = gravity;
    tr.velocity = {(to.x - from.x) / total, (to.y - from.y) / total, up};
    return tr;
}

bool landsInOpponentCourt(const Vec3& p, bool fromHomeSide, const CourtGeometry& court) {
    const double r = court.ballRadius;
    const bool rightHalf = fromHomeSide ? p.x >= -r : p.x <= r;
    return rightHalf && std::abs(p.x) <= court.halfLength + r && std::abs(p.y) <= court.halfWidth + r;
}

ServePlan planServe(const Vec3& hand, const Vec3& target, double power, const CourtGeometry& court) {
    power = std::clamp(power, 0.0, 1.0);
    const bool fromHome = hand.x < 0.0;

    // Power error moves the landing point along the serve direction and
    // changes how high the ball travels.
    double dx = target.x - hand.x;
    double dy = target.y - hand.y;
    const double len = std::max(1.0, std::sqrt(dx * dx + dy * dy));
    dx /= len;
    dy /= len;
    const double carry = (power - kServeSweetSpot) * 1000.0;
    const Vec3 landing{target.x + dx * carry, target.y + dy * carry, court.ballRadius};
    const double apex = 260.0 + 320.0 * power;

    ServePlan plan;
    plan.flight = solveArc(hand, landing, apex, court.gravity);
    plan.flightTime = plan.flight.timeDownAt(court.ballRadius).value_or(0.0);
    plan.landing = plan.flight.at(plan.flightTime);

    // Net: does the ball reach the net plane before landing, and is it too low there?
    if (const auto tNet = plan.flight.timeAtX(0.0); tNet && *tNet <= plan.flightTime) {
        const Vec3 atNet = plan.flight.at(*tNet);
        if (atNet.z < court.netHeight + court.ballRadius && std::abs(atNet.y) <= court.halfWidth + 100.0) {
            plan.outcome = ServeOutcome::Net;
            plan.flightTime = *tNet;
            plan.landing = atNet;
            return plan;
        }
    }
    // Fell short on the server's own side: it never crossed the net.
    if (fromHome ? plan.landing.x < 0.0 : plan.landing.x > 0.0) {
        plan.outcome = ServeOutcome::Net;
        return plan;
    }
    plan.outcome = landsInOpponentCourt(plan.landing, fromHome, court) ? ServeOutcome::In : ServeOutcome::Out;
    return plan;
}

}  // namespace vc26
