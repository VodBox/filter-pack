#include <obs-module.h>
#include <obs-source.h>
#include <obs.h>
#include <util/platform.h>
#include "corner-pin-widget.hpp"

struct corner_pin_data {
	obs_source_t                   *context;

	gs_effect_t                    *effect;
	gs_eparam_t                    *uv1_param, *uv2_param, *uv3_param, *uv4_param;
	gs_eparam_t                    *width, *height;
	gs_eparam_t *outline_param;

	int                            topLeftX;
	int                            topRightX;
	int                            bottomLeftX;
	int                            bottomRightX;
	int                            topLeftY;
	int                            topRightY;
	int                            bottomLeftY;
	int                            bottomRightY;
	float                          texwidth, texheight;
	struct vec2                    uv1;
	struct vec2                    uv2;
	struct vec2                    uv3;
	struct vec2                    uv4;
	bool outline;

	CornerPinWindow                *window;
};

static const char *corner_pin_getname(void *unused)
{
	UNUSED_PARAMETER(unused);
	return "Corner Pin";
}

static void corner_pin_update(void *data, obs_data_t *settings)
{
	struct corner_pin_data *filter = (corner_pin_data *)data;

	filter->topLeftX = obs_data_get_int(settings, "topLeftX");
	filter->topLeftY = obs_data_get_int(settings, "topLeftY");
	filter->topRightX = obs_data_get_int(settings, "topRightX");
	filter->topRightY = obs_data_get_int(settings, "topRightY");
	filter->bottomLeftX = obs_data_get_int(settings, "bottomLeftX");
	filter->bottomLeftY = obs_data_get_int(settings, "bottomLeftY");
	filter->bottomRightX = obs_data_get_int(settings, "bottomRightX");
	filter->bottomRightY = obs_data_get_int(settings, "bottomRightY");
	filter->outline = obs_data_get_bool(settings, "outline");
	obs_source_t *target = obs_filter_get_target(filter->context);
	filter->texheight = obs_source_get_base_height(target);
	filter->texwidth = obs_source_get_base_width(target);
}

static void corner_pin_destroy(void *data)
{
	struct corner_pin_data *filter = (corner_pin_data *)data;

	if (filter->effect) {
		obs_enter_graphics();
		gs_effect_destroy(filter->effect);
		obs_leave_graphics();
	}

	if (filter->window) {
		filter->window->close();
		delete filter->window;
		filter->window = nullptr;
	}

	bfree(data);
}

static void *corner_pin_create(obs_data_t *settings, obs_source_t *context)
{
	struct corner_pin_data *filter = (corner_pin_data *)
		bzalloc(sizeof(struct corner_pin_data));
	char *effect_path = obs_module_file("corner_pin_filter.effect");

	filter->context = context;

	obs_enter_graphics();

	filter->effect = gs_effect_create_from_file(effect_path, NULL);

	if (filter->effect) {
		filter->uv1_param = gs_effect_get_param_by_name(
			filter->effect, "uv1");
		filter->uv2_param = gs_effect_get_param_by_name(
			filter->effect, "uv2");
		filter->uv3_param = gs_effect_get_param_by_name(
			filter->effect, "uv3");
		filter->uv4_param = gs_effect_get_param_by_name(
			filter->effect, "uv4");
		filter->outline_param = gs_effect_get_param_by_name(
			filter->effect, "outline");
		filter->width = gs_effect_get_param_by_name(
			filter->effect, "texwidth");
		filter->height = gs_effect_get_param_by_name(
			filter->effect, "texheight");
	}

	obs_leave_graphics();

	bfree(effect_path);

	if (!filter->effect) {
		corner_pin_destroy(filter);
		return NULL;
	}

	corner_pin_update(filter, settings);
	return filter;
}

static void calc_uv(struct corner_pin_data *filter,
	struct vec2 *uv1, struct vec2 *uv2,
	struct vec2 *uv3, struct vec2 *uv4)
{
	obs_source_t *target = obs_filter_get_target(filter->context);
	uint32_t width;
	uint32_t height;

	if (!target) {
		width = 0;
		height = 0;
		return;
	}
	else {
		width = obs_source_get_base_width(target);
		height = obs_source_get_base_height(target);
	}

	uv1->x = (float)filter->topLeftX / (float)width;
	uv1->y = (float)filter->topLeftY / (float)height;

