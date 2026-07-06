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
