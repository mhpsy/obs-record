#pragma once
#include <string>

namespace rec {

enum class CmdType {
    ZoomToggle, ZoomCycle, ZoomSet,
    HighlightToggle, TrailToggle, SpotlightToggle,
    PinAdd, PinBox, PinUndo, PinClear,
    Invalid,
};

struct Command {
    CmdType type = CmdType::Invalid;
    double value = 0;
};

Command parse_command(const std::string &line);

} // namespace rec
