/*                      N M G _ F C U T . C
 * BRL-CAD
 *
 * Copyright (c) 2007-2026 United States Government as represented by
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
/** @addtogroup nmg */
/** @{ */
/** @file primitives/nmg/nmg_fcut.c
 *
 * After two faces have been intersected, cut or join loops crossed
 * by the line of intersection.  (Formerly nmg_comb.c)
 *
 * The main external routine here is nmg_face_cutjoin().
 *
 * The line of intersection ("ray") will divide the face into two sets
 * of loops.
 * No one loop may cross the ray after this routine is finished.
 *
 * Intersection points of significance to the other face but not yet
 * part of the current face's geometry are denoted by a vu on the ray
 * list, which points to a loop of a single vertex.  These points
 * need to be incorporated into the final face.
 */
/** @} */

#include "common.h"

#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "bio.h"

#include "vmath.h"
#include "bu/log.h"
#include "bu/malloc.h"
#include "bu/sort.h"
#include "bg/plane.h"
#include "bv/plot3.h"
#include "nmg.h"

#define PLOT_BOTH_FACES 1

#define VAVERAGE(a, b, c) { \
	(a)[X] = ((b)[X] + (c)[X]) * 0.5;\
	(a)[Y] = ((b)[Y] + (c)[Y]) * 0.5;\
	(a)[Z] = ((b)[Z] + (c)[Z]) * 0.5;\
    }

/* States of the state machine */
#define NMG_STATE_ERROR		0
#define NMG_STATE_OUT		1
#define NMG_STATE_ON_L		2
#define NMG_STATE_ON_R		3
#define NMG_STATE_ON_B		4
#define NMG_STATE_ON_N		5
#define NMG_STATE_IN		6

#define NMG_E_ASSESSMENT_LEFT		0
#define NMG_E_ASSESSMENT_RIGHT		1
#define NMG_E_ASSESSMENT_ON_FORW	2
#define NMG_E_ASSESSMENT_ON_REV		3
#define NMG_E_ASSESSMENT_ERROR		4

#define NMG_V_ASSESSMENT_LONE		16
#define NMG_V_ASSESSMENT_ERROR		17
#define NMG_V_COMB(_p, _n)		(((_p)<<2)|(_n))

#define WEDGE_LEFT 0
#define WEDGE_CROSS 1
#define WEDGE_RIGHT 2
#define WEDGE_ON 3
#define WEDGECLASS2STR(_cl) (((_cl) >= 0 && (_cl) <= 3) ? nmg_wedgeclass_string[(_cl)] : nmg_wedgeclass_string[4])
static const char *nmg_wedgeclass_string[] = {
    "LEFT",
    "CROSS",
    "RIGHT",
    "ON",
    "???"
};

/* The "ray" here is the intersection line between two faces */
struct nmg_ray_state {
    uint32_t magic;
    struct vertexuse **vu;		/* ptr to vu array */
    int nvu;				/* len of vu[] */
    point_t pt;				/* The ray */
    vect_t dir;
    struct edge_g_lseg *eg_p;		/* Edge geom of the ray */
    struct shell *sA;
    struct shell *sB;
    struct faceuse *fu1;
    struct faceuse *fu2;
    vect_t left;			/* points left of ray, on face */
    int state;				/* current (old) state */
    int last_action;			/* last action taken */
    vect_t ang_x_dir;			/* x axis for angle measure */
    vect_t ang_y_dir;			/* y axis for angle measure */
    const struct bn_tol *tol;
};
#define NMG_RAYSTATE_MAGIC 0x54322345
#define NMG_CK_RAYSTATE(_p) NMG_CKMAG(_p, NMG_RAYSTATE_MAGIC, "nmg_ray_state")

struct loop_cuts {
    struct loopuse *lu;
    struct vertexuse *vu1;
    struct vertexuse *vu2;
};

/**
 * Sort list of hit points (vertexuse's) in fu1 (bu_ptbl 'b') on plane
 * of fu2 (defined by pt+dir), by increasing distance, vertex ptr, and
 * vu ptr.  Eliminate duplications of vu at same distance.  (Actually,
 * a given vu should show up at exactly 1 distance!)  The line of
 * intersection is pt + t * dir.
 */
static void
ptbl_vsort(struct bu_ptbl *b, fastf_t *pt, fastf_t *dir, fastf_t *mag, fastf_t dist_tol)
{
    struct vertexuse **vu;
    size_t i, j;

    if (!b || !b->buffer || !pt || !dir || !mag || b->end <= 0)
	return;

    vu = (struct vertexuse **)b->buffer;

    if (nmg_debug) {
	/* Ensure that distance from points to ray is reasonable */
	for (i = 0; i < b->end; ++i) {
	    fastf_t dist;

	    if (!vu[i]) continue;
	    NMG_CK_VERTEXUSE(vu[i]);
	    if (!vu[i]->v_p || !vu[i]->v_p->vg_p) continue;
	    dist = bg_dist_line3_pnt3(pt, dir, vu[i]->v_p->vg_p->coord);
	    if (dist > dist_tol) {
		bu_log("WARNING ptbl_vsort() vu=%p point off line by %e %g*tol, tol=%e\n",
		       (void *)vu[i], dist,
		       (dist_tol > SMALL_FASTF) ? (dist/dist_tol) : 0.0, dist_tol);
		if (nmg_debug&NMG_DEBUG_VU_SORT) {
		    VPRINT("  vu", vu[i]->v_p->vg_p->coord);
		    VPRINT("  pt", pt);
		    VPRINT(" dir", dir);
		}
	    }
	    if (dist > 100*dist_tol) {
		bu_log("WARNING ptbl_vsort() vu=%p point off line by %g > 100*dist_tol\n",
		       (void *)vu[i], dist);
	    }
	}
    }

    /* check vertexuses and compute distance from start of line */
    for (i = 0; i < b->end; ++i) {
	vect_t vect;
	if (!vu[i]) continue;
	NMG_CK_VERTEXUSE(vu[i]);
	if (!vu[i]->v_p || !vu[i]->v_p->vg_p) continue;

	if (mag[i] >= MAX_FASTF - SMALL_FASTF) {
	    VSUB2(vect, vu[i]->v_p->vg_p->coord, pt);
	    mag[i] = VDOT(vect, dir);
	}

	/* Find previous vu's at "same" distance, within dist_tol */
	for (j = 0; j < i; j++) {
	    fastf_t tmag;

	    if (!vu[j]) continue;
	    tmag = mag[i] - mag[j];
	    if (tmag < -dist_tol) continue;
	    if (tmag > dist_tol) continue;
	    /* Nearly equal at same vertex */
	    if (!ZERO(mag[i] - mag[j])
		&& vu[i]->v_p == vu[j]->v_p)
	    {
		bu_log("ptbl_vsort: forcing vu=%p & vu=%p mag equal\n", (void *)vu[i], (void *)vu[j]);
		mag[j] = mag[i]; /* force equal */
	    }
	}
    }

    for (i = 0; i + 1 < b->end; ++i) {
	for (j = i + 1; j < b->end; ++j) {
	    if (!vu[i] || !vu[j]) continue;

	    if (mag[i] < mag[j]) continue;
	    if (ZERO(mag[i] - mag[j])) {
		if (vu[i]->v_p < vu[j]->v_p) continue;
		if (vu[i]->v_p == vu[j]->v_p) {
		    if (vu[i] < vu[j]) continue;
		    if (vu[i] == vu[j]) {
			ssize_t last = (ssize_t)b->end - 1;
			/* vu duplication, eliminate! */
			bu_log("ptbl_vsort: vu duplication eliminated\n");
			if ((ssize_t)j >= last) {
			    /* j is last element */
			    b->end--;
			    break;
			}
			/* rewrite j with last element */
			vu[j] = vu[last];
			mag[j] = mag[last];
			b->end--;
			/* Repeat this index */
			j--;
			continue;
		    }
		    /* vu[i] > vu[j], fall through */
		}
		/* vu[i]->v_p > vu[j]->v_p, fall through */
	    }
	    /* mag[i] > mag[j] */

	    /* exchange [i] and [j] */
	    {
		struct vertexuse *tvu;
		tvu = vu[i];
		vu[i] = vu[j];
		vu[j] = tvu;
	    }

	    {
		fastf_t tmag;
		tmag = mag[i];
		mag[i] = mag[j];
		mag[j] = tmag;
	    }
	}
    }
}

