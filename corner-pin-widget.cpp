/*****************************************************************************
Copyright (C) 2016-2018 by Dillon Pentz.

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 2 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program.  If not, see <http://www.gnu.org/licenses/>.
*****************************************************************************/

#include "corner-pin-widget.hpp"
#include <obs-frontend-api/obs-frontend-api.h>
#include <QScreen>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QCheckBox>
#include <QMouseEvent>
#include <QWheelEvent>
#include "graphics/matrix4.h"

struct TextSource {
	obs_source_t *source = nullptr;

	gs_texture_t *tex = nullptr;
};

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

	CornerPinWidget                *window;
};

using namespace std;

CornerPinWindow::CornerPinWindow(QWidget *parent, obs_source_t *source_,
	void *data)
	: QWidget(parent)
{
	setAttribute(Qt::WA_NativeWindow);

	obs_source_t *curScene = obs_frontend_get_current_scene();
	scene = obs_scene_from_source(curScene);

	source = source_;

	QVBoxLayout *verticalLayout = new QVBoxLayout(this);
	cornerWidget = new CornerPinWidget(this, source_, data);

	QHBoxLayout *horizontalLayout = new QHBoxLayout(this);
	comboBox = new QComboBox(this);
	obs_scene_enum_items(scene, [](obs_scene_t*, obs_sceneitem_t *item, void *param)
	{
		obs_source_t *source = obs_sceneitem_get_source(item);
		CornerPinWindow *window = (CornerPinWindow *)param;
		char *name = (char *)obs_source_get_name(source);
		if (name == obs_source_get_name(window->source)) {
			int items = window->comboBox->count();
			window->comboBox->insertItem(0, ("#" + to_string(items + 1))
				.c_str(), obs_sceneitem_get_id(item));
		}
		return true;
	}, this);
	QCheckBox *check = new QCheckBox("Zoom To Scene Item", this);

	QSizePolicy sizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
	sizePolicy.setHorizontalStretch(0);
	sizePolicy.setVerticalStretch(0);
	sizePolicy.setHeightForWidth(cornerWidget->sizePolicy().hasHeightForWidth());
	cornerWidget->setSizePolicy(sizePolicy);
	cornerWidget->setMinimumSize(QSize(20, 20));

	QSizePolicy minSizePolicy(QSizePolicy::Minimum, QSizePolicy::Minimum);
	minSizePolicy.setHorizontalStretch(0);
	minSizePolicy.setVerticalStretch(0);
	check->setSizePolicy(minSizePolicy);
	comboBox->setSizePolicy(minSizePolicy);

	resize(QSize(500, 300));

	verticalLayout->addWidget(cornerWidget);
	horizontalLayout->addWidget(comboBox);
	horizontalLayout->addWidget(check);
	verticalLayout->addLayout(horizontalLayout);

	auto checked = [this](int state) {
		bool enabled = state == Qt::Checked;
		cornerWidget->zoom = enabled;
	};

	auto changed = [this](int index) {
		obs_scene_enum_items(scene, [](obs_scene_t*, obs_sceneitem_t *item, void *param)
		{
			obs_source_t *source = obs_sceneitem_get_source(item);
			CornerPinWindow *window = (CornerPinWindow *)param;
			char *name = (char *)obs_source_get_name(source);
			if (name == obs_source_get_name(window->source)
				&& obs_sceneitem_get_id(item)
				== window->comboBox->currentData()) {
				window->cornerWidget->sceneitem = item;
			}
			return true;
		}, this);
	};

	connect(comboBox, QOverload<int>::of(&QComboBox::activated), changed);
	connect(check, &QCheckBox::stateChanged, checked);

	obs_source_release(curScene);
}

void CornerPinWindow::showEvent(QShowEvent *event)
{
	if (this->isMaximized())
		showNormal();
	UNUSED_PARAMETER(event);
}

void CornerPinWindow::resizeEvent(QResizeEvent *event)
{
	UNUSED_PARAMETER(event);
}

CornerPinWindow::~CornerPinWindow()
{
	return;
}

