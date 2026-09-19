/*               T R I M E S H _ I S E C T . C P P
 * BRL-CAD
 *
 * Copyright (c) 2018-2026 United States Government as represented by
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
/** @file trimesh_isect.cpp
 *
 * Test if two meshes intersect and return the set of intersecting faces.
 *
 */

#include "common.h"

#include <vector>
#include <set>
#include <map>
#include <cmath>

#include <float.h>
#include <locale.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <time.h>

#ifndef PLOT_PREFIX_STR
#  define PLOT_PREFIX_STR bg_plot3_
#endif
#include "bv/plot3.h"
#include "bu/log.h"
#include "bu/malloc.h"
#include "bn/tol.h"
#include "bg/tri_tri.h"
#include "bg/trimesh.h"

static void
plot_faces(const char *fname, std::set<int> *faces, int *f, point_t *v)
{
    if (!fname || !faces || !f || !v)
	return;
    FILE* plot_file = fopen(fname, "wb");
    if (!plot_file)
	return;
    int r = int(256*drand48() + 100.0);
    int g = int(256*drand48() + 100.0);
    int b = int(256*drand48() + 100.0);
    pl_color(plot_file, r, g, b);
    std::set<int>::iterator f_it;
    for (f_it = faces->begin(); f_it != faces->end(); f_it++) {
	point_t p1, p2, p3;
	VMOVE(p1, v[f[(*f_it)*3+0]]);
	VMOVE(p2, v[f[(*f_it)*3+1]]);
	VMOVE(p3, v[f[(*f_it)*3+2]]);
	pdv_3move(plot_file, p1);
	pdv_3cont(plot_file, p2);
	pdv_3move(plot_file, p1);
	pdv_3cont(plot_file, p3);
	pdv_3move(plot_file, p2);
	pdv_3cont(plot_file, p3);
    }
    fclose(plot_file);
}

/* For NURBS refinement, we do this once and keep the set of "inside" faces from
 * each mesh.  If any of the vertices from those faces are still inside after a
 * refinement and remeshing step, they are either genuinely inside the mesh (bad
 * NURBS geometry) or our closestpoint routines haven't found the right match. */


/* See Mesh Arrangements for Solid Geometry - this implements the initial stages
 * of that workflow, although it does not go on to construct the new faces and
 * assemble the new meshes. */
