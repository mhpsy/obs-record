# obs-record 设计文档

日期:2026-07-06
状态:已与用户逐段确认

## 1. 背景与目标

用户在 Hyprland(Wayland)桌面下用 OBS 录制"帮同学讲解简历修改"类教学视频。痛点:

- 4K 屏(3840x2160 @ 1.25 缩放)录文档,文字太小,观众看不清
- 观众容易跟丢鼠标,不知道讲的是哪一行
- 想标记"这几处有问题"并逐个讲解,缺少手段

参照 obs-zoom-to-mouse 的缩放跟随体验,但该项目依赖 X11/Windows API 获取全局鼠标坐标,在 Wayland 下不可用。Hyprland 提供 IPC(`cursorpos`),已在用户机器实测可用(Hyprland 0.55.4,OBS 32.1.2),因此做一个 **Hyprland 专用**的 OBS 插件。

### v1 功能范围(用户已确认全选)

1. 缩放跟随鼠标(快捷键平滑放大,死区防抖跟随)
2. 光标可视化:常驻高亮圈、点击波纹、移动轨迹拖尾
3. 固定标注:编号徽章 + 两次按键定义的高亮矩形,可撤销/清空
4. 聚光灯模式:光标周围保持明亮,其余画面压暗

### 明确不做(v1)

- 按键显示(已有 showmethekey 等现成 Wayland 工具)
- 自由手绘(固定标注基本覆盖需求,交互实现重)
- 多显示器支持(用户单屏;架构上预留显示器选择下拉)
- X11 / 其他 Wayland 合成器支持(仅 Hyprland)

## 2. 技术方案选型

考虑过三个方案:

| 方案 | 说明 | 结论 |
|---|---|---|
| A. 原生 C/C++ OBS 滤镜插件 | 单一组件,全部效果在 OBS 内渲染 | **采用** |
| B. Lua 脚本 | 免编译,但点击监听/socket/逐帧绘制需 LuaJIT ffi 硬写,脆弱难维护 | 否决 |
| C. OBS 缩放 + 屏幕侧 layer-shell 覆盖层 | 自己也能看到标注,但两个组件、工程量最大 | 否决 |

方案 A 的已知取舍:效果只出现在录制画面/OBS 预览里,讲解者自己的屏幕完全正常。用户已接受(钉标注时鼠标就在目标位置,位置自己清楚)。

## 3. 架构

**形态**:C++ OBS **滤镜**插件(基于官方 obs-plugintemplate,CMake),产物 `obs-record.so`,安装到用户插件目录(`~/.config/obs-studio/plugins/obs-record/`)。使用时挂在 PipeWire 屏幕采集源的滤镜链上,滤镜显示名"Hyprland 录制增强"。

### 模块划分

| 模块 | 职责 | 运行方式 | 可独立测试 |
|---|---|---|---|
| cursor-tracker | 轮询 Hyprland IPC `cursorpos`(约 60Hz,每次请求新建连接);启动时及需要时查 `monitors` 获取分辨率/缩放/位置 | 后台线程 | 是(mock IPC socket) |
| input-listener | libinput 读 `/dev/input`,监听 BTN_LEFT/BTN_RIGHT 按下事件 | 后台线程 | 是(需真机/权限) |
| control-server | 监听 `$XDG_RUNTIME_DIR/obs-record.sock`,行式文本命令协议 | 后台线程 | 是 |
| effects-state | 纯逻辑:缩放动画与死区跟随数学、轨迹环形缓冲、波纹生命周期、标注列表、命令解析。**零 OBS 依赖** | 被 tick 调用 | 是(单元测试主体) |
| renderer | 滤镜 `video_render`:源 → 纹理 → 源像素空间叠加效果 → 统一缩放裁切输出 | OBS 图形线程 | 人工验收 |

### 数据流与线程模型

三个后台线程只写入互斥锁保护的共享状态(最新光标坐标、点击事件队列、待执行命令队列)。每帧:

1. `video_tick`:取状态快照 → 执行排队命令 → 推进动画(缩放插值、波纹年龄、轨迹淡出、box 预览)
2. `video_render`:按当前状态绘制,无任何逻辑、无阻塞调用

锁持有时间微秒级,渲染线程永不等待 IPC。

### 渲染顺序(关键设计点)

先把源画面渲染到纹理,在**源像素空间**叠加轨迹/波纹/高亮圈/标注/聚光灯,最后对合成结果统一做缩放裁切。这样缩放时所有效果自然跟随放大,标注位置无需二次换算。

### 坐标映射

Hyprland IPC 返回**逻辑坐标**;采集画面是**物理像素**。映射:

```
px = (logical - monitor.logical_x) * monitor.scale
```

用户环境 scale=1.25(fractional),映射需用浮点并测试取整边界。滤镜属性提供显示器下拉(数据来自 IPC `monitors`,默认第一个)。

**前提假设(v1)**:滤镜挂在整屏、原生分辨率的采集源上(采集像素尺寸 = 显示器物理像素尺寸)。若尺寸不匹配,按比例缩放映射并记日志提醒。

## 4. 功能行为

### 4.1 缩放跟随鼠标

