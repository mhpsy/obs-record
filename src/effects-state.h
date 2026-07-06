#pragma once
#include "commands.h"
#include "geometry.h"

#include <cstdint>
#include <vector>

namespace rec {

struct Ripple {
    Vec2 pos;
    double age = 0;
    int button = 0;
};

struct TrailPoint {
    Vec2 pos;
    double age = 0;
};

struct Pin {
    enum Kind { Badge, Box } kind = Badge;
    int number = 0; // Badge 用
    Vec2 pos;       // Badge 用
    Rect rect;      // Box 用
};

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

    // 颜色格式 0xAABBGGRR(与 OBS 颜色属性、vec4_from_rgba 一致)
    uint32_t highlight_color = 0x6600D4FF;    // 半透明黄
    uint32_t ripple_left_color = 0xCC00D4FF;  // 黄
    uint32_t ripple_right_color = 0xCCFF8800; // 蓝
    uint32_t badge_color = 0xFF3538F5;        // 红
    uint32_t box_color = 0x4000D4FF;          // 半透明黄
    double badge_radius = 22.0;               // 徽章半径(px)
};

class EffectsState {
public:
    EffectsConfig cfg;
    bool cursor_valid = false;

    bool highlight_on = true;
    bool trail_on = false;
    bool spotlight_on = false;

    void on_cursor(Vec2 px);
    void apply(const Command &c);
    void tick(double dt, double src_w, double src_h);
    void on_click(int button);

    double zoom() const { return zoom_current_; }
    double zoom_target() const { return zoom_target_; }
    Rect viewport(double src_w, double src_h) const;
    Vec2 cursor() const { return cursor_px_; }
    Vec2 smooth_cursor() const { return smooth_cursor_; }

    const std::vector<Ripple> &ripples() const { return ripples_; }
    const std::vector<TrailPoint> &trail() const { return trail_; }
    const std::vector<Pin> &pins() const { return pins_; }
    bool pinbox_pending() const { return box_pending_; }
    Rect pinbox_preview() const { return rect_from_corners(box_corner_, cursor_px_); }

private:
    double zoom_current_ = 1.0, zoom_target_ = 1.0, zoom_memory_ = 2.0;
    int preset_index_ = -1;
    Vec2 view_center_;
    bool view_center_init_ = false;
    Vec2 cursor_px_, smooth_cursor_;

    std::vector<Ripple> ripples_;
    std::vector<TrailPoint> trail_;
    std::vector<Pin> pins_;
    int next_badge_ = 1;
    bool box_pending_ = false;
    Vec2 box_corner_;
    double box_age_ = 0;
};

} // namespace rec
