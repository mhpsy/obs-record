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
