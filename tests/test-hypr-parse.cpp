#include "test-framework.h"
#include "hypr-ipc.h"

using namespace rec;

TEST(parse_cursorpos_real_format)
{
    Vec2 p;
    CHECK(parse_cursorpos("2010, 1210", &p)); // 真机实测格式
    CHECK_NEAR(p.x, 2010, 1e-9);
    CHECK_NEAR(p.y, 1210, 1e-9);
    CHECK(parse_cursorpos("0, 0\n", &p)); // 容忍尾部换行
}

TEST(parse_cursorpos_rejects_garbage)
{
    Vec2 p;
    CHECK(!parse_cursorpos("", &p));
    CHECK(!parse_cursorpos("unknown request", &p));
}

TEST(parse_monitors_real_json)
{
    // 真机 j/monitors 输出裁剪版(含无关字段验证容忍性)
    const char *json = R"([{
        "id": 0, "name": "HDMI-A-2", "description": "XXX",
        "width": 3840, "height": 2160, "refreshRate": 60.0,
        "x": 0, "y": 0, "scale": 1.25, "transform": 0, "focused": true
    }])";
    std::vector<MonitorInfo> ms;
    CHECK(parse_monitors_json(json, &ms));
    CHECK(ms.size() == 1);
    CHECK(ms[0].name == "HDMI-A-2");
    CHECK(ms[0].width == 3840);
    CHECK(ms[0].height == 2160);
    CHECK_NEAR(ms[0].scale, 1.25, 1e-9);
    CHECK_NEAR(ms[0].x, 0, 1e-9);
}

TEST(parse_monitors_rejects_bad_json)
{
    std::vector<MonitorInfo> ms;
    CHECK(!parse_monitors_json("not json", &ms));
    CHECK(!parse_monitors_json("{}", &ms)); // 不是数组
}
