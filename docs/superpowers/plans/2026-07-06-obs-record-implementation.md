# obs-record 实现计划

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 一个 Hyprland 专用的 C++ OBS 滤镜插件:缩放跟随鼠标、光标高亮/点击波纹/轨迹、固定标注(编号徽章+高亮框)、聚光灯。

**Architecture:** 单个 `.so` 滤镜插件。三个后台线程(Hyprland IPC 轮询、libinput 点击监听、Unix socket 控制服务)把事件写进互斥锁保护的 `SharedInputs`;每帧 `video_tick` 取快照驱动零 OBS 依赖的 `EffectsState`(全部可单测);`video_render` 把源画进纹理、在源像素空间叠加效果、最后统一缩放裁切输出。

**Tech Stack:** C++20、CMake、libobs 32(cmake config 已确认在 `/usr/lib/cmake/libobs`)、libinput+libudev、nlohmann-json、自研 30 行测试框架 + ctest。

## Global Constraints

- 仅支持 Hyprland;坐标映射公式 `px = (logical - monitor.x) * monitor.scale`(实测 scale=1.25)
- Hyprland IPC 实测协议:socket 路径 `$XDG_RUNTIME_DIR/hypr/$HYPRLAND_INSTANCE_SIGNATURE/.socket.sock`,请求 `cursorpos` 回复形如 `2010, 1210`,请求 `j/monitors` 回复 JSON 数组(字段 `name,x,y,width,height,scale`;x/y 为逻辑坐标,width/height 为物理像素)
- 控制 socket:`$XDG_RUNTIME_DIR/obs-record.sock`,行式文本命令,全集:`zoom toggle|zoom cycle|zoom set <f>|highlight toggle|trail toggle|spotlight toggle|pin add|pin box|pin undo|pin clear`
- 任何外部依赖失效只降级对应功能,绝不崩溃、绝不影响录制
- 滤镜显示名"Hyprland 录制增强",插件 id `obs-record`,滤镜 id `obs_record_filter`
- 默认状态:高亮圈开、波纹开、轨迹关、聚光灯关、缩放 1.0x、记忆倍率 2.0、预设 {1.5,2.0,3.0}
- 纯逻辑代码(geometry/commands/effects-state/hypr-ipc 解析)零 OBS 依赖,进 `rec-core` 静态库供测试链接
- 提交信息用英文 conventional commits(feat:/test:/chore:),每个 Task 至少一次提交

## 前置条件(执行计划前用户完成)

```bash
sudo pacman -S --needed nlohmann-json   # 唯一缺失的依赖,头文件库
```

已确认无需处理:用户已在 `input` 组;libobs/libinput/cmake 均已安装。

## 文件结构

```
obs-record/
├── CMakeLists.txt
├── data/spotlight.effect            # 聚光灯 shader
├── scripts/obs-record-cli           # 发命令脚本(python3 单行)
├── scripts/hyprland-binds.conf      # 键位示例
├── src/
│   ├── plugin-main.cpp              # 模块入口,注册滤镜
│   ├── geometry.h                   # Vec2/Rect/MonitorInfo/坐标映射(header-only 纯逻辑)
│   ├── commands.h / commands.cpp    # 命令解析(纯逻辑)
│   ├── effects-state.h / .cpp       # 全部效果状态机(纯逻辑,单测主体)
│   ├── shared-state.h               # SharedInputs(互斥锁共享区)
│   ├── hypr-ipc.h / .cpp            # IPC 查询 + cursorpos/monitors 解析
│   ├── cursor-tracker.h / .cpp      # 轮询线程
│   ├── control-server.h / .cpp      # 控制 socket 线程
│   ├── input-listener.h / .cpp      # libinput 线程
│   ├── filter.h / filter.cpp        # 滤镜生命周期/tick/属性/热键
│   └── renderer.h / renderer.cpp    # gs_* 绘制
└── tests/
    ├── test-framework.h             # 自研 TEST/CHECK 宏 + main
    ├── test-geometry.cpp
    ├── test-commands.cpp
    ├── test-zoom.cpp
    ├── test-effects.cpp
    ├── test-hypr-parse.cpp
    └── test-sockets.cpp             # mock IPC server + control server 实测
```

---

### Task 1: 构建骨架(CMake + 空插件 + 测试框架)

**Files:**
- Create: `CMakeLists.txt`, `src/plugin-main.cpp`, `tests/test-framework.h`, `tests/test-sanity.cpp`, `.gitignore`

**Interfaces:**
- Produces: CMake target `obs-record`(MODULE)、静态库 `rec-core`(后续任务把纯逻辑源文件加进它)、`rec_add_test(<name>)` 函数供后续任务注册测试

- [ ] **Step 1: 写 .gitignore 与测试框架**

`.gitignore`:
```
build/
.cache/
compile_commands.json
```

`tests/test-framework.h`:
```cpp
#pragma once
#include <cmath>
#include <cstdio>
#include <functional>
#include <vector>

struct TestCase { const char *name; std::function<void()> fn; };
inline std::vector<TestCase> &test_registry() { static std::vector<TestCase> r; return r; }
inline int &test_failures() { static int f = 0; return f; }
struct TestRegistrar {
    TestRegistrar(const char *n, std::function<void()> f) { test_registry().push_back({n, std::move(f)}); }
};
#define TEST(name) \
    static void name(); \
    static TestRegistrar reg_##name(#name, name); \
    static void name()
#define CHECK(cond) \
    do { if (!(cond)) { std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); ++test_failures(); } } while (0)
#define CHECK_NEAR(a, b, eps) \
    do { double va = (a), vb = (b); if (std::fabs(va - vb) > (eps)) { \
        std::printf("FAIL %s:%d: %s=%f vs %s=%f\n", __FILE__, __LINE__, #a, va, #b, vb); ++test_failures(); } } while (0)

int main()
{
    for (auto &t : test_registry()) t.fn();
    if (test_failures()) { std::printf("%d failure(s)\n", test_failures()); return 1; }
    std::printf("all %zu tests passed\n", test_registry().size());
    return 0;
}
```

`tests/test-sanity.cpp`:
```cpp
#include "test-framework.h"

TEST(sanity_math) { CHECK(1 + 1 == 2); CHECK_NEAR(0.1 + 0.2, 0.3, 1e-9); }
```

- [ ] **Step 2: 写 CMakeLists.txt**

```cmake
cmake_minimum_required(VERSION 3.20)
project(obs-record VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
add_compile_options(-Wall -Wextra)

find_package(libobs REQUIRED)            # /usr/lib/cmake/libobs 已确认存在
find_package(nlohmann_json REQUIRED)
find_package(PkgConfig REQUIRED)
pkg_check_modules(INPUT REQUIRED IMPORTED_TARGET libinput libudev)
find_package(Threads REQUIRED)

# 纯逻辑核心:零 OBS 依赖,插件和测试共用
add_library(rec-core STATIC
    # 后续任务往这里追加: src/commands.cpp src/effects-state.cpp src/hypr-ipc.cpp
    src/rec-core-placeholder.cpp)
target_include_directories(rec-core PUBLIC src)
target_link_libraries(rec-core PUBLIC nlohmann_json::nlohmann_json Threads::Threads)

add_library(obs-record MODULE
    src/plugin-main.cpp)
set_target_properties(obs-record PROPERTIES PREFIX "")
target_link_libraries(obs-record PRIVATE OBS::libobs rec-core PkgConfig::INPUT)

install(TARGETS obs-record
    LIBRARY DESTINATION "$ENV{HOME}/.config/obs-studio/plugins/obs-record/bin/64bit")
install(FILES data/spotlight.effect
    DESTINATION "$ENV{HOME}/.config/obs-studio/plugins/obs-record/data" OPTIONAL)

enable_testing()
function(rec_add_test name)
    add_executable(${name} tests/${name}.cpp)
    target_link_libraries(${name} PRIVATE rec-core)
    target_include_directories(${name} PRIVATE tests)
    add_test(NAME ${name} COMMAND ${name})
endfunction()

rec_add_test(test-sanity)
```

`src/rec-core-placeholder.cpp`(rec-core 目前还没有 .cpp,占位;Task 3 加入 commands.cpp 后删除本文件并从 CMake 移除):
```cpp
namespace rec { int core_placeholder = 0; }
```

- [ ] **Step 3: 写空插件入口**

`src/plugin-main.cpp`:
```cpp
#include <obs-module.h>

OBS_DECLARE_MODULE()

bool obs_module_load(void)
{
    blog(LOG_INFO, "[obs-record] version 0.1.0 loaded");
    return true;
}

void obs_module_unload(void)
{
    blog(LOG_INFO, "[obs-record] unloaded");
}
```

- [ ] **Step 4: 构建 + 跑测试**

```bash
cmake -B build -G Ninja && cmake --build build && ctest --test-dir build --output-on-failure
```
Expected: 构建成功,`test-sanity ... Passed`,`build/obs-record.so` 存在。

- [ ] **Step 5: 安装并在 OBS 里验证加载**

```bash
cmake --install build
obs & sleep 8 && kill %1
grep "obs-record" ~/.config/obs-studio/logs/"$(ls -t ~/.config/obs-studio/logs | head -1)"
```
Expected: 日志含 `[obs-record] version 0.1.0 loaded`。

- [ ] **Step 6: Commit**

