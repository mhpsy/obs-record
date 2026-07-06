#include "filter.h"

#include "commands.h"
#include "control-server.h"
#include "cursor-tracker.h"
#include "effects-state.h"
#include "input-listener.h"
#include "log.h"
#include "renderer.h"
#include "shared-state.h"

#include <cstdlib>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include <obs-module.h>

namespace rec {

struct HotkeyBinding;

struct FilterCtx {
    obs_source_t *source = nullptr;
    SharedInputs shared;
    EffectsState fx;
    RenderCtx render;
    std::unique_ptr<CursorTracker> tracker;
    std::unique_ptr<ControlServer> server;
    std::unique_ptr<InputListener> input;
    std::vector<std::unique_ptr<HotkeyBinding>> hotkeys;
    std::string monitor_name;
    std::string socket_path;
};

struct HotkeyBinding {
    FilterCtx *ctx;
    std::string cmd;
};

static void hotkey_cb(void *data, obs_hotkey_id, obs_hotkey_t *, bool pressed)
{
    auto *b = (HotkeyBinding *)data;
    if (pressed)
        b->ctx->shared.push_command(b->cmd);
}

static void register_hotkeys(FilterCtx *ctx)
{
    static const struct { const char *name, *desc, *cmd; } defs[] = {
        {"obs_record_zoom_toggle", "obs-record: 缩放开关", "zoom toggle"},
        {"obs_record_zoom_cycle", "obs-record: 缩放倍率循环", "zoom cycle"},
        {"obs_record_highlight_toggle", "obs-record: 光标高亮开关", "highlight toggle"},
        {"obs_record_trail_toggle", "obs-record: 轨迹开关", "trail toggle"},
        {"obs_record_spotlight_toggle", "obs-record: 聚光灯开关", "spotlight toggle"},
        {"obs_record_pin_add", "obs-record: 钉编号标注", "pin add"},
        {"obs_record_pin_box", "obs-record: 钉高亮框", "pin box"},
        {"obs_record_pin_undo", "obs-record: 撤销标注", "pin undo"},
        {"obs_record_pin_clear", "obs-record: 清空标注", "pin clear"},
    };
    for (const auto &d : defs) {
        auto b = std::make_unique<HotkeyBinding>(HotkeyBinding{ctx, d.cmd});
        obs_hotkey_register_source(ctx->source, d.name, d.desc, hotkey_cb, b.get());
        ctx->hotkeys.push_back(std::move(b));
    }
}

static const char *filter_name(void *)
{
    return "Hyprland 录制增强";
}

static void filter_update(void *data, obs_data_t *s)
{
    auto *ctx = (FilterCtx *)data;
    auto &cfg = ctx->fx.cfg;
    ctx->monitor_name = obs_data_get_string(s, "monitor");
    cfg.zoom_presets = parse_presets(obs_data_get_string(s, "zoom_presets"));
    cfg.follow_speed = obs_data_get_double(s, "follow_speed");
    cfg.dead_zone_frac = obs_data_get_double(s, "dead_zone_frac");
    cfg.highlight_radius = obs_data_get_double(s, "highlight_radius");
    cfg.ripples_enabled = obs_data_get_bool(s, "ripples_enabled");
    cfg.ripple_lifetime = obs_data_get_double(s, "ripple_lifetime");
    cfg.trail_lifetime = obs_data_get_double(s, "trail_lifetime");
    cfg.spotlight_radius = obs_data_get_double(s, "spotlight_radius");
    cfg.spotlight_feather = obs_data_get_double(s, "spotlight_feather");
    cfg.spotlight_dim = obs_data_get_double(s, "spotlight_dim");
    cfg.highlight_color = (uint32_t)obs_data_get_int(s, "highlight_color");
    cfg.ripple_left_color = (uint32_t)obs_data_get_int(s, "ripple_left_color");
    cfg.ripple_right_color = (uint32_t)obs_data_get_int(s, "ripple_right_color");
    cfg.badge_color = (uint32_t)obs_data_get_int(s, "badge_color");
    cfg.box_color = (uint32_t)obs_data_get_int(s, "box_color");
    cfg.badge_radius = obs_data_get_double(s, "badge_radius");
    // 面板勾选是初始状态;socket/热键命令在运行时翻转,再动面板会重置
    ctx->fx.highlight_on = obs_data_get_bool(s, "highlight_on");
    ctx->fx.trail_on = obs_data_get_bool(s, "trail_on");
    ctx->fx.spotlight_on = obs_data_get_bool(s, "spotlight_on");

    std::string sock = obs_data_get_string(s, "socket_path");
    if (!ctx->server || sock != ctx->socket_path) {
        ctx->socket_path = sock;
        // 必须先 reset() 销毁旧 server(析构会停止旧监听线程并 unlink 旧 socket
        // 文件),再构造新 server。若直接赋值 ctx->server = make_unique<...>(...),
        // make_unique 会先构造新对象再赋值——此时旧 server 仍存活、仍绑在同一
        // path 上,新构造函数里的 has_live_listener() 探测会连上"旧我",误判
        // 该路径"已被占用"而放弃绑定,导致新 server 退化失效。
        ctx->server.reset();
        ctx->server = std::make_unique<ControlServer>(ctx->shared, sock);
    }
}

static void *filter_create(obs_data_t *settings, obs_source_t *source)
{
    log_sink() = [](const std::string &m) { blog(LOG_INFO, "[obs-record] %s", m.c_str()); };
    auto *ctx = new FilterCtx;
    ctx->source = source;
    ctx->tracker = std::make_unique<CursorTracker>(ctx->shared);
    ctx->input = std::make_unique<InputListener>(ctx->shared);
    register_hotkeys(ctx);
    filter_update(ctx, settings);
    return ctx;
}

static void filter_destroy(void *data)
{
    auto *ctx = (FilterCtx *)data;
    release_badges(ctx->render);
    obs_enter_graphics();
    render_free(ctx->render);
    obs_leave_graphics();
    delete ctx; // 线程全部 RAII 停止
}

static void filter_tick(void *data, float seconds)
{
    auto *ctx = (FilterCtx *)data;
    obs_source_t *target = obs_filter_get_target(ctx->source);
    double w = target ? obs_source_get_base_width(target) : 0;
    double h = target ? obs_source_get_base_height(target) : 0;
    if (w <= 0 || h <= 0)
        return;

    auto s = ctx->shared.drain();
    const MonitorInfo *mon = nullptr;
    if (!s.monitors.empty()) {
        if (!ctx->monitor_name.empty())
            for (auto &m : s.monitors)
                if (m.name == ctx->monitor_name)
                    mon = &m;
        if (!mon && s.cursor_valid)
            mon = monitor_at(s.monitors, s.cursor_logical);
        if (!mon)
            mon = &s.monitors[0];
    }
    if (s.cursor_valid && mon) {
        Vec2 px = logical_to_pixel(s.cursor_logical, *mon);
        ctx->fx.on_cursor(scale_to_capture(px, *mon, w, h));
    } else {
        ctx->fx.cursor_valid = false;
    }
    for (auto &c : s.clicks)
        ctx->fx.on_click(c.button);
    for (auto &line : s.command_lines) {
        Command cmd = parse_command(line);
        if (cmd.type == CmdType::Invalid)
            log("忽略非法命令: " + line);
        else
            ctx->fx.apply(cmd);
    }
    ctx->fx.tick(seconds, w, h);
}

static void filter_render(void *data, gs_effect_t *)
{
    auto *ctx = (FilterCtx *)data;
    render_frame(ctx->render, ctx->source, ctx->fx);
}

static void filter_defaults(obs_data_t *s)
{
    const char *rt = std::getenv("XDG_RUNTIME_DIR");
    std::string sock = std::string(rt ? rt : "/tmp") + "/obs-record.sock";
    obs_data_set_default_string(s, "monitor", "");
    obs_data_set_default_string(s, "socket_path", sock.c_str());
    obs_data_set_default_string(s, "zoom_presets", "1.5, 2.0, 3.0");
    obs_data_set_default_double(s, "follow_speed", 8.0);
    obs_data_set_default_double(s, "dead_zone_frac", 0.30);
    obs_data_set_default_bool(s, "highlight_on", true);
    obs_data_set_default_double(s, "highlight_radius", 40.0);
    obs_data_set_default_bool(s, "ripples_enabled", true);
    obs_data_set_default_double(s, "ripple_lifetime", 0.4);
    obs_data_set_default_bool(s, "trail_on", false);
    obs_data_set_default_double(s, "trail_lifetime", 0.6);
    obs_data_set_default_bool(s, "spotlight_on", false);
    obs_data_set_default_double(s, "spotlight_radius", 250.0);
    obs_data_set_default_double(s, "spotlight_feather", 60.0);
    obs_data_set_default_double(s, "spotlight_dim", 0.6);
    obs_data_set_default_int(s, "highlight_color", 0x6600D4FF);
    obs_data_set_default_int(s, "ripple_left_color", 0xCC00D4FF);
    obs_data_set_default_int(s, "ripple_right_color", 0xCCFF8800);
    obs_data_set_default_int(s, "badge_color", 0xFF3538F5);
    obs_data_set_default_int(s, "box_color", 0x4000D4FF);
    obs_data_set_default_double(s, "badge_radius", 22.0);
}

static obs_properties_t *filter_properties(void *data)
{
    auto *ctx = (FilterCtx *)data;
    obs_properties_t *p = obs_properties_create();

    obs_property_t *mon = obs_properties_add_list(p, "monitor", "显示器(空=自动)",
                                                  OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_STRING);
    obs_property_list_add_string(mon, "自动", "");
    if (ctx) {
        std::lock_guard<std::mutex> lk(ctx->shared.mu);
        for (auto &m : ctx->shared.monitors)
            obs_property_list_add_string(mon, m.name.c_str(), m.name.c_str());
    }
    obs_properties_add_text(p, "socket_path", "控制 socket 路径", OBS_TEXT_DEFAULT);
    obs_properties_add_text(p, "zoom_presets", "缩放倍率预设(逗号分隔)", OBS_TEXT_DEFAULT);
    obs_properties_add_float_slider(p, "follow_speed", "跟随速度", 1.0, 20.0, 0.5);
    obs_properties_add_float_slider(p, "dead_zone_frac", "死区比例", 0.05, 0.8, 0.05);
    obs_properties_add_bool(p, "highlight_on", "光标高亮圈");
    obs_properties_add_float_slider(p, "highlight_radius", "高亮半径(px)", 10.0, 150.0, 1.0);
    obs_properties_add_color_alpha(p, "highlight_color", "高亮颜色");
    obs_properties_add_bool(p, "ripples_enabled", "点击波纹");
    obs_properties_add_float_slider(p, "ripple_lifetime", "波纹时长(s)", 0.2, 1.5, 0.05);
    obs_properties_add_color_alpha(p, "ripple_left_color", "左键波纹颜色");
    obs_properties_add_color_alpha(p, "ripple_right_color", "右键波纹颜色");
    obs_properties_add_bool(p, "trail_on", "移动轨迹");
    obs_properties_add_float_slider(p, "trail_lifetime", "轨迹时长(s)", 0.2, 2.0, 0.1);
    obs_properties_add_bool(p, "spotlight_on", "聚光灯");
    obs_properties_add_float_slider(p, "spotlight_radius", "聚光灯半径(px)", 80.0, 800.0, 10.0);
    obs_properties_add_float_slider(p, "spotlight_feather", "边缘羽化(px)", 0.0, 200.0, 5.0);
    obs_properties_add_float_slider(p, "spotlight_dim", "压暗强度", 0.1, 0.95, 0.05);
    obs_properties_add_color_alpha(p, "badge_color", "编号徽章颜色");
    obs_properties_add_float_slider(p, "badge_radius", "徽章半径(px)", 10.0, 60.0, 1.0);
    obs_properties_add_color_alpha(p, "box_color", "高亮框颜色");
    return p;
}

void register_record_filter()
{
    static obs_source_info info = {};
    info.id = "obs_record_filter";
    info.type = OBS_SOURCE_TYPE_FILTER;
    // 需要 CUSTOM_DRAW:渲染器自己管理 gs_effect_loop(Default/Solid 多次),
    // 若不设置该 flag,OBS 会预先激活一个 Default 特效并已开始技术流程,
    // 我们再次 gs_effect_loop 会与其冲突,导致 "invalid param"/"No vertex
    // shader specified" 之类的 GL 错误,画面变黑。
    info.output_flags = OBS_SOURCE_VIDEO | OBS_SOURCE_CUSTOM_DRAW;
    info.get_name = filter_name;
    info.create = filter_create;
    info.destroy = filter_destroy;
    info.video_tick = filter_tick;
    info.video_render = filter_render;
    info.update = filter_update;
    info.get_defaults = filter_defaults;
    info.get_properties = filter_properties;
    obs_register_source(&info);
}

} // namespace rec
