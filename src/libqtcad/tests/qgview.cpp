/*                     Q G V I E W . C P P
 * BRL-CAD
 *
 * Copyright (c) 2021-2026 United States Government as represented by
 * the U.S. Army Research Laboratory.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public License
 * version 2.1 as published by the Free Software Foundation.
 *
 * This library is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details->
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this file; see the file named COPYING for more
 * information.
 */
/** @file qgview.cpp
 *
 * Brief description
 *
 */

#include <iostream>
#include <unordered_map>
#include <vector>

#include "bu/app.h"
#include "bu/log.h"
#include  <QApplication>
#include "qtcad/QgModel.h"
#include "qtcad/QgTreeView.h"

void
open_children(QgItem *itm, QgModel *s, int depth, int max_depth)
{
    if (!itm || !itm->ihash)
	return;

    if (max_depth > 0 && depth >= max_depth)
	return;

    itm->open();
    for (int j = 0; j < itm->childCount(); j++) {
	QgItem *c = itm->child(j);
	open_children(c, s, depth+1, max_depth);
    }
}

void
open_tops(QgModel *s, int depth)
{
    for (size_t i = 0; i < s->tops_items.size(); i++) {
	QgItem *itm = s->tops_items[i];
	if (!itm->ihash)
	    continue;
	open_children(itm, s, 0, depth);
    }
}


int main(int argc, char *argv[])
{
    if (argc < 2 || !argv || !argv[0]) {
	bu_log("Usage: qgview file.g\n");
	return 1;
    }

    bu_setprogname(argv[0]);

    if (BU_STR_EQUAL(argv[1], "-h") || BU_STR_EQUAL(argv[1], "-?") || BU_STR_EQUAL(argv[1], "--help")) {
	bu_log("Usage: %s file.g\n", argv[0]);
	return 0;
    }

    if (argc != 2) {
	bu_log("Usage: %s file.g\n", argv[0]);
	return 1;
    }

    QApplication app(argc, argv);

    QgModel sm(NULL, argv[1]);
    QgModel *s = &sm;

    //open_tops(s, -1);

    QgTreeView tree(NULL, s);
    tree.setWindowTitle(argv[1]);
    tree.show();

    return app.exec();
}

/*
 * Local Variables:
 * mode: C++
 * tab-width: 8
 * c-basic-offset: 4
 * indent-tabs-mode: t
 * c-file-style: "stroustrup"
 * End:
 * ex: shiftwidth=4 tabstop=8
 */