/**
 * As an automatic check for the intersector failing to find
 * all intersections, check all the vertices on the intersection line.
 * For each one, find all the other uses in this faceuse, and
 * if they are not also listed on the line, they were overlooked.
 */
int
nmg_ck_vu_ptbl(struct bu_ptbl *p, struct faceuse *fu)
{
    struct vertex *v;
    struct vertexuse *vu;
    struct vertexuse *tvu;
    struct faceuse *tfu;
    size_t i;
    int ret = 0;
    int iteration = 0;

    if (!p || !fu)
	return 0;

    BU_CK_PTBL(p);
    NMG_CK_FACEUSE(fu);

top:
    if (++iteration > 100) {
	bu_log("nmg_ck_vu_ptbl(): iteration limit reached\n");
	return ret;
    }
    for (i = 0; i < BU_PTBL_LEN(p); i++) {
	vu = (struct vertexuse *)BU_PTBL_GET(p, i);
	if (!vu) continue;
	NMG_CK_VERTEXUSE(vu);
	v = vu->v_p;
	if (!v) continue;
	NMG_CK_VERTEX(v);
	tfu = nmg_find_fu_of_vu(vu);
	if (tfu != fu) {
	    bu_log("ERROR: vu=%p v=%p up_fu=%p != arg_fu=%p\n",
		   (void *)vu, (void *)v, (void *)tfu, (void *)fu);
	    return -1;
	}
	for (BU_LIST_FOR(tvu, vertexuse, &v->vu_hd)) {
	    if (!tvu) continue;
	    NMG_CK_VERTEXUSE(tvu);
	    if (tvu == vu) continue;
	    if ((tfu = nmg_find_fu_of_vu(tvu)) == (struct faceuse *)NULL)
		continue;
	    if (tfu != fu) continue;
	    /* tvu is in fu.  Is tvu on the line? */
	    if (bu_ptbl_locate(p, (long *)&tvu->l.magic) >= 0) continue;
	    /* No, not on list */
	    bu_log("WARNING: vu=%p v=%p %s=%p is on isect line, tvu=%p %s=%p isn't.\n",
		   (void *)vu, (void *)v,
		   (vu->up.magic_p ? bu_identify_magic(*vu->up.magic_p) : "NULL"),
		   (void *)vu->up.magic_p,
		   (void *)tvu,
		   (tvu->up.magic_p ? bu_identify_magic(*tvu->up.magic_p) : "NULL"),
		   (void *)tvu->up.magic_p);
	    /* Add it in to heal list */
	    (void)bu_ptbl_ins(p, (long *)&tvu->l.magic);
	    ret++;
	    goto top;
	}
    }
    if (ret && (nmg_debug&NMG_DEBUG_FCUT))
	bu_log("nmg_ck_vu_ptbl() ret=%d\n", ret);
    return ret;
}

int
nmg_wedge_class(int ass, double a, double b)
{
    double ha, hb;
    int ret;

    ha = a - 180.0;
    hb = b - 180.0;

    if (ass == NMG_V_COMB(NMG_E_ASSESSMENT_ON_FORW, NMG_E_ASSESSMENT_ON_FORW)) {
	return WEDGE_ON;
    }
    if (ass == NMG_V_COMB(NMG_E_ASSESSMENT_ON_REV, NMG_E_ASSESSMENT_ON_REV)) {
	return WEDGE_ON;
    }

    if (NEAR_ZERO(ha, 0.01)) {
	/* A is on the ray, within tol */
	if (NEAR_ZERO(hb, 0.01)) {
	    /* B is on the ray, within tol; 0-angle wedge */
	    ret = WEDGE_CROSS;
	    goto out;
	}
	if (hb < 0.0) {
	    ret = WEDGE_RIGHT;
	    goto out;
	}
	ret = WEDGE_LEFT;
	goto out;
    }
    if (ha < 0.0) {
	/* A is to the right */
	if (hb <= 0.0) {
	    ret = WEDGE_RIGHT;
	    goto out;
	}
	ret = WEDGE_CROSS;
	goto out;
    }
    /* ha is > 0, A is to the left */
    if (NEAR_ZERO(hb, 0.01)) {
	/* A is left, B is ON_FORW (180) */
	ret = WEDGE_LEFT;
	goto out;
    }
    if (hb >= 0.0) {
	/* A is left, B is LEFT */
	ret = WEDGE_LEFT;
	goto out;
    }
    /* A is left, B is RIGHT */
    ret = WEDGE_CROSS;
out:
    if (nmg_debug&NMG_DEBUG_VU_SORT) {
	bu_log("nmg_wedge_class(%g, %g) = %s\n",
	       a, b, WEDGECLASS2STR(ret));
    }
    return ret;
}

/**
 * Eliminate any OT_BOOLPLACE self-loops that remain behind in this face.
 */
void
nmg_sanitize_fu(struct faceuse *fu)
{
    struct loopuse *lu;
    struct loopuse *lunext;

    if (!fu)
	return;

    NMG_CK_FACEUSE(fu);

    lu = BU_LIST_FIRST(loopuse, &fu->lu_hd);
    while (BU_LIST_NOT_HEAD(lu, &fu->lu_hd)) {
	NMG_CK_LOOPUSE(lu);
	lunext = BU_LIST_PNEXT(loopuse, lu);
	if (lu->orientation == OT_BOOLPLACE) {
	    if (nmg_klu(lu)) {
		bu_log("nmg_sanitize_fu() nmg_klu() emptied face\n");
		break;
	    }
	}
	lu = lunext;
    }
}

/**
 * Set up nmg_ray_state structure.
 * "left" is a vector that lies in the plane of the face
 * which contains the loops being operated on.
 */
static void
nmg_face_rs_init(struct nmg_ray_state *rs, struct bu_ptbl *b, struct faceuse *fu1, struct faceuse *fu2, fastf_t *pt, fastf_t *dir, struct edge_g_lseg *eg, const struct bn_tol *tol)
{
    plane_t n1;

    if (!rs || !b || !fu1 || !fu2 || !pt || !dir || !tol)
	return;

    BN_CK_TOL(tol);
    BU_CK_PTBL(b);
    NMG_CK_FACEUSE(fu1);
    NMG_CK_FACEUSE(fu2);
    if (eg) NMG_CK_EDGE_G_LSEG(eg);

    memset((void *)rs, 0, sizeof(*rs));

    rs->magic = NMG_RAYSTATE_MAGIC;
    rs->tol = tol;
    rs->vu = (struct vertexuse **)b->buffer;
    rs->nvu = (int)b->end;
    rs->eg_p = eg;
    rs->sA = fu1->s_p;
    rs->sB = fu2->s_p;
    rs->fu1 = fu1;
    rs->fu2 = fu2;
    VMOVE(rs->pt, pt);
    VMOVE(rs->dir, dir);
    NMG_GET_FU_PLANE(n1, fu1);
    VCROSS(rs->left, n1, dir);
    if (MAGSQ(rs->left) > SMALL_FASTF) {
	VUNITIZE(rs->left);
    } else {
	VSET(rs->left, 0.0, 0.0, 1.0);
    }
    switch (fu1->orientation) {
	case OT_SAME:
	    break;
	case OT_OPPOSITE:
	    VREVERSE(rs->left, rs->left);
	    break;
	default:
	    bu_log("nmg_face_rs_init: bad orientation %d\n", fu1->orientation);
	    break;
    }
    if (nmg_debug&NMG_DEBUG_FCUT) {
	struct loopuse *lu;
	struct edgeuse *eu;
	struct vertexuse *vu;
	size_t i;

	bu_log("\tfu->orientation=%s\n", nmg_orientation(fu1->orientation));
	HPRINT("\tfg N", n1);
	VPRINT("\t  pt", pt);
	VPRINT("\t dir", dir);
	VPRINT("\tleft", rs->left);
	bu_log("\tvertexuses in fu that are on lintersect line:\n");
	for (i = 0; i < BU_PTBL_LEN(b); i++) {
	    vu = (struct vertexuse *)BU_PTBL_GET(b, i);
	    if (vu) nmg_pr_vu_briefly(vu, "\t  ");
	}
	bu_log("\tLoopuse in fu (%p):\n", (void *)fu1);
	for (BU_LIST_FOR(lu, loopuse, &fu1->lu_hd)) {
	    bu_log("\tLOOPUSE %p:\n", (void *)lu);
	    if (BU_LIST_FIRST_MAGIC(&lu->down_hd) == NMG_VERTEXUSE_MAGIC) {
		vu = BU_LIST_FIRST(vertexuse, &lu->down_hd);
		if (vu) nmg_pr_vu_briefly(vu, "\tVertex Loop: ");
	    } else {
		for (BU_LIST_FOR(eu, edgeuse, &lu->down_hd)) {
		    struct edgeuse *eu_next;
		    vect_t eu_dir;
		    fastf_t eu_len;
		    double inv_len;

		    nmg_pr_eu_briefly(eu, "\t\t");
		    eu_next = BU_LIST_PNEXT_CIRC(edgeuse, eu);
		    if (eu_next && eu_next->vu_p && eu_next->vu_p->v_p && eu_next->vu_p->v_p->vg_p &&
			eu->vu_p && eu->vu_p->v_p && eu->vu_p->v_p->vg_p) {
			VSUB2(eu_dir, eu_next->vu_p->v_p->vg_p->coord, eu->vu_p->v_p->vg_p->coord);
			eu_len = MAGNITUDE(eu_dir);
			if (eu_len < VDIVIDE_TOL)
			    inv_len = 0.0;
			else
			    inv_len = 1.0/eu_len;
			for (i = 0; i < 3; i++)
			    eu_dir[i] = eu_dir[i] * inv_len;
			bu_log("\t\t\teu_dir = (%g, %g, %g), length = %g\n", V3ARGS(eu_dir), eu_len);
		    }
		}
	    }
	}
    }
    rs->state = NMG_STATE_OUT;

    /* For measuring angle CCW around plane from -dir */
    VREVERSE(rs->ang_x_dir, dir);
    VREVERSE(rs->ang_y_dir, rs->left);
}

