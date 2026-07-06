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
    bool bound_ = false; // 仅当本实例成功 bind/listen 时才在析构里 unlink
    std::atomic<bool> running_{true};
    std::thread th_;
};

} // namespace rec
