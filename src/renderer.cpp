#include "renderer.h"

#include "log.h"

#include <algorithm>
#include <cmath>

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

[[maybe_unused]] // Task 12 使用
static void draw_rect_outline(Rect r, double t, uint32_t rgba)
{
    draw_rect_fill({r.x - t, r.y - t, r.w + 2 * t, t}, rgba);
    draw_rect_fill({r.x - t, r.y + r.h, r.w + 2 * t, t}, rgba);
    draw_rect_fill({r.x - t, r.y, t, r.h}, rgba);
    draw_rect_fill({r.x + r.w, r.y, t, r.h}, rgba);
}

[[maybe_unused]] // Task 12 使用
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
    (void)rc;
    (void)w;
    (void)h;
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
