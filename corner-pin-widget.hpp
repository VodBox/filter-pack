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

#include <QWidget>
#include <QWindow>
#include <QComboBox>
#include <obs.hpp>

class CornerPinWindow;
class CornerPinWidget;

class CornerPinWindow : public QWidget {
	CornerPinWidget *cornerWidget;

public:
	obs_source_t *source;
	obs_scene_t *scene;
	QComboBox *comboBox;
	CornerPinWindow(QWidget *parent, obs_source_t *source_, void *data);
	~CornerPinWindow();
	void showEvent(QShowEvent *event) override;
	void resizeEvent(QResizeEvent *event) override;
};

class CornerPinWidget : public QWidget {
	obs_display_t *display = nullptr;
	obs_source_t *text = nullptr;
	void *filter_data = nullptr;
	gs_vertbuffer_t *verts[8];
	int selected = 0;
	vec2 mouse;
	vec2 movedMouse;
	bool mouseDrag = false;

	int textSize = 50;

	int offX;
	int offY;
	int previewW;
	int previewH;
	float previewScale;

	void CreateDisplay();
	void paintEvent(QPaintEvent *event) override;
	void resizeEvent(QResizeEvent *event) override;
	void showEvent(QShowEvent *event) override;
	void hideEvent(QHideEvent *event) override;

	void mousePressEvent(QMouseEvent *event) override;
	void mouseReleaseEvent(QMouseEvent *event) override;
	void mouseMoveEvent(QMouseEvent *event) override;
	void wheelEvent(QWheelEvent *event) override;
public:
	obs_source_t *source;
	obs_sceneitem_t *sceneitem = nullptr;
	bool zoom = false;

	CornerPinWidget(QWidget *parent, obs_source_t *source_, void *data);
	~CornerPinWidget();
	void handleResizeRequest(int width, int height);
	static void drawPreview(void *data, uint32_t cx, uint32_t cy);

	virtual QPaintEngine *paintEngine() const override;

	inline obs_display_t *GetDisplay() const { return display; }
};