CornerPinWidget::CornerPinWidget(QWidget *parent, obs_source_t *source_,
	void *data)
	: QWidget(parent)
{
	filter_data = data;

	setAttribute(Qt::WA_PaintOnScreen);
	setAttribute(Qt::WA_NoSystemBackground);
	setAttribute(Qt::WA_OpaquePaintEvent);
	setAttribute(Qt::WA_DontCreateNativeAncestors);
	setAttribute(Qt::WA_NativeWindow);

	source = source_;

	fill(verts, verts + 8, nullptr);

	auto windowVisible = [this](bool visible)
	{
		if (!visible)
			return;

		if (!display) {
			CreateDisplay();
		}
		else {
			QSize size = this->size() * this->devicePixelRatio();
			obs_display_resize(display, size.width(), size.height());
		}
	};

	auto sizeChanged = [this](QScreen*)
	{
		CreateDisplay();

		QSize size = this->size() * this->devicePixelRatio();
		obs_display_resize(display, size.width(), size.height());
	};

	connect(windowHandle(), &QWindow::visibleChanged, windowVisible);
	connect(windowHandle(), &QWindow::screenChanged, sizeChanged);
}

CornerPinWidget::~CornerPinWidget()
{
	obs_display_remove_draw_callback(this->GetDisplay(),
		drawPreview, this);
	obs_display_destroy(display);
	if(text)
		obs_source_release(text);

	obs_enter_graphics();
	for (int i = 0; i < 8; i++) {
		if (verts[i])
			gs_vertexbuffer_destroy(verts[i]);
	}
	obs_leave_graphics();
}

void CornerPinWidget::CreateDisplay()
{
	if (display || !windowHandle()->isExposed())
		return;

	obs_data_t *settings = obs_data_create();
	obs_data_t *font = obs_data_create();

	vec4 color;
	vec4_set(&color, 0.0f, 0.0f, 0.0f, 1.0f);

#if defined(_WIN32)
	obs_data_set_string(font, "face", "Arial");
#elif defined(__APPLE__)
	obs_data_set_string(font, "face", "Helvetica");
#else
	obs_data_set_string(font, "face", "Monospace");
#endif
	obs_data_set_int(font, "flags", 1); // Bold text
	obs_data_set_int(font, "size", 50);

	obs_data_set_obj(settings, "font", font);
	obs_data_set_string(settings, "text", "hidden");
	obs_data_set_bool(settings, "outline", true);
	obs_data_set_int(settings, "outline_size", 5);
	obs_data_set_vec4(settings, "outline_color", &color);

#ifdef _WIN32
	const char *text_source_id = "text_gdiplus";
#else
	const char *text_source_id = "text_ft2_source";
#endif
	if(!text)
		text = obs_source_create_private(text_source_id, "test",
			settings);
	obs_data_release(settings);
	obs_data_release(font);

	QSize size = this->size() * this->devicePixelRatio();

	gs_init_data info = {};
	info.cx = size.width();
	info.cy = size.height();
	info.format = GS_RGBA;
	info.zsformat = GS_ZS_NONE;

#ifdef _WIN32
	info.window.hwnd = (HWND)winId();
#elif __APPLE__
	info.window.view = (id)winId();
#else
	info.window.id = winId();
	info.window.display = QX11Info::display();
#endif

	display = obs_display_create(&info);

	obs_display_add_draw_callback(this->GetDisplay(),
		drawPreview, this);
}

void CornerPinWidget::handleResizeRequest(int width, int height)
{
	UNUSED_PARAMETER(width);
	UNUSED_PARAMETER(height);
}

static void GetScaleAndCenterPos(
	int baseCX, int baseCY, int windowCX, int windowCY,
	int &x, int &y, float &scale)
{
	double windowAspect, baseAspect;
	int newCX, newCY;

	windowAspect = double(windowCX) / double(windowCY);
	baseAspect = double(baseCX) / double(baseCY);

	if (windowAspect > baseAspect) {
		scale = float(windowCY) / float(baseCY);
		newCX = int(double(windowCY) * baseAspect);
		newCY = windowCY;
	}
	else {
		scale = float(windowCX) / float(baseCX);
		newCX = windowCX;
		newCY = int(float(windowCX) / baseAspect);
	}

	x = windowCX / 2 - newCX / 2;
	y = windowCY / 2 - newCY / 2;
}

static void fillVert(float r, float g, float b, float a) {
	gs_effect_t    *solid = obs_get_base_effect(OBS_EFFECT_SOLID);
	gs_eparam_t    *color = gs_effect_get_param_by_name(solid, "color");
	gs_technique_t *tech = gs_effect_get_technique(solid, "Solid");

	vec4 colorVal;
	vec4_set(&colorVal, r, g, b, a);
	gs_effect_set_vec4(color, &colorVal);

	gs_technique_begin(tech);
	gs_technique_begin_pass(tech, 0);

	gs_draw(GS_TRISTRIP, 0, 0);

	gs_technique_end_pass(tech);
	gs_technique_end(tech);
}

