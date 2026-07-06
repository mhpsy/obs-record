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

std::vector<double> parse_presets(const std::string &csv)
{
    std::vector<double> out;
    std::istringstream ss(csv);
    std::string item;
    while (std::getline(ss, item, ',')) {
        try {
            double v = std::stod(item);
            if (v > 1.0 && v <= 10.0)
                out.push_back(v);
        } catch (...) {
        }
    }
    if (out.empty())
        out = {1.5, 2.0, 3.0};
    return out;
}

} // namespace rec
