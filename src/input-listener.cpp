#include "input-listener.h"

#include "log.h"

#include <atomic>
#include <cerrno>

#include <fcntl.h>
#include <libinput.h>
#include <libudev.h>
#include <linux/input-event-codes.h>
#include <poll.h>
#include <unistd.h>

namespace rec {

// libinput_udev_assign_seat() 即使设备打开失败(权限不足)也会返回成功,
// 所以真正的权限判定要靠 open_restricted 自己的调用结果来统计。
struct OpenStats {
    std::atomic<int> ok{0};
    std::atomic<int> denied{0};
};

static int open_restricted(const char *path, int flags, void *user_data)
{
    auto *stats = static_cast<OpenStats *>(user_data);
    int fd = ::open(path, flags);
    if (fd < 0) {
        if (errno == EACCES || errno == EPERM)
            stats->denied++;
        return -errno;
    }
    stats->ok++;
    return fd;
}

static void close_restricted(int fd, void *)
{
    ::close(fd);
}

static const libinput_interface kIface = {open_restricted, close_restricted};

// 丢弃 li 上所有待处理事件(不作为点击处理),用于初次 dispatch 排空设备
// 枚举产生的 DEVICE_ADDED 等事件。
static void drain_events(libinput *li)
{
    libinput_event *ev;
    while ((ev = libinput_get_event(li)) != nullptr)
        libinput_event_destroy(ev);
}

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
    OpenStats stats;
    udev *ud = udev_new();
    libinput *li = ud ? libinput_udev_create_context(&kIface, &stats, ud) : nullptr;
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

    // udev 枚举设备的事件(DEVICE_ADDED 等)要 dispatch 一次才会产生,
    // open_restricted 的调用结果也在此时落地到 stats 里。
    libinput_dispatch(li);
    drain_events(li);

    if (stats.ok == 0 && stats.denied > 0) {
        log("input-listener: libinput 初始化失败(用户在 input 组吗?"
            "sudo usermod -aG input $USER 后重新登录)——点击波纹已禁用");
        {
            std::lock_guard<std::mutex> lk(shared_.mu);
            shared_.click_listener_ok = false;
        }
        libinput_unref(li);
        udev_unref(ud);
        return;
    }
    if (stats.ok == 0 && stats.denied == 0) {
        log("input-listener: 未发现输入设备(udev 会在热插拔时补上)");
    } else {
        log("input-listener: libinput 就绪");
    }

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
