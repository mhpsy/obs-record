#include "input-listener.h"
#include "log.h"

#include <chrono>
#include <cstdio>
#include <thread>

int main()
{
    rec::log_sink() = [](const std::string &m) { std::printf("[log] %s\n", m.c_str()); };
    rec::SharedInputs shared;
    rec::InputListener listener(shared);
    std::printf("5 秒内点鼠标左/右键...\n");
    for (int i = 0; i < 50; i++) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        auto s = shared.drain();
        for (auto &c : s.clicks)
            std::printf("click button=%d\n", c.button);
    }
    return 0;
}