static void drawLine(int x1, int y1, int x2, int y2, gs_vertbuffer_t **verts,
	int index)
{
	int border = 2;

	int dX = x2 - x1;
	int dY = y2 - y1;

	double angle = atan2(dY, dX);
	double rotSin = sin(angle);
	double rotCos = cos(angle);

	float cx1 = x1 + border * (-1*rotCos - -1*rotSin);
	float cx2 = x1 + border * (-1*rotCos - 1*rotSin);
	float cx3 = x2 + border * (1*rotCos - 1*rotSin);
	float cx4 = x2 + border * (1*rotCos - -1*rotSin);

	float cy1 = y1 + border * (-1*rotSin + -1*rotCos);
	float cy2 = y1 + border * (-1*rotSin + 1*rotCos);
	float cy3 = y2 + border * (1*rotSin + 1*rotCos);
	float cy4 = y2 + border * (1*rotSin + -1*rotCos);

	if (verts[index])
		gs_vertexbuffer_destroy(verts[index]);

	gs_render_start(true);
	gs_vertex2f(cx1, cy1);
	gs_vertex2f(cx2, cy2);
	gs_vertex2f(cx3, cy3);
	gs_vertex2f(cx4, cy4);
	gs_vertex2f(cx1, cy1);
	verts[index] = gs_render_save();

	gs_load_vertexbuffer(verts[index]);

	fillVert(0.0f, 0.0f, 1.0f, 1.0f);
}

static void drawHandle(int x, int y, bool selected,
	gs_vertbuffer_t **verts, int index) {
	int size = 5;

	if (verts[index])
		gs_vertexbuffer_destroy(verts[index]);

	gs_render_start(true);
	gs_vertex2f(x-size, y-size);
	gs_vertex2f(x+size, y-size);
	gs_vertex2f(x+size, y+size);
	gs_vertex2f(x-size, y+size);
	gs_vertex2f(x-size, y-size);
	verts[index] = gs_render_save();

	gs_load_vertexbuffer(verts[index]);

	fillVert(selected ? 0.0f : 1.0f, selected ? 1.0f : 0.0f, 0.0f, 1.0f);
}