	uv2->x = (float)filter->topRightX / (float)width;
	uv2->y = (float)filter->topRightY / (float)height;

	uv3->x = (float)filter->bottomLeftX / (float)width;
	uv3->y = (float)filter->bottomLeftY / (float)height;

	uv4->x = (float)filter->bottomRightX / (float)width;
	uv4->y = (float)filter->bottomRightY / (float)height;
}

static void corner_pin_tick(void *data, float seconds) {
	struct corner_pin_data *filter = (corner_pin_data *)data;

	vec2_zero(&filter->uv1);
	vec2_zero(&filter->uv2);
	vec2_zero(&filter->uv3);
	vec2_zero(&filter->uv4);
	calc_uv(filter, &filter->uv1, &filter->uv2, &filter->uv3, &filter->uv4);

	UNUSED_PARAMETER(seconds);
}

static void corner_pin_render(void *data, gs_effect_t *effect)
{
	struct corner_pin_data *filter = (corner_pin_data *)data;

	if (!obs_source_process_filter_begin(filter->context, GS_RGBA,
				OBS_ALLOW_DIRECT_RENDERING))
		return;

	gs_effect_set_vec2(filter->uv1_param, &filter->uv1);
	gs_effect_set_vec2(filter->uv2_param, &filter->uv2);
	gs_effect_set_vec2(filter->uv3_param, &filter->uv3);
	gs_effect_set_vec2(filter->uv4_param, &filter->uv4);
	gs_effect_set_int(filter->height, (int)&filter->texheight);
	gs_effect_set_int(filter->width, (int)&filter->texwidth);
	gs_effect_set_bool(filter->outline_param, filter->outline);

	obs_source_process_filter_end(filter->context, filter->effect, 0, 0);
	
	UNUSED_PARAMETER(effect);
}

static bool openUI(obs_properties_t *props, obs_property_t *property, void *data)
{
	struct corner_pin_data *filter = (corner_pin_data *)data;
	obs_source_t *target = obs_filter_get_target(filter->context);

	if(!filter->window)
		filter->window = new CornerPinWindow(nullptr, target, filter);
	filter->window->show();
	return true;
}

static obs_properties_t *corner_pin_properties(void *data)
{
	obs_properties_t *props = obs_properties_create();

	obs_properties_add_button(props, "openUI", "Open", openUI);

	obs_properties_add_int_slider(props, "topLeftX", "Top Left X", -8192, 8192, 1);
	obs_properties_add_int_slider(props, "topLeftY", "Top Left Y", -8192, 8192, 1);
	obs_properties_add_int_slider(props, "topRightX", "Top Right X", -8192, 8192, 1);
	obs_properties_add_int_slider(props, "topRightY", "Top Right Y", -8192, 8192, 1);
	obs_properties_add_int_slider(props, "bottomLeftX", "Bottom Left X", -8192, 8192, 1);
	obs_properties_add_int_slider(props, "bottomLeftY", "Bottom Left Y", -8192, 8192, 1);
	obs_properties_add_int_slider(props, "bottomRightX", "Bottom Right X", -8192, 8192, 1);
	obs_properties_add_int_slider(props, "bottomRightY", "Bottom Right Y", -8192, 8192, 1);
	obs_properties_add_bool(props, "outline", "Display Box");

	UNUSED_PARAMETER(data);
	return props;
}

static void corner_pin_defaults(obs_data_t *settings)
{
	obs_data_set_default_bool(settings, "outline", false);
}

struct obs_source_info corner_pin_filter = [&] {
	obs_source_info corner_pin_filter = { 0 };

	corner_pin_filter.id = "corner_pin_filter";
	corner_pin_filter.type = OBS_SOURCE_TYPE_FILTER;
	corner_pin_filter.output_flags = OBS_SOURCE_VIDEO;
	corner_pin_filter.get_name = corner_pin_getname;
	corner_pin_filter.create = corner_pin_create;
	corner_pin_filter.destroy = corner_pin_destroy;
	corner_pin_filter.update = corner_pin_update;
	corner_pin_filter.video_tick = corner_pin_tick;
	corner_pin_filter.video_render = corner_pin_render;
	corner_pin_filter.get_properties = corner_pin_properties;
	corner_pin_filter.get_defaults = corner_pin_defaults;
	return corner_pin_filter;
}();
