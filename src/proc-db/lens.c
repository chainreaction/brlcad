/*                          L E N S . C
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

/** @file proc-db/lens.c
 *
 * Lens Generator
 *
 * Program to create basic lenses.
 *
 * In lens making, there are 4 basic types of lenses that result
 * from the two basic surfaces (concave and convex):
 *
 * Plano-Convex (PCX)
 * Plano-Concave (PCV)
 * BiConvex or Double Convex (DCX)
 * BiConcave or Double Concave (DCV)
 *
 * This program takes the lensmaker's equation:
 *
 *
 *               1            d (n - 1)   1    1
 *               - = (n - 1) (--------- - -- + --)
 *               f             n R1 R2    R2   R1
 *
 * and uses it to deduce the physical geometry needed
 * to represent simple lenses based on the inputs:
 *
 * Type (P or D), Diameter of lens,
 * focal length (+ for convex, - for concave),
 * n - the refractive index of the lens material,
 * and d - the thickness of the lens
 * at the center the lens along the optical axis).
 * The latter two are set to the refractive index of
 * soda-lime glass (1.5) and to 1/5 of the diameter
 * of the lens if not specified.
 *
 */

#include "common.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "vmath.h"
#include "bu/app.h"
#include "bu/getopt.h"
#include "bn.h"
#include "raytrace.h"
#include "wdb.h"

#define D2R(x) (x * DEG2RAD)
#define R2D(x) (x / DEG2RAD)
#define DEFAULT_LENS_FILENAME "lens.g"

static void
printusage(void) {
    bu_log("Usage: [-T lens_type] [-r refractive_index] [-d diameter]\n");
    bu_log("       [-t thickness] [-f focal_length] [filename]\n");
    bu_log("defaults: T = 2 (the other possible value is 1), r = 1.5, d = 200, t = 40, f = 600\n");
    bu_log("(units mm)\n");
}

static void
MakeP(struct rt_wdb *file, const char *prefix, fastf_t diameter, fastf_t focal_length, fastf_t ref_ind, fastf_t thickness)
{
    struct wmember lensglass, lens;
    struct bu_vls str = BU_VLS_INIT_ZERO;
    fastf_t sph_R, epa_H, epa_R, rcc_h;
    int lens_type;
    point_t origin;
    vect_t height;
    vect_t breadth;
    double disc;

    if (!file || !prefix)
	return;

    if (focal_length > 0)
	lens_type = 1;
    else
	lens_type = -1;

    sph_R = lens_type*focal_length*(ref_ind - 1);
    bu_log("sph_R = %f\n", sph_R);
    epa_R = diameter / 2;
    bu_log("epa_R = %f\n", epa_R);
    disc = sph_R*sph_R - epa_R*epa_R;
    if (disc < 0.0) {
	bu_log("ERROR: specified parameters result in non-physical geometry (discriminant < 0)\n");
	return;
    }
    epa_H = sph_R - sqrt(disc);
    bu_log("epa_H = %f\n", epa_H);
    rcc_h = thickness - lens_type * epa_H;
    bu_log("rcc_h = %f\n", rcc_h);

    BU_LIST_INIT(&lensglass.l);
    BU_LIST_INIT(&lens.l);

    if (epa_R > 0 && epa_H > 0) {
	if (rcc_h >= 0) {
	    VSET(origin, 0, 0, 0);
	    VSET(height, 0, -rcc_h, 0);
	    bu_vls_trunc(&str, 0);
	    bu_vls_printf(&str, "%s-cyl.s", prefix);
	    mk_rcc(file, bu_vls_cstr(&str), origin, height, diameter/2);
	    (void)mk_addmember(bu_vls_cstr(&str), &lensglass.l, NULL, WMOP_UNION);
	} else {
	    bu_log("Warning - specified thickness too thin for lens\n");
	}

	VSET(origin, 0, -rcc_h, 0);
	VSET(height, 0, -lens_type*epa_H, 0);
	VSET(breadth, 0, 0, 1);
	bu_vls_trunc(&str, 0);
	bu_vls_printf(&str, "%s-epa.s", prefix);
	mk_epa(file, bu_vls_cstr(&str), origin, height, breadth, epa_R, epa_R);
	if (lens_type == 1)
	    (void)mk_addmember(bu_vls_cstr(&str), &lensglass.l, NULL, WMOP_UNION);
	else
	    (void)mk_addmember(bu_vls_cstr(&str), &lensglass.l, NULL, WMOP_SUBTRACT);

	bu_vls_trunc(&str, 0);
	bu_vls_printf(&str, "%s.c", prefix);
	mk_lcomb(file, bu_vls_cstr(&str), &lensglass, 0,  NULL, NULL, NULL, 0);

	(void)mk_addmember(bu_vls_cstr(&str), &lens.l, NULL, WMOP_UNION);
	bu_vls_trunc(&str, 0);
	bu_vls_printf(&str, "%s.r", prefix);
	mk_lcomb(file, bu_vls_cstr(&str), &lens, 1, "glass", "ri=1.5", NULL, 0);
    } else {
	bu_log("ERROR: specified parameters result in non-physical geometry\n");
    }

    mk_freemembers(&lensglass.l);
    mk_freemembers(&lens.l);
    bu_vls_free(&str);
}


