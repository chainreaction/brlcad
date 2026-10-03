/*                   Q P O L Y C R E A T E . H
 * BRL-CAD
 *
 * Copyright (c) 2014-2026 United States Government as represented by
 * the U.S. Army Research Laboratory.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public License
 * version 2.1 as published by the Free Software Foundation.
 *
 * This library is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this file; see the file named COPYING for more
 * information.
 */
/** @file QPolyCreate.h
 *
 */

#ifndef QPOLYCREATE_H
#define QPOLYCREATE_H

#include <QCheckBox>
#include <QComboBox>
#include <QGroupBox>
#include <QLineEdit>
#include <QPushButton>
#include <QRadioButton>
#include "bg/polygon_types.h"
#include "qtcad/QgColorRGB.h"
#include "qtcad/QgPolyFilter.h"
#include "qtcad/QgView.h"
#include "QPolySettings.h"

class QPolyCreate : public QWidget
{
    Q_OBJECT

    public:
	explicit QPolyCreate(QWidget *parent = nullptr);
	~QPolyCreate();

	// Boolean Operation Mode
	QComboBox *csg_modes = nullptr;

	// Adding polygons
	QRadioButton *circle_mode = nullptr;
	QRadioButton *ellipse_mode = nullptr;
	QRadioButton *square_mode = nullptr;
	QRadioButton *rectangle_mode = nullptr;
	QRadioButton *general_mode = nullptr;

	// Draw default settings
	// Default edge color
	QPolySettings *ps = nullptr;

	// Modifying polygons
	QCheckBox *close_general_poly = nullptr;

	// Existing view polygon copy
	QLineEdit *vpoly_name = nullptr;
	QPushButton *vpoly_copy = nullptr;

	// Sketch import
	QLineEdit *import_name = nullptr;
	QPushButton *import_sketch = nullptr;

    signals:
	void poly_added();
	void settings_changed(unsigned long long);
	void view_updated(unsigned long long);

    public slots:
	void checkbox_refresh(unsigned long long);

    private slots:
	void toplevel_config(bool);
	void finalize(bool);
	void do_import_sketch();
	void do_vpoly_copy();
	void propagate_update(int);

	void sketch_sync_bool(bool);
	void sketch_sync_str(const QString &);
	void sketch_sync();
	void view_sync_str(const QString &);
	void view_sync();
	void toggle_line_snapping(bool);
	void toggle_grid_snapping(bool);

    protected:
	bool eventFilter(QObject *, QEvent *);

    private:
	bg_clip_t op = bg_Union;
	int poly_cnt = 0;
	struct bv_scene_obj *p = nullptr;
	bool do_bool = false;

	QgPolyFilter *cf = nullptr;
	QPolyCreateFilter *pcf = nullptr;
};

#endif //QPOLYCREATE_H

// Local Variables:
// tab-width: 8
// mode: C++
// c-basic-offset: 4
// indent-tabs-mode: t
// c-file-style: "stroustrup"
// End:
// ex: shiftwidth=4 tabstop=8
