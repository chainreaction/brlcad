/*                       P Y R A M I D . C
 * BRL-CAD
 *
 * Copyright (c) 1986-2026 United States Government as represented by
 * the U.S. Army Research Laboratory.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public License
 * version 2.1 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this file; see the file named COPYING for more
 * information.
 */
/** @file proc-db/pyramid.c
 *
 * Program to generate recursive 3-d pyramids (arb4).
 * Inspired by the SigGraph paper of Glassner.
 *
 */

#include "common.h"

#include <stdlib.h>
#include <stdio.h>
#include <math.h>

#include "vmath.h"
#include "bu/app.h"
#include "bn.h"
#include "raytrace.h"
#include "wdb.h"


double sin60;

struct rt_wdb *outfp;

/* Make a leaf node out of an ARB4 */
void
do_leaf(const char *name)
{
    point_t pt[4];

    VSET(pt[0], 0, 0, 0);
    VSET(pt[1], 100, 0, 0);
    VSET(pt[2], 50, 100*sin60, 0);
    VSET(pt[3], 50, 100*sin60/3, 100*sin60);

    mk_arb4(outfp, name, &pt[0][X]);
}

void
do_tree(const char *name, const char *lname, int level)
{
    int i;
    char nm[64];
    const char *leafp;
    int scale;
    struct wmember head;
    struct wmember *wp;

    BU_LIST_INIT(&head.l);

    if (level <= 1)
	leafp = lname;
    else
	leafp = nm;

    scale = 100;
    for (i=1; i<level; i++)
	scale *= 2;

    snprintf(nm, 64, "%sL", name);
    wp = mk_addmember(leafp, &head.l, NULL, WMOP_UNION);
    MAT_IDN(wp->wm_mat);

    snprintf(nm, 64, "%sR", name);
    wp = mk_addmember(leafp, &head.l, NULL, WMOP_UNION);
    MAT_DELTAS(wp->wm_mat, 1*scale, 0, 0);

    snprintf(nm, 64, "%sB", name);
    wp = mk_addmember(leafp, &head.l, NULL, WMOP_UNION);
    MAT_DELTAS(wp->wm_mat, 0.5*scale, sin60*scale, 0);

    snprintf(nm, 64, "%sT", name);
    wp = mk_addmember(leafp, &head.l, NULL, WMOP_UNION);
    MAT_DELTAS(wp->wm_mat, 0.5*scale, sin60/3*scale, sin60*scale);

    /* Set region flag on lowest level */
    mk_lcomb(outfp, name, &head, level<=1, NULL, NULL, NULL, 0);
    mk_freemembers(&head.l);

    /* Loop for children if level > 1 */
    if (level <= 1)
	return;
    for (i=0; i<4; i++) {
	snprintf(nm, 64, "%s%c", name, "LRBTx"[i]);
	do_tree(nm, lname, level-1);
    }
}


int
main(int argc, char **argv)
{
    int depth;

    if (argv && argv[0]) {
	bu_setprogname(argv[0]);
    }

    if (argc == 2 && (BU_STR_EQUAL(argv[1], "-h") || BU_STR_EQUAL(argv[1], "-?") || BU_STR_EQUAL(argv[1], "--help"))) {
	bu_log("Usage: %s recursion\n      (the argument is an integer between 1 and 8)\n", argv[0]);
	return 0;
    }
    if (argc != 2) {
	bu_log("Usage: %s recursion\n      (the argument is an integer between 1 and 8)\n", (argv && argv[0]) ? argv[0] : "pyramid");
	return 1;
    }

    if (bu_sscanf(argv[1], "%d", &depth) != 1 || depth < 1 || depth > 8) {
	bu_log("ERROR: recursion depth must be an integer between 1 and 8\n");
	return 1;
    }
    sin60 = sin(60.0 * DEG2RAD);

    outfp = wdb_fopen("pyramid.g");
    if (!outfp) {
	bu_log("ERROR: failed to open pyramid.g for writing\n");
	return 1;
    }
    printf("Creating file pyramid.g\n");

    mk_id(outfp, "3-D Pyramids");

    do_leaf("leaf");
    do_tree("tree", "leaf", depth);

    wdb_close(outfp);

    return 0;
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
