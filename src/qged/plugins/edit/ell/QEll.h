/*                        Q E L L. H
 * BRL-CAD
 *
 * Copyright (c) 2022-2026 United States Government as represented by
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
/** @file QEll.h
 *
 */

#ifndef QELL_H
#define QELL_H

#include <QCheckBox>
#include <QComboBox>
#include <QGroupBox>
#include <QLineEdit>
#include <QPushButton>
#include <QRadioButton>
#include "raytrace.h"
#include "qtcad/QgColorRGB.h"

class QEll : public QWidget
{
    Q_OBJECT

    public:
	QEll();
	~QEll();

	// Ell origin
	QCheckBox *O_pnt = nullptr;

	// Select one or more axes to set the length on
	QCheckBox *A_axis = nullptr;
	QCheckBox *B_axis = nullptr;
	QCheckBox *C_axis = nullptr;

	// Primitive name
	QLineEdit *ell_name = nullptr;
	QPushButton *write_edit = nullptr;
	QPushButton *make_sph = nullptr; // ell -> sph
	QPushButton *reset_values = nullptr;

    signals:
	void view_updated(unsigned long long);

    private slots:
	void read_from_db();
	void write_to_db();
	void update_obj_wireframe();
	void update_viewobj_name(const QString &);

    protected:
	bool eventFilter(QObject *, QEvent *);

    private:
	struct directory *dp = NULL;
	struct rt_ell_internal ell;
	struct bv_scene_obj *p = NULL;
	struct bu_vls oname = BU_VLS_INIT_ZERO;
};

#endif //QELL_H

// Local Variables:
// tab-width: 8
// mode: C++
// c-basic-offset: 4
// indent-tabs-mode: t
// c-file-style: "stroustrup"
// End:
// ex: shiftwidth=4 tabstop=8