static void
MakeD(struct rt_wdb *file, const char *prefix, fastf_t diameter, fastf_t focal_length, fastf_t ref_ind, fastf_t thickness)
{
    struct wmember lensglass, lens;
    struct bu_vls str = BU_VLS_INIT_ZERO;
    fastf_t sph_R, epa_H, epa_R, rcc_h;
    int lens_type;
    point_t origin;
    vect_t height;
    vect_t breadth;
    double radicand;
    double disc;

    if (!file || !prefix)
	return;

    if (focal_length > 0)
	lens_type = 1;
    else
	lens_type = -1;

    if (ZERO(ref_ind)) {
	bu_log("ERROR: refractive index cannot be zero\n");
	return;
    }

    radicand = focal_length * lens_type * focal_length * lens_type * ref_ind * ref_ind - thickness * focal_length * lens_type * ref_ind;
    if (radicand < 0.0) {
	bu_log("ERROR: specified parameters result in non-physical geometry (negative radicand)\n");
	return;
    }

    sph_R = ((ref_ind - 1) * sqrt(radicand) + focal_length * lens_type * ref_ind * ref_ind - focal_length * lens_type * ref_ind)/ref_ind;
    bu_log("sph_R = %f\n", sph_R);
    epa_R = diameter / 2;
    bu_log("epa_R = %f\n", epa_R);
    disc = sph_R*sph_R - epa_R*epa_R;
    if (disc < 0.0) {
	bu_log("ERROR: specified parameters result in non-physical geometry (discriminant < 0)\n");
	return;
    }
    epa_H = sph_R - sqrt(disc);
    bu_log("epa_H = %f\n", epa_H);
    rcc_h = thickness - 2 * lens_type * epa_H;
    bu_log("rcc_h = %f\n", rcc_h);

    BU_LIST_INIT(&lensglass.l);
    BU_LIST_INIT(&lens.l);

    if (epa_R > 0 && epa_H > 0) {
	if (rcc_h >= 0) {
	    VSET(origin, 0, -rcc_h/2, 0);
	    VSET(height, 0, rcc_h, 0);
	    bu_vls_trunc(&str, 0);
	    bu_vls_printf(&str, "%s-cyl.s", prefix);
	    mk_rcc(file, bu_vls_cstr(&str), origin, height, diameter/2);
	    (void)mk_addmember(bu_vls_cstr(&str), &lensglass.l, NULL, WMOP_UNION);
	} else {
	    bu_log("Warning - specified thickness too thin for lens\n");
	}

	VSET(origin, 0, -rcc_h/2, 0);
	VSET(height, 0, -lens_type*epa_H, 0);
	VSET(breadth, 0, 0, 1);
	bu_vls_trunc(&str, 0);
	bu_vls_printf(&str, "%s-epa1.s", prefix);
	mk_epa(file, bu_vls_cstr(&str), origin, height, breadth, epa_R, epa_R);
	if (lens_type == 1)
	    (void)mk_addmember(bu_vls_cstr(&str), &lensglass.l, NULL, WMOP_UNION);
	else
	    (void)mk_addmember(bu_vls_cstr(&str), &lensglass.l, NULL, WMOP_SUBTRACT);
	VSET(origin, 0, rcc_h/2, 0);
	VSET(height, 0, lens_type * epa_H, 0);
	VSET(breadth, 0, 0, 1);
	bu_vls_trunc(&str, 0);
	bu_vls_printf(&str, "%s-epa2.s", prefix);
	mk_epa(file, bu_vls_cstr(&str), origin, height, breadth, epa_R, epa_R);
	if (lens_type == 1)
	    (void)mk_addmember(bu_vls_cstr(&str), &lensglass.l, NULL, WMOP_UNION);
	else
	    (void)mk_addmember(bu_vls_cstr(&str), &lensglass.l, NULL, WMOP_SUBTRACT);

	bu_vls_trunc(&str, 0);
	bu_vls_printf(&str, "%s.c", prefix);
	mk_lcomb(file, bu_vls_cstr(&str), &lensglass, 0,  NULL, NULL, NULL, 0);

	(void)mk_addmember(bu_vls_cstr(&str), &lens.l, NULL, WMOP_UNION);
	bu_vls_trunc(&str, 0);
	bu_vls_printf(&str, "%s.r", prefix);
	mk_lcomb(file, bu_vls_cstr(&str), &lens, 1, "glass", "ri=1.5", NULL, 0);
    } else {
	bu_log("ERROR: specified parameters result in non-physical geometry\n");
    }

    mk_freemembers(&lensglass.l);
    mk_freemembers(&lens.l);
    bu_vls_free(&str);
}


