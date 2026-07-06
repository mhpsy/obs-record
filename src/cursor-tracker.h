#pragma once
#include "shared-state.h"

#include <atomic>
#include <string>
#include <thread>

namespace rec {

class CursorTracker {
public:
    // sock_path 为空则自动用 hypr_socket_path()
    explicit CursorTracker(SharedInputs &shared, std::string sock_path = "");
    ~CursorTracker();

private:
    void loop();
    SharedInputs &shared_;
    std::string sock_path_;
    std::atomic<bool> running_{true};
    std::thread th_;
};

} // namespace rec
