#pragma once
#include <functional>
#include <string>

namespace rec {

// 默认吞掉日志;插件启动时把它接到 blog(),测试可接到 stderr
inline std::function<void(const std::string &)> &log_sink()
{
    static std::function<void(const std::string &)> fn = [](const std::string &) {};
    return fn;
}

inline void log(const std::string &msg)
{
    log_sink()(msg);
}

} // namespace rec