/* Process command line arguments */
static int
ReadArgs(int argc, char **argv, int *lens_1side_2side, fastf_t *ref_ind, fastf_t *diameter, fastf_t *thickness, fastf_t *focal_length)
{
    int c;
    const char *options="T:r:d:t:f:h?";
    int ltype;
    double val;

    while ((c=bu_getopt(argc, argv, options)) != -1) {
	switch (c) {
	    case 'T' :
		if (bu_sscanf(bu_optarg, "%d", &ltype) == 1) {
		    if (ltype == 1 || ltype == 2)
			*lens_1side_2side = ltype;
		}
		break;
	    case 'r':
		if (bu_sscanf(bu_optarg, "%lf", &val) == 1)
		    *ref_ind = val;
		break;
	    case 'd':
		if (bu_sscanf(bu_optarg, "%lf", &val) == 1)
		    *diameter = val;
		break;
	    case 't':
		if (bu_sscanf(bu_optarg, "%lf", &val) == 1)
		    *thickness = val;
		break;
	    case 'f':
		if (bu_sscanf(bu_optarg, "%lf", &val) == 1)
		    *focal_length = val;
		break;
	    case 'h':
	    case '?':
	    default:
		printusage();
		bu_exit(0, NULL);
	}
    }
    return bu_optind;
}


int
main(int ac, char *av[])
{
    struct rt_wdb *db_fp = NULL;
    struct bu_vls lens_type = BU_VLS_INIT_ZERO;
    struct bu_vls name = BU_VLS_INIT_ZERO;
    int lens_1side_2side = 2;
    fastf_t ref_ind, thickness, diameter, focal_length;
    const char *outfilename;

    if (ac > 0 && av && av[0])
	bu_setprogname(av[0]);

    ref_ind = 1.5;
    diameter = 200;
    thickness = diameter/5;
    focal_length = 600;
    bu_vls_printf(&lens_type, "DCX");
    bu_vls_printf(&name, "lens_%s_f%.1f_d%.1f", bu_vls_cstr(&lens_type), focal_length, diameter);

    /* Process arguments */
    ReadArgs(ac, av, &lens_1side_2side, &ref_ind, &diameter, &thickness, &focal_length);

    outfilename = (bu_optind < ac && av[bu_optind]) ? av[bu_optind] : DEFAULT_LENS_FILENAME;
    bu_log("Writing out geometry to file [%s] ...\n", outfilename);

    if (bu_file_exists(outfilename, NULL)) {
	bu_vls_free(&lens_type);
	bu_vls_free(&name);
	bu_exit(1, "ERROR: refusing to overwrite pre-existing file %s\n", outfilename);
    }
    db_fp = wdb_fopen(outfilename);
    if (!db_fp) {
	perror(outfilename);
	bu_vls_free(&lens_type);
	bu_vls_free(&name);
	return 2;
    }

    /* Make the requested lens*/
    if (lens_1side_2side == 1) {
	if (focal_length > 0) {
		bu_log("Making Plano-Convex lens...\n");
		bu_vls_trunc(&lens_type, 0);
		bu_vls_trunc(&name, 0);
		bu_vls_printf(&lens_type, "PCX");
	} else if (focal_length < 0) {
		bu_log("Making Plano-Concave lens...\n");
		bu_vls_trunc(&lens_type, 0);
		bu_vls_trunc(&name, 0);
		bu_vls_printf(&lens_type, "PCV");
	}
	bu_vls_printf(&name, "lens_%s_f%.1f_d%.1f", bu_vls_cstr(&lens_type), focal_length, diameter);
	MakeP(db_fp, bu_vls_cstr(&name), diameter, focal_length, ref_ind, thickness);
    } else if (lens_1side_2side == 2) {
	if (focal_length > 0) {
		bu_log("Making BiConvex lens...\n");
		bu_vls_trunc(&lens_type, 0);
		bu_vls_trunc(&name, 0);
		bu_vls_printf(&lens_type, "DCX");
	} else if (focal_length < 0) {
		bu_log("Making BiConcave lens...\n");
		bu_vls_trunc(&lens_type, 0);
		bu_vls_trunc(&name, 0);
		bu_vls_printf(&lens_type, "DCV");
	}
	bu_vls_printf(&name, "lens_%s_f%.1f_d%.1f", bu_vls_cstr(&lens_type), focal_length, diameter);
	MakeD(db_fp, bu_vls_cstr(&name), diameter, focal_length, ref_ind, thickness);
    }

    /* Close database */
    wdb_close(db_fp);

    bu_vls_free(&lens_type);
    bu_vls_free(&name);

    bu_log("Done.\n");

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
