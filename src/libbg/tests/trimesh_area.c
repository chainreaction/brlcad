/*                    T R I M E S H _ A R E A . C
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
#include <string.h>

#include "bu.h"
#include "bg.h"

int
main(int UNUSED(argc), const char *argv[])
{
    bu_setprogname(argv[0]);

    point_t verts[4];
    int faces[6] = {0, 1, 2, 1, 2, 3};

    for (int i = 0; i < 10; i++) {
	VSET(verts[0], 0, 0, 7);
	VSET(verts[1], 0, i, 7);
	VSET(verts[2], i, i, 7);
	VSET(verts[3], i, 0, 7);
	fastf_t square_area = (fastf_t)i*i;
	fastf_t tri_area = bg_trimesh_area((const int *)faces, 2, (const point_t *)verts, 4);
	bu_log("tri_area: %0.17g, square area: %0.17g\n", tri_area, square_area);
	if (!NEAR_EQUAL(tri_area, square_area, 2*SMALL_FASTF))
	    return -1;
	if (!i)
	    continue;
	VSET(verts[3], i, 0, 10);
	tri_area = bg_trimesh_area((const int *)faces, 2, (const point_t *)verts, 4);
	bu_log("tri_area: %0.17g, square area: %0.17g\n", tri_area, square_area);
	if (NEAR_EQUAL(tri_area, square_area, 2*SMALL_FASTF))
	    return -1;
    }

    /* Test bounds validation on bg_trimesh_area */
    int bad_faces[3] = {0, 1, 99};
    if (bg_trimesh_area(bad_faces, 1, (const point_t *)verts, 4) >= 0.0) {
	bu_log("ERROR: bg_trimesh_area accepted out-of-bounds vertex index\n");
	return -1;
    }

    /* Unit cube definition (8 vertices, 12 faces) */
    point_t cube_verts[8] = {
	{0, 0, 0}, {1, 0, 0}, {1, 1, 0}, {0, 1, 0},
	{0, 0, 1}, {1, 0, 1}, {1, 1, 1}, {0, 1, 1}
    };
    int cube_faces[36] = {
	// bottom (-Z)
	0, 2, 1,  0, 3, 2,
	// top (+Z)
	4, 5, 6,  4, 6, 7,
	// south (-Y)
	0, 1, 5,  0, 5, 4,
	// north (+Y)
	2, 3, 7,  2, 7, 6,
	// west (-X)
	0, 4, 7,  0, 7, 3,
	// east (+X)
	1, 2, 6,  1, 6, 5
    };

    /* Test bg_trimesh_volume on unit cube */
    fastf_t cube_vol = bg_trimesh_volume(cube_faces, 12, (const point_t *)cube_verts, 8);
    bu_log("cube_vol: %0.17g (expected 1.0)\n", cube_vol);
    if (!NEAR_EQUAL(cube_vol, 1.0, 1e-6)) {
	bu_log("ERROR: bg_trimesh_volume incorrect for unit cube\n");
	return -1;
    }

    /* Test volume with out-of-bounds index */
    int bad_cube_faces[36];
    memcpy(bad_cube_faces, cube_faces, sizeof(cube_faces));
    bad_cube_faces[0] = 100;
    if (bg_trimesh_volume(bad_cube_faces, 12, (const point_t *)cube_verts, 8) >= 0.0) {
	bu_log("ERROR: bg_trimesh_volume accepted out-of-bounds vertex index\n");
	return -1;
    }

    /* Test bg_trimesh_aabb */
    point_t bmin, bmax;
    if (bg_trimesh_aabb(&bmin, &bmax, cube_faces, 12, (const point_t *)cube_verts, 8) != 0) {
	bu_log("ERROR: bg_trimesh_aabb failed for unit cube\n");
	return -1;
    }
    if (!NEAR_EQUAL(bmin[X], 0.0, 1e-6) || !NEAR_EQUAL(bmax[X], 1.0, 1e-6) ||
	!NEAR_EQUAL(bmin[Y], 0.0, 1e-6) || !NEAR_EQUAL(bmax[Y], 1.0, 1e-6) ||
	!NEAR_EQUAL(bmin[Z], 0.0, 1e-6) || !NEAR_EQUAL(bmax[Z], 1.0, 1e-6)) {
	bu_log("ERROR: bg_trimesh_aabb returned wrong bounds\n");
	return -1;
    }

    /* Test aabb with invalid index */
    if (bg_trimesh_aabb(&bmin, &bmax, bad_cube_faces, 12, (const point_t *)cube_verts, 8) == 0) {
	bu_log("ERROR: bg_trimesh_aabb accepted out-of-bounds vertex index\n");
	return -1;
    }

    /* Test bg_trimesh_solid on valid closed cube */
    int *bedges = NULL;
    int is_solid = bg_trimesh_solid(8, 12, (fastf_t *)cube_verts, cube_faces, &bedges);
    if (is_solid != 0 || bedges != NULL) {
	bu_log("ERROR: bg_trimesh_solid reported unit cube is not solid (is_solid=%d)\n", is_solid);
	if (bedges) bu_free(bedges, "bedges");
	return -1;
    }

    /* Test bg_trimesh_solid on open mesh (omit last 2 faces) */
    is_solid = bg_trimesh_solid(8, 10, (fastf_t *)cube_verts, cube_faces, &bedges);
    if (is_solid != 1 || bedges == NULL) {
	bu_log("ERROR: bg_trimesh_solid reported open mesh is solid\n");
	if (bedges) bu_free(bedges, "bedges");
	return -1;
    }
    bu_free(bedges, "bedges");
    bedges = NULL;

    /* Test bg_trimesh_diff and bg_trimesh_hash */
    int diff = bg_trimesh_diff(cube_faces, 12, cube_verts, 8,
			       cube_faces, 12, cube_verts, 8, 1e-4);
    if (diff != 0) {
	bu_log("ERROR: bg_trimesh_diff failed comparing identical meshes\n");
	return -1;
    }

    unsigned long long h1 = bg_trimesh_hash(cube_faces, 12, cube_verts, 8, 1e-4);
    unsigned long long h2 = bg_trimesh_hash(cube_faces, 12, cube_verts, 8, 1e-4);
    if (h1 == 0 || h1 != h2) {
	bu_log("ERROR: bg_trimesh_hash not deterministic\n");
	return -1;
    }

    /* Permute faces in cube 2 (reverse face order) */
    int permuted_cube_faces[36];
    for (int f = 0; f < 12; f++) {
	permuted_cube_faces[3*f+0] = cube_faces[3*(11-f)+0];
	permuted_cube_faces[3*f+1] = cube_faces[3*(11-f)+1];
	permuted_cube_faces[3*f+2] = cube_faces[3*(11-f)+2];
    }
    diff = bg_trimesh_diff(cube_faces, 12, cube_verts, 8,
			   permuted_cube_faces, 12, cube_verts, 8, 1e-4);
    if (diff != 0) {
	bu_log("ERROR: bg_trimesh_diff reported difference on permuted face ordering\n");
	return -1;
    }

    unsigned long long h_perm = bg_trimesh_hash(permuted_cube_faces, 12, cube_verts, 8, 1e-4);
    if (h1 != h_perm) {
	bu_log("ERROR: bg_trimesh_hash differs on permuted face order (%llu vs %llu)\n", h1, h_perm);
	return -1;
    }

    /* Test bg_3d_spsr input validation */
    int *spsr_faces = NULL;
    int spsr_num_faces = 0;
    point_t *spsr_points = NULL;
    int spsr_num_points = 0;
    if (bg_3d_spsr(NULL, NULL, NULL, NULL, NULL, NULL, 0, NULL) != -1) {
	bu_log("ERROR: bg_3d_spsr accepted NULL inputs\n");
	return -1;
    }
    if (bg_3d_spsr(&spsr_faces, &spsr_num_faces, &spsr_points, &spsr_num_points,
		   cube_verts, (const vect_t *)cube_verts, 2, NULL) != -1) {
	bu_log("ERROR: bg_3d_spsr accepted count < 3\n");
	return -1;
    }

    /* Test bg_trimesh_separate validation */
    int *sep_idx = NULL;
    int *sep_off = NULL;
    if (bg_trimesh_separate(&sep_idx, &sep_off, cube_faces, -1) != -1) {
	bu_log("ERROR: bg_trimesh_separate accepted negative face count\n");
	return -1;
    }

    /* Test bg_trimesh_sync validation */
    int synced_faces[36];
    if (bg_trimesh_sync(synced_faces, cube_faces, -1) != -1) {
	bu_log("ERROR: bg_trimesh_sync accepted negative face count\n");
	return -1;
    }

    /* Test bg_trimesh_isect validation */
    if (bg_trimesh_isect(NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL,
			NULL, 0, NULL, 0, NULL, 0, NULL, 0) != 0) {
	bu_log("ERROR: bg_trimesh_isect failed on NULL inputs\n");
	return -1;
    }

    bu_log("All trimesh tests passed successfully.\n");
    return 0;
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
