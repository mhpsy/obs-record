#include <obs-module.h>

#include "filter.h"

OBS_DECLARE_MODULE()

bool obs_module_load(void)
{
    rec::register_record_filter();
    blog(LOG_INFO, "[obs-record] version 0.1.0 loaded");
    return true;
}

void obs_module_unload(void)
{
    blog(LOG_INFO, "[obs-record] unloaded");
}