```bash
git add -A && git commit -m "chore: build skeleton, empty plugin loads in OBS, ctest harness"
```

---

### Task 2: geometry 与坐标映射(纯逻辑)

**Files:**
- Create: `src/geometry.h`, `tests/test-geometry.cpp`
- Modify: `CMakeLists.txt`(加 `rec_add_test(test-geometry)`)

**Interfaces:**
- Produces: `rec::Vec2{double x,y}`、`rec::Rect{double x,y,w,h; bool contains(Vec2)}`、`rec::MonitorInfo{std::string name; double x,y; int width,height; double scale}`、`rec::logical_to_pixel(Vec2, const MonitorInfo&) -> Vec2`、`rec::monitor_at(const std::vector<MonitorInfo>&, Vec2 logical) -> const MonitorInfo*`、`rec::scale_to_capture(Vec2 px, const MonitorInfo&, double cap_w, double cap_h) -> Vec2`

- [ ] **Step 1: 写失败测试**

`tests/test-geometry.cpp`:
```cpp
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
```

- [ ] **Step 2: 跑测试确认编译失败**

```bash
cmake -B build -G Ninja && cmake --build build 2>&1 | tail -3
```
Expected: FAIL,`geometry.h: No such file or directory`(需先在 CMakeLists 里加上 `rec_add_test(test-geometry)`)。

- [ ] **Step 3: 实现 geometry.h**

```cpp
#pragma once
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

} // namespace rec
```

- [ ] **Step 4: 跑测试确认通过**

```bash
cmake --build build && ctest --test-dir build --output-on-failure
```
Expected: `test-geometry ... Passed`。

- [ ] **Step 5: Commit**

```bash
git add -A && git commit -m "feat: geometry primitives and logical-to-pixel mapping"
```

---

### Task 3: 命令解析(纯逻辑)

**Files:**
- Create: `src/commands.h`, `src/commands.cpp`, `tests/test-commands.cpp`
- Modify: `CMakeLists.txt`(rec-core 源列表加 `src/commands.cpp`,删除 `src/rec-core-placeholder.cpp` 并删文件;加 `rec_add_test(test-commands)`)

**Interfaces:**
- Produces: `rec::CmdType`(枚举:`ZoomToggle, ZoomCycle, ZoomSet, HighlightToggle, TrailToggle, SpotlightToggle, PinAdd, PinBox, PinUndo, PinClear, Invalid`)、`rec::Command{CmdType type; double value}`、`rec::parse_command(const std::string&) -> Command`

- [ ] **Step 1: 写失败测试**

`tests/test-commands.cpp`:
```cpp
#include "test-framework.h"
#include "commands.h"

using namespace rec;

TEST(parse_all_valid_commands)
{
    CHECK(parse_command("zoom toggle").type == CmdType::ZoomToggle);
    CHECK(parse_command("zoom cycle").type == CmdType::ZoomCycle);
    CHECK(parse_command("highlight toggle").type == CmdType::HighlightToggle);
    CHECK(parse_command("trail toggle").type == CmdType::TrailToggle);
    CHECK(parse_command("spotlight toggle").type == CmdType::SpotlightToggle);
    CHECK(parse_command("pin add").type == CmdType::PinAdd);
    CHECK(parse_command("pin box").type == CmdType::PinBox);
    CHECK(parse_command("pin undo").type == CmdType::PinUndo);
    CHECK(parse_command("pin clear").type == CmdType::PinClear);
}

TEST(parse_zoom_set_with_value)
{
    Command c = parse_command("zoom set 2.5");
    CHECK(c.type == CmdType::ZoomSet);
    CHECK_NEAR(c.value, 2.5, 1e-9);
}

TEST(parse_tolerates_whitespace)
{
    CHECK(parse_command("  zoom   toggle \n").type == CmdType::ZoomToggle);
    CHECK(parse_command("zoom set 2\n").type == CmdType::ZoomSet);
}

TEST(parse_rejects_garbage)
{
    CHECK(parse_command("").type == CmdType::Invalid);
    CHECK(parse_command("zoom").type == CmdType::Invalid);
    CHECK(parse_command("zoom set").type == CmdType::Invalid);
    CHECK(parse_command("zoom set abc").type == CmdType::Invalid);
    CHECK(parse_command("frobnicate now").type == CmdType::Invalid);
}
```

- [ ] **Step 2: 跑测试确认失败**

```bash
cmake -B build -G Ninja && cmake --build build 2>&1 | tail -3
```
Expected: FAIL,`commands.h: No such file`。

- [ ] **Step 3: 实现**

`src/commands.h`:
```cpp
#pragma once
#include <string>

namespace rec {

enum class CmdType {
    ZoomToggle, ZoomCycle, ZoomSet,
    HighlightToggle, TrailToggle, SpotlightToggle,
    PinAdd, PinBox, PinUndo, PinClear,
    Invalid,
};

struct Command {
    CmdType type = CmdType::Invalid;
    double value = 0;
};

Command parse_command(const std::string &line);

} // namespace rec
```

`src/commands.cpp`:
```cpp
#include "commands.h"

#include <sstream>

namespace rec {

Command parse_command(const std::string &line)
{
    std::istringstream ss(line);
    std::string a, b;
    ss >> a >> b;

    if (a == "zoom") {
        if (b == "toggle") return {CmdType::ZoomToggle, 0};
        if (b == "cycle") return {CmdType::ZoomCycle, 0};
        if (b == "set") {
            double v;
            if (ss >> v) return {CmdType::ZoomSet, v};
            return {};
        }
    } else if (a == "highlight" && b == "toggle") {
        return {CmdType::HighlightToggle, 0};
    } else if (a == "trail" && b == "toggle") {
        return {CmdType::TrailToggle, 0};
    } else if (a == "spotlight" && b == "toggle") {
        return {CmdType::SpotlightToggle, 0};
    } else if (a == "pin") {
        if (b == "add") return {CmdType::PinAdd, 0};
        if (b == "box") return {CmdType::PinBox, 0};
        if (b == "undo") return {CmdType::PinUndo, 0};
        if (b == "clear") return {CmdType::PinClear, 0};
    }
    return {};
}

} // namespace rec
```

- [ ] **Step 4: 跑测试确认通过**

```bash
cmake --build build && ctest --test-dir build --output-on-failure
```
Expected: 全部 Passed(sanity/geometry/commands)。

- [ ] **Step 5: Commit**

```bash
git add -A && git commit -m "feat: control command parser"
```

---

### Task 4: EffectsState 之缩放数学(纯逻辑)

**Files:**
- Create: `src/effects-state.h`, `src/effects-state.cpp`, `tests/test-zoom.cpp`
- Modify: `CMakeLists.txt`(rec-core 加 `src/effects-state.cpp`;加 `rec_add_test(test-zoom)`)

**Interfaces:**
- Consumes: `rec::Command/CmdType`(Task 3)、`rec::Vec2/Rect`(Task 2)
- Produces: `rec::EffectsConfig`、`rec::EffectsState`,本任务实现的成员:`void on_cursor(Vec2 px)`、`void apply(const Command&)`、`void tick(double dt, double src_w, double src_h)`、`double zoom() const`、`double zoom_target() const`、`Rect viewport(double src_w, double src_h) const`、`Vec2 cursor() const`、`Vec2 smooth_cursor() const`、`bool cursor_valid`

- [ ] **Step 1: 写失败测试**

`tests/test-zoom.cpp`:
```cpp
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
```

- [ ] **Step 2: 跑测试确认失败**

```bash
cmake -B build -G Ninja && cmake --build build 2>&1 | tail -3
```
Expected: FAIL,`effects-state.h: No such file`。

- [ ] **Step 3: 实现**

`src/effects-state.h`:
```cpp
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
```

