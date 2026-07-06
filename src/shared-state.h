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