- `zoom toggle`:1.0x ↔ 记忆倍率,约 300ms 缓出动画;记忆倍率初始为 2.0,被 `zoom cycle` / `zoom set` 更新
- `zoom cycle`:预设倍率循环(默认 1.5 / 2.0 / 3.0,属性面板可配)
- `zoom set <factor>`:直接指定
- 跟随:视口中心死区(默认视口宽高的 30%),鼠标在死区内画面静止;出死区后视口指数平滑追踪,速度可调
- 视口 clamp 在画面边界内,靠边时光标可偏离视口中心

### 4.2 光标可视化(三件独立开关)

- 高亮圈:常驻半透明圆 + 描边,默认黄色、半径 40px,`highlight toggle`,**默认开**
- 点击波纹:按下瞬间从点击位置扩散圆环,约 0.4s 淡出;左键黄 / 右键蓝(可配);无需命令,监听到即触发,**默认开**(属性面板可关)
- 移动轨迹:最近约 0.6s 光标路径渐隐拖尾,`trail toggle`,**默认关**

(聚光灯与缩放为瞬时状态,启动时始终关闭/1.0x。)

### 4.3 固定标注(存储于源像素空间)

- `pin add`:当前鼠标位置钉自动编号圆形徽章(①②③…)
- `pin box`:第一次按记录角点 A;此后显示 A→当前鼠标的**虚线预览框**;第二次按确定生成半透明高亮矩形;10s 未确认自动取消
- `pin undo`:撤销最后一个(含未完成的 box)
- `pin clear`:全清

### 4.4 聚光灯

- `spotlight toggle`:全画面压暗(默认 60% 黑),光标周围半径约 250px(源像素)保持原亮,边缘羽化
- 圆心使用平滑后的光标位置,防手抖

### 4.5 控制通道(双通道)

主通道:Unix socket 文本协议,一行一命令。命令全集:

```
zoom toggle | zoom cycle | zoom set <f>
highlight toggle | trail toggle | spotlight toggle
pin add | pin box | pin undo | pin clear
```

仓库附 `scripts/obs-record-cli`(向 socket 写一行命令的 shell 脚本)和 hyprland.conf 绑定示例:

```
bind = SUPER, Z, exec, obs-record-cli zoom toggle
bind = SUPER, C, exec, obs-record-cli zoom cycle
bind = SUPER, A, exec, obs-record-cli pin add
bind = SUPER, B, exec, obs-record-cli pin box
bind = SUPER, D, exec, obs-record-cli pin clear
bind = SUPER, S, exec, obs-record-cli spotlight toggle
bind = SUPER, T, exec, obs-record-cli trail toggle
```

备用通道:同一批动作注册为 OBS 内置快捷键(仅 OBS 聚焦时可用,Wayland 限制)。

### 4.6 滤镜属性面板

各功能开关、颜色、尺寸(高亮半径/聚光灯半径/徽章大小)、时长(波纹/轨迹)、缩放倍率列表、跟随速度、死区比例、显示器选择、socket 路径。

## 5. 错误处理

原则:**任何外部依赖失效,只降级对应功能,绝不影响录制本身。**

| 故障 | 行为 |
|---|---|
| Hyprland IPC 不可用(环境变量缺失/socket 消失) | 日志警告;光标类效果隐藏、缩放居中不跟随;每 2s 重试 |
| libinput 权限不足(用户不在 input 组) | 启动日志给出修复命令提示;仅点击波纹失效 |
| control socket 残留(异常退出遗留) | 启动时清理 stale socket |
| 非法命令 | 记日志,忽略,不崩溃 |
| 显示器配置变化 | 光标坐标越界时重新查询 monitors 更新映射 |

## 6. 测试策略

- **单元测试**(ctest,零 OBS 依赖):坐标映射(重点 1.25 fractional scale 与取整边界)、死区/缓动数学、命令解析、标注列表操作、波纹/轨迹生命周期
- **协议测试**:mock Hyprland IPC socket,验证 cursor-tracker 解析与重连
- **人工验收清单**:真机 OBS 逐条验证缩放手感、边界 clamp、pin box 预览、聚光灯羽化、权限降级行为
- **CI**:obs-plugintemplate 自带 GitHub Actions,构建 + 单测

## 7. 仓库结构

```
obs-record/
├── src/                        # 五个模块各自独立文件
├── tests/                      # 单元测试
├── scripts/                    # obs-record-cli、hyprland.conf 示例
├── docs/superpowers/specs/     # 设计文档
└── CMakeLists.txt              # 基于 obs-plugintemplate
```

## 8. 系统要求(一次性设置)

- Hyprland(依赖其 IPC;其他合成器不支持)
- OBS Studio ≥ 30(开发基于 32.1.2)
- 点击波纹需用户在 `input` 组:`sudo usermod -aG input $USER` 后重新登录
- 构建依赖:cmake、libobs 头文件(Arch 的 obs-studio 包自带)、libinput

## 9. 开发环境事实(实测)

- Hyprland 0.55.4,`hyprctl cursorpos` 返回如 `2070, 1351`
- OBS Studio 32.1.2(Arch 包 32.1.2-7)
- 显示器 HDMI-A-2:3840x2160,scale 1.25,位于 (0,0)
