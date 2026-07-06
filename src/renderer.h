#pragma once
#include "effects-state.h"

#include <map>

#include <obs-module.h>

namespace rec {

struct RenderCtx {
    gs_texrender_t *texrender = nullptr;
    gs_effect_t *spot_effect = nullptr;   // Task 12 装载
    bool spot_effect_tried = false;
    std::map<int, obs_source_t *> badges; // Task 12 使用
};

void render_frame(RenderCtx &rc, obs_source_t *source, EffectsState &fx);
void render_free(RenderCtx &rc); // 调用方负责 obs_enter_graphics()
void release_badges(RenderCtx &rc);

} // namespace rec