extern "C" int
bg_trimesh_isect(
    int **faces_inside_1, int *num_faces_inside_1, int **faces_inside_2, int *num_faces_inside_2,
    int **faces_isect_1, int *num_faces_isect_1, int **faces_isect_2, int *num_faces_isect_2,
    int *faces_1, int num_faces_1, point_t *vertices_1, int num_vertices_1,
    int *faces_2, int num_faces_2, point_t *vertices_2, int num_vertices_2)
{
    if (num_faces_inside_1) (*num_faces_inside_1) = 0;
    if (num_faces_inside_2) (*num_faces_inside_2) = 0;
    if (num_faces_isect_1) (*num_faces_isect_1) = 0;
    if (num_faces_isect_2) (*num_faces_isect_2) = 0;
    if (faces_inside_1) (*faces_inside_1) = NULL;
    if (faces_inside_2) (*faces_inside_2) = NULL;
    if (faces_isect_1) (*faces_isect_1) = NULL;
    if (faces_isect_2) (*faces_isect_2) = NULL;

    if (!faces_1 || num_faces_1 <= 0 || !vertices_1 || num_vertices_1 <= 0) return 0;
    if (!faces_2 || num_faces_2 <= 0 || !vertices_2 || num_vertices_2 <= 0) return 0;

    /* TODO - check solidity.  If these aren't both valid/solid, this test
     * won't (currently) handle it */


    /* Step 1 - construct and check bboxes.  If they don't overlap, there's no
     * need for any further work. */

    point_t bb1min, bb2min;
    point_t bb1max, bb2max;

    VSETALL(bb1min, MAX_FASTF);
    VSETALL(bb2min, MAX_FASTF);
    VSETALL(bb1max, -1.0*MAX_FASTF);
    VSETALL(bb2max, -1.0*MAX_FASTF);

    for (int i = 0; i < num_vertices_1; i++) {
	VMINMAX(bb1min, bb1max, vertices_1[i]);
    }

    for (int i = 0; i < num_vertices_2; i++) {
	VMINMAX(bb2min, bb2max, vertices_2[i]);
    }

    int isect = (
	(bb1min[0] <= bb2max[0] && bb1max[0] >= bb2min[0]) &&
	(bb1min[1] <= bb2max[1] && bb1max[1] >= bb2min[1]) &&
	(bb1min[2] <= bb2max[2] && bb1max[2] >= bb2min[2])
	) ? 1 : 0;

    if (!isect) {
	return 0;
    }

    /* If the bboxes overlap, build the sets of faces with at least one vertex in the
     * other mesh's bounding box.  Those are the only ones we need to worry about */
    std::vector<int> m1_working_faces;
    std::vector<int> m2_working_faces;

    for (int i = 0; i < num_faces_1; i++) {
	int v0 = faces_1[3*i+0];
	int v1 = faces_1[3*i+1];
	int v2 = faces_1[3*i+2];
	if (v0 < 0 || v0 >= num_vertices_1 ||
	    v1 < 0 || v1 >= num_vertices_1 ||
	    v2 < 0 || v2 >= num_vertices_1) {
	    return 0;
	}

	if (V3PNT_IN_RPP(vertices_1[v0], bb2min, bb2max) ||
	    V3PNT_IN_RPP(vertices_1[v1], bb2min, bb2max) ||
	    V3PNT_IN_RPP(vertices_1[v2], bb2min, bb2max)) {
	    m1_working_faces.push_back(i);
	}
    }

    for (int i = 0; i < num_faces_2; i++) {
	int v0 = faces_2[3*i+0];
	int v1 = faces_2[3*i+1];
	int v2 = faces_2[3*i+2];
	if (v0 < 0 || v0 >= num_vertices_2 ||
	    v1 < 0 || v1 >= num_vertices_2 ||
	    v2 < 0 || v2 >= num_vertices_2) {
	    return 0;
	}

	if (V3PNT_IN_RPP(vertices_2[v0], bb1min, bb1max) ||
	    V3PNT_IN_RPP(vertices_2[v1], bb1min, bb1max) ||
	    V3PNT_IN_RPP(vertices_2[v2], bb1min, bb1max)) {
	    m2_working_faces.push_back(i);
	}
    }

    bu_log("m1_working_faces size: %zd\n", m1_working_faces.size());
    bu_log("m2_working_faces size: %zd\n", m2_working_faces.size());

    /* For each "working" face in faces_1, check it against "working" faces in
     * face_2 for intersections.  IFF it intersects, add it and the other face
     * to their intersects set. */
    std::set<int> m1_intersecting_faces;
    std::set<int> m2_intersecting_faces;
    for (size_t i = 0; i < m1_working_faces.size(); i++) {
	for (size_t j = 0; j < m2_working_faces.size(); j++) {
	    int v1_1 = faces_1[3*m1_working_faces[i]];
	    int v1_2 = faces_1[3*m1_working_faces[i]+1];
	    int v1_3 = faces_1[3*m1_working_faces[i]+2];
	    int v2_1 = faces_2[3*m2_working_faces[j]];
	    int v2_2 = faces_2[3*m2_working_faces[j]+1];
	    int v2_3 = faces_2[3*m2_working_faces[j]+2];
	    if (bg_tri_tri_isect(
		    vertices_1[v1_1], vertices_1[v1_2], vertices_1[v1_3],
		    vertices_2[v2_1], vertices_2[v2_2], vertices_2[v2_3]))
	    {
		m1_intersecting_faces.insert(m1_working_faces[i]);
		m2_intersecting_faces.insert(m2_working_faces[j]);
	    }
	}
    }

    bu_log("m1_intersecting_faces size: %zd\n", m1_intersecting_faces.size());
    plot_faces("m1.plot3", &m1_intersecting_faces, faces_1, vertices_1);
    bu_log("m2_intersecting_faces size: %zd\n", m2_intersecting_faces.size());
    plot_faces("m2.plot3", &m2_intersecting_faces, faces_2, vertices_2);

    if (faces_isect_1 && num_faces_isect_1) {
	*num_faces_isect_1 = (int)m1_intersecting_faces.size();
	if (*num_faces_isect_1 > 0) {
	    *faces_isect_1 = (int *)bu_calloc((size_t)*num_faces_isect_1, sizeof(int), "isect faces 1");
	    int idx = 0;
	    for (auto fit = m1_intersecting_faces.begin(); fit != m1_intersecting_faces.end(); ++fit) {
		(*faces_isect_1)[idx++] = *fit;
	    }
	}
    }
    if (faces_isect_2 && num_faces_isect_2) {
	*num_faces_isect_2 = (int)m2_intersecting_faces.size();
	if (*num_faces_isect_2 > 0) {
	    *faces_isect_2 = (int *)bu_calloc((size_t)*num_faces_isect_2, sizeof(int), "isect faces 2");
	    int idx = 0;
	    for (auto fit = m2_intersecting_faces.begin(); fit != m2_intersecting_faces.end(); ++fit) {
		(*faces_isect_2)[idx++] = *fit;
	    }
	}
    }

    int ret = (m1_intersecting_faces.size() > 0 || m2_intersecting_faces.size() > 0) ? 1 : 0;
    return ret;
}

// Local Variables:
// tab-width: 8
// mode: C++
// c-basic-offset: 4
// indent-tabs-mode: t
// c-file-style: "stroustrup"
// End:
// ex: shiftwidth=4 tabstop=8
