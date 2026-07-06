#pragma once
#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

namespace rec {

struct Vec2 {
    double x = 0, y = 0;
};

struct Rect {
    double x = 0, y = 0, w = 0, h = 0;
    bool contains(Vec2 p) const
    {
        return p.x >= x && p.x < x + w && p.y >= y && p.y < y + h;
    }
};

struct MonitorInfo {
    std::string name;
    double x = 0, y = 0;   // 逻辑坐标系中的位置
    int width = 0, height = 0; // 物理像素
    double scale = 1.0;
    double logical_w() const { return width / scale; }
    double logical_h() const { return height / scale; }
};

inline Vec2 logical_to_pixel(Vec2 logical, const MonitorInfo &m)
{
    return {(logical.x - m.x) * m.scale, (logical.y - m.y) * m.scale};
}

inline const MonitorInfo *monitor_at(const std::vector<MonitorInfo> &ms, Vec2 logical)
{
    for (const auto &m : ms) {
        Rect r{m.x, m.y, m.logical_w(), m.logical_h()};
        if (r.contains(logical))
            return &m;
    }
    return nullptr;
}

inline Vec2 scale_to_capture(Vec2 px, const MonitorInfo &m, double cap_w, double cap_h)
{
    if (m.width <= 0 || m.height <= 0)
        return px;
    return {px.x * cap_w / m.width, px.y * cap_h / m.height};
}

inline Rect rect_from_corners(Vec2 a, Vec2 b)
{
    double x0 = std::min(a.x, b.x), y0 = std::min(a.y, b.y);
    return {x0, y0, std::fabs(a.x - b.x), std::fabs(a.y - b.y)};
}

} // namespace rec
