/*               C A D V I E W S E L E C T O R . H
 * BRL-CAD
 *
 * Copyright (c) 2023-2026 United States Government as represented by
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
/** @file CADViewSelector.h
 *
 * Brief description
 *
 */

#include <QWidget>
#include <QPushButton>
#include <QButtonGroup>
#include <QGroupBox>
#include <QCheckBox>
#include <QComboBox>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QRadioButton>
#include "ged.h"
#include "qtcad/QgSelectFilter.h"

class CADViewSelector : public QWidget
{
    Q_OBJECT

    public:
	CADViewSelector(QWidget *p = 0);
	~CADViewSelector();

	QRadioButton *use_pnt_select_button = nullptr;
	QRadioButton *use_rect_select_button = nullptr;

	QCheckBox *use_ray_test_ckbx = nullptr;
	QCheckBox *select_all_depth_ckbx = nullptr;

	QRadioButton *erase_from_scene_button = nullptr;
	QRadioButton *add_to_group_button = nullptr;
	QRadioButton *rm_from_group_button = nullptr;
	//QComboBox *current_group;
	QListWidget *group_contents = nullptr;
	//QPushButton *add_new_group;
	//QPushButton *rm_group;

	QPushButton *draw_selections = nullptr;
	QPushButton *erase_selections = nullptr;


signals:
	void view_changed(unsigned long long);

    public slots:
	void enable_groups(bool);
    	void disable_groups(bool);
	void enable_raytrace_opt(bool);
    	void disable_raytrace_opt(bool);
	void enable_useall_opt(bool);
    	void disable_useall_opt(bool);
	void do_view_update(unsigned long long);
	void do_draw_selections();
	void do_erase_selections();

    protected:
	bool eventFilter(QObject *, QEvent *);

    private:
	void select_objs();
	void deselect_objs();
	void erase_objs();

	QgSelectFilter *cf = NULL;
	QgSelectPntFilter *pf = nullptr;
	QgSelectBoxFilter *bf = nullptr;
	QgSelectRayFilter *rf = nullptr;

	struct ged *gedp = NULL;
	unsigned long long ohash = 0;
	unsigned long long omhash = 0;
};

// Local Variables:
// tab-width: 8
// mode: C++
// c-basic-offset: 4
// indent-tabs-mode: t
// c-file-style: "stroustrup"
// End:
// ex: shiftwidth=4 tabstop=8
