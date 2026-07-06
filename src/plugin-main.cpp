#include <obs-module.h>

#include "filter.h"
#include "log.h"

OBS_DECLARE_MODULE()

bool obs_module_load(void)
{
    // 绑定一次 log_sink() 即可:模块加载时(此刻还不可能有任何滤镜实例存在)
    // 完成绑定,避免 filter_create() 里每个新滤镜实例都重新赋值这个
    // std::function——那样会在已有实例的线程并发调用 log() 读取它时,构成
    // 数据竞争(UB)。lambda 无捕获,绑定内容与原来完全一致。
    rec::log_sink() = [](const std::string &m) { blog(LOG_INFO, "[obs-record] %s", m.c_str()); };
    rec::register_record_filter();
    blog(LOG_INFO, "[obs-record] version 0.1.0 loaded");
    return true;
}

void obs_module_unload(void)
{
    blog(LOG_INFO, "[obs-record] unloaded");
}
