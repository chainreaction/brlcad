/*                      M O L E C U L E . C
 * BRL-CAD
 *
 * Copyright (c) 2004-2026 United States Government as represented by
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
/** @file proc-db/molecule.c
 *
 * Create a molecule from G. Adams format
 *
 */

#include "common.h"

#include <stdlib.h>
#include <stdio.h>
#include <math.h>
#include <string.h>

#include "vmath.h"
#include "bu/app.h"
#include "raytrace.h"
#include "wdb.h"


struct sphere  {
    struct sphere * next;		/* Next Sphere */
    int s_id;			/* Sphere id */
#define NAME_LEN 128
    char s_name[NAME_LEN+1];	/* Sphere name */
    point_t s_center;		/* Sphere Center */
    fastf_t s_rad;			/* Sphere radius */
    int s_atom_type;		/* Atom Type */
};


struct sphere *s_list = (struct sphere *) 0;
struct sphere *s_head = (struct sphere *) 0;

struct atoms  {
    int a_id;
    char a_name[NAME_LEN+1];
    unsigned char red, green, blue;
};

#define MAX_ATOMS 50
struct atoms atom_list[MAX_ATOMS];

const char * matname = "plastic";
const char * matparm = "shine=100.0 diffuse=.8 specular=.2";

void read_data(void), process_sphere(int id, fastf_t *center, double rad, int sph_type);
int make_bond(int sp1, int sp2);

struct wmember head;

static const char usage[] = "Usage: molecule db_title < mol-cube.dat\n      (output file molecule.g)\n";

struct rt_wdb *outfp;

int
main(int argc, char **argv)
{
    struct sphere *sp;

    if (!argv || !argv[0])
	return 1;

    bu_setprogname(argv[0]);

    if (argc == 2 && (BU_STR_EQUAL(argv[1], "-h") || BU_STR_EQUAL(argv[1], "-?") || BU_STR_EQUAL(argv[1], "--help"))) {
	fputs(usage, stdout);
	return 0;
    }

    if (argc != 2) {
	fputs(usage, stderr);
	return 1;
    }

    BU_LIST_INIT(&head.l);
    outfp = wdb_fopen("molecule.g");
    if (!outfp) {
	bu_exit(EXIT_FAILURE, "ERROR: Unable to open molecule.g for writing\n");
    }

    mk_id(outfp, argv[1]);
    read_data();

    /* Build the overall combination */
    mk_lfcomb(outfp, "molecule", &head, 0);

    mk_freemembers(&head.l);
    wdb_close(outfp);

    /* Free linked list of spheres */
    sp = s_head;
    while (sp) {
	struct sphere *next = sp->next;
	bu_free(sp, "free sphere");
	sp = next;
    }
    s_head = s_list = NULL;

    return 0;
}


/* File format from stdin
 *
 *   Atom definition line: 0 atom_id name red green blue
 * Sphere definition line: 1 sph_id center_x center_y center_z radius atom_id
 *   Bond definition line: 2 sph_id sph_id
 *
 * Example File (Water):
 * 0 0 Oxygen   255 0   0
 * 0 1 Hydrogen 255 255 255
 * 1 0  0.0  0.0 0.0 1.5 0
 * 1 1 -3.0 -2.0 0.0 1.0 1
 * 1 2  3.0 -2.0 0.0 1.0 1
 */
void
read_data(void)
{
    char line[BUFSIZ];

    while (bu_fgets(line, sizeof(line), stdin) != NULL) {
	int data_type;
	if (bu_sscanf(line, "%d", &data_type) != 1)
	    continue;

	switch (data_type) {
	    case (0): {
		int i;
		char name[NAME_LEN + 1];
		int red, green, blue;
		if (bu_sscanf(line, "%*d %d %128s %d %d %d", &i, name, &red, &green, &blue) == 5) {
		    if (i < 0 || i >= MAX_ATOMS) {
			fprintf(stderr, "Atom index value %d is out of bounds [0, %d]\n", i, MAX_ATOMS - 1);
			return;
		    }
		    bu_strlcpy(atom_list[i].a_name, name, sizeof(atom_list[i].a_name));
		    atom_list[i].red  = (unsigned char)(red < 0 ? 0 : (red > 255 ? 255 : red));
		    atom_list[i].green  = (unsigned char)(green < 0 ? 0 : (green > 255 ? 255 : green));
		    atom_list[i].blue  = (unsigned char)(blue < 0 ? 0 : (blue > 255 ? 255 : blue));
		}
		break;
	    }
	    case (1): {
		int sphere_id;
		float x, y, z;
		float sphere_radius;
		int atom_type;
		point_t center;
		if (bu_sscanf(line, "%*d %d %f %f %f %f %d", &sphere_id, &x, &y, &z, &sphere_radius, &atom_type) == 6) {
		    VSET(center, x, y, z);
		    process_sphere(sphere_id, center, sphere_radius, atom_type);
		}
		break;
	    }
	    case (2): {
		int b_1, b_2;
		if (bu_sscanf(line, "%*d %d %d", &b_1, &b_2) == 2) {
		    (void)make_bond(b_1, b_2);
		}
		break;
	    }
	    default:
		return;
	}
    }
}


