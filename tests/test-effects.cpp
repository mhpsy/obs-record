#include "test-framework.h"
#include "effects-state.h"

using namespace rec;

static void run(EffectsState &fx, int frames)
{
    for (int i = 0; i < frames; i++)
        fx.tick(1.0 / 60.0, 1920, 1080);
}

TEST(click_spawns_ripple_that_expires)
{
    EffectsState fx;
    fx.on_cursor({100, 200});
    fx.on_click(0);
    CHECK(fx.ripples().size() == 1);
    CHECK_NEAR(fx.ripples()[0].pos.x, 100, 1e-9);
    CHECK(fx.ripples()[0].button == 0);
    run(fx, 20); // 0.33s < 0.4s 存活
    CHECK(fx.ripples().size() == 1);
    run(fx, 10); // 超过 0.4s 消亡
    CHECK(fx.ripples().empty());
}

TEST(ripples_disabled_by_config)
{
    EffectsState fx;
    fx.cfg.ripples_enabled = false;
    fx.on_cursor({100, 200});
    fx.on_click(0);
    CHECK(fx.ripples().empty());
}

TEST(trail_records_only_when_enabled)
{
    EffectsState fx;
    fx.on_cursor({0, 0});
    run(fx, 5);
    CHECK(fx.trail().empty()); // 默认关
    fx.apply({CmdType::TrailToggle, 0});
    for (int i = 1; i <= 10; i++) {
        fx.on_cursor({i * 20.0, 0});
        fx.tick(1.0 / 60.0, 1920, 1080);
    }
    CHECK(fx.trail().size() >= 5);
    fx.apply({CmdType::TrailToggle, 0}); // 关闭立即清空
    CHECK(fx.trail().empty());
}

TEST(trail_points_expire)
{
    EffectsState fx;
    fx.apply({CmdType::TrailToggle, 0});
    fx.on_cursor({0, 0});
    fx.tick(1.0 / 60.0, 1920, 1080);
    fx.on_cursor({50, 0});
    fx.tick(1.0 / 60.0, 1920, 1080);
    CHECK(!fx.trail().empty());
    run(fx, 60); // 1s > 0.6s 生命周期(光标不动,不再新增点)
    CHECK(fx.trail().size() <= 1); // 最多剩最新一个采样点
}

TEST(pin_add_numbers_sequentially)
{
    EffectsState fx;
    fx.on_cursor({10, 10});
    fx.apply({CmdType::PinAdd, 0});
    fx.on_cursor({20, 20});
    fx.apply({CmdType::PinAdd, 0});
    CHECK(fx.pins().size() == 2);
    CHECK(fx.pins()[0].number == 1);
    CHECK(fx.pins()[1].number == 2);
    CHECK_NEAR(fx.pins()[1].pos.x, 20, 1e-9);
}

TEST(pin_box_two_presses_make_normalized_rect)
{
    EffectsState fx;
    fx.on_cursor({300, 400});
    fx.apply({CmdType::PinBox, 0});
    CHECK(fx.pinbox_pending());
    fx.on_cursor({100, 150}); // 第二角在左上方 → 矩形须归一化
    Rect prev = fx.pinbox_preview();
    CHECK_NEAR(prev.x, 100, 1e-9);
    CHECK_NEAR(prev.w, 200, 1e-9);
    fx.apply({CmdType::PinBox, 0});
    CHECK(!fx.pinbox_pending());
    CHECK(fx.pins().size() == 1);
    CHECK(fx.pins()[0].kind == Pin::Box);
    CHECK_NEAR(fx.pins()[0].rect.x, 100, 1e-9);
    CHECK_NEAR(fx.pins()[0].rect.y, 150, 1e-9);
    CHECK_NEAR(fx.pins()[0].rect.w, 200, 1e-9);
    CHECK_NEAR(fx.pins()[0].rect.h, 250, 1e-9);
}

TEST(pin_box_times_out)
{
    EffectsState fx;
    fx.on_cursor({0, 0});
    fx.apply({CmdType::PinBox, 0});
    run(fx, 660); // 11s > 10s
    CHECK(!fx.pinbox_pending());
    CHECK(fx.pins().empty());
}

TEST(pin_undo_cancels_pending_box_first)
{
    EffectsState fx;
    fx.on_cursor({0, 0});
    fx.apply({CmdType::PinAdd, 0});
    fx.apply({CmdType::PinBox, 0});
    fx.apply({CmdType::PinUndo, 0}); // 只取消未完成的 box
    CHECK(!fx.pinbox_pending());
    CHECK(fx.pins().size() == 1);
    fx.apply({CmdType::PinUndo, 0}); // 再撤销徽章
    CHECK(fx.pins().empty());
}

TEST(pin_undo_reuses_badge_number)
{
    EffectsState fx;
    fx.on_cursor({0, 0});
    fx.apply({CmdType::PinAdd, 0}); // #1
    fx.apply({CmdType::PinAdd, 0}); // #2
    fx.apply({CmdType::PinUndo, 0});
    fx.apply({CmdType::PinAdd, 0}); // 应复用 #2
    CHECK(fx.pins()[1].number == 2);
}

TEST(pin_clear_resets_everything)
{
    EffectsState fx;
    fx.on_cursor({0, 0});
    fx.apply({CmdType::PinAdd, 0});
    fx.apply({CmdType::PinBox, 0});
    fx.apply({CmdType::PinClear, 0});
    CHECK(fx.pins().empty());
    CHECK(!fx.pinbox_pending());
    fx.apply({CmdType::PinAdd, 0});
    CHECK(fx.pins()[0].number == 1); // 编号重置
}

TEST(click_before_cursor_seen_is_ignored)
{
    EffectsState fx; // on_cursor 从未被调用,cursor_valid 仍为 false
    fx.on_click(0);
    CHECK(fx.ripples().empty());
}

TEST(pin_add_before_cursor_seen_is_ignored)
{
    EffectsState fx;
    fx.apply({CmdType::PinAdd, 0});
    CHECK(fx.pins().empty());
}

TEST(pin_box_first_press_before_cursor_seen_is_ignored)
{
    EffectsState fx;
    fx.apply({CmdType::PinBox, 0}); // 光标无效,忽略,不应挂起
    CHECK(!fx.pinbox_pending());
    fx.on_cursor({50, 50});
    fx.apply({CmdType::PinBox, 0}); // 光标有效后第一次按下才真正挂起
    CHECK(fx.pinbox_pending());
}

TEST(toggles_flip_flags)
{
    EffectsState fx;
    CHECK(fx.highlight_on);
    CHECK(!fx.spotlight_on);
    fx.apply({CmdType::HighlightToggle, 0});
    fx.apply({CmdType::SpotlightToggle, 0});
    CHECK(!fx.highlight_on);
    CHECK(fx.spotlight_on);
}