static void
free_cuts_ptbl(struct bu_ptbl *cuts)
{
    size_t k;
    if (!cuts) return;
    for (k = 0; k < BU_PTBL_LEN(cuts); k++) {
	struct loop_cuts *lcut = (struct loop_cuts *)BU_PTBL_GET(cuts, k);
	if (lcut) bu_free(lcut, "loop_cuts");
    }
    bu_ptbl_free(cuts);
    bu_free(cuts, "cuts");
}

static struct bu_ptbl *
find_loop_to_cut(int *index1, int *index2, size_t prior_start, size_t prior_end, size_t next_start, size_t next_end, fastf_t *mid_pt, struct nmg_ray_state *rs, struct bu_list *vlfree)
{
    struct loopuse *lu1, *lu2;
    struct vertexuse *vu1 = (struct vertexuse *)NULL;
    struct vertexuse *vu2 = (struct vertexuse *)NULL;
    struct loopuse *match_lu = (struct loopuse *)NULL;
    struct loopuse *prior_lu, *next_lu;
    struct bu_ptbl *cuts = (struct bu_ptbl *)NULL;
    struct loop_cuts *lcut;
    int count = 0;
    size_t i, j, k;
    int done = 0;

    if (!index1 || !index2 || !mid_pt || !rs || !rs->vu || rs->nvu <= 0)
	return (struct bu_ptbl *)NULL;

    if (prior_end > (size_t)rs->nvu || next_end > (size_t)rs->nvu)
	return (struct bu_ptbl *)NULL;

    if (nmg_debug&NMG_DEBUG_FCUT)
	bu_log("find_loop_to_cut: prior_start=%zu, prior_end=%zu, next_start=%zu, next_end=%zu, rs=%p\n",
	       prior_start, prior_end, next_start, next_end, (void *)rs);

    NMG_CK_RAYSTATE(rs);

    /* check if any coincident VU's can give us a loop to cut */
    while (!done) {
	done = 1;
	count++;
	if (count > 100) {
	    bu_log("find_loop_to_cut: iteration limit reached\n");
	    bu_log("prior_start = %zu, prior_end = %zu, next_start = %zu next_end = %zu\n",
		   prior_start, prior_end, next_start, next_end);
	    bu_log("mid_point = (%f %f %f)\n", V3ARGS(mid_pt));
	    for (i = 0; i < (size_t)rs->nvu; i++)
		bu_log("\t%zu %p\n", i, (void *)rs->vu[i]);
	    free_cuts_ptbl(cuts);
	    return (struct bu_ptbl *)NULL;
	}
	for (i = prior_start; i < prior_end; i++) {
	    int class_pt;

	    if (!rs->vu[i]) continue;
	    prior_lu = nmg_find_lu_of_vu(rs->vu[i]);
	    if (!prior_lu) continue;
	    class_pt = NMG_CLASS_Unknown;
	    for (j = next_start; j < next_end; j++) {
		if (!rs->vu[j]) continue;
		next_lu = nmg_find_lu_of_vu(rs->vu[j]);
		if (!next_lu) continue;
		if (prior_lu == next_lu) {
		    int class_lu;

		    if (!match_lu) {
			match_lu = next_lu;
			NMG_ALLOC(cuts, struct bu_ptbl);
			bu_ptbl_init(cuts, 64, " cuts");
			NMG_ALLOC(lcut, struct loop_cuts);
			lcut->lu = match_lu;
			lcut->vu1 = (struct vertexuse *)NULL;
			lcut->vu2 = (struct vertexuse *)NULL;
			bu_ptbl_ins(cuts, (long *)lcut);
			continue;
		    }

		    if (match_lu == next_lu)
			continue;

		    if (class_pt == NMG_CLASS_Unknown) {
			class_pt = nmg_class_pnt_lu_except(mid_pt,
							  match_lu, (struct edge *)NULL, vlfree, rs->tol);
			if (match_lu->orientation == OT_OPPOSITE) {
			    if (class_pt == NMG_CLASS_AinB)
				class_pt = NMG_CLASS_AoutB;
			    else if (class_pt == NMG_CLASS_AoutB)
				class_pt = NMG_CLASS_AinB;
			}
		    }
		    class_lu = nmg_classify_lu_lu(next_lu, match_lu, vlfree, rs->tol);

		    if (class_lu == class_pt ||
			class_lu == NMG_CLASS_AonBshared) {
			int found = 0;

			match_lu = next_lu;
			for (k = 0; k < BU_PTBL_LEN(cuts); k++) {
			    lcut = (struct loop_cuts *)BU_PTBL_GET(cuts, k);
			    if (lcut && lcut->lu == match_lu) {
				found = 1;
				break;
			    }
			}
			if (!found) {
			    done = 0;
			    NMG_ALLOC(lcut, struct loop_cuts);
			    lcut->lu = match_lu;
			    lcut->vu1 = (struct vertexuse *)NULL;
			    lcut->vu2 = (struct vertexuse *)NULL;
			    bu_ptbl_ins(cuts, (long *)lcut);
			}
		    }
		}
	    }
	}
    }

    if (cuts) {
	for (k = 0; k < BU_PTBL_LEN(cuts); k++) {
	    lcut = (struct loop_cuts *)BU_PTBL_GET(cuts, k);
	    if (!lcut) continue;
	    match_lu = lcut->lu;

	    if (nmg_debug&NMG_DEBUG_FCUT)
		bu_log("\tfind_loop_to_cut: matching lu's = %p\n", (void *)match_lu);
	    lu1 = match_lu;
	    for (i = prior_start; i < prior_end; i++) {
		if (rs->vu[i] && nmg_find_lu_of_vu(rs->vu[i]) == lu1) {
		    *index1 = (int)i;
		    vu1 = rs->vu[i];
		    lcut->vu1 = vu1;
		    break;
		}
	    }
	    lu2 = match_lu;
	    for (i = next_start; i < next_end; i++) {
		if (rs->vu[i] && nmg_find_lu_of_vu(rs->vu[i]) == lu2) {
		    *index2 = (int)i;
		    vu2 = rs->vu[i];
		    lcut->vu2 = vu2;
		    break;
		}
	    }
	}
    } else {
	if (nmg_debug&NMG_DEBUG_FCUT)
	    bu_log("\tfind_loop_to_cut returning 0\n");
	return (struct bu_ptbl *)NULL;
    }

    for (k = 0; k < BU_PTBL_LEN(cuts); k++) {
	lcut = (struct loop_cuts *)BU_PTBL_GET(cuts, k);
	if (!lcut || !lcut->lu) continue;
	lu1 = lcut->lu;
	lu2 = lu1;

	/* Check if there is more than one VU from lu2 */
	count = 0;
	for (i = next_start; i < next_end; i++) {
	    if (rs->vu[i] && nmg_find_lu_of_vu(rs->vu[i]) == lu2)
		count++;
	}

	if (count > 1 && vu1 && vu2 && vu1->v_p && vu1->v_p->vg_p && vu2->v_p && vu2->v_p->vg_p) {
	    struct vertexuse *vu_best;
	    fastf_t vu_angle;
	    vect_t x_dir, y_dir;
	    vect_t norm;

	    if (nmg_debug&NMG_DEBUG_FCUT)
		bu_log("\tfind_loop_to_cut: %d VU's from lu %p\n", count, (void *)lu2);

	    /* need to select correct VU */
	    vu_angle = (-M_PI);
	    vu_best = (struct vertexuse *)NULL;

	    VSUB2(x_dir, vu2->v_p->vg_p->coord, vu1->v_p->vg_p->coord);
	    if (MAGSQ(x_dir) > SMALL_FASTF) {
		VUNITIZE(x_dir);
	    } else {
		VSET(x_dir, 1.0, 0.0, 0.0);
	    }
	    NMG_GET_FU_NORMAL(norm, rs->fu1);

	    VCROSS(y_dir, norm, x_dir);
	    if (MAGSQ(y_dir) > SMALL_FASTF) {
		VUNITIZE(y_dir);
	    } else {
		VSET(y_dir, 0.0, 1.0, 0.0);
	    }

	    if (nmg_debug&NMG_DEBUG_FCUT)
		bu_log("\tx_dir=(%g %g %g), y_dir=(%g %g %g)\n",
		       V3ARGS(x_dir), V3ARGS(y_dir));

	    for (i = next_start; i < next_end; i++) {
		struct edgeuse *eu;
		fastf_t angle;
		vect_t eu_dir;

		if (!rs->vu[i] || nmg_find_lu_of_vu(rs->vu[i]) != lu2)
		    continue;

		if (!rs->vu[i]->up.magic_p || *(rs->vu[i]->up.magic_p) != NMG_EDGEUSE_MAGIC) {
		    bu_log("nmg_fcut_face: VU (%p) is not from an EU\n", (void *)rs->vu[i]);
		    continue;
		}

		/* calculate angle this EU will make with edgeuse
		 * that will be created by the cut/join
		 */
		eu = rs->vu[i]->up.eu_p;
		if (!eu || !eu->eumate_p || !eu->eumate_p->vu_p || !eu->eumate_p->vu_p->v_p ||
		    !eu->eumate_p->vu_p->v_p->vg_p || !eu->vu_p || !eu->vu_p->v_p || !eu->vu_p->v_p->vg_p)
		    continue;

		VSUB2(eu_dir, eu->eumate_p->vu_p->v_p->vg_p->coord, eu->vu_p->v_p->vg_p->coord);
		angle = atan2(VDOT(y_dir, eu_dir), VDOT(x_dir, eu_dir));

		if (nmg_debug&NMG_DEBUG_FCUT)
		    bu_log("\tangle for eu %p (vu=%p, #%zu) is %g\n",
			   (void *)eu, (void *)rs->vu[i], i, angle);

		/* select max angle */
		if (angle > vu_angle) {
		    if (nmg_debug&NMG_DEBUG_FCUT)
			bu_log("\t\tabove is the new best VU\n");
		    vu_angle = angle;
		    vu_best = rs->vu[i];
		    *index2 = (int)i;
		}
	    }

	    if (vu_best) {
		vu2 = vu_best;
		lcut->vu2 = vu2;
	    }

	    if (nmg_debug&NMG_DEBUG_FCUT)
		bu_log("\tfind_loop_to_cut: selecting VU2 %p\n", (void *)vu2);
	}

	/* Check for duplicate cuts (cutting two different loops across same two vertices) */
	if (BU_PTBL_LEN(cuts) > 1) {
	    for (i = 0; i < BU_PTBL_LEN(cuts); i++) {
		struct loop_cuts *lcut1, *lcut2;
		int class1, class2;

		lcut1 = (struct loop_cuts *)BU_PTBL_GET(cuts, i);
		if (!lcut1 || !lcut1->vu1 || !lcut1->vu2 || !lcut1->vu1->v_p || !lcut1->vu2->v_p)
		    continue;
		for (j = i + 1; j < BU_PTBL_LEN(cuts); j++) {
		    lcut2 = (struct loop_cuts *)BU_PTBL_GET(cuts, j);
		    if (!lcut2 || !lcut2->vu1 || !lcut2->vu2 || !lcut2->vu1->v_p || !lcut2->vu2->v_p)
			continue;

		    if (lcut1->vu1->v_p != lcut2->vu1->v_p ||
			lcut1->vu2->v_p != lcut2->vu2->v_p)
			continue;

		    /* lcut1 and lcut2 are the same cut, choose one loop to cut */
		    if (lcut1->lu && lcut1->lu->orientation == OT_OPPOSITE &&
			lcut2->lu && lcut2->lu->orientation == OT_OPPOSITE)
		    {
			bu_log("find_loop_to_cut: Two OT_OPPOSITE loops to be cut: %p and %p\n",
			       (void *)lcut1->lu, (void *)lcut2->lu);
		    }
		    if (lcut1->lu && lcut1->lu->orientation == OT_OPPOSITE) {
			/* don't cut an OT_OPPOSITE loop */
			bu_ptbl_rm(cuts, (long *)lcut1);
			bu_free(lcut1, "loop_cuts");
			break;
		    }
		    if (lcut2->lu && lcut2->lu->orientation == OT_OPPOSITE) {
			/* don't cut an OT_OPPOSITE loop */
			bu_ptbl_rm(cuts, (long *)lcut2);
			bu_free(lcut2, "loop_cuts");
			break;
		    }
		    class1 = lcut1->lu ? nmg_class_pnt_lu_except(mid_pt, lcut1->lu,
								  (struct edge *)NULL, vlfree, rs->tol) : NMG_CLASS_AoutB;
		    class2 = lcut2->lu ? nmg_class_pnt_lu_except(mid_pt, lcut2->lu,
								  (struct edge *)NULL, vlfree, rs->tol) : NMG_CLASS_AoutB;

		    if (class1 == NMG_CLASS_AoutB && class2 == NMG_CLASS_AoutB) {
			bu_log("find_loop_to_cut: mid point is outside both loops: %p and %p pt=(%g %g %g)\n",
			       (void *)lcut1->lu, (void *)lcut2->lu, V3ARGS(mid_pt));
		    }

		    if (class1 == NMG_CLASS_AoutB) {
			/* Don't cut this loop (cut is outside loop) */
			bu_ptbl_rm(cuts, (long *)lcut1);
			bu_free(lcut1, "loop_cuts");
			break;
		    }
		    if (class2 == NMG_CLASS_AoutB) {
			/* Don't cut this loop (cut is outside loop) */
			bu_ptbl_rm(cuts, (long *)lcut2);
			bu_free(lcut2, "loop_cuts");
			break;
		    }
		}
	    }
	}

	/* Check if there is more than one VU from lu1 */
	count = 0;
	for (i = prior_start; i < prior_end; i++) {
	    if (rs->vu[i] && nmg_find_lu_of_vu(rs->vu[i]) == lu1)
		count++;
	}

	if (count > 1 && vu1 && vu2 && vu1->v_p && vu1->v_p->vg_p && vu2->v_p && vu2->v_p->vg_p) {
	    struct vertexuse *vu_best;
	    fastf_t vu_angle;
	    vect_t x_dir, y_dir;
	    vect_t norm;

	    if (nmg_debug&NMG_DEBUG_FCUT)
		bu_log("\tfind_loop_to_cut: %d VU's from lu %p\n", count, (void *)lu1);

	    /* need to select correct VU */
	    vu_angle = (-M_PI);
	    vu_best = (struct vertexuse *)NULL;

	    VSUB2(x_dir, vu1->v_p->vg_p->coord, vu2->v_p->vg_p->coord);
	    if (MAGSQ(x_dir) > SMALL_FASTF) {
		VUNITIZE(x_dir);
	    } else {
		VSET(x_dir, 1.0, 0.0, 0.0);
	    }
	    NMG_GET_FU_NORMAL(norm, rs->fu1);

	    VCROSS(y_dir, norm, x_dir);
	    if (MAGSQ(y_dir) > SMALL_FASTF) {
		VUNITIZE(y_dir);
	    } else {
		VSET(y_dir, 0.0, 1.0, 0.0);
	    }

	    if (nmg_debug&NMG_DEBUG_FCUT)
		bu_log("\tx_dir=(%g %g %g), y_dir=(%g %g %g)\n",
		       V3ARGS(x_dir), V3ARGS(y_dir));

	    for (i = prior_start; i < prior_end; i++) {
		struct edgeuse *eu;
		fastf_t angle;
		vect_t eu_dir;

		if (!rs->vu[i] || nmg_find_lu_of_vu(rs->vu[i]) != lu1)
		    continue;

		if (!rs->vu[i]->up.magic_p || *(rs->vu[i]->up.magic_p) != NMG_EDGEUSE_MAGIC) {
		    bu_log("nmg_fcut_face: VU (%p) is not from an EU\n", (void *)rs->vu[i]);
		    continue;
		}

		/* calculate angle this EU will make with edgeuse
		 * that will be created by the cut/join
		 */
		eu = rs->vu[i]->up.eu_p;
		if (!eu || !eu->eumate_p || !eu->eumate_p->vu_p || !eu->eumate_p->vu_p->v_p ||
		    !eu->eumate_p->vu_p->v_p->vg_p || !eu->vu_p || !eu->vu_p->v_p || !eu->vu_p->v_p->vg_p)
		    continue;

		VSUB2(eu_dir, eu->eumate_p->vu_p->v_p->vg_p->coord, eu->vu_p->v_p->vg_p->coord);
		angle = atan2(VDOT(y_dir, eu_dir), VDOT(x_dir, eu_dir));

		if (nmg_debug&NMG_DEBUG_FCUT)
		    bu_log("\tangle for eu %p (vu=%p, #%zu) is %g\n",
			   (void *)eu, (void *)rs->vu[i], i, angle);

		/* select max angle */
		if (angle > vu_angle) {
		    if (nmg_debug&NMG_DEBUG_FCUT)
			bu_log("\t\tabove is the new best VU\n");
		    vu_angle = angle;
		    vu_best = rs->vu[i];
		    *index1 = (int)i;
		}
	    }

	    if (vu_best) {
		vu1 = vu_best;
		lcut->vu1 = vu1;
	    }

	    if (nmg_debug&NMG_DEBUG_FCUT)
		bu_log("\tfind_loop_to_cut: selecting VU1 %p\n", (void *)vu1);
	}
    }

    if (nmg_debug&NMG_DEBUG_FCUT)
	bu_log("\tfind_loop_to_cut: returning %zu cuts (index1=%d, index2=%d)\n",
	       BU_PTBL_LEN(cuts), *index1, *index2);

    return cuts;
}

