#include <obs-module.h>

OBS_DECLARE_MODULE()

OBS_MODULE_USE_DEFAULT_LOCALE("obs-filters", "en-US")

extern struct obs_source_info corner_pin_filter;
extern struct obs_source_info lens_distortion_filter;
extern struct obs_source_info stroke_filter;

bool obs_module_load(void)
{
	obs_register_source(&corner_pin_filter);
	obs_register_source(&lens_distortion_filter);
	obs_register_source(&stroke_filter);
	return true;
}
