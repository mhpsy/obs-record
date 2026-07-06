#include "test-framework.h"
#include "commands.h"

using namespace rec;

TEST(parse_all_valid_commands)
{
    CHECK(parse_command("zoom toggle").type == CmdType::ZoomToggle);
    CHECK(parse_command("zoom cycle").type == CmdType::ZoomCycle);
    CHECK(parse_command("highlight toggle").type == CmdType::HighlightToggle);
    CHECK(parse_command("trail toggle").type == CmdType::TrailToggle);
    CHECK(parse_command("spotlight toggle").type == CmdType::SpotlightToggle);
    CHECK(parse_command("pin add").type == CmdType::PinAdd);
    CHECK(parse_command("pin box").type == CmdType::PinBox);
    CHECK(parse_command("pin undo").type == CmdType::PinUndo);
    CHECK(parse_command("pin clear").type == CmdType::PinClear);
}

TEST(parse_zoom_set_with_value)
{
    Command c = parse_command("zoom set 2.5");
    CHECK(c.type == CmdType::ZoomSet);
    CHECK_NEAR(c.value, 2.5, 1e-9);
}

TEST(parse_tolerates_whitespace)
{
    CHECK(parse_command("  zoom   toggle \n").type == CmdType::ZoomToggle);
    CHECK(parse_command("zoom set 2\n").type == CmdType::ZoomSet);
}

TEST(parse_rejects_garbage)
{
    CHECK(parse_command("").type == CmdType::Invalid);
    CHECK(parse_command("zoom").type == CmdType::Invalid);
    CHECK(parse_command("zoom set").type == CmdType::Invalid);
    CHECK(parse_command("zoom set abc").type == CmdType::Invalid);
    CHECK(parse_command("frobnicate now").type == CmdType::Invalid);
}

TEST(parse_presets_happy_and_fallback)
{
    auto v = parse_presets("1.5, 2.0, 3.0");
    CHECK(v.size() == 3);
    CHECK_NEAR(v[1], 2.0, 1e-9);
    auto d = parse_presets("");
    CHECK(d.size() == 3); // 回退默认
    CHECK_NEAR(d[0], 1.5, 1e-9);
    auto f = parse_presets("abc, 0.5, 2.5"); // 非法与 <=1 被丢弃
    CHECK(f.size() == 1);
    CHECK_NEAR(f[0], 2.5, 1e-9);
}