static fastf_t
nmg_eu_angle(struct edgeuse *eu, struct vertex *vp)
{
    struct faceuse *fu;
    struct vertex_g *vg1, *vg2;
    vect_t norm;
    vect_t x_dir;
    vect_t y_dir;
    vect_t eu_dir;
    fastf_t angle;

    if (!eu || !vp || !vp->vg_p)
	return 0.0;

    NMG_CK_EDGEUSE(eu);
    NMG_CK_VERTEX(vp);

    fu = nmg_find_fu_of_eu(eu);
    if (!fu)
	return 0.0;
    NMG_CK_FACEUSE(fu);
    NMG_GET_FU_NORMAL(norm, fu);

    if (!eu->vu_p || !eu->vu_p->v_p || !eu->vu_p->v_p->vg_p ||
	!eu->eumate_p || !eu->eumate_p->vu_p || !eu->eumate_p->vu_p->v_p || !eu->eumate_p->vu_p->v_p->vg_p)
	return 0.0;

    vg1 = eu->vu_p->v_p->vg_p;
    vg2 = eu->eumate_p->vu_p->v_p->vg_p;

    VSUB2(x_dir, vg1->coord, vp->vg_p->coord);
    if (MAGSQ(x_dir) <= SMALL_FASTF)
	return 0.0;
    VUNITIZE(x_dir);

    VCROSS(y_dir, norm, x_dir);
    if (MAGSQ(y_dir) <= SMALL_FASTF)
	return 0.0;
    VUNITIZE(y_dir);

    VSUB2(eu_dir, vg2->coord, vg1->coord);
    angle = atan2(VDOT(eu_dir, y_dir), VDOT(eu_dir, x_dir));

    return angle;
}

