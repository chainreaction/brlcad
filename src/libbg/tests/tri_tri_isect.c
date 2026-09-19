/*               B N _ T R I _ T R I _ I S E C T . C
 * BRL-CAD
 *
 * Copyright (c) 2011-2026 United States Government as represented by
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

#include "common.h"

#include <stdio.h>

#include "bu.h"
#include "bg.h"


int
main(int argc, char **argv)
{
    int expected_result = 0;
    int actual_result = 0;
    float f1 = 0.0f, f2 = 0.0f, f3 = 0.0f;
    point_t V0 = VINIT_ZERO;
    point_t V1 = VINIT_ZERO;
    point_t V2 = VINIT_ZERO;
    point_t U0 = VINIT_ZERO;
    point_t U1 = VINIT_ZERO;
    point_t U2 = VINIT_ZERO;

    bu_setprogname(argv[0]);

    if (argc != 8)
	bu_exit(1, "ERROR: input format is V0x,V0y,V0z V1x,V1y,V1z V2x,V2y,V2z U0x,U0y,U0z U1x,U1y,U1z U2x,U2y,U2z expected_result [%s]\n", argv[0]);

    if (sscanf(argv[1], "%f,%f,%f", &f1, &f2, &f3) != 3)
	bu_exit(1, "ERROR: failed to parse V0\n");
    VSET(V0, f1, f2, f3);

    if (sscanf(argv[2], "%f,%f,%f", &f1, &f2, &f3) != 3)
	bu_exit(1, "ERROR: failed to parse V1\n");
    VSET(V1, f1, f2, f3);

    if (sscanf(argv[3], "%f,%f,%f", &f1, &f2, &f3) != 3)
	bu_exit(1, "ERROR: failed to parse V2\n");
    VSET(V2, f1, f2, f3);

    if (sscanf(argv[4], "%f,%f,%f", &f1, &f2, &f3) != 3)
	bu_exit(1, "ERROR: failed to parse U0\n");
    VSET(U0, f1, f2, f3);

    if (sscanf(argv[5], "%f,%f,%f", &f1, &f2, &f3) != 3)
	bu_exit(1, "ERROR: failed to parse U1\n");
    VSET(U1, f1, f2, f3);

    if (sscanf(argv[6], "%f,%f,%f", &f1, &f2, &f3) != 3)
	bu_exit(1, "ERROR: failed to parse U2\n");
    VSET(U2, f1, f2, f3);

    if (sscanf(argv[7], "%d", &expected_result) != 1)
	bu_exit(1, "ERROR: failed to parse expected_result\n");

    actual_result = bg_tri_tri_isect(V0, V1, V2, U0, U1, U2);

    bu_log("result: %d\n", actual_result);

    if (expected_result == actual_result) {
	return 0;
    }

    return -1;
}


/** @} */
/*
 * Local Variables:
 * mode: C
 * tab-width: 8
 * indent-tabs-mode: t
 * c-file-style: "stroustrup"
 * End:
 * ex: shiftwidth=4 tabstop=8
 */
