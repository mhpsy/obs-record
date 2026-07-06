#include "test-framework.h"
#include "geometry.h"

using namespace rec;

static MonitorInfo mon()
{
    return {"HDMI-A-2", 0, 0, 3840, 2160, 1.25};
}

TEST(logical_to_pixel_fractional_scale)
{
    Vec2 p = logical_to_pixel({2010, 1210}, mon());
    CHECK_NEAR(p.x, 2512.5, 1e-9);
    CHECK_NEAR(p.y, 1512.5, 1e-9);
}

TEST(logical_to_pixel_monitor_offset)
{
    MonitorInfo m{"DP-1", 3072, 0, 1920, 1080, 1.0}; // 第二屏在逻辑 x=3072
    Vec2 p = logical_to_pixel({3172, 50}, m);
    CHECK_NEAR(p.x, 100, 1e-9);
    CHECK_NEAR(p.y, 50, 1e-9);
}

TEST(monitor_at_picks_containing_monitor)
{
    std::vector<MonitorInfo> ms{mon(), {"DP-1", 3072, 0, 1920, 1080, 1.0}};
    CHECK(monitor_at(ms, {100, 100}) == &ms[0]);
    CHECK(monitor_at(ms, {3100, 100}) == &ms[1]);
    CHECK(monitor_at(ms, {-5, 0}) == nullptr);
    // 逻辑尺寸边界: 3840/1.25 = 3072,恰好在界外
    CHECK(monitor_at(ms, {3071.9, 100}) == &ms[0]);
}

TEST(scale_to_capture_mismatched_size)
{
    // 采集源 1920x1080 但屏幕物理 3840x2160 → 减半
    Vec2 p = scale_to_capture({2512.5, 1512.5}, mon(), 1920, 1080);
    CHECK_NEAR(p.x, 1256.25, 1e-9);
    CHECK_NEAR(p.y, 756.25, 1e-9);
}

TEST(rect_contains)
{
    Rect r{10, 10, 100, 50};
    CHECK(r.contains({10, 10}));
    CHECK(r.contains({109.9, 59.9}));
    CHECK(!r.contains({110, 60}));
    CHECK(!r.contains({9.9, 30}));
}