static int
find_best_vu(int start, int end, struct vertex *other_vp, struct nmg_ray_state *rs, struct bu_list *vlfree)
{
    struct edgeuse *eu;
    struct vertexuse *best_vu;
    struct loopuse *best_lu;
    fastf_t best_angle;
    int best_index;
    int other_is_in_best = -42;
    int nmg_class;
    int i;

    if (!other_vp || !other_vp->vg_p || !rs || !rs->vu || start < 0 || end <= start || end > rs->nvu)
	return start >= 0 ? start : 0;

    if (nmg_debug&NMG_DEBUG_FCUT)
	bu_log("find_best_vu: start=%d, end=%d, other_vp=%p, rs=%p\n",
	       start, end, (void *)other_vp, (void *)rs);

    NMG_CK_VERTEX(other_vp);
    NMG_CK_RAYSTATE(rs);

    if (start == end - 1) {
	if (nmg_debug&NMG_DEBUG_FCUT)
	    bu_log("\tfind_best_vu returning %d\n", start);

	return start;
    }

    best_vu = rs->vu[start];
    if (!best_vu)
	return start;

    best_lu = nmg_find_lu_of_vu(best_vu);
    if (!best_lu)
	return start;

    best_index = start;
    if (best_vu->up.magic_p && *best_vu->up.magic_p == NMG_EDGEUSE_MAGIC) {
	eu = best_vu->up.eu_p;
	best_angle = nmg_eu_angle(eu, other_vp);
    } else {
	best_angle = -M_PI;
    }

    if (BU_LIST_FIRST_MAGIC(&best_lu->down_hd) == NMG_VERTEXUSE_MAGIC) {
	other_is_in_best = 0;
    } else if (nmg_loop_is_a_crack(best_lu)) {
	other_is_in_best = 0;
    } else {
	nmg_class = nmg_class_pnt_lu_except(other_vp->vg_p->coord, best_lu, (struct edge *)NULL, vlfree, rs->tol);

	if ((nmg_class == NMG_CLASS_AinB && best_lu->orientation == OT_SAME) ||
	    (nmg_class == NMG_CLASS_AoutB && best_lu->orientation == OT_OPPOSITE))
	    other_is_in_best = 1;
	else if (nmg_class == NMG_CLASS_AonBshared) {
	    bu_log("find_best_vu: loop on shared boundary, lu=%p\n", (void *)best_lu);
	    other_is_in_best = 1;
	} else
	    other_is_in_best = 0;
    }

    if (nmg_debug&NMG_DEBUG_FCUT)
	bu_log("\tfind_best_vu: first choice is index=%d, vu=%p, lu=%p, other_is_in_best=%d\n",
	       best_index, (void *)best_vu, (void *)best_lu, other_is_in_best);

    for (i = start + 1; i < end; i++) {
	struct loopuse *lu;

	if (!rs->vu[i]) continue;
	lu = nmg_find_lu_of_vu(rs->vu[i]);
	if (!lu) continue;

	if (lu != best_lu) {
	    nmg_class = nmg_classify_lu_lu(lu, best_lu, vlfree, rs->tol);
	    if (nmg_debug&NMG_DEBUG_FCUT) {
		bu_log("lu %p is %s\n", (void *)lu, nmg_orientation(lu->orientation));
		bu_log("best_lu %p is %s\n", (void *)best_lu, nmg_orientation(best_lu->orientation));
		bu_log("lu %p is %s w.r.t lu %p\n",
		       (void *)lu, nmg_class_name(nmg_class), (void *)best_lu);
	    }

	    if (other_is_in_best) {
		if (nmg_class == NMG_CLASS_AinB || (nmg_class == NMG_CLASS_AonBshared && lu->orientation == OT_SAME)) {
		    best_vu = rs->vu[i];
		    best_lu = lu;
		    best_index = i;

		    if (nmg_debug&NMG_DEBUG_FCUT)
			bu_log("\tfind_best_vu: better choice (inside) - index=%d, vu=%p, lu=%p, other_is_in_best=%d\n",
			       best_index, (void *)best_vu, (void *)best_lu, other_is_in_best);
		}
	    } else {
		if (nmg_class == NMG_CLASS_AoutB || (nmg_class == NMG_CLASS_AonBshared && lu->orientation == OT_OPPOSITE)) {
		    best_vu = rs->vu[i];
		    best_lu = lu;
		    best_index = i;

		    if (BU_LIST_FIRST_MAGIC(&best_lu->down_hd) == NMG_VERTEXUSE_MAGIC) {
			other_is_in_best = 0;
		    } else if (nmg_loop_is_a_crack(best_lu)) {
			other_is_in_best = 0;
		    } else {
			nmg_class = nmg_class_pnt_lu_except(other_vp->vg_p->coord,
							   best_lu, (struct edge *)NULL, vlfree, rs->tol);

			if ((nmg_class == NMG_CLASS_AinB && best_lu->orientation == OT_SAME) ||
			    (nmg_class == NMG_CLASS_AoutB && best_lu->orientation == OT_OPPOSITE))
			    other_is_in_best = 1;
			else if (nmg_class == NMG_CLASS_AonBshared) {
			    bu_log("find_best_vu: loop on shared boundary, lu=%p\n", (void *)best_lu);
			    other_is_in_best = 1;
			} else
			    other_is_in_best = 0;
		    }
		    if (nmg_debug&NMG_DEBUG_FCUT)
			bu_log("\tfind_best_vu: better choice (outside) - index=%d, vu=%p, lu=%p, other_is_in_best=%d\n",
			       best_index, (void *)best_vu, (void *)best_lu, other_is_in_best);
		}
	    }
	} else if (rs->vu[i]->up.magic_p && *rs->vu[i]->up.magic_p == NMG_EDGEUSE_MAGIC) {
	    fastf_t angle;

	    eu = rs->vu[i]->up.eu_p;
	    NMG_CK_EDGEUSE(eu);
	    angle = nmg_eu_angle(eu, other_vp);

	    if (nmg_debug&NMG_DEBUG_FCUT)
		bu_log("best_angle = %f, eu=%p, eu_angle=%f\n",
		       best_angle, (void *)eu, angle);
	    if (angle > best_angle) {
		best_angle = angle;
		best_vu = rs->vu[i];
		best_lu = nmg_find_lu_of_vu(best_vu);
		best_index = i;
	    }
	}
    }

    return best_index;
}