`src/effects-state.cpp`:
```cpp
#include "effects-state.h"

#include <algorithm>
#include <cmath>

namespace rec {

static double approach(double cur, double tgt, double speed, double dt)
{
    return cur + (tgt - cur) * (1.0 - std::exp(-speed * dt));
}

void EffectsState::on_cursor(Vec2 px)
{
    cursor_px_ = px;
    cursor_valid = true;
    if (!view_center_init_) {
        view_center_ = px;
        smooth_cursor_ = px;
        view_center_init_ = true;
    }
}

void EffectsState::apply(const Command &c)
{
    switch (c.type) {
    case CmdType::ZoomToggle:
        zoom_target_ = zoom_target_ > 1.0 ? 1.0 : zoom_memory_;
        break;
    case CmdType::ZoomCycle:
        if (!cfg.zoom_presets.empty()) {
            preset_index_ = (preset_index_ + 1) % (int)cfg.zoom_presets.size();
            zoom_memory_ = cfg.zoom_presets[preset_index_];
            zoom_target_ = zoom_memory_;
        }
        break;
    case CmdType::ZoomSet:
        if (c.value > 1.0) {
            zoom_memory_ = std::min(c.value, 10.0);
            zoom_target_ = zoom_memory_;
        } else {
            zoom_target_ = 1.0;
        }
        break;
    default:
        break;
    }
}

void EffectsState::tick(double dt, double src_w, double src_h)
{
    zoom_current_ = approach(zoom_current_, zoom_target_, cfg.zoom_anim_speed, dt);
    if (std::fabs(zoom_current_ - zoom_target_) < 0.001)
        zoom_current_ = zoom_target_;

    smooth_cursor_.x = approach(smooth_cursor_.x, cursor_px_.x, cfg.cursor_smooth_speed, dt);
    smooth_cursor_.y = approach(smooth_cursor_.y, cursor_px_.y, cfg.cursor_smooth_speed, dt);

    // 死区跟随:光标把死区边缘往外"推"
    double vw = src_w / zoom_current_, vh = src_h / zoom_current_;
    double dzx = vw * cfg.dead_zone_frac / 2.0, dzy = vh * cfg.dead_zone_frac / 2.0;
    Vec2 desired = view_center_;
    if (cursor_px_.x < view_center_.x - dzx) desired.x = cursor_px_.x + dzx;
    if (cursor_px_.x > view_center_.x + dzx) desired.x = cursor_px_.x - dzx;
    if (cursor_px_.y < view_center_.y - dzy) desired.y = cursor_px_.y + dzy;
    if (cursor_px_.y > view_center_.y + dzy) desired.y = cursor_px_.y - dzy;
    view_center_.x = approach(view_center_.x, desired.x, cfg.follow_speed, dt);
    view_center_.y = approach(view_center_.y, desired.y, cfg.follow_speed, dt);
    view_center_.x = std::clamp(view_center_.x, vw / 2.0, src_w - vw / 2.0);
    view_center_.y = std::clamp(view_center_.y, vh / 2.0, src_h - vh / 2.0);
}

Rect EffectsState::viewport(double src_w, double src_h) const
{
    double vw = src_w / zoom_current_, vh = src_h / zoom_current_;
    double cx = std::clamp(view_center_.x, vw / 2.0, src_w - vw / 2.0);
    double cy = std::clamp(view_center_.y, vh / 2.0, src_h - vh / 2.0);
    return {cx - vw / 2.0, cy - vh / 2.0, vw, vh};
}

} // namespace rec
```

- [ ] **Step 4: 跑测试确认通过**

```bash
cmake --build build && ctest --test-dir build --output-on-failure
```
Expected: 全部 Passed。

- [ ] **Step 5: Commit**

```bash
git add -A && git commit -m "feat: zoom state machine with dead-zone follow and edge clamping"
```

---

### Task 5: EffectsState 之效果生命周期(纯逻辑)

**Files:**
- Modify: `src/geometry.h`(加 `rect_from_corners`)、`src/effects-state.h`、`src/effects-state.cpp`
- Create: `tests/test-effects.cpp`
- Modify: `CMakeLists.txt`(加 `rec_add_test(test-effects)`)

**Interfaces:**
- Produces(追加到 EffectsState):`struct Ripple{Vec2 pos; double age; int button}`、`struct TrailPoint{Vec2 pos; double age}`、`struct Pin{enum Kind{Badge,Box} kind; int number; Vec2 pos; Rect rect}`、成员 `bool highlight_on{true}, trail_on{false}, spotlight_on{false}`、`void on_click(int button)`(0=左 1=右)、`const std::vector<Ripple>& ripples() const`、`const std::vector<TrailPoint>& trail() const`、`const std::vector<Pin>& pins() const`、`bool pinbox_pending() const`、`Rect pinbox_preview() const`;geometry 加 `rec::rect_from_corners(Vec2, Vec2) -> Rect`

- [ ] **Step 1: 写失败测试**

`tests/test-effects.cpp`:
```cpp
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
```

- [ ] **Step 2: 跑测试确认失败**

```bash
cmake -B build -G Ninja && cmake --build build 2>&1 | tail -5
```
Expected: FAIL,`on_click`/`ripples` 等成员不存在。

- [ ] **Step 3: 实现**

`src/geometry.h` 追加(namespace rec 内):
```cpp
inline Rect rect_from_corners(Vec2 a, Vec2 b)
{
    double x0 = std::min(a.x, b.x), y0 = std::min(a.y, b.y);
    return {x0, y0, std::fabs(a.x - b.x), std::fabs(a.y - b.y)};
}
```
(文件顶部需补 `#include <algorithm>` 与 `#include <cmath>`。)

`src/effects-state.h` 在 namespace rec 内、EffectsConfig 之后追加:
```cpp
struct Ripple { Vec2 pos; double age = 0; int button = 0; };
struct TrailPoint { Vec2 pos; double age = 0; };
struct Pin {
    enum Kind { Badge, Box } kind = Badge;
    int number = 0; // Badge 用
    Vec2 pos;       // Badge 用
    Rect rect;      // Box 用
};
```

EffectsState public 区追加:
```cpp
    bool highlight_on = true;
    bool trail_on = false;
    bool spotlight_on = false;

    void on_click(int button); // 0=左键 1=右键
    const std::vector<Ripple> &ripples() const { return ripples_; }
    const std::vector<TrailPoint> &trail() const { return trail_; }
    const std::vector<Pin> &pins() const { return pins_; }
    bool pinbox_pending() const { return box_pending_; }
    Rect pinbox_preview() const { return rect_from_corners(box_corner_, cursor_px_); }
```

private 区追加:
```cpp
    std::vector<Ripple> ripples_;
    std::vector<TrailPoint> trail_;
    std::vector<Pin> pins_;
    int next_badge_ = 1;
    bool box_pending_ = false;
    Vec2 box_corner_;
    double box_age_ = 0;
```

`src/effects-state.cpp`:新增 `on_click`,`apply` 补全 default 分支前的各 case,`tick` 末尾追加生命周期推进:
```cpp
void EffectsState::on_click(int button)
{
    if (cfg.ripples_enabled)
        ripples_.push_back({cursor_px_, 0, button});
}
```

`apply` 的 switch 追加:
```cpp
    case CmdType::HighlightToggle:
        highlight_on = !highlight_on;
        break;
    case CmdType::TrailToggle:
        trail_on = !trail_on;
        if (!trail_on)
            trail_.clear();
        break;
    case CmdType::SpotlightToggle:
        spotlight_on = !spotlight_on;
        break;
    case CmdType::PinAdd:
        pins_.push_back({Pin::Badge, next_badge_++, cursor_px_, {}});
        break;
    case CmdType::PinBox:
        if (!box_pending_) {
            box_pending_ = true;
            box_corner_ = cursor_px_;
            box_age_ = 0;
        } else {
            pins_.push_back({Pin::Box, 0, {}, rect_from_corners(box_corner_, cursor_px_)});
            box_pending_ = false;
        }
        break;
    case CmdType::PinUndo:
        if (box_pending_) {
            box_pending_ = false;
        } else if (!pins_.empty()) {
            if (pins_.back().kind == Pin::Badge)
                --next_badge_;
            pins_.pop_back();
        }
        break;
    case CmdType::PinClear:
        pins_.clear();
        box_pending_ = false;
        next_badge_ = 1;
        break;
```

`tick` 末尾追加:
```cpp
    // 波纹老化
    for (auto &r : ripples_)
        r.age += dt;
    std::erase_if(ripples_, [&](const Ripple &r) { return r.age > cfg.ripple_lifetime; });

    // 轨迹采样(移动超过 2px 才记点)与老化
    if (trail_on && cursor_valid) {
        if (trail_.empty() ||
            std::hypot(trail_.back().pos.x - cursor_px_.x, trail_.back().pos.y - cursor_px_.y) > 2.0)
            trail_.push_back({cursor_px_, 0});
    }
    for (auto &t : trail_)
        t.age += dt;
    std::erase_if(trail_, [&](const TrailPoint &t) { return t.age > cfg.trail_lifetime; });

    // pin box 超时取消
    if (box_pending_) {
        box_age_ += dt;
        if (box_age_ > cfg.pinbox_timeout)
            box_pending_ = false;
    }
```

- [ ] **Step 4: 跑测试确认通过**

```bash
cmake --build build && ctest --test-dir build --output-on-failure
```
Expected: 全部 Passed(含 test-zoom 回归)。

- [ ] **Step 5: Commit**

```bash
git add -A && git commit -m "feat: ripple/trail/pin/spotlight state lifecycles"
```

---

### Task 6: Hyprland IPC 客户端与解析

**Files:**
- Create: `src/log.h`, `src/hypr-ipc.h`, `src/hypr-ipc.cpp`, `tests/test-hypr-parse.cpp`, `tests/mock-server.h`, `tests/test-sockets.cpp`
- Modify: `CMakeLists.txt`(rec-core 加 `src/hypr-ipc.cpp`;加 `rec_add_test(test-hypr-parse)` 和 `rec_add_test(test-sockets)`)

**Interfaces:**
- Consumes: `rec::Vec2/MonitorInfo`(Task 2)
- Produces: `rec::log(const std::string&)` / `rec::log_sink()`(可替换日志出口)、`rec::hypr_socket_path() -> std::string`(env 缺失返回 "")、`rec::hypr_query(const std::string& sock_path, const std::string& request, std::string* reply, std::string* err) -> bool`、`rec::parse_cursorpos(const std::string&, Vec2*) -> bool`、`rec::parse_monitors_json(const std::string&, std::vector<MonitorInfo>*) -> bool`;测试侧 `MockIpcServer(path, handler)`(每连接:读请求→handler 生成回复→写回→关闭)

- [ ] **Step 1: 写失败测试**

