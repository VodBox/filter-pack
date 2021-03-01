#include <obs-module.h>
#include <graphics/vec4.h>

struct stroke_data {
	obs_source_t *context;
	gs_effect_t *effect;
	gs_eparam_t *color_param, *sharp_param;
	gs_eparam_t *width, *height;
	gs_eparam_t *image;

	gs_texrender_t *render;

	uint32_t stroke_width;
	vec4 color;
	vec4 color_srgb;
	bool sharp;
};

static const char *stroke_getname(void *unused)
{
	UNUSED_PARAMETER(unused);
	return "Stroke";
}

static void stroke_update(void *data, obs_data_t *settings)
{
	struct stroke_data *filter = (stroke_data *)data;

	filter->stroke_width = (uint32_t)obs_data_get_int(settings, "width");
	filter->sharp = obs_data_get_bool(settings, "sharp");

	uint32_t color = (uint32_t)obs_data_get_int(settings, "color");

	vec4_from_rgba(&filter->color, color);
	vec4_from_rgba_srgb(&filter->color_srgb, color);
}

static void stroke_destroy(void *data)
{
	struct stroke_data *filter = (stroke_data *)data;

	if (filter->effect) {
		obs_enter_graphics();
		gs_effect_destroy(filter->effect);
		gs_texrender_destroy(filter->render);
		obs_leave_graphics();
	}

	bfree(data);
}

static void *stroke_create(obs_data_t *settings, obs_source_t *context)
{
	struct stroke_data *filter =
		(stroke_data *)bzalloc(sizeof(struct stroke_data));
	char *effect_path = obs_module_file("stroke_filter.effect");

	filter->context = context;

	obs_enter_graphics();

	filter->effect = gs_effect_create_from_file(effect_path, NULL);

	if (filter->effect) {
		filter->color_param =
			gs_effect_get_param_by_name(filter->effect, "color");
		filter->sharp_param =
			gs_effect_get_param_by_name(filter->effect, "sharp");
		filter->width =
			gs_effect_get_param_by_name(filter->effect, "texwidth");
		filter->height = gs_effect_get_param_by_name(filter->effect,
							     "texheight");
		filter->image =
			gs_effect_get_param_by_name(filter->effect, "image");
	}

	filter->render = gs_texrender_create(GS_RGBA, GS_ZS_NONE);

	obs_leave_graphics();

	bfree(effect_path);

	if (!filter->effect) {
		stroke_destroy(filter);
		return NULL;
	}

	stroke_update(filter, settings);
	return filter;
}

