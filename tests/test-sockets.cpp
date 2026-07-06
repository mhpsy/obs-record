#include "test-framework.h"
#include "hypr-ipc.h"
#include "mock-server.h"
#include "cursor-tracker.h"
#include "log.h"

#include <chrono>
#include <cstdlib>
#include <vector>

using namespace rec;

TEST(hypr_query_roundtrip_via_mock)
{
    MockIpcServer srv("mock-ipc.sock", [](const std::string &req) -> std::string {
        if (req == "cursorpos")
            return "123, 456";
        return "unknown request";
    });
    std::string reply, err;
    CHECK(hypr_query("mock-ipc.sock", "cursorpos", &reply, &err));
    CHECK(reply == "123, 456");
    Vec2 p;
    CHECK(parse_cursorpos(reply, &p));
    CHECK_NEAR(p.x, 123, 1e-9);
}

TEST(hypr_query_fails_on_missing_socket)
{
    std::string reply, err;
    CHECK(!hypr_query("does-not-exist.sock", "cursorpos", &reply, &err));
    CHECK(!err.empty());
}

TEST(cursor_tracker_populates_shared_state)
{
    MockIpcServer srv("mock-hypr.sock", [](const std::string &req) -> std::string {
        if (req == "cursorpos")
            return "100, 200";
        if (req == "j/monitors")
            return R"([{"name":"HDMI-A-2","x":0,"y":0,"width":3840,"height":2160,"scale":1.25}])";
        return "unknown request";
    });
    SharedInputs shared;
    {
        CursorTracker tracker(shared, "mock-hypr.sock");
        bool ok = false;
        for (int i = 0; i < 100 && !ok; i++) { // 最多等 2s
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
            auto s = shared.drain();
            ok = s.cursor_valid && !s.monitors.empty();
            if (ok) {
                CHECK_NEAR(s.cursor_logical.x, 100, 1e-9);
                CHECK_NEAR(s.cursor_logical.y, 200, 1e-9);
                CHECK(s.monitors[0].name == "HDMI-A-2");
            }
        }
        CHECK(ok);
    } // 析构必须干净退出、不卡住
}

TEST(cursor_tracker_degrades_when_socket_dies)
{
    SharedInputs shared;
    CursorTracker tracker(shared, "never-existed.sock");
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    auto s = shared.drain();
    CHECK(!s.cursor_valid); // 降级为无效,而不是崩溃
}

TEST(shared_drain_moves_and_clears_queues)
{
    SharedInputs shared;
    shared.push_click(0);
    shared.push_command("zoom toggle");
    auto s = shared.drain();
    CHECK(s.clicks.size() == 1);
    CHECK(s.command_lines.size() == 1);
    auto s2 = shared.drain();
    CHECK(s2.clicks.empty());
    CHECK(s2.command_lines.empty());
}

TEST(cursor_tracker_logs_once_when_hyprland_env_missing)
{
    // Save current env state
    const char *orig_sig = std::getenv("HYPRLAND_INSTANCE_SIGNATURE");
    std::string saved_sig;
    bool was_set = false;
    if (orig_sig) {
        saved_sig = orig_sig;
        was_set = true;
    }

    // Unset the env var to simulate missing Hyprland
    unsetenv("HYPRLAND_INSTANCE_SIGNATURE");

    // Capture logs into a vector
    std::vector<std::string> captured_logs;
    auto original_sink = log_sink();
    log_sink() = [&captured_logs](const std::string &msg) {
        captured_logs.push_back(msg);
    };

    try {
        SharedInputs shared;
        {
            CursorTracker tracker(shared); // default socket path (empty)
            std::this_thread::sleep_for(std::chrono::milliseconds(300));
            auto s = shared.drain();
            CHECK(!s.cursor_valid); // degraded to invalid
        }

        // Should have exactly 1 log line (constructor's message)
        CHECK(captured_logs.size() == 1);
        CHECK(captured_logs[0].find("HYPRLAND_INSTANCE_SIGNATURE 缺失") != std::string::npos);

    } catch (...) {
        // Restore env and sink in any case
        log_sink() = original_sink;
        if (was_set)
            setenv("HYPRLAND_INSTANCE_SIGNATURE", saved_sig.c_str(), 1);
        throw;
    }

    // Restore env and sink
    log_sink() = original_sink;
    if (was_set)
        setenv("HYPRLAND_INSTANCE_SIGNATURE", saved_sig.c_str(), 1);
}