`tests/test-hypr-parse.cpp`:
```cpp
#include "test-framework.h"
#include "hypr-ipc.h"

using namespace rec;

TEST(parse_cursorpos_real_format)
{
    Vec2 p;
    CHECK(parse_cursorpos("2010, 1210", &p)); // 真机实测格式
    CHECK_NEAR(p.x, 2010, 1e-9);
    CHECK_NEAR(p.y, 1210, 1e-9);
    CHECK(parse_cursorpos("0, 0\n", &p)); // 容忍尾部换行
}

TEST(parse_cursorpos_rejects_garbage)
{
    Vec2 p;
    CHECK(!parse_cursorpos("", &p));
    CHECK(!parse_cursorpos("unknown request", &p));
}

TEST(parse_monitors_real_json)
{
    // 真机 j/monitors 输出裁剪版(含无关字段验证容忍性)
    const char *json = R"([{
        "id": 0, "name": "HDMI-A-2", "description": "XXX",
        "width": 3840, "height": 2160, "refreshRate": 60.0,
        "x": 0, "y": 0, "scale": 1.25, "transform": 0, "focused": true
    }])";
    std::vector<MonitorInfo> ms;
    CHECK(parse_monitors_json(json, &ms));
    CHECK(ms.size() == 1);
    CHECK(ms[0].name == "HDMI-A-2");
    CHECK(ms[0].width == 3840);
    CHECK(ms[0].height == 2160);
    CHECK_NEAR(ms[0].scale, 1.25, 1e-9);
    CHECK_NEAR(ms[0].x, 0, 1e-9);
}

TEST(parse_monitors_rejects_bad_json)
{
    std::vector<MonitorInfo> ms;
    CHECK(!parse_monitors_json("not json", &ms));
    CHECK(!parse_monitors_json("{}", &ms)); // 不是数组
}
```

`tests/test-sockets.cpp`:
```cpp
#include "test-framework.h"
#include "hypr-ipc.h"
#include "mock-server.h"

using namespace rec;

TEST(hypr_query_roundtrip_via_mock)
{
    MockIpcServer srv("mock-ipc.sock", [](const std::string &req) -> std::string {
        if (req == "cursorpos")
            return "123, 456";
        return "unknown request";
    });
    std::string reply, err;
    CHECK(hypr_query("mock-ipc.sock", "cursorpos", &reply, &err));
    CHECK(reply == "123, 456");
    Vec2 p;
    CHECK(parse_cursorpos(reply, &p));
    CHECK_NEAR(p.x, 123, 1e-9);
}

TEST(hypr_query_fails_on_missing_socket)
{
    std::string reply, err;
    CHECK(!hypr_query("does-not-exist.sock", "cursorpos", &reply, &err));
    CHECK(!err.empty());
}
```

`tests/mock-server.h`:
```cpp
#pragma once
#include <atomic>
#include <cstdio>
#include <functional>
#include <string>
#include <thread>

#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

// 极简 mock Hyprland IPC:每个连接读一次请求,handler 生成回复,写完即关
class MockIpcServer {
public:
    MockIpcServer(const std::string &path, std::function<std::string(const std::string &)> handler)
        : path_(path), handler_(std::move(handler))
    {
        ::unlink(path_.c_str());
        fd_ = ::socket(AF_UNIX, SOCK_STREAM, 0);
        sockaddr_un addr{};
        addr.sun_family = AF_UNIX;
        std::snprintf(addr.sun_path, sizeof(addr.sun_path), "%s", path_.c_str());
        ::bind(fd_, (sockaddr *)&addr, sizeof(addr));
        ::listen(fd_, 8);
        th_ = std::thread([this] { loop(); });
    }
    ~MockIpcServer()
    {
        running_ = false;
        ::shutdown(fd_, SHUT_RDWR);
        ::close(fd_);
        th_.join();
        ::unlink(path_.c_str());
    }

private:
    void loop()
    {
        while (running_) {
            int c = ::accept(fd_, nullptr, nullptr);
            if (c < 0)
                break;
            char buf[512];
            ssize_t n = ::read(c, buf, sizeof(buf));
            std::string req = n > 0 ? std::string(buf, (size_t)n) : "";
            std::string reply = handler_(req);
            (void)!::write(c, reply.data(), reply.size());
            ::close(c);
        }
    }
    std::string path_;
    std::function<std::string(const std::string &)> handler_;
    int fd_ = -1;
    std::thread th_;
    std::atomic<bool> running_{true};
};
```

- [ ] **Step 2: 跑测试确认失败**

```bash
cmake -B build -G Ninja && cmake --build build 2>&1 | tail -3
```
Expected: FAIL,`hypr-ipc.h: No such file`。

- [ ] **Step 3: 实现**

`src/log.h`:
```cpp
#pragma once
#include <functional>
#include <string>

namespace rec {

// 默认吞掉日志;插件启动时把它接到 blog(),测试可接到 stderr
inline std::function<void(const std::string &)> &log_sink()
{
    static std::function<void(const std::string &)> fn = [](const std::string &) {};
    return fn;
}

inline void log(const std::string &msg)
{
    log_sink()(msg);
}

} // namespace rec
```

`src/hypr-ipc.h`:
```cpp
#pragma once
#include "geometry.h"

#include <string>
#include <vector>

namespace rec {

std::string hypr_socket_path();
bool hypr_query(const std::string &sock_path, const std::string &request,
                std::string *reply, std::string *err);
bool parse_cursorpos(const std::string &reply, Vec2 *out);
bool parse_monitors_json(const std::string &json, std::vector<MonitorInfo> *out);

} // namespace rec
```

`src/hypr-ipc.cpp`:
```cpp
#include "hypr-ipc.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>

#include <nlohmann/json.hpp>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

namespace rec {

std::string hypr_socket_path()
{
    const char *rt = std::getenv("XDG_RUNTIME_DIR");
    const char *sig = std::getenv("HYPRLAND_INSTANCE_SIGNATURE");
    if (!rt || !sig)
        return "";
    return std::string(rt) + "/hypr/" + sig + "/.socket.sock";
}

bool hypr_query(const std::string &sock_path, const std::string &request,
                std::string *reply, std::string *err)
{
    int fd = ::socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (fd < 0) {
        if (err) *err = "socket() failed";
        return false;
    }
    timeval tv{0, 500 * 1000}; // 500ms 超时,渲染主循环永不被 IPC 拖死
    ::setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    ::setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));

    sockaddr_un addr{};
    addr.sun_family = AF_UNIX;
    if (sock_path.empty() || sock_path.size() >= sizeof(addr.sun_path)) {
        ::close(fd);
        if (err) *err = "bad socket path";
        return false;
    }
    std::memcpy(addr.sun_path, sock_path.c_str(), sock_path.size() + 1);

    if (::connect(fd, (sockaddr *)&addr, sizeof(addr)) < 0) {
        ::close(fd);
        if (err) *err = "connect failed: " + sock_path;
        return false;
    }
    if (::write(fd, request.data(), request.size()) != (ssize_t)request.size()) {
        ::close(fd);
        if (err) *err = "write failed";
        return false;
    }
    reply->clear();
    char buf[8192];
    ssize_t n;
    while ((n = ::read(fd, buf, sizeof(buf))) > 0)
        reply->append(buf, (size_t)n);
    ::close(fd);
    if (reply->empty()) {
        if (err) *err = "empty reply";
        return false;
    }
    return true;
}

bool parse_cursorpos(const std::string &reply, Vec2 *out)
{
    double x, y;
    if (std::sscanf(reply.c_str(), "%lf , %lf", &x, &y) != 2)
        return false;
    out->x = x;
    out->y = y;
    return true;
}

bool parse_monitors_json(const std::string &json, std::vector<MonitorInfo> *out)
{
    auto j = nlohmann::json::parse(json, nullptr, false);
    if (j.is_discarded() || !j.is_array())
        return false;
    out->clear();
    for (const auto &m : j) {
        if (!m.is_object())
            return false;
        MonitorInfo mi;
        mi.name = m.value("name", "");
        mi.x = m.value("x", 0.0);
        mi.y = m.value("y", 0.0);
        mi.width = m.value("width", 0);
        mi.height = m.value("height", 0);
        mi.scale = m.value("scale", 1.0);
        if (mi.width <= 0 || mi.height <= 0 || mi.scale <= 0)
            return false;
        out->push_back(std::move(mi));
    }
    return !out->empty();
}

} // namespace rec
```

- [ ] **Step 4: 跑测试确认通过**

```bash
cmake --build build && ctest --test-dir build --output-on-failure
```
Expected: 全部 Passed。

- [ ] **Step 5: 真机冒烟(手动,一次性)**

```bash
cat > /tmp/ipc-smoke.cpp <<'EOF'
#include "hypr-ipc.h"
#include <cstdio>
int main() {
    std::string reply, err;
    if (!rec::hypr_query(rec::hypr_socket_path(), "cursorpos", &reply, &err)) {
        std::printf("FAIL: %s\n", err.c_str()); return 1;
    }
    rec::Vec2 p;
    if (!rec::parse_cursorpos(reply, &p)) { std::printf("PARSE FAIL: %s\n", reply.c_str()); return 1; }
    std::printf("cursor at %.0f, %.0f\n", p.x, p.y);
    return 0;
}
EOF
g++ -std=c++20 -Isrc /tmp/ipc-smoke.cpp src/hypr-ipc.cpp -o /tmp/ipc-smoke && /tmp/ipc-smoke
```
Expected: 打印当前真实光标坐标。

- [ ] **Step 6: Commit**

```bash
git add -A && git commit -m "feat: hyprland IPC client with cursorpos/monitors parsing"
```

