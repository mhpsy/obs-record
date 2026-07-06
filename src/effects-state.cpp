#include "effects-state.h"

#include <algorithm>
#include <cmath>

namespace rec {

static double approach(double cur, double tgt, double speed, double dt)
{
    return cur + (tgt - cur) * (1.0 - std::exp(-speed * dt));
}

void EffectsState::on_cursor(Vec2 px)
{
    cursor_px_ = px;
    cursor_valid = true;
    if (!view_center_init_) {
        view_center_ = px;
        smooth_cursor_ = px;
        view_center_init_ = true;
    }
}

void EffectsState::apply(const Command &c)
{
    switch (c.type) {
    case CmdType::ZoomToggle:
        zoom_target_ = zoom_target_ > 1.0 ? 1.0 : zoom_memory_;
        break;
    case CmdType::ZoomCycle:
        if (!cfg.zoom_presets.empty()) {
            preset_index_ = (preset_index_ + 1) % (int)cfg.zoom_presets.size();
            zoom_memory_ = cfg.zoom_presets[preset_index_];
            zoom_target_ = zoom_memory_;
        }
        break;
    case CmdType::ZoomSet:
        if (c.value > 1.0) {
            zoom_memory_ = std::min(c.value, 10.0);
            zoom_target_ = zoom_memory_;
        } else {
            zoom_target_ = 1.0;
        }
        break;
    default:
        break;
    }
}

void EffectsState::tick(double dt, double src_w, double src_h)
{
    zoom_current_ = approach(zoom_current_, zoom_target_, cfg.zoom_anim_speed, dt);
    if (std::fabs(zoom_current_ - zoom_target_) < 0.001)
        zoom_current_ = zoom_target_;

    smooth_cursor_.x = approach(smooth_cursor_.x, cursor_px_.x, cfg.cursor_smooth_speed, dt);
    smooth_cursor_.y = approach(smooth_cursor_.y, cursor_px_.y, cfg.cursor_smooth_speed, dt);

    // 死区跟随:光标把死区边缘往外"推"
    double vw = src_w / zoom_current_, vh = src_h / zoom_current_;
    double dzx = vw * cfg.dead_zone_frac / 2.0, dzy = vh * cfg.dead_zone_frac / 2.0;
    Vec2 desired = view_center_;
    if (cursor_px_.x < view_center_.x - dzx) desired.x = cursor_px_.x + dzx;
    if (cursor_px_.x > view_center_.x + dzx) desired.x = cursor_px_.x - dzx;
    if (cursor_px_.y < view_center_.y - dzy) desired.y = cursor_px_.y + dzy;
    if (cursor_px_.y > view_center_.y + dzy) desired.y = cursor_px_.y - dzy;
    view_center_.x = approach(view_center_.x, desired.x, cfg.follow_speed, dt);
    view_center_.y = approach(view_center_.y, desired.y, cfg.follow_speed, dt);
    view_center_.x = std::clamp(view_center_.x, vw / 2.0, src_w - vw / 2.0);
    view_center_.y = std::clamp(view_center_.y, vh / 2.0, src_h - vh / 2.0);
}

Rect EffectsState::viewport(double src_w, double src_h) const
{
    double vw = src_w / zoom_current_, vh = src_h / zoom_current_;
    double cx = std::clamp(view_center_.x, vw / 2.0, src_w - vw / 2.0);
    double cy = std::clamp(view_center_.y, vh / 2.0, src_h - vh / 2.0);
    return {cx - vw / 2.0, cy - vh / 2.0, vw, vh};
}

} // namespace rec
