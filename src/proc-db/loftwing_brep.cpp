/*                 L O F T W I N G _ B R E P . C P P
 * BRL-CAD
 *
 * Copyright (c) 2026 United States Government as represented by
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
/** @file proc-db/loftwing_brep.cpp
 *
 * NURBS/BREP back end for loftwing(1).
 *
 * loftwing(1) skins a set of transformed NACA sections into an ARS surface.
 * That is the right primitive to raytrace inside BRL-CAD, but an ARS is not
 * an exchangeable exact surface: g-iges(1) flattens it into planar facets.
 * This file adds the second back end - the same sections skinned into a
 * degree 3x3 NURBS solid and written as a BREP (a genuine NURBS surface,
 * IGES type 128).
 *
 * The construction is generic over the section rings and lives in libbrep:
 * see brep_loft() and include/brep/loft.h.  What is left here is the part
 * that is specific to loftwing(1) - unpacking the section array the host
 * hands over, and writing the result into the database.
 */

#include "common.h"

#include "bu/app.h"
#include "rt/wdb.h"
#include "wdb.h"
#include "opennurbs.h"
#include "brep/loft.h"

/* ------------------------------------------------------------------ writer */

static int
emit_solid(struct rt_wdb *fp, const char *name, ON_Brep *brep)
{
    if (mk_brep(fp, name, brep) < 0) {
	bu_log("loftwing: mk_brep(%s) failed\n", name);
	return -1;
    }

    /* The caller (loftwing.c) builds the region and the shader. */
    return 0;
}

/* ------------------------------------------------------------------ entry */

/* Called by loftwing(1) in --format brep mode.
 *
 *   sec  - ns * np * 3 doubles, row major: sec[(station*np + point)*3 + xyz]
 *          Each station must be a planar ring, closed implicitly (the last
 *          point joins the first), listed in order around the section.
 */
extern "C" int
loftwing_brep(struct rt_wdb *fp, const char *name, const double *sec, int ns, int np)
{
    if (ns < 2 || np < 8) {
	bu_log("loftwing: need at least 2 stations and 8 points per section "
	       "(got %d and %d)\n", ns, np);
	return -1;
    }

    ON_SimpleArray<ON_3dPoint> points(ns * np);
    for (int j = 0; j < ns; ++j)
	for (int i = 0; i < np; ++i) {
	    const double *p = sec + ((size_t)j * np + i) * 3;
	    points.Append(ON_3dPoint(p[0], p[1], p[2]));
	}

    ON_Brep *brep = brep_loft(points.Array(), ns, np);

    if (!brep) {
	bu_log("loftwing: solid construction failed for %s\n", name);
	return -1;
    }

    if (!brep->IsSolid()) {
	bu_log("loftwing: warning - %s is not a closed oriented solid\n", name);
    }

    int rc = emit_solid(fp, name, brep);
    delete brep;
    return rc;
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