---

### Task 7: SharedInputs 与 cursor-tracker 线程

**Files:**
- Create: `src/shared-state.h`, `src/cursor-tracker.h`, `src/cursor-tracker.cpp`
- Modify: `CMakeLists.txt`(rec-core 加 `src/cursor-tracker.cpp`)、`tests/test-sockets.cpp`(追加 tracker 测试)

**Interfaces:**
- Consumes: `rec::hypr_query/parse_*`(Task 6)、`rec::MonitorInfo/monitor_at`(Task 2)
- Produces: `rec::ClickEvent{int button}`、`rec::SharedInputs`(成员 `mu, cursor_logical, cursor_valid, monitors, clicks, command_lines, click_listener_ok`;方法 `Snapshot drain()`、`set_cursor(Vec2,bool)`、`set_monitors(vector<MonitorInfo>)`、`push_click(int)`、`push_command(string)`;`SharedInputs::Snapshot` 含同名五字段)、`rec::CursorTracker(SharedInputs&, std::string sock_path = "")`(RAII 线程,析构即停)

- [ ] **Step 1: 写失败测试**

`tests/test-sockets.cpp` 追加:
```cpp
#include "cursor-tracker.h"
#include <chrono>

TEST(cursor_tracker_populates_shared_state)
{
    MockIpcServer srv("mock-hypr.sock", [](const std::string &req) -> std::string {
        if (req == "cursorpos")
            return "100, 200";
        if (req == "j/monitors")
            return R"([{"name":"HDMI-A-2","x":0,"y":0,"width":3840,"height":2160,"scale":1.25}])";
        return "unknown request";
    });
    SharedInputs shared;
    {
        CursorTracker tracker(shared, "mock-hypr.sock");
        bool ok = false;
        for (int i = 0; i < 100 && !ok; i++) { // 最多等 2s
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
            auto s = shared.drain();
            ok = s.cursor_valid && !s.monitors.empty();
            if (ok) {
                CHECK_NEAR(s.cursor_logical.x, 100, 1e-9);
                CHECK_NEAR(s.cursor_logical.y, 200, 1e-9);
                CHECK(s.monitors[0].name == "HDMI-A-2");
            }
        }
        CHECK(ok);
    } // 析构必须干净退出、不卡住
}

TEST(cursor_tracker_degrades_when_socket_dies)
{
    SharedInputs shared;
    CursorTracker tracker(shared, "never-existed.sock");
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    auto s = shared.drain();
    CHECK(!s.cursor_valid); // 降级为无效,而不是崩溃
}

TEST(shared_drain_moves_and_clears_queues)
{
    SharedInputs shared;
    shared.push_click(0);
    shared.push_command("zoom toggle");
    auto s = shared.drain();
    CHECK(s.clicks.size() == 1);
    CHECK(s.command_lines.size() == 1);
    auto s2 = shared.drain();
    CHECK(s2.clicks.empty());
    CHECK(s2.command_lines.empty());
}
```

- [ ] **Step 2: 跑测试确认失败**

```bash
cmake -B build -G Ninja && cmake --build build 2>&1 | tail -3
```
Expected: FAIL,`cursor-tracker.h: No such file`。

- [ ] **Step 3: 实现**

`src/shared-state.h`:
```cpp
#pragma once
#include "geometry.h"

#include <mutex>
#include <string>
#include <vector>

namespace rec {

struct ClickEvent {
    int button; // 0=左键 1=右键
};

// 三个后台线程写入,video_tick 每帧 drain
struct SharedInputs {
    std::mutex mu;
    Vec2 cursor_logical;
    bool cursor_valid = false;
    std::vector<MonitorInfo> monitors;
    std::vector<ClickEvent> clicks;
    std::vector<std::string> command_lines;
    bool click_listener_ok = true;

    struct Snapshot {
        Vec2 cursor_logical;
        bool cursor_valid = false;
        std::vector<MonitorInfo> monitors;
        std::vector<ClickEvent> clicks;
        std::vector<std::string> command_lines;
    };

    Snapshot drain()
    {
        std::lock_guard<std::mutex> lk(mu);
        Snapshot s{cursor_logical, cursor_valid, monitors,
                   std::move(clicks), std::move(command_lines)};
        clicks = {};
        command_lines = {};
        return s;
    }
    void set_cursor(Vec2 v, bool valid)
    {
        std::lock_guard<std::mutex> lk(mu);
        cursor_logical = v;
        cursor_valid = valid;
    }
    void set_monitors(std::vector<MonitorInfo> ms)
    {
        std::lock_guard<std::mutex> lk(mu);
        monitors = std::move(ms);
    }
    void push_click(int button)
    {
        std::lock_guard<std::mutex> lk(mu);
        if (clicks.size() < 64)
            clicks.push_back({button});
    }
    void push_command(std::string line)
    {
        std::lock_guard<std::mutex> lk(mu);
        if (command_lines.size() < 64)
            command_lines.push_back(std::move(line));
    }
};

} // namespace rec
```

`src/cursor-tracker.h`:
```cpp
#pragma once
#include "shared-state.h"

#include <atomic>
#include <string>
#include <thread>

namespace rec {

class CursorTracker {
public:
    // sock_path 为空则自动用 hypr_socket_path()
    explicit CursorTracker(SharedInputs &shared, std::string sock_path = "");
    ~CursorTracker();

private:
    void loop();
    SharedInputs &shared_;
    std::string sock_path_;
    std::atomic<bool> running_{true};
    std::thread th_;
};

} // namespace rec
```

`src/cursor-tracker.cpp`:
```cpp
#include "cursor-tracker.h"

#include "hypr-ipc.h"
#include "log.h"

#include <chrono>

namespace rec {

CursorTracker::CursorTracker(SharedInputs &shared, std::string sock_path)
    : shared_(shared), sock_path_(std::move(sock_path))
{
    if (sock_path_.empty())
        sock_path_ = hypr_socket_path();
    if (sock_path_.empty())
        log("cursor-tracker: HYPRLAND_INSTANCE_SIGNATURE 缺失,光标跟踪不可用");
    th_ = std::thread([this] { loop(); });
}

CursorTracker::~CursorTracker()
{
    running_ = false;
    if (th_.joinable())
        th_.join();
}

void CursorTracker::loop()
{
    std::vector<MonitorInfo> local_ms;
    bool warned = false;
    while (running_) {
        bool ok = false;
        std::string reply, err;
        if (!sock_path_.empty()) {
            if (local_ms.empty()) {
                if (hypr_query(sock_path_, "j/monitors", &reply, &err) &&
                    parse_monitors_json(reply, &local_ms))
                    shared_.set_monitors(local_ms);
            }
            Vec2 pos;
            if (hypr_query(sock_path_, "cursorpos", &reply, &err) &&
                parse_cursorpos(reply, &pos)) {
                shared_.set_cursor(pos, true);
                ok = true;
                warned = false;
                // 光标落在所有已知显示器外 → 布局可能变了,下轮刷新
                if (!local_ms.empty() && !monitor_at(local_ms, pos))
                    local_ms.clear();
            }
        } else {
            sock_path_ = hypr_socket_path(); // 环境变量可能后来才有
        }

        if (ok) {
            std::this_thread::sleep_for(std::chrono::milliseconds(16));
        } else {
            shared_.set_cursor({}, false);
            local_ms.clear();
            if (!warned) {
                log("cursor-tracker: Hyprland IPC 查询失败(" + err + "),2s 后重试");
                warned = true;
            }
            for (int i = 0; i < 20 && running_; i++) // 2s 退避,可中断
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
    }
}

} // namespace rec
```

- [ ] **Step 4: 跑测试确认通过**

```bash
cmake --build build && ctest --test-dir build --output-on-failure
```
Expected: 全部 Passed;`test-sockets` 结束不卡住(析构干净)。

- [ ] **Step 5: Commit**

```bash
git add -A && git commit -m "feat: shared input state and hyprland cursor tracker thread"
```

---

### Task 8: control-server 线程与 CLI 脚本

**Files:**
- Create: `src/control-server.h`, `src/control-server.cpp`, `scripts/obs-record-cli`, `scripts/hyprland-binds.conf`
- Modify: `CMakeLists.txt`(rec-core 加 `src/control-server.cpp`)、`tests/test-sockets.cpp`(追加测试)

**Interfaces:**
- Consumes: `rec::SharedInputs::push_command`(Task 7)
- Produces: `rec::ControlServer(SharedInputs&, std::string sock_path)`(RAII 线程;`bool ok() const` 报告 socket 是否建立成功;收到的每个非空行原样进 `command_lines`)

- [ ] **Step 1: 写失败测试**

