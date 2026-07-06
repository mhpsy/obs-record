#include "commands.h"

#include <sstream>

namespace rec {

Command parse_command(const std::string &line)
{
    std::istringstream ss(line);
    std::string a, b;
    ss >> a >> b;

    if (a == "zoom") {
        if (b == "toggle") return {CmdType::ZoomToggle, 0};
        if (b == "cycle") return {CmdType::ZoomCycle, 0};
        if (b == "set") {
            double v;
            if (ss >> v) return {CmdType::ZoomSet, v};
            return {};
        }
    } else if (a == "highlight" && b == "toggle") {
        return {CmdType::HighlightToggle, 0};
    } else if (a == "trail" && b == "toggle") {
        return {CmdType::TrailToggle, 0};
    } else if (a == "spotlight" && b == "toggle") {
        return {CmdType::SpotlightToggle, 0};
    } else if (a == "pin") {
        if (b == "add") return {CmdType::PinAdd, 0};
        if (b == "box") return {CmdType::PinBox, 0};
        if (b == "undo") return {CmdType::PinUndo, 0};
        if (b == "clear") return {CmdType::PinClear, 0};
    }
    return {};
}

} // namespace rec
