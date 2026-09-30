/*                         N M G _ K I L L _ V. C
 * BRL-CAD
 *
 * Copyright (c) 2015-2026 United States Government as represented by
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
/** @file libged/nmg_kill_v.c
 *
 * The kill V subcommand for nmg top-level command.
 *
 */

#include "common.h"

#include <string.h>

#include "bu/cmd.h"
#include "rt/geom.h"

#include "../ged_private.h"

void remove_vertex(const struct model *m, point_t rv)
{
    struct nmgregion *r;
    struct shell *s;
    struct faceuse *fu;
    struct loopuse *lu;
    struct edgeuse *eu;
    struct vertexuse *vu;
    int changed;

    NMG_CK_MODEL(m);

    /* Traverse NMG model and remove instances of vertexuses matching rv.
     * After deleting an element, topology may change, so we restart
     * the search to avoid Use-After-Free or iterator invalidation.
     */
    do {
	changed = 0;

	/* 1. Check lone vertices in shells */
	for (BU_LIST_FOR(r, nmgregion, &m->r_hd)) {
	    NMG_CK_REGION(r);
	    for (BU_LIST_FOR(s, shell, &r->s_hd)) {
		NMG_CK_SHELL(s);
		vu = s->vu_p;
		if (vu) {
		    NMG_CK_VERTEXUSE(vu);
		    if (vu->v_p && vu->v_p->vg_p) {
			NMG_CK_VERTEX_G(vu->v_p->vg_p);
			if (VNEAR_EQUAL(vu->v_p->vg_p->coord, rv, BN_TOL_DIST)) {
			    nmg_kvu(vu);
			    changed = 1;
			    break;
			}
		    }
		}
	    }
	    if (changed) break;
	}
	if (changed) continue;

	/* 2. Check wire loops and wire edges in shells */
	for (BU_LIST_FOR(r, nmgregion, &m->r_hd)) {
	    NMG_CK_REGION(r);
	    for (BU_LIST_FOR(s, shell, &r->s_hd)) {
		NMG_CK_SHELL(s);
		for (BU_LIST_FOR(lu, loopuse, &s->lu_hd)) {
		    NMG_CK_LOOPUSE(lu);
		    if (BU_LIST_FIRST_MAGIC(&lu->down_hd) == NMG_VERTEXUSE_MAGIC) {
			vu = BU_LIST_FIRST(vertexuse, &lu->down_hd);
			NMG_CK_VERTEXUSE(vu);
			if (vu->v_p && vu->v_p->vg_p) {
			    NMG_CK_VERTEX_G(vu->v_p->vg_p);
			    if (VNEAR_EQUAL(vu->v_p->vg_p->coord, rv, BN_TOL_DIST)) {
				nmg_klu(lu);
				changed = 1;
				break;
			    }
			}
		    } else {
			for (BU_LIST_FOR(eu, edgeuse, &lu->down_hd)) {
			    NMG_CK_EDGEUSE(eu);
			    vu = eu->vu_p;
			    if (vu && vu->v_p && vu->v_p->vg_p) {
				NMG_CK_VERTEX_G(vu->v_p->vg_p);
				if (VNEAR_EQUAL(vu->v_p->vg_p->coord, rv, BN_TOL_DIST)) {
				    nmg_keu(eu);
				    changed = 1;
				    break;
				}
			    }
			}
		    }
		    if (changed) break;
		}
		if (changed) break;

		for (BU_LIST_FOR(eu, edgeuse, &s->eu_hd)) {
		    NMG_CK_EDGEUSE(eu);
		    vu = eu->vu_p;
		    if (vu && vu->v_p && vu->v_p->vg_p) {
			NMG_CK_VERTEX_G(vu->v_p->vg_p);
			if (VNEAR_EQUAL(vu->v_p->vg_p->coord, rv, BN_TOL_DIST)) {
			    nmg_keu(eu);
			    changed = 1;
			    break;
			}
		    }
		}
		if (changed) break;
	    }
	    if (changed) break;
	}
	if (changed) continue;

	/* 3. Check face loops */
	for (BU_LIST_FOR(r, nmgregion, &m->r_hd)) {
	    NMG_CK_REGION(r);
	    for (BU_LIST_FOR(s, shell, &r->s_hd)) {
		NMG_CK_SHELL(s);
		for (BU_LIST_FOR(fu, faceuse, &s->fu_hd)) {
		    NMG_CK_FACEUSE(fu);
		    for (BU_LIST_FOR(lu, loopuse, &fu->lu_hd)) {
			NMG_CK_LOOPUSE(lu);
			if (BU_LIST_FIRST_MAGIC(&lu->down_hd) == NMG_VERTEXUSE_MAGIC) {
			    vu = BU_LIST_FIRST(vertexuse, &lu->down_hd);
			    NMG_CK_VERTEXUSE(vu);
			    if (vu->v_p && vu->v_p->vg_p) {
				NMG_CK_VERTEX_G(vu->v_p->vg_p);
				if (VNEAR_EQUAL(vu->v_p->vg_p->coord, rv, BN_TOL_DIST)) {
				    nmg_klu(lu);
				    changed = 1;
				    break;
				}
			    }
			} else {
			    for (BU_LIST_FOR(eu, edgeuse, &lu->down_hd)) {
				NMG_CK_EDGEUSE(eu);
				vu = eu->vu_p;
				if (vu && vu->v_p && vu->v_p->vg_p) {
				    NMG_CK_VERTEX_G(vu->v_p->vg_p);
				    if (VNEAR_EQUAL(vu->v_p->vg_p->coord, rv, BN_TOL_DIST)) {
					nmg_keu(eu);
					changed = 1;
					break;
				    }
				}
			    }
			}
			if (changed) break;
		    }
		    if (changed) break;
		}
		if (changed) break;
	    }
	    if (changed) break;
	}
    } while (changed);
}