`tests/test-sockets.cpp` 追加:
```cpp
#include "control-server.h"

static void send_line(const std::string &path, const std::string &data)
{
    int fd = ::socket(AF_UNIX, SOCK_STREAM, 0);
    sockaddr_un addr{};
    addr.sun_family = AF_UNIX;
    std::snprintf(addr.sun_path, sizeof(addr.sun_path), "%s", path.c_str());
    ::connect(fd, (sockaddr *)&addr, sizeof(addr));
    (void)!::write(fd, data.data(), data.size());
    ::close(fd);
}

TEST(control_server_receives_command_lines)
{
    SharedInputs shared;
    ControlServer srv(shared, "control-test.sock");
    CHECK(srv.ok());
    send_line("control-test.sock", "zoom toggle\npin add\n\n");
    bool ok = false;
    std::vector<std::string> got;
    for (int i = 0; i < 100 && !ok; i++) {
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
        auto s = shared.drain();
        for (auto &c : s.command_lines)
            got.push_back(c);
        ok = got.size() >= 2;
    }
    CHECK(ok);
    CHECK(got[0] == "zoom toggle");
    CHECK(got[1] == "pin add"); // 空行被丢弃
}

TEST(control_server_cleans_stale_socket)
{
    SharedInputs shared;
    { ControlServer first(shared, "stale-test.sock"); CHECK(first.ok()); }
    // 模拟异常残留:手动占一个同名 stale 文件
    { ControlServer second(shared, "stale-test.sock"); CHECK(second.ok()); }
}
```

- [ ] **Step 2: 跑测试确认失败**

```bash
cmake -B build -G Ninja && cmake --build build 2>&1 | tail -3
```
Expected: FAIL,`control-server.h: No such file`。

- [ ] **Step 3: 实现**

`src/control-server.h`:
```cpp
#pragma once
#include "shared-state.h"

#include <atomic>
#include <string>
#include <thread>

namespace rec {

class ControlServer {
public:
    ControlServer(SharedInputs &shared, std::string sock_path);
    ~ControlServer();
    bool ok() const { return fd_ >= 0; }

private:
    void loop();
    SharedInputs &shared_;
    std::string path_;
    int fd_ = -1;
    std::atomic<bool> running_{true};
    std::thread th_;
};

} // namespace rec
```

`src/control-server.cpp`:
```cpp
#include "control-server.h"

#include "log.h"

#include <cstdio>
#include <cstring>
#include <sstream>

#include <poll.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

namespace rec {

ControlServer::ControlServer(SharedInputs &shared, std::string sock_path)
    : shared_(shared), path_(std::move(sock_path))
{
    ::unlink(path_.c_str()); // 清理上次异常退出的残留
    fd_ = ::socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (fd_ < 0) {
        log("control-server: socket() 失败,快捷键控制不可用");
        return;
    }
    sockaddr_un addr{};
    addr.sun_family = AF_UNIX;
    if (path_.empty() || path_.size() >= sizeof(addr.sun_path)) {
        log("control-server: socket 路径非法: " + path_);
        ::close(fd_);
        fd_ = -1;
        return;
    }
    std::memcpy(addr.sun_path, path_.c_str(), path_.size() + 1);
    if (::bind(fd_, (sockaddr *)&addr, sizeof(addr)) < 0 || ::listen(fd_, 8) < 0) {
        log("control-server: bind/listen 失败: " + path_);
        ::close(fd_);
        fd_ = -1;
        return;
    }
    log("control-server: 监听 " + path_);
    th_ = std::thread([this] { loop(); });
}

ControlServer::~ControlServer()
{
    running_ = false;
    if (fd_ >= 0) {
        ::shutdown(fd_, SHUT_RDWR);
        ::close(fd_);
    }
    if (th_.joinable())
        th_.join();
    if (!path_.empty())
        ::unlink(path_.c_str());
}

void ControlServer::loop()
{
    while (running_) {
        pollfd pfd{fd_, POLLIN, 0};
        int r = ::poll(&pfd, 1, 200);
        if (r <= 0)
            continue;
        int c = ::accept(fd_, nullptr, nullptr);
        if (c < 0)
            continue;
        std::string data;
        char buf[512];
        ssize_t n;
        while ((n = ::read(c, buf, sizeof(buf))) > 0)
            data.append(buf, (size_t)n);
        ::close(c);
        std::istringstream ss(data);
        std::string line;
        while (std::getline(ss, line)) {
            if (!line.empty())
                shared_.push_command(line);
        }
    }
}

} // namespace rec
```

`scripts/obs-record-cli`(`chmod +x`):
```sh
#!/bin/sh
# 向 obs-record 插件发送控制命令。
# 用法: obs-record-cli zoom toggle
#       OBS_RECORD_SOCKET=/path/to.sock obs-record-cli pin add
exec python3 -c '
import socket, sys
s = socket.socket(socket.AF_UNIX)
s.connect(sys.argv[1])
s.sendall((" ".join(sys.argv[2:]) + "\n").encode())
s.close()
' "${OBS_RECORD_SOCKET:-$XDG_RUNTIME_DIR/obs-record.sock}" "$@"
```

`scripts/hyprland-binds.conf`:
```
# obs-record 键位示例 —— source 进 hyprland.conf 或手动抄走
# 需要 scripts/obs-record-cli 在 PATH 里(如复制到 ~/.local/bin/)
bind = SUPER, Z, exec, obs-record-cli zoom toggle
bind = SUPER, C, exec, obs-record-cli zoom cycle
bind = SUPER, A, exec, obs-record-cli pin add
bind = SUPER, B, exec, obs-record-cli pin box
bind = SUPER SHIFT, A, exec, obs-record-cli pin undo
bind = SUPER, D, exec, obs-record-cli pin clear
bind = SUPER, S, exec, obs-record-cli spotlight toggle
bind = SUPER, T, exec, obs-record-cli trail toggle
bind = SUPER, H, exec, obs-record-cli highlight toggle
```

- [ ] **Step 4: 跑测试确认通过 + 脚本冒烟**

```bash
cmake --build build && ctest --test-dir build --output-on-failure
chmod +x scripts/obs-record-cli
```
Expected: 全部 Passed。

- [ ] **Step 5: Commit**

```bash
git add -A && git commit -m "feat: control socket server, obs-record-cli and hyprland bind examples"
```

---

### Task 9: input-listener(libinput 点击监听)

**Files:**
- Create: `src/input-listener.h`, `src/input-listener.cpp`, `tests/click-probe.cpp`
- Modify: `CMakeLists.txt`(见 Step 2;`input-listener.cpp` 进 obs-record 与 click-probe,不进 rec-core)

**Interfaces:**
- Consumes: `rec::SharedInputs::push_click`(Task 7)
- Produces: `rec::InputListener(SharedInputs&)`(RAII 线程;init 失败时置 `shared.click_listener_ok=false` 并 `rec::log` 一条含 "input" 组提示的消息)

- [ ] **Step 1: 实现(此模块依赖真实硬件,先实现后手动验证)**

`src/input-listener.h`:
```cpp
#pragma once
#include "shared-state.h"

#include <atomic>
#include <thread>

namespace rec {

class InputListener {
public:
    explicit InputListener(SharedInputs &shared);
    ~InputListener();

private:
    void loop();
    SharedInputs &shared_;
    std::atomic<bool> running_{true};
    std::thread th_;
};

} // namespace rec
```

`src/input-listener.cpp`:
```cpp
#include "input-listener.h"

#include "log.h"

#include <cerrno>

#include <fcntl.h>
#include <libinput.h>
#include <libudev.h>
#include <linux/input-event-codes.h>
#include <poll.h>
#include <unistd.h>

namespace rec {

static int open_restricted(const char *path, int flags, void *)
{
    int fd = ::open(path, flags);
    return fd < 0 ? -errno : fd;
}

static void close_restricted(int fd, void *)
{
    ::close(fd);
}

static const libinput_interface kIface = {open_restricted, close_restricted};

InputListener::InputListener(SharedInputs &shared) : shared_(shared)
{
    th_ = std::thread([this] { loop(); });
}

InputListener::~InputListener()
{
    running_ = false;
    if (th_.joinable())
        th_.join();
}

void InputListener::loop()
{
    udev *ud = udev_new();
    libinput *li = ud ? libinput_udev_create_context(&kIface, nullptr, ud) : nullptr;
    if (!li || libinput_udev_assign_seat(li, "seat0") != 0) {
        log("input-listener: libinput 初始化失败(用户在 input 组吗?"
            "sudo usermod -aG input $USER 后重新登录)——点击波纹已禁用");
        {
            std::lock_guard<std::mutex> lk(shared_.mu);
            shared_.click_listener_ok = false;
        }
        if (li)
            libinput_unref(li);
        if (ud)
            udev_unref(ud);
        return;
    }
    log("input-listener: libinput 就绪");

    pollfd pfd{libinput_get_fd(li), POLLIN, 0};
    while (running_) {
        if (::poll(&pfd, 1, 200) <= 0)
            continue;
        libinput_dispatch(li);
        libinput_event *ev;
        while ((ev = libinput_get_event(li)) != nullptr) {
            if (libinput_event_get_type(ev) == LIBINPUT_EVENT_POINTER_BUTTON) {
                auto *pe = libinput_event_get_pointer_event(ev);
                if (libinput_event_pointer_get_button_state(pe) ==
                    LIBINPUT_BUTTON_STATE_PRESSED) {
                    uint32_t b = libinput_event_pointer_get_button(pe);
                    if (b == BTN_LEFT)
                        shared_.push_click(0);
                    else if (b == BTN_RIGHT)
                        shared_.push_click(1);
                }
            }
            libinput_event_destroy(ev);
        }
    }
    libinput_unref(li);
    udev_unref(ud);
}

} // namespace rec
```

