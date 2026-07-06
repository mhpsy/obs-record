# obs-record

专为 **Hyprland** 桌面打造的 OBS 录屏增强插件,面向"讲解式录屏"场景(演示文档、讲解简历、教学演示)。

## 为什么需要它

在 Wayland 下,经典的 [obs-zoom-to-mouse](https://github.com/BlankSourceCode/obs-zoom-to-mouse) 拿不到全局鼠标坐标,无法工作。本插件利用 **Hyprland IPC** 获取光标位置,把缩放跟随带回 Wayland,并在此基础上加入一整套讲解辅助效果。

## 功能

- **缩放跟随鼠标** —— 快捷键平滑放大(1.5x / 2x / 3x 可配),镜头带死区跟随光标:小幅晃动画面纹丝不动,大幅移动平滑追踪
- **光标高亮圈** —— 光标周围常驻半透明高亮,观众永远不会跟丢鼠标
- **点击波纹** —— 点击瞬间扩散圆环,左键/右键不同颜色
- **移动轨迹** —— 快速淡出的光标拖尾,做视线引导
- **固定标注** —— 快捷键在屏幕上钉编号徽章 ①②③,或两次按键框出高亮矩形(标记"这一行有问题"),支持撤销与一键清空
- **聚光灯模式** —— 光标周围保持明亮,其余画面压暗,把观众注意力锁在讲解位置

所有效果只出现在**录制画面**里,你自己的屏幕完全正常,不受干扰。

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

## 工作原理

一个 C++ 编写的 OBS **滤镜**插件,挂在 PipeWire 屏幕采集源上:

- 光标坐标:轮询 Hyprland IPC(约 60Hz)
- 点击事件:libinput 监听 `/dev/input`
- 快捷键:插件监听 Unix socket,直接用 Hyprland 的 `bind = ..., exec` 发命令,不受 Wayland 全局快捷键限制

```
bind = SUPER, Z, exec, obs-record-cli zoom toggle
bind = SUPER, A, exec, obs-record-cli pin add
bind = SUPER, S, exec, obs-record-cli spotlight toggle
```

> 注意:同一时间只需给一个采集源挂一个滤镜实例;第二个实例的 socket 控制会自动让位(探测到已有监听则降级)。

## 系统要求

- Hyprland(依赖其 IPC,其他合成器暂不支持)
- OBS Studio ≥ 30
- 点击波纹功能需要用户在 `input` 组:
  ```bash
  sudo usermod -aG input $USER   # 之后重新登录生效
  ```

## 许可

待定。
