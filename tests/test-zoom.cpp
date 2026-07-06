#include "test-framework.h"
#include "effects-state.h"

using namespace rec;

// 辅助:执行 n 帧 60fps tick
static void run(EffectsState &fx, int frames, double w = 1920, double h = 1080)
{
    for (int i = 0; i < frames; i++)
        fx.tick(1.0 / 60.0, w, h);
}

TEST(zoom_toggle_uses_memory_factor)
{
    EffectsState fx;
    fx.apply({CmdType::ZoomToggle, 0});
    CHECK_NEAR(fx.zoom_target(), 2.0, 1e-9); // 记忆倍率初始 2.0
    fx.apply({CmdType::ZoomToggle, 0});
    CHECK_NEAR(fx.zoom_target(), 1.0, 1e-9);
}

TEST(zoom_set_updates_memory)
{
    EffectsState fx;
    fx.apply({CmdType::ZoomSet, 2.5});
    CHECK_NEAR(fx.zoom_target(), 2.5, 1e-9);
    fx.apply({CmdType::ZoomToggle, 0});
    CHECK_NEAR(fx.zoom_target(), 1.0, 1e-9);
    fx.apply({CmdType::ZoomToggle, 0});
    CHECK_NEAR(fx.zoom_target(), 2.5, 1e-9); // 记忆被 set 更新
}

TEST(zoom_set_below_one_only_resets)
{
    EffectsState fx;
    fx.apply({CmdType::ZoomSet, 0.5});
    CHECK_NEAR(fx.zoom_target(), 1.0, 1e-9);
    fx.apply({CmdType::ZoomToggle, 0});
    CHECK_NEAR(fx.zoom_target(), 2.0, 1e-9); // 记忆不被 <1 的 set 破坏
}

TEST(zoom_cycle_walks_presets)
{
    EffectsState fx; // 预设 {1.5, 2.0, 3.0}
    fx.apply({CmdType::ZoomCycle, 0});
    CHECK_NEAR(fx.zoom_target(), 1.5, 1e-9);
    fx.apply({CmdType::ZoomCycle, 0});
    CHECK_NEAR(fx.zoom_target(), 2.0, 1e-9);
    fx.apply({CmdType::ZoomCycle, 0});
    CHECK_NEAR(fx.zoom_target(), 3.0, 1e-9);
    fx.apply({CmdType::ZoomCycle, 0});
    CHECK_NEAR(fx.zoom_target(), 1.5, 1e-9); // 回绕
}

TEST(zoom_animates_and_converges)
{
    EffectsState fx;
    fx.on_cursor({960, 540});
    fx.apply({CmdType::ZoomToggle, 0});
    fx.tick(1.0 / 60.0, 1920, 1080);
    CHECK(fx.zoom() > 1.0);
    CHECK(fx.zoom() < 2.0); // 动画中,不是瞬间到位
    run(fx, 120);           // 2 秒后收敛
    CHECK_NEAR(fx.zoom(), 2.0, 0.01);
}

TEST(dead_zone_holds_camera_still)
{
    EffectsState fx;
    fx.on_cursor({960, 540});
    fx.apply({CmdType::ZoomSet, 2.0});
    run(fx, 180); // 收敛:视口 960x540,死区半宽 960*0.3/2=144
    Rect before = fx.viewport(1920, 1080);
    fx.on_cursor({1000, 560}); // 死区内小幅移动
    run(fx, 60);
    Rect after = fx.viewport(1920, 1080);
    CHECK_NEAR(before.x, after.x, 1e-6);
    CHECK_NEAR(before.y, after.y, 1e-6);
}

TEST(camera_follows_when_outside_dead_zone)
{
    EffectsState fx;
    fx.on_cursor({960, 540});
    fx.apply({CmdType::ZoomSet, 2.0});
    run(fx, 180);
    fx.on_cursor({1500, 540}); // 死区外
    run(fx, 180);
    Rect vp = fx.viewport(1920, 1080);
    double cx = vp.x + vp.w / 2;
    CHECK(cx > 1100); // 视口中心明显右移
    CHECK(vp.x + vp.w <= 1920 + 1e-6); // 不越界
}

TEST(viewport_clamps_at_edges)
{
    EffectsState fx;
    fx.on_cursor({0, 0}); // 左上角
    fx.apply({CmdType::ZoomSet, 3.0});
    run(fx, 240);
    Rect vp = fx.viewport(1920, 1080);
    CHECK_NEAR(vp.x, 0, 1e-6);
    CHECK_NEAR(vp.y, 0, 1e-6);
    CHECK_NEAR(vp.w, 640, 0.5);
    CHECK_NEAR(vp.h, 360, 0.5);
}

TEST(viewport_at_zoom_one_is_full_frame)
{
    EffectsState fx;
    fx.on_cursor({500, 500});
    run(fx, 10);
    Rect vp = fx.viewport(1920, 1080);
    CHECK_NEAR(vp.x, 0, 1e-6);
    CHECK_NEAR(vp.y, 0, 1e-6);
    CHECK_NEAR(vp.w, 1920, 1e-6);
    CHECK_NEAR(vp.h, 1080, 1e-6);
}
