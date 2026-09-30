/*                         M O V E _ A R B _ F A C E . C
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
/** @file libged/move_arb_face.c
 *
 * The move_arb_face command.
 *
 */

#include "common.h"

#include <string.h>

#include "bu/cmd.h"
#include "rt/geom.h"
#include "raytrace.h"

#include "../ged_private.h"


/* The arbX_faces arrays are used for relative face movement. */
static const int arb8_faces_first_vertex[6] = {
    0, 4, 0, 1, 0, 2
};


static const int arb7_faces_first_vertex[6] = {
    0, 0, 1, 1, 1
};


static const int arb6_faces_first_vertex[5] = {
    0, 1, 0, 0, 2
};


static const int arb5_faces_first_vertex[5] = {
    0, 0, 1, 2, 0
};


static const int arb4_faces_first_vertex[4] = {
    0, 0, 1, 0
};

static const int ARB4_MAX_FACE_INDEX = (int)(sizeof(arb4_faces_first_vertex) - 1);
static const int ARB5_MAX_FACE_INDEX = (int)(sizeof(arb5_faces_first_vertex) - 1);
static const int ARB6_MAX_FACE_INDEX = (int)(sizeof(arb6_faces_first_vertex) - 1);
static const int ARB7_MAX_FACE_INDEX = (int)(sizeof(arb7_faces_first_vertex) - 1);
static const int ARB8_MAX_FACE_INDEX = (int)(sizeof(arb8_faces_first_vertex) - 1);

