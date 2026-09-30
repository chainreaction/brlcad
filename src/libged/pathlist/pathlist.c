/*                         P A T H L I S T . C
 * BRL-CAD
 *
 * Copyright (c) 2008-2026 United States Government as represented by
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
/** @file libged/pathlist.c
 *
 * The pathlist command.
 *
 */

#include "common.h"

#include <string.h>

#include "bu/cmd.h"

#include "../ged_private.h"


struct pathlist_data {
    struct ged *gedp;
    int no_leaf;
};


static union tree *
pathlist_leaf_func(struct db_tree_state *UNUSED(tsp), const struct db_full_path *pathp, struct rt_db_internal *ip, void *client_data)
{
    struct pathlist_data *pldata = (struct pathlist_data *)client_data;
    char *str;

    RT_CK_FULL_PATH(pathp);
    RT_CK_DB_INTERNAL(ip);

    if (pldata->no_leaf) {
	struct db_full_path pp;
	db_full_path_init(&pp);
	db_dup_full_path(&pp, pathp);
	if (pp.fp_len > 1)
	    --pp.fp_len;
	str = db_path_to_string(&pp);
	bu_vls_printf(pldata->gedp->ged_result_str, " %s", str);
	db_free_full_path(&pp);
    } else {
	str = db_path_to_string(pathp);
	bu_vls_printf(pldata->gedp->ged_result_str, " %s", str);
    }

    bu_free((void *)str, "path string");
    return TREE_NULL;
}


int
ged_pathlist_core(struct ged *gedp, int argc, const char *argv[])
{
    static const char *usage = "[-noleaf] name";
    struct pathlist_data pldata;
    struct db_tree_state init_ts;
    struct rt_wdb *wdbp;

    GED_CHECK_DATABASE_OPEN(gedp, BRLCAD_ERROR);
    GED_CHECK_ARGC_GT_0(gedp, argc, BRLCAD_ERROR);

    /* initialize result */
    bu_vls_trunc(gedp->ged_result_str, 0);

    /* must be wanting help */
    if (argc == 1) {
	bu_vls_printf(gedp->ged_result_str, "Usage: %s %s\n", argv[0], usage);
	return GED_HELP;
    }

    if (3 < argc) {
	bu_vls_printf(gedp->ged_result_str, "Usage: %s %s\n", argv[0], usage);
	return BRLCAD_ERROR;
    }

    pldata.gedp = gedp;
    pldata.no_leaf = 0;

    if (argc == 3) {
	if (BU_STR_EQUAL(argv[1], "-noleaf"))
	    pldata.no_leaf = 1;
	else {
	    bu_vls_printf(gedp->ged_result_str, "Usage: %s %s\n", argv[0], usage);
	    return BRLCAD_ERROR;
	}

	++argv;
	--argc;
    }

    wdbp = wdb_dbopen(gedp->dbip, RT_WDB_TYPE_DB_DEFAULT);
    init_ts = wdbp->wdb_initial_tree_state;
    wdb_close(wdbp);

    if (db_walk_tree(gedp->dbip, argc-1, (const char **)argv+1, 1,
		     &init_ts,
		     0, 0, pathlist_leaf_func, (void *)&pldata) < 0) {
	bu_vls_printf(gedp->ged_result_str, "ged_pathlist_core: db_walk_tree() error\n");
	return BRLCAD_ERROR;
    }

    return BRLCAD_OK;
}


#include "../include/plugin.h"

#define GED_PATHLIST_COMMANDS(X, XID) \
    X(pathlist, ged_pathlist_core, GED_CMD_DEFAULT) \

GED_DECLARE_COMMAND_SET(GED_PATHLIST_COMMANDS)
GED_DECLARE_PLUGIN_MANIFEST("libged_pathlist", 1, GED_PATHLIST_COMMANDS)

/*
 * Local Variables:
 * mode: C
 * tab-width: 8
 * indent-tabs-mode: t
 * c-file-style: "stroustrup"
 * End:
 * ex: shiftwidth=4 tabstop=8
 */
