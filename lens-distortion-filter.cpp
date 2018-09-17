#include <obs-module.h>
#include <obs-source.h>
#include <obs.h>
#include <util/platform.h>

struct lens_distortion_data {
	obs_source_t                   *context;

	gs_effect_t                    *effect;
	gs_eparam_t                    *amount, *zoom_param;
	gs_eparam_t                    *width, *height;
	
	double strength;
	float zoom;
	bool dimension;
};

static const char *lens_distortion_getname(void *unused)
{
	UNUSED_PARAMETER(unused);
	return "Lens Distortion";
}

static void lens_distortion_update(void *data, obs_data_t *settings)
{
	struct lens_distortion_data *filter = (lens_distortion_data *)data;

	filter->strength = obs_data_get_double(settings, "Strength");
	filter->zoom = obs_data_get_double(settings, "Zoom");
	filter->dimension = obs_data_get_string(settings, "Dimension") == "Horizontal";
}

static void lens_distortion_destroy(void *data)
{
	struct lens_distortion_data *filter = (lens_distortion_data *)data;

	if (filter->effect) {
		obs_enter_graphics();
		gs_effect_destroy(filter->effect);
		obs_leave_graphics();
	}

	bfree(data);
}

static void *lens_distortion_create(obs_data_t *settings, obs_source_t *context)
{
	struct lens_distortion_data *filter = (lens_distortion_data *)
		bzalloc(sizeof(struct lens_distortion_data));
	char *effect_path = obs_module_file("lens_distortion_filter.effect");

	filter->context = context;

	obs_enter_graphics();

	filter->effect = gs_effect_create_from_file(effect_path, NULL);

	if (filter->effect) {
		filter->amount = gs_effect_get_param_by_name(
			filter->effect, "strength");
		filter->zoom_param = gs_effect_get_param_by_name(
			filter->effect, "zoom");
		filter->width = gs_effect_get_param_by_name(
			filter->effect, "texwidth");
		filter->height = gs_effect_get_param_by_name(
			filter->effect, "texheight");
	}

	obs_leave_graphics();

	bfree(effect_path);

	if (!filter->effect) {
		lens_distortion_destroy(filter);
		return NULL;
	}

	lens_distortion_update(filter, settings);
	return filter;
}

static void lens_distortion_render(void *data, gs_effect_t *effect)
{
	struct lens_distortion_data *filter = (lens_distortion_data *)data;

	if (!obs_source_process_filter_begin(filter->context, GS_RGBA,
		OBS_ALLOW_DIRECT_RENDERING))
		return;

	gs_effect_set_float(filter->amount, filter->strength);
	gs_effect_set_float(filter->zoom_param, filter->zoom);
	obs_source_t *target = obs_filter_get_target(filter->context);
	gs_effect_set_int(filter->height, obs_source_get_base_height(target));
	gs_effect_set_int(filter->width, obs_source_get_base_width(target));

	obs_source_process_filter_end(filter->context, filter->effect, 0, 0);

	UNUSED_PARAMETER(effect);
}

static obs_properties_t *lens_distortion_properties(void *data)
{
	obs_properties_t *props = obs_properties_create();
	obs_property_t *p;

	obs_properties_add_float_slider(props, "Strength", "Strength", -90, 90, 1);
	obs_properties_add_float_slider(props, "Zoom", "Zoom", 0.00, 2, 0.01);
	p = obs_properties_add_list(props, "Dimension", "Horizontal or Vertical", OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_STRING);
	obs_property_list_add_string(p, "Horizontal", "Horizontal");
	obs_property_list_add_string(p, "Vertical", "Vertical");

	UNUSED_PARAMETER(data);
	return props;
}

static void lens_distortion_defaults(obs_data_t *settings)
{
	obs_data_set_default_double(settings, "Strength", 0);
	obs_data_set_default_string(settings, "Dimension", "Vertical");
	obs_data_set_default_double(settings, "Zoom", 1.0);
}

struct obs_source_info lens_distortion_filter = [&] {
	obs_source_info lens_distortion_filter = { 0 };
	lens_distortion_filter.id = "lens_distortion_filter";
	lens_distortion_filter.type = OBS_SOURCE_TYPE_FILTER;
	lens_distortion_filter.output_flags = OBS_SOURCE_VIDEO;
	lens_distortion_filter.get_name = lens_distortion_getname;
	lens_distortion_filter.create = lens_distortion_create;
	lens_distortion_filter.destroy = lens_distortion_destroy;
	lens_distortion_filter.update = lens_distortion_update;
	lens_distortion_filter.video_render = lens_distortion_render;
	lens_distortion_filter.get_properties = lens_distortion_properties;
	lens_distortion_filter.get_defaults = lens_distortion_defaults;
	return lens_distortion_filter;
}();