static void
#if PLOT_BOTH_FACES
nmg_fcut_face(struct nmg_ray_state *rs, struct bu_list *vlfree)
#else
nmg_fcut_face(struct nmg_ray_state *rs, struct bu_list *UNUSED(vlfree))
#endif
{
    int cur;
    struct vertexuse *vu1, *vu2;
    struct loopuse *new_lu;
    struct edgeuse *new_eu1;
    struct edgeuse *old_eu;
    struct vertex *prev_v;
    struct bu_ptbl *cuts;
    size_t prior_start;
    size_t cut_no;

    if (!rs || !rs->vu || rs->nvu < 2 || !rs->fu1 || !rs->fu2 || !rs->tol)
	return;

    NMG_CK_RAYSTATE(rs);
    BN_CK_TOL(rs->tol);

    if (nmg_debug&NMG_DEBUG_FCUT)
	bu_log("nmg_face_combine()\n");

    if (rs->eg_p) NMG_CK_EDGE_G_LSEG(rs->eg_p);

#if PLOT_BOTH_FACES
    nmg_2face_plot(rs->fu1, rs->fu2, vlfree);
#else
    nmg_face_plot(rs->fu1);
    nmg_face_plot(rs->fu2);
#endif

    if (nmg_debug&NMG_DEBUG_FCUT) {
	bu_log("rs->fu1 = %p\n", (void *)rs->fu1);
	bu_log("rs->fu2 = %p\n", (void *)rs->fu2);
	nmg_pr_fu_briefly(rs->fu1, "");
	bu_log("%d vertices on intersect line\n", rs->nvu);
	for (cur = 0; cur < rs->nvu; cur++) {
	    if (!rs->vu[cur]) continue;
	    bu_log("\tvu=%p, v=%p, lu=%p\n",
		   (void *)rs->vu[cur], (void *)rs->vu[cur]->v_p, (void *)nmg_find_lu_of_vu(rs->vu[cur]));
	}
    }

    prev_v = (struct vertex *)NULL;
    prior_start = 0;
    while (1) {
	struct edgeuse *eu_tmp;
	struct loopuse *lu1 = (struct loopuse *)NULL;
	struct loopuse *lu2 = (struct loopuse *)NULL;
	point_t mid_pt;
	int nmg_class;
	int orient1 = 0;
	int orient2 = 0;
	size_t prior_end;
	size_t next_start, next_end;
	size_t i;
	int index1 = 0, index2 = 0;

	while (prior_start < (size_t)rs->nvu && rs->vu[prior_start] && rs->vu[prior_start]->v_p == prev_v)
	    prior_start++;

	if (prior_start >= (size_t)rs->nvu || !rs->vu[prior_start])
	    break;

	vu1 = rs->vu[prior_start];
	prior_end = prior_start;

	prev_v = vu1->v_p;

	while (++prior_end < (size_t)rs->nvu && rs->vu[prior_end] && rs->vu[prior_end]->v_p == vu1->v_p)
	    ;

	next_start = prior_end;

	if (next_start >= (size_t)rs->nvu || !rs->vu[next_start])
	    break;		/* all done */

	vu2 = rs->vu[next_start];
	next_end = next_start;
	while (++next_end < (size_t)rs->nvu && rs->vu[next_end] && rs->vu[next_end]->v_p == vu2->v_p)
	    ;

	if (nmg_debug&NMG_DEBUG_FCUT) {
	    bu_log("rs->fu1 = %p\n", (void *)rs->fu1);
	    bu_log("rs->fu2 = %p\n", (void *)rs->fu2);
	    bu_log("prior_start=%zu, prior_end=%zu, next_start=%zu, next_end=%zu\n",
		   prior_start, prior_end, next_start, next_end);
	    bu_log("%d vertices on intersect line\n", rs->nvu);
	    for (cur = 0; cur < rs->nvu; cur++) {
		if (!rs->vu[cur]) continue;
		bu_log("\tvu=%p, v=%p, lu=%p\n",
		       (void *)rs->vu[cur], (void *)rs->vu[cur]->v_p, (void *)nmg_find_lu_of_vu(rs->vu[cur]));
	    }
	}

	if (!vu1->v_p || !vu2->v_p || !vu1->v_p->vg_p || !vu2->v_p->vg_p)
	    continue;

	/* look for an EU in this face that connects vu1 and vu2 */
	if (nmg_find_eu_in_face(vu1->v_p, vu2->v_p, rs->fu1,
				(struct edgeuse *)NULL, 0))
	{
	    if (nmg_debug&NMG_DEBUG_FCUT)
		bu_log("Already an edge here\n");
	    continue;
	}

	if (rs->fu1->fumate_p &&
	    nmg_find_eu_in_face(vu1->v_p, vu2->v_p, rs->fu1->fumate_p,
				(struct edgeuse *)NULL, 0))
	{
	    if (nmg_debug&NMG_DEBUG_FCUT)
		bu_log("Already an edge here\n");
	    continue;
	}

	/* we know that fu1 does not contain an edge connecting vu1 and vu2,
	 * Now check if the midpoint is inside or outside fu1.
	 */
	VAVERAGE(mid_pt, vu1->v_p->vg_p->coord, vu2->v_p->vg_p->coord);
	nmg_class = nmg_class_pnt_fu_except(mid_pt, rs->fu1, NULL,
					   NULL, NULL, NULL, 0, 1, vlfree, rs->tol);

	if (nmg_debug&NMG_DEBUG_FCUT) {
	    bu_log("vu1=%p (%g %g %g), vu2=%p (%g %g %g)\n",
		   (void *)vu1, V3ARGS(vu1->v_p->vg_p->coord),
		   (void *)vu2, V3ARGS(vu2->v_p->vg_p->coord));
	    bu_log("\tmid_pt = (%g %g %g)\n", V3ARGS(mid_pt));
	    bu_log("class for mid point is %s\n", nmg_class_name(nmg_class));
	}

	if (nmg_class == NMG_CLASS_AoutB)
	    continue;

	/* Check if mid-point is in fu2. If fu2 is disjoint loops, this point
	 * may be outside fu2, and we don't want to cut fu1 here.
	 */
	nmg_class = nmg_class_pnt_fu_except(mid_pt, rs->fu2, NULL,
					   NULL, NULL, NULL, 0, 0, vlfree, rs->tol);

	if (nmg_class == NMG_CLASS_AoutB)
	    continue;

	/* See if there is an edge joining the 2 vertices already, this
	 * will be used for radial join later */
	old_eu = nmg_findeu(vu1->v_p, vu2->v_p, (struct shell *)NULL,
			    (struct edgeuse *)NULL, 0);

	cuts = find_loop_to_cut(&index1, &index2, prior_start, prior_end,
				next_start, next_end, mid_pt, rs, vlfree);
	if (cuts == (struct bu_ptbl *)NULL) {
	    index1 = find_best_vu((int)prior_start, (int)prior_end, vu2->v_p, rs, vlfree);
	    index2 = find_best_vu((int)next_start, (int)next_end, vu1->v_p, rs, vlfree);
	    vu1 = rs->vu[index1];
	    vu2 = rs->vu[index2];
	    if (!vu1 || !vu2) continue;
	    lu1 = nmg_find_lu_of_vu(vu1);
	    lu2 = nmg_find_lu_of_vu(vu2);
	    if (!lu1 || !lu2) continue;
	    orient1 = lu1->orientation;
	    orient2 = lu2->orientation;
	}

	if (cuts) {
	    for (cut_no = 0; cut_no < BU_PTBL_LEN(cuts); cut_no++) {
		struct loop_cuts *lcut;

		if (nmg_debug&NMG_DEBUG_FCUT)
		    bu_log("\tcut loop (#%zu of %zu)\n", cut_no, BU_PTBL_LEN(cuts));

		lcut = (struct loop_cuts *)BU_PTBL_GET(cuts, cut_no);
		if (!lcut) continue;

		vu1 = lcut->vu1;
		vu2 = lcut->vu2;
		lu1 = lcut->lu;
		if (!vu1 || !vu2 || !lu1) continue;

		new_lu = nmg_cut_loop(vu1, vu2, vlfree);
		if (!new_lu) continue;
		new_eu1 = BU_LIST_LAST(edgeuse, &new_lu->down_hd);
		if (!new_eu1) continue;

		NMG_CK_EDGEUSE(new_eu1);

		/* make intersection edges real */
		if (new_eu1->e_p) new_eu1->e_p->is_real = 1;

		nmg_loop_a(lu1->l_p, rs->tol);
		nmg_loop_a(new_lu->l_p, rs->tol);

		nmg_lu_reorient(lu1);
		nmg_lu_reorient(new_lu);

		if (nmg_debug&NMG_DEBUG_FCUT) {
		    bu_log("\t\t new_eu = %p\n", (void *)new_eu1);
		    nmg_pr_fu_briefly(rs->fu1, "");
		}
		if (old_eu) nmg_radial_join_eu(old_eu, new_eu1, rs->tol);

		/* A new VU has been added to vu2->v_p, add it to the rs->vu[]
		 * array by overwriting the last use of vu1->v_p
		 */
		eu_tmp = vu1->up.eu_p;
		if (eu_tmp) {
		    eu_tmp = BU_LIST_PPREV_CIRC(edgeuse, &eu_tmp->l);
		    if (eu_tmp && eu_tmp->vu_p && eu_tmp->vu_p->v_p == vu2->v_p) {
			if (prior_end > cut_no) {
			    rs->vu[prior_end - 1 - cut_no] = eu_tmp->vu_p;
			}
		    } else {
			bu_log("nmg_fcut_face: eu (%p) has wrong vertex\n", (void *)eu_tmp);
		    }
		}
	    }
	    free_cuts_ptbl(cuts);
	    continue;
	}

	if (!lu1 || !lu2) continue;

	if (BU_LIST_FIRST_MAGIC(&lu1->down_hd) == NMG_VERTEXUSE_MAGIC &&
	    BU_LIST_FIRST_MAGIC(&lu2->down_hd) == NMG_VERTEXUSE_MAGIC)
	{
	    struct vertexuse *new_vu2;

	    new_vu2 = nmg_join_2singvu_loops(vu1, vu2);
	    if (!new_vu2) continue;
	    new_eu1 = new_vu2->up.eu_p;
	    if (!new_eu1) continue;
	    NMG_CK_EDGEUSE(new_eu1);

	    /* make intersection edges real */
	    if (new_eu1->e_p) new_eu1->e_p->is_real = 1;

	    nmg_loop_a(lu1->l_p, rs->tol);
	    lu1->orientation = OT_SAME;
	    if (lu1->lumate_p) lu1->lumate_p->orientation = OT_SAME;

	    if (nmg_debug&NMG_DEBUG_FCUT) {
		bu_log("\tjoin 2 singvu loops\n");
		nmg_pr_fu_briefly(rs->fu1, "");
	    }
	    if (old_eu) nmg_radial_join_eu(old_eu, new_eu1, rs->tol);

	    /* vu2 has been killed, replace it in rs->vu[] */
	    for (i = next_start; i < next_end; i++) {
		if (rs->vu[i] == vu2) {
		    rs->vu[i] = new_vu2;
		    break;
		}
	    }

	    continue;
	}

	if (BU_LIST_FIRST_MAGIC(&lu1->down_hd) == NMG_VERTEXUSE_MAGIC &&
	    BU_LIST_FIRST_MAGIC(&lu2->down_hd) == NMG_EDGEUSE_MAGIC)
	{
	    struct vertexuse *new_vu1;

	    new_vu1 = nmg_join_singvu_loop(vu2, vu1);
	    if (!new_vu1) continue;
	    new_eu1 = new_vu1->up.eu_p;
	    if (!new_eu1) continue;
	    NMG_CK_EDGEUSE(new_eu1);

	    /* make intersection edges real */
	    if (new_eu1->e_p) new_eu1->e_p->is_real = 1;

	    nmg_loop_a(lu2->l_p, rs->tol);
	    lu2->orientation = orient2;
	    if (lu2->lumate_p) lu2->lumate_p->orientation = orient2;

	    if (nmg_debug&NMG_DEBUG_FCUT) {
		bu_log("\tjoin loops vu1 (%p) is sing vu loop\n", (void *)vu1);
		nmg_pr_fu_briefly(rs->fu1, "");
	    }
	    if (old_eu) nmg_radial_join_eu(old_eu, new_eu1, rs->tol);

	    /* A new VU has been added to vu2->v_p, add it to the rs->vu[]
	     * array by overwriting the last use of vu1->v_p
	     */
	    eu_tmp = new_vu1->up.eu_p;
	    if (eu_tmp) {
		eu_tmp = BU_LIST_PPREV_CIRC(edgeuse, &eu_tmp->l);
		if (eu_tmp && eu_tmp->vu_p && eu_tmp->vu_p->v_p == vu2->v_p) {
		    if (prior_end > 0) rs->vu[prior_end - 1] = eu_tmp->vu_p;
		} else {
		    bu_log("nmg_fcut_face: eu (%p) has wrong vertex\n", (void *)eu_tmp);
		}
	    }
	    continue;
	}

	if (BU_LIST_FIRST_MAGIC(&lu1->down_hd) == NMG_EDGEUSE_MAGIC &&
	    BU_LIST_FIRST_MAGIC(&lu2->down_hd) == NMG_VERTEXUSE_MAGIC)
	{
	    struct vertexuse *new_vu2;

	    new_vu2 = nmg_join_singvu_loop(vu1, vu2);
	    if (!new_vu2) continue;
	    new_eu1 = new_vu2->up.eu_p;
	    if (!new_eu1) continue;
	    NMG_CK_EDGEUSE(new_eu1);

	    /* make intersection edges real */
	    if (new_eu1->e_p) new_eu1->e_p->is_real = 1;

	    nmg_loop_a(lu1->l_p, rs->tol);
	    lu1->orientation = orient1;
	    if (lu1->lumate_p) lu1->lumate_p->orientation = orient1;

	    if (nmg_debug&NMG_DEBUG_FCUT) {
		bu_log("\tjoin loops vu2 (%p) is sing vu loop\n", (void *)vu2);
		nmg_pr_fu_briefly(rs->fu1, "");
	    }

	    if (old_eu) nmg_radial_join_eu(old_eu, new_eu1, rs->tol);

	    /* vu2 has been killed, replace it in rs->vu[] */
	    for (i = next_start; i < next_end; i++) {
		if (rs->vu[i] == vu2) {
		    rs->vu[i] = new_vu2;
		    break;
		}
	    }

	    continue;
	}

	if (lu1 != lu2) {
	    struct vertexuse *new_vu2;

	    new_vu2 = nmg_join_2loops(vu1, vu2);
	    if (!new_vu2) continue;
	    new_eu1 = new_vu2->up.eu_p;
	    if (!new_eu1) continue;
	    NMG_CK_EDGEUSE(new_eu1);

	    /* make intersection edges real */
	    if (new_eu1->e_p) new_eu1->e_p->is_real = 1;

	    nmg_loop_a(lu1->l_p, rs->tol);

	    if (nmg_debug&NMG_DEBUG_FCUT) {
		bu_log("\t join 2 loops\n");
		bu_log("\t\tvu2 (%p) replaced with new_vu2 %p\n", (void *)vu2, (void *)new_vu2);
		nmg_pr_fu_briefly(rs->fu1, "");
	    }

	    if (old_eu) nmg_radial_join_eu(old_eu, new_eu1, rs->tol);

	    /* a new use of vu2->v_p has been created add it to the list by
	     * overwriting a use of the previous vertex
	     */
	    if (prior_end > 0) rs->vu[prior_end - 1] = new_vu2;

	    continue;
	}

	bu_log("WARNING in face cutter: something should have been cut\n");
	bu_log("vu1=%p, vu2=%p\n", (void *)vu1, (void *)vu2);
	nmg_pr_fu_briefly(rs->fu1, "");
	break;
    }

    if (nmg_debug&NMG_DEBUG_FCUT)
	nmg_pr_fu_briefly(rs->fu1, "");
}