int
ged_move_arb_face_core(struct ged *gedp, int argc, const char *argv[])
{
    struct rt_db_internal intern;
    struct rt_arb_internal *arb;
    fastf_t planes[7][4];		/* ARBs defining plane equations */
    fastf_t save_tol_dist;
    int arb_type;
    int face;
    int rflag = 0;
    point_t pt;
    double scan[3];
    mat_t mat;
    char *last;
    struct directory *dp;
    static const char *usage = "[-r] arb face pt";

    GED_CHECK_DATABASE_OPEN(gedp, BRLCAD_ERROR);
    GED_CHECK_READ_ONLY(gedp, BRLCAD_ERROR);
    GED_CHECK_ARGC_GT_0(gedp, argc, BRLCAD_ERROR);

    /* initialize result */
    bu_vls_trunc(gedp->ged_result_str, 0);

    if (!argv)
	return BRLCAD_ERROR;

    /* must be wanting help */
    if (argc == 1) {
	bu_vls_printf(gedp->ged_result_str, "Usage: %s %s\n", argv[0], usage);
	return GED_HELP;
    }

    if (argc < 4 || 5 < argc) {
	bu_vls_printf(gedp->ged_result_str, "Usage: %s %s\n", argv[0], usage);
	return BRLCAD_ERROR;
    }

    if (argc == 5) {
	if (!argv[1] || argv[1][0] != '-' || argv[1][1] != 'r' || argv[1][2] != '\0') {
	    bu_vls_printf(gedp->ged_result_str, "Usage: %s %s\n", argv[0], usage);
	    return BRLCAD_ERROR;
	}

	rflag = 1;
	--argc;
	++argv;
    }

    if (!argv[1] || !argv[2] || !argv[3])
	return BRLCAD_ERROR;

    if ((last = strrchr(argv[1], '/')) == NULL)
	last = (char *)argv[1];
    else
	++last;

    if (last[0] == '\0') {
	bu_vls_printf(gedp->ged_result_str, "illegal input - %s\n", argv[1]);
	return BRLCAD_ERROR;
    }

    if ((dp = db_lookup(gedp->dbip, last, LOOKUP_QUIET)) == RT_DIR_NULL) {
	bu_vls_printf(gedp->ged_result_str, "%s not found\n", argv[1]);
	return BRLCAD_ERROR;
    }

    struct rt_wdb *wdbp = wdb_dbopen(gedp->dbip, RT_WDB_TYPE_DB_DEFAULT);
    if (!wdbp) {
	bu_vls_printf(gedp->ged_result_str, "Failed to open database handle\n");
	return BRLCAD_ERROR;
    }

    RT_DB_INTERNAL_INIT(&intern);
    if (wdb_import_from_path2(gedp->ged_result_str, &intern, argv[1], wdbp, mat) & BRLCAD_ERROR) {
	wdb_close(wdbp);
	return BRLCAD_ERROR;
    }

    if (intern.idb_major_type != DB5_MAJORTYPE_BRLCAD ||
	intern.idb_minor_type != DB5_MINORTYPE_BRLCAD_ARB8) {
	bu_vls_printf(gedp->ged_result_str, "Object not an ARB\n");
	rt_db_free_internal(&intern);
	wdb_close(wdbp);
	return BRLCAD_ERROR;
    }

    if (bu_sscanf(argv[2], "%d", &face) != 1) {
	bu_vls_printf(gedp->ged_result_str, "bad face - %s\n", argv[2]);
	rt_db_free_internal(&intern);
	wdb_close(wdbp);
	return BRLCAD_ERROR;
    }

    face -= 1;

    if (bu_sscanf(argv[3], "%lf %lf %lf", &scan[X], &scan[Y], &scan[Z]) != 3) {
	bu_vls_printf(gedp->ged_result_str, "bad point - %s\n", argv[3]);
	rt_db_free_internal(&intern);
	wdb_close(wdbp);
	return BRLCAD_ERROR;
    }
    /* convert from double to fastf_t */
    VMOVE(pt, scan);

    arb = (struct rt_arb_internal *)intern.idb_ptr;
    RT_ARB_CK_MAGIC(arb);

    arb_type = rt_arb_std_type(&intern, &wdbp->wdb_tol);

    if (rt_arb_calc_planes(gedp->ged_result_str, arb, arb_type, planes, &wdbp->wdb_tol)) {
	rt_db_free_internal(&intern);
	wdb_close(wdbp);
	return BRLCAD_ERROR;
    }

    VSCALE(pt, pt, gedp->dbip->dbi_local2base);

    int max_face_index = -1;
    int arb_pt_index = 0;
    switch (arb_type) {
	case ARB4:
	    max_face_index = ARB4_MAX_FACE_INDEX;
	    if (face >= 0 && face <= max_face_index)
		arb_pt_index = arb4_faces_first_vertex[face];
	    break;
	case ARB5:
	    max_face_index = ARB5_MAX_FACE_INDEX;
	    if (face >= 0 && face <= max_face_index)
		arb_pt_index = arb5_faces_first_vertex[face];
	    break;
	case ARB6:
	    max_face_index = ARB6_MAX_FACE_INDEX;
	    if (face >= 0 && face <= max_face_index)
		arb_pt_index = arb6_faces_first_vertex[face];
	    break;
	case ARB7:
	    max_face_index = ARB7_MAX_FACE_INDEX;
	    if (face >= 0 && face <= max_face_index)
		arb_pt_index = arb7_faces_first_vertex[face];
	    break;
	case ARB8:
	    max_face_index = ARB8_MAX_FACE_INDEX;
	    if (face >= 0 && face <= max_face_index)
		arb_pt_index = arb8_faces_first_vertex[face];
	    break;
	default:
	    bu_vls_printf(gedp->ged_result_str, "unrecognized arb type\n");
	    rt_db_free_internal(&intern);
	    wdb_close(wdbp);
	    return BRLCAD_ERROR;
    }

    if (face < 0 || face > max_face_index) {
	bu_vls_printf(gedp->ged_result_str, "bad face - %s\n", argv[2]);
	rt_db_free_internal(&intern);
	wdb_close(wdbp);
	return BRLCAD_ERROR;
    }

    if (rflag) {
	VADD2(pt, pt, arb->pt[arb_pt_index]);
    }

    /* change D of planar equation */
    planes[face][3] = VDOT(&planes[face][0], pt);

    /* calculate new points for the arb */
    save_tol_dist = wdbp->wdb_tol.dist;
    wdbp->wdb_tol.dist = wdbp->wdb_tol.dist * 2;
    if (rt_arb_calc_points(arb, arb_type, (const plane_t *)planes, &wdbp->wdb_tol) < 0) {
	wdbp->wdb_tol.dist = save_tol_dist;
	rt_db_free_internal(&intern);
	wdb_close(wdbp);
	return BRLCAD_ERROR;
    }
    wdbp->wdb_tol.dist = save_tol_dist;

    {
	int i;
	mat_t invmat;

	bn_mat_inv(invmat, mat);

	for (i = 0; i < 8; ++i) {
	    point_t arb_pt;

	    MAT4X3PNT(arb_pt, invmat, arb->pt[i]);
	    VMOVE(arb->pt[i], arb_pt);
	}

	int ret = BRLCAD_OK;
	if (rt_db_put_internal(dp, gedp->dbip, &intern) < 0) {
	    bu_vls_printf(gedp->ged_result_str, "Database write failure.\n");
	    ret = BRLCAD_ERROR;
	}
	wdb_close(wdbp);
	rt_db_free_internal(&intern);

	return ret;
    }

    return BRLCAD_OK;
}


#include "../include/plugin.h"

#define GED_MOVE_ARB_FACE_COMMANDS(X, XID) \
    X(move_arb_face, ged_move_arb_face_core, GED_CMD_DEFAULT) \

GED_DECLARE_COMMAND_SET(GED_MOVE_ARB_FACE_COMMANDS)
GED_DECLARE_PLUGIN_MANIFEST("libged_move_arb_face", 1, GED_MOVE_ARB_FACE_COMMANDS)

/*
 * Local Variables:
 * mode: C
 * tab-width: 8
 * indent-tabs-mode: t
 * c-file-style: "stroustrup"
 * End:
 * ex: shiftwidth=4 tabstop=8
 */
