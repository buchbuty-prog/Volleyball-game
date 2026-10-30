// Ball flight and serve resolution (GDD 1.2). Units: centimetres and seconds.
// Court frame: x along the court (net at x = 0, home side x < 0), y across
// (home team's right is +y), z up from the floor.
#pragma once

#include <optional>

namespace vc26 {

struct Vec3 {
    double x = 0.0;
    double y = 0.0;
    double z = 0.0;
};

struct CourtGeometry {
    double halfLength = 900.0;  // 18 m court
    double halfWidth = 450.0;   // 9 m court
    double netHeight = 243.0;   // men; 224 for women
    double ballRadius = 10.5;
    double gravity = 981.0;
};

struct Trajectory {
    Vec3 start;
    Vec3 velocity;
    double gravity = 981.0;

    Vec3 at(double t) const;
    // Time the ball is at height z on its way down; nullopt if it never is.
    std::optional<double> timeDownAt(double z) const;
    // Time the ball crosses the plane x = value; nullopt if it never does.
    std::optional<double> timeAtX(double x) const;
};

// Arc from `from` to `to` whose highest point is at height apexZ.
// apexZ is raised to just above the higher end point if needed.
Trajectory solveArc(const Vec3& from, const Vec3& to, double apexZ, double gravity = 981.0);

enum class ServeOutcome { In, Net, Out };

// Power gauge sweet spot: hitting exactly kServeSweetSpot lands on the target.
constexpr double kServeSweetSpot = 0.78;

struct ServePlan {
    Trajectory flight;
    Vec3 landing;          // where the ball lands (or where it meets the net)
    double flightTime = 0; // seconds until landing / net contact
    ServeOutcome outcome = ServeOutcome::In;
};

// power in [0, 1] from the gauge. Too little power drops the ball short (into
// the net); too much carries it long. The server hits from `hand`, aiming at
// `target` on the opponent's floor.
ServePlan planServe(const Vec3& hand, const Vec3& target, double power, const CourtGeometry& court = {});

// True if a ball landing at `p` touches the court on the side opposite to
// `fromHomeSide` (lines are in).
bool landsInOpponentCourt(const Vec3& p, bool fromHomeSide, const CourtGeometry& court = {});

}  // namespace vc26
