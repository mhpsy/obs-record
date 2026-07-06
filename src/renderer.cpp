#include "renderer.h"

#include "log.h"

#include <algorithm>
#include <cmath>
#include <string>

namespace rec {

static gs_effect_t *solid()
{
    return obs_get_base_effect(OBS_EFFECT_SOLID);
}

static void set_solid_color(uint32_t rgba)
{
    struct vec4 v;
    vec4_from_rgba(&v, rgba);
    gs_effect_set_vec4(gs_effect_get_param_by_name(solid(), "color"), &v);
}

static uint32_t with_alpha_scale(uint32_t rgba, double k)
{
    double a = ((rgba >> 24) & 0xFF) * k;
    uint32_t ai = (uint32_t)std::clamp(a, 0.0, 255.0);
    return (rgba & 0x00FFFFFF) | (ai << 24);
}

static void draw_ring(Vec2 c, double r_in, double r_out, uint32_t rgba)
{
    gs_effect_t *e = solid();
    set_solid_color(rgba);
    while (gs_effect_loop(e, "Solid")) {
        gs_render_start(true);
        const int N = 64;
        for (int i = 0; i <= N; i++) {
            double a = 2.0 * M_PI * i / N;
            gs_vertex2f((float)(c.x + r_in * std::cos(a)), (float)(c.y + r_in * std::sin(a)));
            gs_vertex2f((float)(c.x + r_out * std::cos(a)), (float)(c.y + r_out * std::sin(a)));
        }
        gs_render_stop(GS_TRISTRIP);
    }
}

static void draw_circle_fill(Vec2 c, double r, uint32_t rgba)
{
    draw_ring(c, 0.0, r, rgba);
}

static void draw_rect_fill(Rect r, uint32_t rgba)
{
    gs_effect_t *e = solid();
    set_solid_color(rgba);
    while (gs_effect_loop(e, "Solid")) {
        gs_render_start(true);
        gs_vertex2f((float)r.x, (float)r.y);
        gs_vertex2f((float)(r.x + r.w), (float)r.y);
        gs_vertex2f((float)r.x, (float)(r.y + r.h));
        gs_vertex2f((float)(r.x + r.w), (float)(r.y + r.h));
        gs_render_stop(GS_TRISTRIP);
    }
}

static void draw_rect_outline(Rect r, double t, uint32_t rgba)
{
    draw_rect_fill({r.x - t, r.y - t, r.w + 2 * t, t}, rgba);
    draw_rect_fill({r.x - t, r.y + r.h, r.w + 2 * t, t}, rgba);
    draw_rect_fill({r.x - t, r.y, t, r.h}, rgba);
    draw_rect_fill({r.x + r.w, r.y, t, r.h}, rgba);
}

static void draw_dashed_rect_outline(Rect r, double t, double dash, uint32_t rgba)
{
    for (double s = 0; s < r.w; s += dash * 2) {
        double len = std::min(dash, r.w - s);
        draw_rect_fill({r.x + s, r.y - t / 2, len, t}, rgba);
        draw_rect_fill({r.x + s, r.y + r.h - t / 2, len, t}, rgba);
    }
    for (double s = 0; s < r.h; s += dash * 2) {
        double len = std::min(dash, r.h - s);
        draw_rect_fill({r.x - t / 2, r.y + s, t, len}, rgba);
        draw_rect_fill({r.x + r.w - t / 2, r.y + s, t, len}, rgba);
    }
}

static gs_effect_t *spot_effect(RenderCtx &rc)
{
    if (!rc.spot_effect && !rc.spot_effect_tried) {
        rc.spot_effect_tried = true;
        char *path = obs_module_file("spotlight.effect");
        if (path) {
            rc.spot_effect = gs_effect_create_from_file(path, nullptr);
            bfree(path);
        }
        if (!rc.spot_effect)
            log("renderer: spotlight.effect 加载失败,聚光灯降级为无羽化环形");
    }
    return rc.spot_effect;
}

static void draw_spotlight(RenderCtx &rc, EffectsState &fx, uint32_t w, uint32_t h)
{
    const auto &cfg = fx.cfg;
    Vec2 c = fx.smooth_cursor();
    gs_effect_t *e = spot_effect(rc);
    if (e) {
        struct vec2 v;
        vec2_set(&v, (float)c.x, (float)c.y);
        gs_effect_set_vec2(gs_effect_get_param_by_name(e, "center"), &v);
        gs_effect_set_float(gs_effect_get_param_by_name(e, "radius"), (float)cfg.spotlight_radius);
        gs_effect_set_float(gs_effect_get_param_by_name(e, "feather"), (float)cfg.spotlight_feather);
        gs_effect_set_float(gs_effect_get_param_by_name(e, "dim_alpha"), (float)cfg.spotlight_dim);
        vec2_set(&v, (float)w, (float)h);
        gs_effect_set_vec2(gs_effect_get_param_by_name(e, "size"), &v);
        while (gs_effect_loop(e, "Draw"))
            gs_draw_sprite(nullptr, 0, w, h);
    } else {
        // 降级:四块矩形压暗圆外区域(无羽化,但功能可用)
        uint32_t dim = with_alpha_scale(0xFF000000, cfg.spotlight_dim);
        double r = cfg.spotlight_radius;
        draw_rect_fill({0, 0, (double)w, c.y - r}, dim);
        draw_rect_fill({0, c.y + r, (double)w, h - c.y - r}, dim);
        draw_rect_fill({0, c.y - r, c.x - r, 2 * r}, dim);
        draw_rect_fill({c.x + r, c.y - r, w - c.x - r, 2 * r}, dim);
        draw_ring(c, r, r + 2, dim);
    }
}

static obs_source_t *badge_text(RenderCtx &rc, int number)
{
    // 缓存按编号复用,pin clear 后保留;上界=历史最大编号,可接受
    auto it = rc.badges.find(number);
    if (it != rc.badges.end())
        return it->second;
    obs_data_t *st = obs_data_create();
    obs_data_t *font = obs_data_create();
    obs_data_set_string(font, "face", "Sans Serif");
    obs_data_set_int(font, "size", 28);
    obs_data_set_obj(st, "font", font);
    obs_data_set_string(st, "text", std::to_string(number).c_str());
    obs_data_set_int(st, "color1", 0xFFFFFFFF);
    obs_data_set_int(st, "color2", 0xFFFFFFFF);
    obs_source_t *src = obs_source_create_private("text_ft2_source_v2", nullptr, st);
    obs_data_release(font);
    obs_data_release(st);
    if (!src)
        log("renderer: text_ft2_source_v2 创建失败,徽章无数字");
    rc.badges[number] = src; // null 也缓存,避免每帧重试
    return src;
}

static void draw_pins(RenderCtx &rc, EffectsState &fx)
{
    const auto &cfg = fx.cfg;
    for (const auto &p : fx.pins()) {
        if (p.kind == Pin::Box) {
            draw_rect_fill(p.rect, cfg.box_color);
            draw_rect_outline(p.rect, 3.0, with_alpha_scale(cfg.box_color, 2.5));
        } else {
            draw_circle_fill(p.pos, cfg.badge_radius, cfg.badge_color);
            draw_ring(p.pos, cfg.badge_radius - 2.0, cfg.badge_radius, 0xFFFFFFFF);
            obs_source_t *txt = badge_text(rc, p.number);
            if (txt) {
                double tw = obs_source_get_width(txt), th = obs_source_get_height(txt);
                gs_matrix_push();
                gs_matrix_translate3f((float)(p.pos.x - tw / 2.0),
                                      (float)(p.pos.y - th / 2.0), 0.0f);
                obs_source_video_render(txt);
                gs_matrix_pop();
            }
        }
    }
    if (fx.pinbox_pending())
        draw_dashed_rect_outline(fx.pinbox_preview(), 2.0, 12.0, 0xCCFFFFFF);
}

// Task 12 在此函数补聚光灯与标注
static void draw_effects(RenderCtx &rc, EffectsState &fx, uint32_t w, uint32_t h)
{
    const auto &cfg = fx.cfg;

    for (const auto &t : fx.trail()) {
        double k = 1.0 - t.age / cfg.trail_lifetime;
        draw_circle_fill(t.pos, cfg.highlight_radius * 0.25,
                         with_alpha_scale(cfg.highlight_color, k));
    }
    for (const auto &r : fx.ripples()) {
        double k = r.age / cfg.ripple_lifetime;
        double radius = 10.0 + (cfg.ripple_max_radius - 10.0) * k;
        uint32_t col = r.button == 0 ? cfg.ripple_left_color : cfg.ripple_right_color;
        draw_ring(r.pos, std::max(0.0, radius - 4.0), radius, with_alpha_scale(col, 1.0 - k));
    }
    if (fx.highlight_on && fx.cursor_valid) {
        draw_circle_fill(fx.cursor(), cfg.highlight_radius, cfg.highlight_color);
        draw_ring(fx.cursor(), cfg.highlight_radius - 2.0, cfg.highlight_radius,
                  with_alpha_scale(cfg.highlight_color, 2.0));
    }
    if (fx.spotlight_on && fx.cursor_valid)
        draw_spotlight(rc, fx, w, h);
    draw_pins(rc, fx); // 标注画在压暗层之上
}

void render_frame(RenderCtx &rc, obs_source_t *source, EffectsState &fx)
{
    obs_source_t *target = obs_filter_get_target(source);
    obs_source_t *parent = obs_filter_get_parent(source);
    uint32_t w = target ? obs_source_get_base_width(target) : 0;
    uint32_t h = target ? obs_source_get_base_height(target) : 0;
    if (!target || !parent || !w || !h) {
        obs_source_skip_video_filter(source);
        return;
    }

    if (!rc.texrender)
        rc.texrender = gs_texrender_create(GS_RGBA, GS_ZS_NONE);
    if (!rc.texrender) {
        obs_source_skip_video_filter(source);
        return;
    }
    gs_texrender_reset(rc.texrender);
    if (gs_texrender_begin(rc.texrender, w, h)) {
        struct vec4 clear = {};
        gs_clear(GS_CLEAR_COLOR, &clear, 0.0f, 0);
        gs_ortho(0.0f, (float)w, 0.0f, (float)h, -100.0f, 100.0f);

        // target(本滤镜的挂载源)自己也挂着本滤镜,filters.num >= 1。这使
        // OBS 内部 obs_source_main_render() 判定 default_effect=false,
        // 从而不会像正常渲染那样自动包一层默认特效的技术循环,而是直接用
        // gs_get_effect() 取"调用方当前激活的特效"传给 target 的
        // video_render。若我们不预先激活一个特效,gs_get_effect() 返回
        // null,target 内部(非 CUSTOM_DRAW 语义)按约定只 set "image" 参数
        // 再画,会对 null 特效取参、且此时并无已激活的技术/pass,导致
        // "effect_setval_inline: invalid param" + "No vertex shader
        // specified" + "device_draw (GL) failed",画面全黑。这里手动包一层
        // Default/"Draw" 技术循环,提供激活上下文(经真机验证定位到此问题,
        // 简报原始代码未包含此包裹层)。
        gs_effect_t *default_effect = obs_get_base_effect(OBS_EFFECT_DEFAULT);
        while (gs_effect_loop(default_effect, "Draw"))
            obs_source_video_render(target);

        gs_blend_state_push();
        gs_blend_function(GS_BLEND_SRCALPHA, GS_BLEND_INVSRCALPHA);
        draw_effects(rc, fx, w, h);
        gs_blend_state_pop();

        gs_texrender_end(rc.texrender);
    }

    gs_texture_t *tex = gs_texrender_get_texture(rc.texrender);
    if (!tex) {
        obs_source_skip_video_filter(source);
        return;
    }

    Rect vp = fx.viewport(w, h);
    gs_effect_t *e = obs_get_base_effect(OBS_EFFECT_DEFAULT);
    gs_effect_set_texture(gs_effect_get_param_by_name(e, "image"), tex);
    gs_matrix_push();
    gs_matrix_scale3f((float)fx.zoom(), (float)fx.zoom(), 1.0f);
    gs_matrix_translate3f((float)-vp.x, (float)-vp.y, 0.0f);
    while (gs_effect_loop(e, "Draw"))
        gs_draw_sprite(tex, 0, w, h);
    gs_matrix_pop();
}

void release_badges(RenderCtx &rc)
{
    for (auto &[n, src] : rc.badges)
        if (src)
            obs_source_release(src);
    rc.badges.clear();
}

void render_free(RenderCtx &rc)
{
    if (rc.texrender) {
        gs_texrender_destroy(rc.texrender);
        rc.texrender = nullptr;
    }
    if (rc.spot_effect) {
        gs_effect_destroy(rc.spot_effect);
        rc.spot_effect = nullptr;
    }
}

} // namespace rec
