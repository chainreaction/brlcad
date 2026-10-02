/*                         V O X E L I Z E . C
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
/** @file libged/voxelize.c
 *
 * The voxelize command.
 *
 */

#include "common.h"

#include <string.h>

#include "bu/cmd.h"
#include "bu/getopt.h"
#include "rt/geom.h"
#include "raytrace.h"

#include "../ged_private.h"
#include "analyze.h"
#include "wdb.h"


struct voxelizeData
{
    fastf_t threshold;
    fastf_t *bbMin;
    fastf_t sizeVoxel[3];
    struct rt_wdb *wdbp;
    char *newname;
    struct wmember content;
};

static void
create_boxes(void *callBackData, int x, int y, int z, const char *a, fastf_t fill)
{
    if (a != NULL) {
	fastf_t min[3], max[3];
	struct bu_vls vp = BU_VLS_INIT_ZERO;
	struct voxelizeData *dataValues = (struct voxelizeData *)callBackData;

	if (dataValues->threshold <= fill) {
	    bu_vls_sprintf(&vp, "%s.x%dy%dz%d.s", dataValues->newname, x, y, z);

	    min[0] = (dataValues->bbMin)[0] + (x * (dataValues->sizeVoxel)[0]);
	    min[1] = (dataValues->bbMin)[1] + (y * (dataValues->sizeVoxel)[1]);
	    min[2] = (dataValues->bbMin)[2] + (z * (dataValues->sizeVoxel)[2]);
	    max[0] = (dataValues->bbMin)[0] + ((x + 1.0) * (dataValues->sizeVoxel)[0]);
	    max[1] = (dataValues->bbMin)[1] + ((y + 1.0) * (dataValues->sizeVoxel)[1]);
	    max[2] = (dataValues->bbMin)[2] + ((z + 1.0) * (dataValues->sizeVoxel)[2]);

	    /* guard against duplicate rpp's
	     *	voxelize() calls this once per region - NOT once per voxel. So overlapping
	     *	regions can/will create duplicate solids in the tree
	     */
	    if (db_lookup(dataValues->wdbp->dbip, bu_vls_cstr(&vp), LOOKUP_QUIET) == RT_DIR_NULL) {
		mk_rpp(dataValues->wdbp, bu_vls_cstr(&vp), min, max);
		mk_addmember(bu_vls_cstr(&vp), &dataValues->content.l, 0, WMOP_UNION);
	    }

	    bu_vls_free(&vp);
	}
    }
    /* else this voxel is air */
}

