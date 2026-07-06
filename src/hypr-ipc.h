#pragma once
#include "geometry.h"

#include <string>
#include <vector>

namespace rec {

std::string hypr_socket_path();
bool hypr_query(const std::string &sock_path, const std::string &request,
                std::string *reply, std::string *err);
bool parse_cursorpos(const std::string &reply, Vec2 *out);
bool parse_monitors_json(const std::string &json, std::vector<MonitorInfo> *out);

} // namespace rec
