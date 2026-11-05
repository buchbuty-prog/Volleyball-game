// Phone controls (GDD 3): a floating stick on the left half of the screen, the
// action button on the right half, and a corner tap that changes difficulty.
// Each finger keeps the job it started with until it lifts.
#pragma once

#include <vector>

#include "vc26/RallySim.h"

namespace vc26 {

struct TouchPoint {
    int id = 0;
    float x = 0.f;  // pixels from the left
    float y = 0.f;  // pixels from the top
};

class VC26CORE_API TouchControls {
public:
    // Call once a frame with every finger currently down.
    void update(const TouchPoint* touches, int count, float screenWidth, float screenHeight);

    SimInput input() const;

    bool stickActive() const { return stickId_ >= 0; }
    float stickOriginX() const { return originX_; }
    float stickOriginY() const { return originY_; }
    float stickRadius() const { return radius_; }
    float stickX() const { return stickX_; }  // -1..1, right is positive
    float stickY() const { return stickY_; }  // -1..1, down is positive

    bool actionHeld() const;
    bool difficultyTapped() const { return tapped_; }  // true for one update

private:
    enum class Job { Stick, Action, Ignored };
    struct Finger {
        int id;
        Job job;
    };

    std::vector<Finger> fingers_;
    int stickId_ = -1;
    float originX_ = 0.f;
    float originY_ = 0.f;
    float radius_ = 1.f;
    float stickX_ = 0.f;
    float stickY_ = 0.f;
    bool tapped_ = false;
};

}  // namespace vc26