void
process_sphere(int id, fastf_t *center, double rad, int sph_type)
{
    struct sphere *newsph;
    char nm[NAME_LEN+1], nm1[NAME_LEN+1];
    unsigned char rgb[3];
    struct wmember reg_head;

    if (!outfp || !center || rad <= 0.0)
	return;

    BU_ALLOC(newsph, struct sphere);

    if (sph_type >= 0 && sph_type < MAX_ATOMS) {
	rgb[0] = atom_list[sph_type].red;
	rgb[1] = atom_list[sph_type].green;
	rgb[2] = atom_list[sph_type].blue;
    } else {
	rgb[0] = 255;
	rgb[1] = 255;
	rgb[2] = 255;
    }

    snprintf(nm1, sizeof(nm1), "sph.%d", id);
    mk_sph(outfp, nm1, center, rad);

    /* Create a region nm to contain the solid nm1 */
    BU_LIST_INIT(&reg_head.l);
    (void)mk_addmember(nm1, &reg_head.l, NULL, WMOP_UNION);
    snprintf(nm, sizeof(nm), "SPH.%d", id);
    mk_lcomb(outfp, nm, &reg_head, 1, matname, matparm, rgb, 0);
    mk_freemembers(&reg_head.l);

    /* Include this region in the larger group */
    (void)mk_addmember(nm, &head.l, NULL, WMOP_UNION);

    newsph->next = (struct sphere *)0;
    newsph->s_id = id;
    bu_strlcpy(newsph->s_name, nm1, sizeof(newsph->s_name));
    VMOVE(newsph->s_center, center);
    newsph->s_rad = rad;
    newsph->s_atom_type = sph_type;

    if (s_head == (struct sphere *) 0) {
	s_head = s_list = newsph;
    } else {
	s_list->next = newsph;
	s_list = newsph;
    }
}


int
make_bond(int sp1, int sp2)
{
    struct sphere * s1, *s2, *s_ptr;
    point_t base;
    vect_t height;
    char nm[NAME_LEN+1], nm1[NAME_LEN+1];
    unsigned char rgb[3];
    struct wmember reg_head;

    if (!outfp)
	return -1;

    s1 = s2 = (struct sphere *) 0;

    for (s_ptr = s_head; s_ptr != (struct sphere *)0; s_ptr = s_ptr->next) {
	if (s_ptr->s_id == sp1)
	    s1 = s_ptr;

	if (s_ptr->s_id == sp2)
	    s2 = s_ptr;
    }

    if (s1 == (struct sphere *) 0 || s2 == (struct sphere *)0)
	return -1;		/* error */

    VMOVE(base, s1->s_center);
    VSUB2(height, s2->s_center, s1->s_center);

    if (s1->s_rad <= 0.0 || MAGNITUDE(height) <= 0.0)
	return -1;

    snprintf(nm, sizeof(nm), "bond.%d.%d", sp1, sp2);

    rgb[0] = 191;
    rgb[1] = 142;
    rgb[2] = 57;

    /* TODO: make the scaling factor configurable or autosize based on
     * overall complexity.
     */
    mk_rcc(outfp, nm, base, height, s1->s_rad * 0.25);

    BU_LIST_INIT(&reg_head.l);
    (void)mk_addmember(nm, &reg_head.l, NULL, WMOP_UNION);
    (void)mk_addmember(s1->s_name, &reg_head.l, NULL, WMOP_SUBTRACT);
    (void)mk_addmember(s2->s_name, &reg_head.l, NULL, WMOP_SUBTRACT);
    snprintf(nm1, sizeof(nm1), "BOND.%d.%d", sp1, sp2);
    mk_lcomb(outfp, nm1, &reg_head, 1, matname, matparm, rgb, 0);
    mk_freemembers(&reg_head.l);
    (void)mk_addmember(nm1, &head.l, NULL, WMOP_UNION);

    return 0;		/* OK */
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
