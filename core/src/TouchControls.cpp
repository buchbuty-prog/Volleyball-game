#include "vc26/TouchControls.h"

#include <algorithm>
#include <cmath>

namespace vc26 {

namespace {
constexpr float kStickRadius = 0.11f;   // share of screen height
constexpr float kStickDeadZone = 0.15f;
constexpr float kCornerWidth = 0.22f;   // the difficulty label's tap area, as shares of the screen
constexpr float kCornerHeight = 0.2f;
}  // namespace

void TouchControls::update(const TouchPoint* touches, int count, float screenWidth, float screenHeight) {
    radius_ = kStickRadius * screenHeight;
    tapped_ = false;

    auto isDown = [&](int id) { return std::any_of(touches, touches + count, [id](const TouchPoint& t) { return t.id == id; }); };

    fingers_.erase(std::remove_if(fingers_.begin(), fingers_.end(), [&](const Finger& f) { return !isDown(f.id); }), fingers_.end());
    if (stickId_ >= 0 && !isDown(stickId_)) {
        stickId_ = -1;
        stickX_ = 0.f;
        stickY_ = 0.f;
    }

    for (int i = 0; i < count; ++i) {
        const TouchPoint& t = touches[i];
        const bool known = std::any_of(fingers_.begin(), fingers_.end(), [&t](const Finger& f) { return f.id == t.id; });

        if (!known) {
            Job job = Job::Action;
            if (t.x < screenWidth * kCornerWidth && t.y < screenHeight * kCornerHeight) {
                job = Job::Ignored;
                tapped_ = true;
            } else if (t.x < screenWidth * 0.5f && stickId_ < 0) {
                job = Job::Stick;
                stickId_ = t.id;
                originX_ = t.x;
                originY_ = t.y;
            }
            fingers_.push_back({t.id, job});
        }

        if (t.id == stickId_) {
            float x = (t.x - originX_) / radius_;
            float y = (t.y - originY_) / radius_;
            const float len = std::hypot(x, y);
            if (len > 1.f) {
                x /= len;
                y /= len;
            }
            stickX_ = len < kStickDeadZone ? 0.f : x;
            stickY_ = len < kStickDeadZone ? 0.f : y;
        }
    }
}

SimInput TouchControls::input() const {
    SimInput in;
    in.moveX = -stickY_;  // up the screen is towards the net
    in.moveY = -stickX_;  // left of the screen is Home's left
    in.action = actionHeld();
    return in;
}

bool TouchControls::actionHeld() const {
    return std::any_of(fingers_.begin(), fingers_.end(), [](const Finger& f) { return f.job == Job::Action; });
}

}  // namespace vc26