void CornerPinWidget::drawPreview(void *data, uint32_t cx, uint32_t cy)
{
	CornerPinWidget *window = static_cast<CornerPinWidget*>(data);

	if (!window->source)
		return;

	corner_pin_data *filter = (corner_pin_data *)window->filter_data;

	obs_source_t *currentScene = obs_frontend_get_current_scene();

	uint32_t sceneCX = max(obs_source_get_width(currentScene), 1u);
	uint32_t sceneCY = max(obs_source_get_height(currentScene), 1u);

	uint32_t areaCX;
	uint32_t areaCY;

	vec2  itemScale, itemSize, pos;
	itemScale.x = 1.0f;
	itemScale.y = 1.0f;

	int   x, y;
	int   newCX, newCY;
	int   offX, offY;
	float scale;

	if(window->sceneitem) {
		obs_sceneitem_get_scale(window->sceneitem, &itemScale);

		if(window->zoom) {
			areaCX = max(obs_source_get_width(window->source), 1u)
				* itemScale.x;
			areaCY = max(obs_source_get_height(window->source), 1u)
				* itemScale.y;
		} else {
			areaCX = sceneCX;
			areaCY = sceneCY;

			itemSize.x = max(obs_source_get_width(window->source), 1u)
				* itemScale.x;
			itemSize.y = max(obs_source_get_width(window->source), 1u)
				* itemScale.x;
		}

		obs_sceneitem_get_pos(window->sceneitem, &pos);

		offX = pos.x;
		offY = pos.y;

		GetScaleAndCenterPos(areaCX, areaCY, cx, cy, x, y, scale);
	} else {
		areaCX = sceneCX;
		areaCY = sceneCY;

		offX = 0;
		offY = 0;

		GetScaleAndCenterPos(areaCX, areaCY, cx, cy, x, y, scale);
	}

	newCX = int(scale * float(areaCX));
	newCY = int(scale * float(areaCY));

	window->offX = x;
	window->offY = y;
	window->previewW = newCX;
	window->previewH = newCY;
	window->previewScale = scale;

	gs_viewport_push();
	gs_projection_push();

	if(window->sceneitem && window->zoom) {
		gs_ortho(offX, areaCX + offX, offY, areaCY + offY,
			-100.0f, 100.0f);
	} else {
		gs_ortho(0.0f, sceneCX, 0.0f, sceneCY,
			-100.0f, 100.0f);
	}

	gs_set_viewport(x, y, newCX, newCY);

	obs_source_video_render(currentScene);

	if (obs_sceneitem_visible(window->sceneitem)) {
		if (window->sceneitem && window->zoom) {
			gs_ortho(0.0f, float(sceneCX), 0.0f, float(sceneCY),
				-100.0f, 100.0f);
			offX = 0;
			offY = 0;

			itemScale.x = 1.0f;
			itemScale.y = 1.0f;
		}

		drawLine(filter->topLeftX * itemScale.x + offX,
			filter->topLeftY * itemScale.y + offY,
			filter->topRightX * itemScale.x + offX,
			filter->topRightY * itemScale.y + offY, window->verts, 0);
		drawLine(filter->topRightX * itemScale.x + offX,
			filter->topRightY * itemScale.y + offY,
			filter->bottomRightX * itemScale.x + offX,
			filter->bottomRightY * itemScale.y + offY, window->verts, 1);
		drawLine(filter->bottomRightX * itemScale.x + offX,
			filter->bottomRightY * itemScale.y + offY,
			filter->bottomLeftX * itemScale.x + offX,
			filter->bottomLeftY * itemScale.y + offY, window->verts, 2);
		drawLine(filter->bottomLeftX * itemScale.x + offX,
			filter->bottomLeftY * itemScale.y + offY,
			filter->topLeftX * itemScale.x + offX,
			filter->topLeftY * itemScale.y + offY, window->verts, 3);

		drawHandle(filter->topLeftX * itemScale.x + offX,
			filter->topLeftY * itemScale.y + offY,
			window->selected == 1, window->verts, 4);
		drawHandle(filter->topRightX * itemScale.x + offX,
			filter->topRightY * itemScale.y + offY,
			window->selected == 2, window->verts, 5);
		drawHandle(filter->bottomLeftX * itemScale.x + offX,
			filter->bottomLeftY * itemScale.y + offY,
			window->selected == 3, window->verts, 6);
		drawHandle(filter->bottomRightX * itemScale.x + offX,
			filter->bottomRightY * itemScale.y + offY,
			window->selected == 4, window->verts, 7);

		if (window->mouseDrag) {
			uint32_t textWidth = obs_source_get_width(window->text);
			uint32_t textHeight = obs_source_get_height(window->text);

			int tX = (window->movedMouse.x - pos.x)*itemScale.x + 10;
			tX = min(tX, (int)(sceneCX - textWidth - 10));
			tX = max(tX, 10);

			int tY = (window->movedMouse.y - pos.y)*itemScale.y - window->textSize / 2;
			tY = min(tY, int(sceneCY - textHeight - 10));
			tY = max(tY, 10);

			vec3 offset;
			vec3_set(&offset, tX, tY, 0.0f);

			gs_matrix_push();
			gs_matrix_translate(&offset);
			obs_source_video_render(window->text);
			gs_matrix_pop();
		}
	}

	gs_projection_pop();
	gs_viewport_pop();

	obs_source_release(currentScene);
}

void CornerPinWidget::resizeEvent(QResizeEvent *event)
{
	QWidget::resizeEvent(event);

	CreateDisplay();

	if (isVisible() && display) {
		QSize size = this->size() * this->devicePixelRatio();
		obs_display_resize(display, size.width(), size.height());
	}
}

void CornerPinWidget::paintEvent(QPaintEvent *event)
{
	CreateDisplay();

	QWidget::paintEvent(event);
}

void CornerPinWidget::showEvent(QShowEvent *event)
{
	if(display)
		obs_display_add_draw_callback(this->GetDisplay(),
			drawPreview, this);
	UNUSED_PARAMETER(event);
}

void CornerPinWidget::hideEvent(QHideEvent *event)
{
	obs_display_remove_draw_callback(this->GetDisplay(),
		drawPreview, this);

	corner_pin_data *filter = (corner_pin_data *)filter_data;
	obs_source_update_properties(filter->context);
	UNUSED_PARAMETER(event);
}

