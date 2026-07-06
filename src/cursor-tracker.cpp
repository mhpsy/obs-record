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
