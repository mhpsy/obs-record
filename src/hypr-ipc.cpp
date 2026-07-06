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
    out->clear();

    auto j = nlohmann::json::parse(json, nullptr, false);
    if (j.is_discarded() || !j.is_array())
        return false;

    std::vector<MonitorInfo> temp;
    try {
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
            temp.push_back(std::move(mi));
        }
    } catch (const nlohmann::json::exception &) {
        return false;
    }

    if (temp.empty())
        return false;

    out->swap(temp);
    return true;
}

} // namespace rec
