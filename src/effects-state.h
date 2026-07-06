#pragma once
#include "commands.h"
#include "geometry.h"

#include <vector>

namespace rec {

struct EffectsConfig {
    std::vector<double> zoom_presets{1.5, 2.0, 3.0};
    double zoom_anim_speed = 12.0;    // 指数逼近速率 (1/s),约 300ms 收敛
    double follow_speed = 8.0;        // 视口跟随速率 (1/s)
    double cursor_smooth_speed = 15.0;// 聚光灯用平滑光标速率 (1/s)
    double dead_zone_frac = 0.30;     // 死区占视口比例
    double highlight_radius = 40.0;
    bool ripples_enabled = true;
    double ripple_lifetime = 0.4;
    double ripple_max_radius = 90.0;
    double trail_lifetime = 0.6;
    double spotlight_radius = 250.0;
    double spotlight_feather = 60.0;
    double spotlight_dim = 0.6;
    double pinbox_timeout = 10.0;
};

class EffectsState {
public:
    EffectsConfig cfg;
    bool cursor_valid = false;

    void on_cursor(Vec2 px);
    void apply(const Command &c);
    void tick(double dt, double src_w, double src_h);

    double zoom() const { return zoom_current_; }
    double zoom_target() const { return zoom_target_; }
    Rect viewport(double src_w, double src_h) const;
    Vec2 cursor() const { return cursor_px_; }
    Vec2 smooth_cursor() const { return smooth_cursor_; }

private:
    double zoom_current_ = 1.0, zoom_target_ = 1.0, zoom_memory_ = 2.0;
    int preset_index_ = -1;
    Vec2 view_center_;
    bool view_center_init_ = false;
    Vec2 cursor_px_, smooth_cursor_;
};

} // namespace rec
