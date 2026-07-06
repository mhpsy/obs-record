#include <obs-module.h>

OBS_DECLARE_MODULE()

bool obs_module_load(void)
{
    blog(LOG_INFO, "[obs-record] version 0.1.0 loaded");
    return true;
}

void obs_module_unload(void)
{
    blog(LOG_INFO, "[obs-record] unloaded");
}
