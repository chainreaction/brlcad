/*                  Q P O L Y S E T T I N G S . H
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
/** @file QPolySettings.h
 *
 */

#ifndef QPOLYSETTINGS_H
#define QPOLYSETTINGS_H

#include <QLineEdit>
#include <QCheckBox>
#include <QComboBox>
#include "bv.h"
#include "qtcad/QgColorRGB.h"

class QPolySettings : public QWidget
{
    Q_OBJECT

    public:
	explicit QPolySettings(QWidget *parent = nullptr);
	~QPolySettings();

	QgColorRGB *edge_color = nullptr;
	QCheckBox *fill_poly = nullptr;
	QgColorRGB *fill_color = nullptr;
	QLineEdit *fill_slope_x = nullptr;
	QLineEdit *fill_slope_y = nullptr;
	QLineEdit *fill_density = nullptr;

	QLineEdit *view_name = nullptr;
	QLineEdit *sketch_name = nullptr;
	QCheckBox *sketch_sync = nullptr;

	QLineEdit *vZ = nullptr;

	QCheckBox *line_snapping = nullptr;
	QCheckBox *grid_snapping = nullptr;

	bool uniq_obj_name(struct bu_vls *oname, struct bview *v);

    signals:
	void settings_changed();
	void line_snapping_changed(bool);
	void grid_snapping_changed(bool);

    public slots:
	void do_settings_changed();
	void do_line_snapping_changed();
	void do_grid_snapping_changed();
	void settings_sync(struct bv_scene_obj *p);

    private slots:
	void sketch_sync_toggled(bool);
};

#endif //QPOLYSETTINGS_H

// Local Variables:
// tab-width: 8
// mode: C++
// c-basic-offset: 4
// indent-tabs-mode: t
// c-file-style: "stroustrup"
// End:
// ex: shiftwidth=4 tabstop=8