int
ged_nmg_kill_v_core(struct ged *gedp, int argc, const char *argv[])
{
    struct rt_db_internal internal;
    struct directory *dp;
    struct model *m;
    const char *name;
    point_t vt;
    double p[3];

    static const char *usage = "kill V x y z";

    GED_CHECK_DATABASE_OPEN(gedp, BRLCAD_ERROR);
    GED_CHECK_DRAWABLE(gedp, BRLCAD_ERROR);
    GED_CHECK_READ_ONLY(gedp, BRLCAD_ERROR);
    GED_CHECK_ARGC_GT_0(gedp, argc, BRLCAD_ERROR);

    /* initialize result */
    bu_vls_trunc(gedp->ged_result_str, 0);

    if (!argv)
	return BRLCAD_ERROR;

    /* must be wanting help */
    if (argc < 6 || !argv[0] || !argv[3] || !argv[4] || !argv[5]) {
	bu_vls_printf(gedp->ged_result_str, "Usage: %s %s\n", argv[0] ? argv[0] : "nmg", usage);
	return (argc == 1) ? GED_HELP : BRLCAD_ERROR;
    }

    /* attempt to resolve and verify */
    name = argv[0];

    dp = db_lookup(gedp->dbip, name, LOOKUP_QUIET);
    if (dp == RT_DIR_NULL) {
	bu_vls_printf(gedp->ged_result_str, "%s does not exist\n", name);
	return BRLCAD_ERROR;
    }

    RT_DB_INTERNAL_INIT(&internal);
    if (rt_db_get_internal(&internal, dp, gedp->dbip, bn_mat_identity) < 0) {
	bu_vls_printf(gedp->ged_result_str, "rt_db_get_internal() error\n");
	return BRLCAD_ERROR;
    }

    if (internal.idb_type != ID_NMG) {
	bu_vls_printf(gedp->ged_result_str, "%s is not an NMG solid\n", name);
	rt_db_free_internal(&internal);
	return BRLCAD_ERROR;
    }

    if (bu_sscanf(argv[3], "%lf", &p[0]) != 1 ||
	bu_sscanf(argv[4], "%lf", &p[1]) != 1 ||
	bu_sscanf(argv[5], "%lf", &p[2]) != 1) {
	bu_vls_printf(gedp->ged_result_str, "bad vertex coordinates: %s %s %s\n",
		      argv[3], argv[4], argv[5]);
	rt_db_free_internal(&internal);
	return BRLCAD_ERROR;
    }
    VMOVE(vt, p);

    m = (struct model *)internal.idb_ptr;
    NMG_CK_MODEL(m);

    remove_vertex(m, vt);

    struct rt_wdb *wdbp = wdb_dbopen(gedp->dbip, RT_WDB_TYPE_DB_DEFAULT);
    if (!wdbp) {
	bu_vls_printf(gedp->ged_result_str, "Failed to open database handle\n");
	rt_db_free_internal(&internal);
	return BRLCAD_ERROR;
    }

    int ret = BRLCAD_OK;
    if (wdb_put_internal(wdbp, name, &internal, 1.0) < 0) {
	bu_vls_printf(gedp->ged_result_str, "wdb_put_internal(%s) error\n", name);
	ret = BRLCAD_ERROR;
    }

    wdb_close(wdbp);
    rt_db_free_internal(&internal);

    return ret;
}

/*
 * Local Variables:
 * mode: C
 * tab-width: 8
 * indent-tabs-mode: t
 * c-file-style: "stroustrup"
 * End:
 * ex: shiftwidth=4 tabstop=8
 */