int
ged_voxelize_core(struct ged *gedp, int argc, const char *argv[])
{
    struct rt_i *rtip;
    static const char *usage = "[-s \"dx dy dz\"] [-d n] [-t f] new_obj old_obj [old_obj2 old_obj3 ...]";
    fastf_t sizeVoxel[3];
    int levelOfDetail;
    void *callBackData;
    struct voxelizeData voxDat;
    int c;

    /* intentionally double for scan */
    double threshold;

    GED_CHECK_DATABASE_OPEN(gedp, BRLCAD_ERROR);
    GED_CHECK_ARGC_GT_0(gedp, argc, BRLCAD_ERROR);

    /* initialization */
    bu_vls_trunc(gedp->ged_result_str, 0);

    /* incorrect arguments */
    if (argc < 3) {
	bu_vls_printf(gedp->ged_result_str, "Usage: %s %s\n", argv[0], usage);
	return GED_HELP;
    }

    sizeVoxel[0]  = 1.0;
    sizeVoxel[1]  = 1.0;
    sizeVoxel[2]  = 1.0;
    levelOfDetail = 1;
    threshold = 0.5;

    bu_optind = 1;
    while ((c = bu_getopt(argc, (char * const *)argv, (const char *)"s:d:t:")) != -1) {
	double scan[3];

	switch (c) {
	    case 's':
		if (bu_sscanf(bu_optarg, "%lf %lf %lf",
			      &scan[0],
			      &scan[1],
			      &scan[2]) != 3) {
		    bu_vls_printf(gedp->ged_result_str, "Usage: %s %s\n", argv[0], usage);
		    return BRLCAD_ERROR;
		} else {
		    /* convert from double to fastf_t */
		    VMOVE(sizeVoxel, scan);

		    sizeVoxel[0] = sizeVoxel[0] * gedp->dbip->dbi_local2base;
		    sizeVoxel[1] = sizeVoxel[1] * gedp->dbip->dbi_local2base;
		    sizeVoxel[2] = sizeVoxel[2] * gedp->dbip->dbi_local2base;
		}
		break;

	    case 'd':
		if (bu_sscanf(bu_optarg, "%d", &levelOfDetail) != 1) {
		    bu_vls_printf(gedp->ged_result_str, "Usage: %s %s\n", argv[0], usage);
		    return BRLCAD_ERROR;
		}
		break;

	    case 't':
		if (bu_sscanf(bu_optarg, "%lf", &threshold) != 1) {
		    bu_vls_printf(gedp->ged_result_str, "Usage: %s %s\n", argv[0], usage);
		    return BRLCAD_ERROR;
		}
		break;

	    default:
		bu_vls_printf(gedp->ged_result_str, "Usage: %s %s\n", argv[0], usage);
		return BRLCAD_ERROR;
	}
    }

    if (sizeVoxel[0] <= 0.0 || sizeVoxel[1] <= 0.0 || sizeVoxel[2] <= 0.0 || levelOfDetail <= 0) {
	bu_vls_printf(gedp->ged_result_str, "error: invalid voxel size or level of detail\n");
	return BRLCAD_ERROR;
    }

    argc -= bu_optind;
    argv += bu_optind;

    if (argc < 2) {
	bu_vls_printf(gedp->ged_result_str, "error: missing argument(s)\n");
	return BRLCAD_ERROR;
    }

    voxDat.newname = (char *)argv[0];
    argc--;
    argv++;

    if (db_lookup(gedp->dbip, voxDat.newname, LOOKUP_QUIET) != RT_DIR_NULL) {
	bu_vls_printf(gedp->ged_result_str, "error: solid '%s' already exists, aborting\n", voxDat.newname);
	return BRLCAD_ERROR;
    }

    rtip = rt_i_create(gedp->dbip);
    rtip->useair = 1;

    /* Walk trees.  Here we identify any object trees in the database
     * that the user wants included in the ray trace.
     */
    while (argc > 0) {
	if (rt_gettree(rtip, argv[0]) < 0) {
	    bu_vls_printf(gedp->ged_result_str, "error: object '%s' does not exist, aborting\n", argv[0]);
	    rt_i_destroy(rtip);
	    return BRLCAD_ERROR;
	}

	argc--;
	argv++;
    }

    struct rt_wdb *wdbp = wdb_dbopen(gedp->dbip, RT_WDB_TYPE_DB_DEFAULT);
    if (!wdbp) {
	bu_vls_printf(gedp->ged_result_str, "error: failed to open wdb\n");
	rt_i_destroy(rtip);
	return BRLCAD_ERROR;
    }

    voxDat.sizeVoxel[0] = sizeVoxel[0];
    voxDat.sizeVoxel[1] = sizeVoxel[1];
    voxDat.sizeVoxel[2] = sizeVoxel[2];
    voxDat.threshold = threshold;
    voxDat.wdbp = wdbp;
    voxDat.bbMin = rtip->mdl_min;
    BU_LIST_INIT(&voxDat.content.l);

    callBackData = (void*)(&voxDat);

    /* voxelize function is called here with rtip(ray trace instance), userParameter and create_boxes function */
    voxelize(rtip, sizeVoxel, levelOfDetail, create_boxes, callBackData);

    mk_comb(wdbp, voxDat.newname, &voxDat.content.l, 1, "plastic", "sh=4 sp=0.5 di=0.5 re=0.1", 0, 1000, 0, 0, 100, 0, 0, 0);

    mk_freemembers(&voxDat.content.l);
    wdb_close(wdbp);
    rt_i_destroy(rtip);

    return BRLCAD_OK;
}


#include "../include/plugin.h"

#define GED_VOXELIZE_COMMANDS(X, XID) \
    X(voxelize, ged_voxelize_core, GED_CMD_DEFAULT) \

GED_DECLARE_COMMAND_SET(GED_VOXELIZE_COMMANDS)
GED_DECLARE_PLUGIN_MANIFEST("libged_voxelize", 1, GED_VOXELIZE_COMMANDS)

/*
 * Local Variables:
 * mode: C
 * tab-width: 8
 * indent-tabs-mode: t
 * c-file-style: "stroustrup"
 * End:
 * ex: shiftwidth=4 tabstop=8
 */
