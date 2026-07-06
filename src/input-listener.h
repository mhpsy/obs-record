#pragma once
#include "shared-state.h"

#include <atomic>
#include <thread>

namespace rec {

class InputListener {
public:
    explicit InputListener(SharedInputs &shared);
    ~InputListener();

private:
    void loop();
    SharedInputs &shared_;
    std::atomic<bool> running_{true};
    std::thread th_;
};

} // namespace rec