/**
 * The main face cut handler.
 * Called from nmg_inter.c by nmg_isect_2faces().
 */
struct edge_g_lseg *
nmg_face_cutjoin(struct bu_ptbl *b1, struct bu_ptbl *b2, fastf_t *mag1, fastf_t *mag2, struct faceuse *fu1, struct faceuse *fu2, point_t pt, vect_t dir, struct edge_g_lseg *eg, struct bu_list *vlfree, const struct bn_tol *tol)
{
    struct vertexuse **vu1, **vu2;
    size_t i;
    struct nmg_ray_state rs1;
    struct nmg_ray_state rs2;
    int retries = 0;

    if (!b1 || !b2 || !mag1 || !mag2 || !fu1 || !fu2 || !tol)
	return eg;

    BN_CK_TOL(tol);
    NMG_CK_FACEUSE(fu1);
    NMG_CK_FACEUSE(fu2);

    if (nmg_debug&NMG_DEBUG_FCUT) {
	bu_log("\nnmg_face_cutjoin(fu1=%p, fu2=%p) eg=%p START\n", (void *)fu1, (void *)fu2, (void *)eg);
	if (b1->end <= 0 || b2->end <= 0) {
	    bu_log("nmg_face_cutjoin(fu1=%p, fu2=%p): WARNING empty list %zu %zu\n",
		   (void *)fu1, (void *)fu2, b1->end, b2->end);
	}
    }

top:
    if (++retries > 10) {
	bu_log("nmg_face_cutjoin(): retry limit exceeded\n");
    } else {
	/*
	 * Sort hit points by increasing distance, vertex ptr, vu ptr,
	 * and eliminate any duplicate vu's.
	 */
	ptbl_vsort(b1, pt, dir, mag1, tol->dist);
	ptbl_vsort(b2, pt, dir, mag2, tol->dist);

	vu1 = (struct vertexuse **)b1->buffer;
	vu2 = (struct vertexuse **)b2->buffer;

	/* Print list of intersections */
	if (nmg_debug&NMG_DEBUG_FCUT) {
	    bu_log("Ray vu intersection list:\n");
	    for (i = 0; i < b1->end; i++) {
		bu_log(" %zu %e ", i, mag1[i]);
		if (vu1 && vu1[i]) nmg_pr_vu_briefly(vu1[i], (char *)0);
	    }
	    for (i = 0; i < b2->end; i++) {
		bu_log(" %zu %e ", i, mag2[i]);
		if (vu2 && vu2[i]) nmg_pr_vu_briefly(vu2[i], (char *)0);
	    }
	}

	/* Check to make sure that intersector didn't miss anything */
	if (nmg_ck_vu_ptbl(b1, fu1) > 0 || nmg_ck_vu_ptbl(b2, fu2) > 0) goto top;
    }

    /* this block of code checks if the two lists of intersection vertexuses
     * contain vertexuses from the appropriate faceuse */
    if (nmg_debug&NMG_DEBUG_FCUT) {
	int found1 = (-1), found2 = (-1);
	int tmp_found;
	struct faceuse *fu;
	struct vertexuse *vu;

	for (i = 0; i < BU_PTBL_LEN(b1); i++) {
	    tmp_found = 0;
	    vu = (struct vertexuse *)BU_PTBL_GET(b1, i);
	    if (vu) {
		fu = nmg_find_fu_of_vu(vu);
		if (fu == fu1)
		    tmp_found = 1;
	    }
	    if (found1 == (-1))
		found1 = tmp_found;
	    else if (tmp_found != found1) {
		bu_log("nmg_face_cutjoin: Intersection list is screwy for face 1\n");
		break;
	    }
	}

	for (i = 0; i < BU_PTBL_LEN(b2); i++) {
	    tmp_found = 0;
	    vu = (struct vertexuse *)BU_PTBL_GET(b2, i);
	    if (vu) {
		fu = nmg_find_fu_of_vu(vu);
		if (fu == fu2)
		    tmp_found = 1;
	    }
	    if (found2 == (-1))
		found2 = tmp_found;
	    else if (tmp_found != found2) {
		bu_log("nmg_face_cutjoin: Intersection list is screwy for face 2\n");
		break;
	    }
	}
	if (!found1)
	    bu_log("nmg_face_cutjoin: intersection list for face 1 doesn't contain vertexuses from face 1!!!\n");
	if (!found2)
	    bu_log("nmg_face_cutjoin: intersection list for face 2 doesn't contain vertexuses from face 2!!!\n");
    }

    nmg_face_rs_init(&rs1, b1, fu1, fu2, pt, dir, eg, tol);
    nmg_face_rs_init(&rs2, b2, fu2, fu1, pt, dir, eg, tol);
    nmg_fcut_face(&rs1, vlfree);
    nmg_fcut_face(&rs2, vlfree);

    /* Merging uses of common edges is OK, though, and quite necessary. */
    i = nmg_mesh_two_faces(fu1, fu2, tol);
    if (i && (nmg_debug&NMG_DEBUG_FCUT)) {
	bu_log("nmg_face_cutjoin() meshed %zu edges\n", i);
    }
    if (nmg_debug&NMG_DEBUG_FCUT) {
	bu_log("nmg_face_cutjoin(fu1=%p, fu2=%p) eg=%p END\n", (void *)fu1, (void *)fu2, (void *)rs1.eg_p);
    }
    if (eg && !rs1.eg_p)
	rs1.eg_p = eg;
    if (eg && eg != rs1.eg_p && (nmg_debug&NMG_DEBUG_FCUT))
	bu_log("nmg_face_cutjoin() changed from eg=%p to rs1.eg_p=%p\n", (void *)eg, (void *)rs1.eg_p);
    return rs1.eg_p;
}

void
nmg_fcut_face_2d(struct bu_ptbl *vu_list, fastf_t *UNUSED(mag), struct faceuse *fu1, struct faceuse *fu2, struct bu_list *vlfree, struct bn_tol *tol)
{
    struct nmg_ray_state rs;
    point_t pt = VINIT_ZERO;
    vect_t dir;
    struct edge_g_lseg *eg;

    if (!vu_list || !fu1 || !fu2 || !tol)
	return;

    NMG_CK_FACEUSE(fu1);
    NMG_CK_FACEUSE(fu2);
    BN_CK_TOL(tol);
    BU_CK_PTBL(vu_list);

    VSET(dir, 1.0, 0.0, 0.0);
    eg = (struct edge_g_lseg *)NULL;

    nmg_face_rs_init(&rs, vu_list, fu1, fu2, pt, dir, eg, tol);
    nmg_fcut_face(&rs, vlfree);
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