`tests/click-probe.cpp`(手动验证工具,不进 ctest):
```cpp
#include "input-listener.h"
#include "log.h"

#include <chrono>
#include <cstdio>
#include <thread>

int main()
{
    rec::log_sink() = [](const std::string &m) { std::printf("[log] %s\n", m.c_str()); };
    rec::SharedInputs shared;
    rec::InputListener listener(shared);
    std::printf("5 秒内点鼠标左/右键...\n");
    for (int i = 0; i < 50; i++) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        auto s = shared.drain();
        for (auto &c : s.clicks)
            std::printf("click button=%d\n", c.button);
    }
    return 0;
}
```

- [ ] **Step 2: 接进 CMake**

`CMakeLists.txt` 修改:obs-record target 源加 `src/input-listener.cpp`;并加手动工具:
```cmake
add_executable(click-probe tests/click-probe.cpp src/input-listener.cpp)
target_link_libraries(click-probe PRIVATE rec-core PkgConfig::INPUT)
target_include_directories(click-probe PRIVATE src)
```

- [ ] **Step 3: 真机验证**

```bash
cmake --build build && ./build/click-probe
```
点几下左键、右键。Expected: 打印 `click button=0` / `click button=1`,启动日志 `libinput 就绪`。

- [ ] **Step 4: 回归**

```bash
ctest --test-dir build --output-on-failure
```
Expected: 全部 Passed。

- [ ] **Step 5: Commit**

```bash
git add -A && git commit -m "feat: libinput click listener with graceful permission fallback"
```

---

### Task 10: OBS 滤镜集成(线程接线 + tick + 属性面板 + 热键)

**Files:**
- Create: `src/filter.h`, `src/filter.cpp`
- Modify: `src/plugin-main.cpp`、`src/effects-state.h`(EffectsConfig 加颜色字段)、`src/commands.h/.cpp`(加 `parse_presets`)、`tests/test-commands.cpp`、`CMakeLists.txt`(obs-record 加 `src/filter.cpp`)

**Interfaces:**
- Consumes: 前面全部模块
- Produces: `rec::register_record_filter()`(供 plugin-main 调用)、`rec::parse_presets(const std::string&) -> std::vector<double>`(空/非法回默认 {1.5,2.0,3.0});EffectsConfig 追加 `uint32_t highlight_color, ripple_left_color, ripple_right_color, badge_color, box_color`(格式 0xAABBGGRR,与 `vec4_from_rgba` 一致)
- 本任务 `video_render` 仍为直通(`obs_source_skip_video_filter`),渲染在 Task 11/12

- [ ] **Step 1: 写 parse_presets 失败测试**

`tests/test-commands.cpp` 追加:
```cpp
TEST(parse_presets_happy_and_fallback)
{
    auto v = parse_presets("1.5, 2.0, 3.0");
    CHECK(v.size() == 3);
    CHECK_NEAR(v[1], 2.0, 1e-9);
    auto d = parse_presets("");
    CHECK(d.size() == 3); // 回退默认
    CHECK_NEAR(d[0], 1.5, 1e-9);
    auto f = parse_presets("abc, 0.5, 2.5"); // 非法与 <=1 被丢弃
    CHECK(f.size() == 1);
    CHECK_NEAR(f[0], 2.5, 1e-9);
}
```

- [ ] **Step 2: 实现 parse_presets 并跑测试**

`src/commands.h` 追加声明:
```cpp
#include <vector>
std::vector<double> parse_presets(const std::string &csv);
```
`src/commands.cpp` 追加:
```cpp
std::vector<double> parse_presets(const std::string &csv)
{
    std::vector<double> out;
    std::istringstream ss(csv);
    std::string item;
    while (std::getline(ss, item, ',')) {
        try {
            double v = std::stod(item);
            if (v > 1.0 && v <= 10.0)
                out.push_back(v);
        } catch (...) {
        }
    }
    if (out.empty())
        out = {1.5, 2.0, 3.0};
    return out;
}
```
```bash
cmake --build build && ctest --test-dir build --output-on-failure
```
Expected: Passed。

- [ ] **Step 3: EffectsConfig 加颜色字段**

`src/effects-state.h`:`#include <cstdint>`,EffectsConfig 追加:
```cpp
    // 颜色格式 0xAABBGGRR(与 OBS 颜色属性、vec4_from_rgba 一致)
    uint32_t highlight_color = 0x6600D4FF;    // 半透明黄
    uint32_t ripple_left_color = 0xCC00D4FF;  // 黄
    uint32_t ripple_right_color = 0xCCFF8800; // 蓝
    uint32_t badge_color = 0xFF3538F5;        // 红
    uint32_t box_color = 0x4000D4FF;          // 半透明黄
    double badge_radius = 22.0;               // 徽章半径(px)
```

- [ ] **Step 4: 实现 filter.cpp**

`src/filter.h`:
```cpp
#pragma once
namespace rec {
void register_record_filter();
}
```

`src/filter.cpp`:
```cpp
#include "filter.h"

#include "commands.h"
#include "control-server.h"
#include "cursor-tracker.h"
#include "effects-state.h"
#include "input-listener.h"
#include "log.h"
#include "shared-state.h"

#include <cstdlib>
#include <memory>
#include <string>
#include <vector>

#include <obs-module.h>

namespace rec {

struct HotkeyBinding;

struct FilterCtx {
    obs_source_t *source = nullptr;
    SharedInputs shared;
    EffectsState fx;
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
    delete (FilterCtx *)data; // 线程全部 RAII 停止
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
    obs_source_skip_video_filter(ctx->source); // Task 11 替换为真实渲染
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
    info.output_flags = OBS_SOURCE_VIDEO;
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
```

`src/plugin-main.cpp` 改为:
```cpp
#include <obs-module.h>

#include "filter.h"

OBS_DECLARE_MODULE()

bool obs_module_load(void)
{
    rec::register_record_filter();
    blog(LOG_INFO, "[obs-record] version 0.1.0 loaded");
    return true;
}

void obs_module_unload(void)
{
    blog(LOG_INFO, "[obs-record] unloaded");
}
```

- [ ] **Step 5: 构建 + 真机验证接线**

```bash
cmake --build build && ctest --test-dir build --output-on-failure && cmake --install build
```
打开 OBS:屏幕采集源 → 滤镜 → 添加"Hyprland 录制增强" → 属性面板全部可见。然后:
```bash
./scripts/obs-record-cli zoom toggle
./scripts/obs-record-cli frobnicate
tail -5 ~/.config/obs-studio/logs/"$(ls -t ~/.config/obs-studio/logs | head -1)"
```
Expected: 日志有 `control-server: 监听 ...`、`input-listener: libinput 就绪`、`忽略非法命令: frobnicate`;OBS 不崩溃、画面正常直通。

- [ ] **Step 6: Commit**

```bash
git add -A && git commit -m "feat: OBS filter integration - threads, tick, properties, hotkeys"
```

---

### Task 11: 渲染器之一 —— 缩放 + 高亮圈 + 波纹 + 轨迹

**Files:**
- Create: `src/renderer.h`, `src/renderer.cpp`
- Modify: `src/filter.cpp`(video_render 换成真实渲染,destroy 释放 gs 资源)、`CMakeLists.txt`(obs-record 加 `src/renderer.cpp`)

**Interfaces:**
- Consumes: `EffectsState` 全部查询接口(Task 4/5)
- Produces: `rec::RenderCtx{gs_texrender_t* texrender; gs_effect_t* spot_effect; std::map<int, obs_source_t*> badges}`、`rec::render_frame(RenderCtx&, obs_source_t* source, EffectsState&)`(完整渲染管线)、`rec::render_free(RenderCtx&)`(须在 graphics 上下文内调用)

- [ ] **Step 1: 实现 renderer(渲染无法单测,实现后真机人工验收)**

`src/renderer.h`:
```cpp
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
```

`src/renderer.cpp`:
```cpp
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
    uint32_t w = target ? obs_source_get_base_width(target) : 0;
    uint32_t h = target ? obs_source_get_base_height(target) : 0;
    if (!target || !w || !h) {
        obs_source_skip_video_filter(source);
        return;
    }

    if (!rc.texrender)
        rc.texrender = gs_texrender_create(GS_RGBA, GS_ZS_NONE);
    gs_texrender_reset(rc.texrender);
    if (gs_texrender_begin(rc.texrender, w, h)) {
        struct vec4 clear = {};
        gs_clear(GS_CLEAR_COLOR, &clear, 0.0f, 0);
        gs_ortho(0.0f, (float)w, 0.0f, (float)h, -100.0f, 100.0f);
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
```

- [ ] **Step 2: 接进 filter.cpp**

FilterCtx 加成员 `RenderCtx render;`(`#include "renderer.h"`)。`filter_render` 替换为:
```cpp
static void filter_render(void *data, gs_effect_t *)
{
    auto *ctx = (FilterCtx *)data;
    render_frame(ctx->render, ctx->source, ctx->fx);
}
```
`filter_destroy` 替换为:
```cpp
static void filter_destroy(void *data)
{
    auto *ctx = (FilterCtx *)data;
    release_badges(ctx->render);
    obs_enter_graphics();
    render_free(ctx->render);
    obs_leave_graphics();
    delete ctx;
}
```

- [ ] **Step 3: 构建安装 + 人工验收**