void CornerPinWidget::mousePressEvent(QMouseEvent *event)
{
	corner_pin_data *filter = (corner_pin_data *)filter_data;

	vec2 itemScale, pos;
	obs_sceneitem_get_scale(sceneitem, &itemScale);
	obs_sceneitem_get_pos(sceneitem, &pos);

	uint32_t width = obs_source_get_width(source);
	uint32_t height = obs_source_get_height(source);

	vec2 scale;
	vec2_set(&scale, previewW / float(width), previewH / float(height));

	vec2_set(&mouse, event->x() - offX, event->y() - offY);
	if(!zoom) {
		vec2_div(&mouse, &mouse, &scale);
		vec2_sub(&mouse, &mouse, &pos);
		vec2_div(&mouse, &mouse, &itemScale);
		vec2_mul(&mouse, &mouse, &scale);
	}

	vec2 res;
	vec2_set(&res, previewW, previewH);

	vec2 uv1;
	vec2_mul(&uv1, &filter->uv1, &res);
	vec2 uv2;
	vec2_mul(&uv2, &filter->uv2, &res);
	vec2 uv3;
	vec2_mul(&uv3, &filter->uv3, &res);
	vec2 uv4;
	vec2_mul(&uv4, &filter->uv4, &res);

	if (vec2_dist(&mouse, &uv1) < 10) {
		selected = 1;
	} else if (vec2_dist(&mouse, &uv2) < 10) {
		selected = 2;
	} else if (vec2_dist(&mouse, &uv3) < 10) {
		selected = 3;
	} else if (vec2_dist(&mouse, &uv4) < 10) {
		selected = 4;
	} else {
		selected = 0;
	}
}

void CornerPinWidget::mouseReleaseEvent(QMouseEvent *event)
{
	mouseDrag = false;
	obs_source_set_enabled(text, false);
}

void CornerPinWidget::mouseMoveEvent(QMouseEvent *event)
{
	corner_pin_data *filter = (corner_pin_data *)filter_data;

	vec2 itemScale, pos;
	obs_sceneitem_get_scale(sceneitem, &itemScale);
	obs_sceneitem_get_pos(sceneitem, &pos);

	int addX = !zoom ? 0 : pos.x / itemScale.x;
	int addY = !zoom ? 0 : pos.y / itemScale.y;

	vec2_set(&movedMouse, event->x() - offX, event->y() - offY);
	vec2_mulf(&movedMouse, &movedMouse, 1/previewScale);

	if (!zoom) {
		vec2_sub(&movedMouse, &movedMouse, &pos);
		vec2_div(&movedMouse, &movedMouse, &itemScale);
	}

	obs_data_t *settings = obs_source_get_settings(filter->context);

	if (!mouseDrag && selected > 0 && vec2_dist(&movedMouse, &mouse) > 3) {
		mouseDrag = true;
		obs_source_set_enabled(text, true);
	}
	if (mouseDrag) {
		if (zoom)
			vec2_div(&movedMouse, &movedMouse, &itemScale);

		corner_pin_data *filter = (corner_pin_data *)filter_data;
		if (selected == 1) {
			filter->topLeftX = movedMouse.x;
			filter->topLeftY = movedMouse.y;
			obs_data_set_int(settings, "topLeftX", movedMouse.x);
			obs_data_set_int(settings, "topLeftY", movedMouse.y);
		} else if (selected == 2) {
			filter->topRightX = movedMouse.x;
			filter->topRightY = movedMouse.y;
			obs_data_set_int(settings, "topRightX", movedMouse.x);
			obs_data_set_int(settings, "topRightY", movedMouse.y);
		} else if (selected == 3) {
			filter->bottomLeftX = movedMouse.x;
			filter->bottomLeftY = movedMouse.y;
			obs_data_set_int(settings, "bottomLeftX", movedMouse.x);
			obs_data_set_int(settings, "bottomLeftY", movedMouse.y);
		} else if (selected == 4) {
			filter->bottomRightX = movedMouse.x;
			filter->bottomRightY = movedMouse.y;
			obs_data_set_int(settings, "bottomRightX", movedMouse.x);
			obs_data_set_int(settings, "bottomRightY", movedMouse.y);
		}

		obs_data_t *textSettings = obs_source_get_settings(text);
		obs_data_set_string(textSettings, "text", ("("
			+ to_string((int)movedMouse.x) + ", "
			+ to_string((int)movedMouse.y) + ")").c_str());

		obs_source_update(text, textSettings);
		obs_data_release(textSettings);

	}

	obs_data_release(settings);
}

void CornerPinWidget::wheelEvent(QWheelEvent *event)
{
	if (mouseDrag) {
		obs_data_t *textSettings = obs_source_get_settings(text);
		obs_data_t *font = obs_data_get_obj(textSettings, "font");
		if (event->angleDelta().y() >= 0) {
			textSize = obs_data_get_int(font, "size") + 2;
		} else {
			textSize = max((int)obs_data_get_int(font, "size") - 2, 2);
		}
		obs_data_set_int(font, "size", textSize);
		obs_data_set_int(textSettings, "outline_size", textSize/10);
		obs_data_set_obj(textSettings, "font", font);
		obs_source_update(text, textSettings);

		obs_data_release(textSettings);
		obs_data_release(font);
	}
}

QPaintEngine *CornerPinWidget::paintEngine() const
{
	return nullptr;
}