static void stroke_render(void *data, gs_effect_t *effect)
{
	struct stroke_data *filter = (stroke_data *)data;

	obs_source_t *target = obs_filter_get_target(filter->context);
	obs_source_t *parent = obs_filter_get_parent(filter->context);

	uint32_t cx = obs_source_get_base_width(target);
	uint32_t cy = obs_source_get_base_height(target);

	if (!target || !parent) {
		obs_source_skip_video_filter(filter->context);
		return;
	}

	gs_texrender_reset(filter->render);

	gs_blend_state_push();
	gs_blend_function(GS_BLEND_ONE, GS_BLEND_ZERO);

	if (gs_texrender_begin(filter->render, cx, cy)) {
		uint32_t parent_flags = obs_source_get_output_flags(target);

		bool custom_draw = (parent_flags & OBS_SOURCE_CUSTOM_DRAW) != 0;
		bool async = (parent_flags & OBS_SOURCE_ASYNC) != 0;
		struct vec4 clear_color;

		vec4_zero(&clear_color);
		gs_clear(GS_CLEAR_COLOR, &clear_color, 0.0f, 0);
		gs_ortho(0.0f, (float)cx, 0.0f, (float)cy, -100.0f, 100.0f);

		if (target == parent && !custom_draw && !async)
			obs_source_default_render(target);
		else
			obs_source_video_render(target);

		gs_texrender_end(filter->render);
	}

	gs_blend_state_pop();

	const bool linear_srgb = gs_get_linear_srgb() ||
				 (filter->color.w < 1.0f);

	const bool previous = gs_framebuffer_srgb_enabled();
	gs_enable_framebuffer_srgb(linear_srgb);

	gs_effect_set_bool(filter->sharp_param, filter->sharp);

	if (linear_srgb) {
		gs_effect_set_vec4(filter->color_param, &filter->color_srgb);
	} else {
		gs_effect_set_vec4(filter->color_param, &filter->color);
	}

	gs_effect_set_int(filter->height, cy);
	gs_effect_set_int(filter->width, cx);

	size_t i = 0;

	gs_texture_t *tex =
		gs_texture_create(cx, cy, GS_RGBA, 1, NULL, GS_RENDER_TARGET);
	gs_copy_texture(tex, gs_texrender_get_texture(filter->render));

	for (i = 0; tex && i < filter->stroke_width; i++) {
		gs_texrender_reset(filter->render);

		gs_blend_state_push();
		gs_blend_function(GS_BLEND_ONE, GS_BLEND_ZERO);

		if (gs_texrender_begin(filter->render, cx, cy)) {
			struct vec4 clear_color;

			vec4_zero(&clear_color);
			gs_clear(GS_CLEAR_COLOR, &clear_color, 0.0f, 0);
			gs_ortho(0.0f, (float)cx, 0.0f, (float)cy, -100.0f,
				 100.0f);

			if (linear_srgb) {
				gs_effect_set_texture_srgb(filter->image, tex);
			} else {
				gs_effect_set_texture(filter->image, tex);
			}

			gs_technique_t *tech =
				gs_effect_get_technique(filter->effect, "Draw");
			size_t passes, x;

			passes = gs_technique_begin(tech);

			for (x = 0; x < passes; x++) {
				gs_technique_begin_pass(tech, x);
				gs_draw_sprite(tex, 0, cx, cy);
				gs_technique_end_pass(tech);
			}

			gs_technique_end(tech);

			gs_texrender_end(filter->render);
		}

		gs_blend_state_pop();

		gs_copy_texture(tex, gs_texrender_get_texture(filter->render));
	}

	if (tex) {
		effect = obs_get_base_effect(OBS_EFFECT_DEFAULT);

		gs_eparam_t *image =
			gs_effect_get_param_by_name(effect, "image");

		if (linear_srgb)
			gs_effect_set_texture_srgb(image, tex);
		else
			gs_effect_set_texture(image, tex);

		while (gs_effect_loop(effect, "Draw"))
			gs_draw_sprite(tex, 0, cx, cy);

		gs_texture_destroy(tex);
	}

	gs_enable_framebuffer_srgb(previous);

	UNUSED_PARAMETER(effect);
}

static obs_properties_t *stroke_properties(void *data)
{
	obs_properties_t *props = obs_properties_create();

	obs_properties_add_int(props, "width", "Stroke Width", 1, 10, 1);
	obs_properties_add_color(props, "color", "Stroke Color");
	obs_properties_add_bool(props, "sharp", "Sharp Corners");

	UNUSED_PARAMETER(data);
	return props;
}

static void stroke_defaults(obs_data_t *settings)
{
	obs_data_set_default_int(settings, "width", 1);
	obs_data_set_default_int(settings, "color", 0xFFFFFFFF);
	obs_data_set_default_bool(settings, "sharp", false);
}

struct obs_source_info stroke_filter = [&] {
	obs_source_info stroke_filter = {0};
	stroke_filter.id = "stroke_filter";
	stroke_filter.type = OBS_SOURCE_TYPE_FILTER;
	stroke_filter.output_flags = OBS_SOURCE_VIDEO;
	stroke_filter.get_name = stroke_getname;
	stroke_filter.create = stroke_create;
	stroke_filter.destroy = stroke_destroy;
	stroke_filter.update = stroke_update;
	stroke_filter.video_render = stroke_render;
	stroke_filter.get_properties = stroke_properties;
	stroke_filter.get_defaults = stroke_defaults;
	return stroke_filter;
}();