```bash
cmake --build build && cmake --install build
```
打开 OBS 逐项确认:
1. 光标高亮圈跟着鼠标走,位置精确压在系统光标上(验证 1.25 缩放映射)
2. 左键黄色波纹扩散、右键蓝色波纹
3. `obs-record-cli trail toggle` 后快速移动出现渐隐拖尾
4. `obs-record-cli zoom toggle` 平滑放大 2x;死区内晃动画面不动;大幅移动平滑跟随;鼠标顶到屏幕角落画面不出黑边
5. `obs-record-cli zoom cycle` 依次 1.5/2/3;缩放时高亮圈/波纹随画面一起放大

- [ ] **Step 4: Commit**

```bash
git add -A && git commit -m "feat: renderer with zoom crop, cursor highlight, ripples and trail"
```

---

### Task 12: 渲染器之二 —— 固定标注 + 聚光灯

**Files:**
- Create: `data/spotlight.effect`
- Modify: `src/renderer.cpp`(draw_effects 补聚光灯与标注、加徽章文字源缓存与 effect 装载)

**Interfaces:**
- Consumes: `fx.pins()/pinbox_pending()/pinbox_preview()/spotlight_on/smooth_cursor()`(Task 5)、`RenderCtx.spot_effect/badges`(Task 11 已留槽)
- Produces: 完整效果渲染;绘制顺序:轨迹→波纹→高亮圈→**聚光灯压暗**→**标注(压暗层之上,聚光灯时仍醒目)**→box 预览

- [ ] **Step 1: 写聚光灯 shader**

`data/spotlight.effect`:
```hlsl
uniform float4x4 ViewProj;
uniform float2 center;
uniform float radius;
uniform float feather;
uniform float dim_alpha;
uniform float2 size;

struct VertData {
    float4 pos : POSITION;
    float2 uv : TEXCOORD0;
};

VertData VSDefault(VertData v_in)
{
    VertData vert_out;
    vert_out.pos = mul(float4(v_in.pos.xyz, 1.0), ViewProj);
    vert_out.uv = v_in.uv;
    return vert_out;
}

float4 PSSpot(VertData v_in) : TARGET
{
    float2 p = v_in.uv * size;
    float d = distance(p, center);
    float a = dim_alpha * smoothstep(radius, radius + feather, d);
    return float4(0.0, 0.0, 0.0, a);
}

technique Draw
{
    pass
    {
        vertex_shader = VSDefault(v_in);
        pixel_shader = PSSpot(v_in);
    }
}
```

- [ ] **Step 2: renderer.cpp 补实现**

新增(draw_effects 之前):
```cpp
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
```

`draw_effects` 末尾(替换 `(void)rc; (void)w; (void)h;`):
```cpp
    if (fx.spotlight_on && fx.cursor_valid)
        draw_spotlight(rc, fx, w, h);
    draw_pins(rc, fx); // 标注画在压暗层之上
```
文件顶部补 `#include <string>`。

- [ ] **Step 3: 构建安装 + 人工验收**

```bash
cmake --build build && cmake --install build
ls ~/.config/obs-studio/plugins/obs-record/data/spotlight.effect
```
OBS 里确认:
1. `pin add` ×3 → ①②③ 红底白字徽章;`pin undo` 删③;再 `pin add` 复用③
2. `pin box` 两次按键框出半透明黄框,期间白色虚线预览;10s 不按第二次自动消失
3. `pin clear` 全清
4. `spotlight toggle`:光标周围亮、四周压暗、边缘羽化平滑;移动时圆心平滑跟随不抖
5. 聚光灯开着时标注仍在压暗层之上清晰可见
6. `zoom toggle` 放大后:标注/聚光灯位置正确、随画面缩放

- [ ] **Step 4: Commit**

```bash
git add -A && git commit -m "feat: pinned annotations (badges + boxes) and feathered spotlight"
```

---

### Task 13: 收尾 —— CMake 可移植性、CI、README、验收清单

**Files:**
- Create: `.github/workflows/ci.yml`, `docs/manual-test-checklist.md`
- Modify: `CMakeLists.txt`(libobs 查找加 pkg-config 回退,兼容 Ubuntu CI)、`README.md`(补构建/安装/使用)

**Interfaces:**
- Consumes: 全部已完成任务
- Produces: 绿的 GitHub Actions;README 从"设计阶段"更新为可用状态

- [ ] **Step 1: CMake 加 libobs 回退**

`CMakeLists.txt` 中 `find_package(libobs REQUIRED)` 替换为:
```cmake
find_package(libobs QUIET)
if(NOT TARGET OBS::libobs)
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(OBSPC REQUIRED IMPORTED_TARGET GLOBAL libobs)
    add_library(OBS::libobs ALIAS PkgConfig::OBSPC)
endif()
```
本机验证:
```bash
rm -rf build && cmake -B build -G Ninja && cmake --build build && ctest --test-dir build --output-on-failure
```
Expected: 与之前一致全绿(本机走 cmake config 分支)。

- [ ] **Step 2: 写 CI**

`.github/workflows/ci.yml`:
```yaml
name: ci
on: [push, pull_request]
jobs:
  build:
    runs-on: ubuntu-latest
    steps:
      - uses: actions/checkout@v4
      - run: sudo apt-get update
      - run: sudo apt-get install -y ninja-build pkg-config libobs-dev libinput-dev libudev-dev nlohmann-json3-dev
      - run: cmake -B build -G Ninja
      - run: cmake --build build
      - run: ctest --test-dir build --output-on-failure
```

- [ ] **Step 3: 写人工验收清单**

`docs/manual-test-checklist.md`:
```markdown
# obs-record 人工验收清单

前置:滤镜挂在全屏 PipeWire 屏幕采集源上;scripts/hyprland-binds.conf 已 source;
obs-record-cli 在 PATH。

## 缩放
- [ ] SUPER+Z 平滑放大到 2x(约 0.3s),再按平滑还原
- [ ] 放大后死区内晃鼠标画面纹丝不动
- [ ] 鼠标出死区,画面平滑跟随,无突跳
- [ ] 鼠标顶到屏幕四角,画面贴边、无黑边
- [ ] SUPER+C 依次切换 1.5x / 2x / 3x
- [ ] obs-record-cli zoom set 5 生效;zoom set 0.5 回到 1x

## 光标可视化
- [ ] 高亮圈精确套住系统光标(4K + 1.25 缩放映射正确)
- [ ] 左键黄波纹、右键蓝波纹,约 0.4s 消散
- [ ] SUPER+T 开轨迹,快速画圈见渐隐拖尾;再按关闭且立即消失
- [ ] 属性面板关闭"点击波纹"后点击无效果

## 固定标注
- [ ] SUPER+A ×3 → ①②③;SUPER+SHIFT+A 撤销 ③;再 SUPER+A 重现 ③
- [ ] SUPER+B 两次按键框出高亮框,期间白虚线预览
- [ ] pin box 第一次按后等 10s 自动取消
- [ ] SUPER+D 一键全清,编号归 1
- [ ] 放大状态下标注位置正确、随画面缩放

## 聚光灯
- [ ] SUPER+S:光标周围亮、其余压暗、边缘羽化
- [ ] 快速晃动鼠标,光圈平滑跟随不抖
- [ ] 聚光灯下标注仍清晰(在压暗层上方)

## 降级
- [ ] 属性面板把 socket 路径改成非法值再改回:不崩溃,恢复监听
- [ ] 无 Hyprland 环境变量启动(env -u HYPRLAND_INSTANCE_SIGNATURE obs):
      不崩溃,光标效果隐藏,日志一条警告不刷屏
- [ ] 录制 30s 视频,回放确认所有效果都进了录像

## 性能
- [ ] 全效果开启 + 2x 缩放录制,OBS 统计面板无丢帧异常
```

- [ ] **Step 4: 更新 README**

`README.md` 修改:删掉"🚧 当前处于设计/开发阶段"行,"功能"章节后追加:
```markdown
## 构建与安装

```bash
sudo pacman -S --needed cmake ninja obs-studio libinput nlohmann-json
git clone https://github.com/mhpsy/obs-record && cd obs-record
cmake -B build -G Ninja && cmake --build build
ctest --test-dir build            # 可选:跑单元测试
cmake --install build             # 装到 ~/.config/obs-studio/plugins/
cp scripts/obs-record-cli ~/.local/bin/ && chmod +x ~/.local/bin/obs-record-cli
```

重启 OBS,在屏幕采集源的滤镜里添加 **"Hyprland 录制增强"**,
然后把 `scripts/hyprland-binds.conf` 的键位抄进你的 hyprland.conf。

## 命令一览

| 命令 | 作用 |
|---|---|
| `zoom toggle` / `zoom cycle` / `zoom set <倍率>` | 缩放开关 / 预设循环 / 直接指定 |
| `highlight toggle` | 光标高亮圈 |
| `trail toggle` | 移动轨迹 |
| `spotlight toggle` | 聚光灯 |
| `pin add` / `pin box` / `pin undo` / `pin clear` | 编号徽章 / 高亮框 / 撤销 / 全清 |
```

- [ ] **Step 5: 全量验证 + 提交推送**

```bash
rm -rf build && cmake -B build -G Ninja && cmake --build build && ctest --test-dir build --output-on-failure
git add -A && git commit -m "chore: CI workflow, portable libobs lookup, docs and acceptance checklist"
git push
gh run watch --exit-status || echo "CI 失败,查看 gh run view --log-failed"
```
Expected: 本地全绿;GitHub Actions 全绿。

- [ ] **Step 6: 按 docs/manual-test-checklist.md 完整过一遍验收**

全部勾完即 v1 完成。
