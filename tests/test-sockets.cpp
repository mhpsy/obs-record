#include "test-framework.h"
#include "hypr-ipc.h"
#include "mock-server.h"

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
