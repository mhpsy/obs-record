#include "control-server.h"

#include "log.h"

#include <cstdio>
#include <cstring>
#include <sstream>

#include <poll.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

namespace {

// 探测 path 上是否有存活实例在监听:能 connect 上就说明有主,不能因为
// 文件存在就认定是异常残留(stale),否则会把活实例的 socket 文件删掉。
bool has_live_listener(const std::string &path)
{
    if (path.empty() || path.size() >= sizeof(sockaddr_un::sun_path))
        return false;
    int fd = ::socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (fd < 0)
        return false;
    sockaddr_un addr{};
    addr.sun_family = AF_UNIX;
    std::memcpy(addr.sun_path, path.c_str(), path.size() + 1);
    bool live = ::connect(fd, (sockaddr *)&addr, sizeof(addr)) == 0;
    ::close(fd);
    return live;
}

} // namespace

namespace rec {

ControlServer::ControlServer(SharedInputs &shared, std::string sock_path)
    : shared_(shared), path_(std::move(sock_path))
{
    if (has_live_listener(path_)) {
        log("control-server: socket 已被占用: " + path_);
        fd_ = -1;
        return;
    }
    ::unlink(path_.c_str()); // 清理上次异常退出的残留(stale 文件,无人监听)
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
    bound_ = true;
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
    if (bound_ && !path_.empty())
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
        // 空闲/慢客户端不能让线程永久卡在 read() 里,析构 join() 才能有界
        timeval tv{0, 500 * 1000}; // 500ms
        ::setsockopt(c, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
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
