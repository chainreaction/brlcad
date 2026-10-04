/*                      N M G _ B O O L . C
 * BRL-CAD
 *
 * Copyright (c) 1993-2026 United States Government as represented by
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
/** @file primitives/nmg/nmg_bool.c
 *
 * Support for boolean operations on NMG objects.  Most of the
 * routines in here are static/local to this file.  The interfaces
 * here are the functions "nmg_do_bool" and "nmg_mesh_faces".  The
 * former does boolean operations on a pair of shells.  The latter is
 * a function to make edges shared between two faces whenever
 * possible.
 *
 */

#include "common.h"

#include <stdlib.h>
#include <math.h>
#include <string.h>
#include "bio.h"

#include "vmath.h"
#include "bu/malloc.h"
#include "bu/vls.h"
#include "bv/plot3.h"
#include "nmg.h"


extern int nmg_class_nothing_broken;

/* XXX Move to nmg_manif.c or nmg_ck.c */
struct dangling_faceuse_state {
    char *visited;
    const char *manifolds;
    int count;
};


int debug_file_count=0;


/**
 * Find open edges, if any, in NMG object pointed to by magic_p and
 * create a UNIX plot file containing these edges.
 *
 * The prefix string will be appended to the front of the file name
 * of the plot file.
 *
 * Returns -
 * 0  No open edges, no plot file created.
 * !0 Has open edges, plot file created.
 */
static size_t
nmg_plot_open_edges(const uint32_t *magic_p, const char *prefix, struct bu_list *vlfree)
{
    struct loopuse *lu;
    struct edgeuse *eu;
    const struct edgeuse *eur;
    struct faceuse *newfu;
    struct bu_ptbl faces;
    struct face *fp;
    struct faceuse *fu, *fu1, *fu2;
    int done;
    const char *manifolds = NULL;
    point_t pt1, pt2;
    size_t i;
    FILE *plotfp = NULL;
    struct bu_vls plot_file_name = BU_VLS_INIT_ZERO;
    size_t cnt;

    if (!magic_p)
	return 0;

    bu_ptbl_init(&faces, 64, "faces buffer");
    nmg_face_tabulate(&faces, magic_p, vlfree);

    cnt = 0;
    for (i = 0; i < (size_t)BU_PTBL_LEN(&faces) ; i++) {
	fp = (struct face *)BU_PTBL_GET(&faces, i);
	NMG_CK_FACE(fp);
	fu = fu1 = fp->fu_p;
	NMG_CK_FACEUSE(fu1);
	fu2 = fp->fu_p->fumate_p;
	NMG_CK_FACEUSE(fu2);
	done = 0;
	while (!done) {
	    NMG_CK_FACEUSE(fu);
	    for (BU_LIST_FOR(lu, loopuse, &fu->lu_hd)) {
		NMG_CK_LOOPUSE(lu);
		if (BU_LIST_FIRST_MAGIC(&lu->down_hd) == NMG_EDGEUSE_MAGIC) {
		    for (BU_LIST_FOR(eu, edgeuse, &lu->down_hd)) {
			NMG_CK_EDGEUSE(eu);
			eur = nmg_radial_face_edge_in_shell(eu);
			if (!eur || !eur->up.lu_p || !eur->up.lu_p->up.fu_p)
			    continue;
			newfu = eur->up.lu_p->up.fu_p;
			while (manifolds &&
			       NMG_MANIFOLDS(manifolds, newfu) &
			       NMG_2MANIFOLD &&
			       eur != eu->eumate_p) {
			    if (!eur->eumate_p)
				break;
			    eur = nmg_radial_face_edge_in_shell(eur->eumate_p);
			    if (!eur || !eur->up.lu_p || !eur->up.lu_p->up.fu_p)
				break;
			    newfu = eur->up.lu_p->up.fu_p;
			}
			if (eur == eu->eumate_p) {
			    if (!eu->vu_p || !eu->vu_p->v_p || !eu->vu_p->v_p->vg_p ||
				!eu->eumate_p || !eu->eumate_p->vu_p || !eu->eumate_p->vu_p->v_p || !eu->eumate_p->vu_p->v_p->vg_p)
				continue;
			    VMOVE(pt1, eu->vu_p->v_p->vg_p->coord);
			    VMOVE(pt2, eu->eumate_p->vu_p->v_p->vg_p->coord);
			    if (!plotfp) {
				bu_vls_sprintf(&plot_file_name, "%s.%p.pl", prefix ? prefix : "open_edges", (void *)magic_p);
				if ((plotfp = fopen(bu_vls_cstr(&plot_file_name), "wb")) == (FILE *)NULL) {
				    bu_log("nmg_plot_open_edges(): Unable to create plot file (%s)\n", bu_vls_cstr(&plot_file_name));
				    bu_vls_free(&plot_file_name);
				    bu_ptbl_free(&faces);
				    bu_bomb("nmg_plot_open_edges(): Unable to create plot file.");
				}
			    }
			    pdv_3line(plotfp, pt1, pt2);
			    cnt++;
			}
		    }
		}
	    }
	    if (fu == fu1) fu = fu2;
	    if (fu == fu2) done = 1;
	};

    }

    if (plotfp) {
	(void)fclose(plotfp);
    }
    bu_vls_free(&plot_file_name);
    bu_ptbl_free(&faces);

    return cnt;
}


static void
nmg_dangling_handler(uint32_t *longp, void *state, int UNUSED(unused))
{
    register struct faceuse *fu = (struct faceuse *)longp;
    register struct dangling_faceuse_state *sp =
	(struct dangling_faceuse_state *)state;

    NMG_CK_FACEUSE(fu);
    if (fu->orientation != OT_SAME) return;
    /* If this faceuse has been processed before, do nothing more */
    if (!NMG_INDEX_FIRST_TIME(sp->visited, fu)) return;

    if (nmg_dangling_face(fu, sp->manifolds)) {
	sp->count++;
    }
}


/**
 * Argument is expected to be model, region, shell, or faceuse
 * pointer.
 *
 * Returns -
 * 0 No dangling faces
 * !0 Has dangling faces
 */
static int
nmg_has_dangling_faces(uint32_t *magic_p, const char *manifolds, struct bu_list *vlfree)
{
    struct model *m;
    struct dangling_faceuse_state st;
    static const struct nmg_visit_handlers handlers = {NULL, NULL, NULL, NULL, NULL,
						       NULL, NULL, NULL, nmg_dangling_handler, NULL,
						       NULL, NULL, NULL, NULL, NULL,
						       NULL, NULL, NULL, NULL, NULL,
						       NULL, NULL, NULL, NULL, NULL};
    /* handlers.bef_faceuse = nmg_dangling_handler; */

    if (!magic_p)
	return 0;

    m = nmg_find_model(magic_p);
    if (!m)
	return 0;
    NMG_CK_MODEL(m);
    if (m->maxindex < 0)
	return 0;

    st.visited = (char *)bu_calloc((size_t)m->maxindex + 1, sizeof(char), "visited[]");
    st.manifolds = manifolds;
    st.count = 0;

    nmg_visit(magic_p, &handlers, (void *)&st, vlfree);

    bu_free((char *)st.visited, "visited[]");
    return st.count;
}


/**
 * Within a shell, show each loop as a separate display.  Pause after
 * displaying each one.
 *
 * Note that in "non-fancy" mode, show_broken_eu() draws just the
 * edge.
 */
static void
nmg_show_each_loop(struct shell *s, char **classlist, int redraw, int fancy, const char *str, struct bu_list *vlfree)
{
    struct faceuse *fu;
    struct loopuse *lu;
    char buf[128];
    long save;

    if (!s)
	return;
    NMG_CK_SHELL(s);
    save = nmg_debug;
    for (BU_LIST_FOR(fu, faceuse, &s->fu_hd)) {
	NMG_CK_FACEUSE(fu);
	if (fu->orientation == OT_OPPOSITE) continue;
	for (BU_LIST_FOR(lu, loopuse, &fu->lu_hd)) {
	    NMG_CK_LOOPUSE(lu);
	    if (BU_LIST_FIRST_MAGIC(&lu->down_hd) == NMG_VERTEXUSE_MAGIC)
		continue;
	    /* Display only OT_SAME, and OT_UNSPEC et.al.  */
	    if (lu->orientation == OT_OPPOSITE) continue;

	    snprintf(buf, sizeof(buf), "%s=%p", str ? str : "lu", (void *)lu);
	    nmg_show_broken_classifier_stuff(&lu->l.magic, classlist, redraw, fancy, buf, vlfree);
	}
    }
    for (BU_LIST_FOR(lu, loopuse, &s->lu_hd)) {
	snprintf(buf, sizeof(buf), "%s=%p (wire)", str ? str : "lu", (void *)lu);
	nmg_show_broken_classifier_stuff(&lu->l.magic, classlist, redraw, fancy, buf, vlfree);
    }
    nmg_debug = save;		/* restore it */
}


/**
 * Preprocessor routine for classifier to get all the easy shared
 * edges and vertices marked as shared.
 */

static void
nmg_classify_shared_edges_verts(struct shell *sA, struct shell *sB, char **classlist, struct bu_list *vlfree)
{
    struct bu_ptbl verts;
    struct bu_ptbl edges;
    size_t i;

    if (!sA || !sB || !classlist)
	return;

    if (nmg_debug & NMG_DEBUG_CLASSIFY)
	bu_log("nmg_classify_shared_edges_verts(sA=%p, sB=%p)\n", (void *)sA, (void *)sB);

    NMG_CK_SHELL(sA);
    NMG_CK_SHELL(sB);

    nmg_vertex_tabulate(&verts, &sA->l.magic, vlfree);
    for (i=0; i<BU_PTBL_LEN(&verts); i++) {
	struct vertex *v;
	struct vertexuse *vu;

	v = (struct vertex *)BU_PTBL_GET(&verts, i);
	NMG_CK_VERTEX(v);

	for (BU_LIST_FOR(vu, vertexuse, &v->vu_hd)) {
	    NMG_CK_VERTEXUSE(vu);

	    if (nmg_find_s_of_vu(vu) == sB) {
		/* set classification in both lists */
		NMG_INDEX_SET(classlist[NMG_CLASS_AonBshared], v);
		NMG_INDEX_SET(classlist[4 + NMG_CLASS_AonBshared], v);

		if (nmg_debug & NMG_DEBUG_CLASSIFY)
		    bu_log("nmg_classify_shared_edges_verts: v=%p is shared\n", (void *)v);

		break;
	    }
	}
    }
    bu_ptbl_free(&verts);

    nmg_edge_tabulate(&edges, &sA->l.magic, vlfree);
    for (i=0; i<BU_PTBL_LEN(&edges); i++) {
	struct edge *e;
	struct edgeuse *eu;
	struct edgeuse *eu_start;

	e = (struct edge *)BU_PTBL_GET(&edges, i);
	NMG_CK_EDGE(e);

	eu_start = e->eu_p;
	if (!eu_start)
	    continue;
	NMG_CK_EDGEUSE(eu_start);

	eu = eu_start;
	do {
	    if (nmg_find_s_of_eu(eu) == sB) {
		NMG_INDEX_SET(classlist[NMG_CLASS_AonBshared], e);
		NMG_INDEX_SET(classlist[4 + NMG_CLASS_AonBshared], e);

		if (nmg_debug & NMG_DEBUG_CLASSIFY)
		    bu_log("nmg_classify_shared_edges_verts: e=%p is shared\n", (void *)e);

		break;
	    }

	    if (!eu->eumate_p)
		break;
	    eu = eu->eumate_p->radial_p;
	    if (!eu)
		break;
	    NMG_CK_EDGEUSE(eu);

	} while (eu != eu_start && eu->eumate_p != eu_start);
    }
    bu_ptbl_free(&edges);
}


/**
 * Look for same loop in opposite direction in shell "s", Kill them.
 */

static void
nmg_kill_anti_loops(struct shell *s)
{
    struct bu_ptbl loops;
    struct faceuse *fu;
    struct loopuse *lu;
    size_t i, j;

    if (!s)
	return;
    NMG_CK_SHELL(s);

    bu_ptbl_init(&loops, 64, " &loops");

    for (BU_LIST_FOR(fu, faceuse, &s->fu_hd)) {

	if (fu->orientation != OT_SAME)
	    continue;

	for (BU_LIST_FOR(lu, loopuse, &fu->lu_hd)) {

	    if (BU_LIST_FIRST_MAGIC(&lu->down_hd) != NMG_EDGEUSE_MAGIC)
		continue;

	    bu_ptbl_ins(&loops, (long *)lu);
	}
    }

    i = 0;
    while (i < (size_t)BU_PTBL_LEN(&loops)) {
	struct loopuse *lu1;
	struct edgeuse *eu1_start;
	struct vertex *v1;
	int killed = 0;

	lu1 = (struct loopuse *)BU_PTBL_GET(&loops, i);
	if (!lu1 || BU_LIST_IS_EMPTY(&lu1->down_hd)) {
	    i++;
	    continue;
	}

	eu1_start = BU_LIST_FIRST(edgeuse, &lu1->down_hd);
	if (!eu1_start || !eu1_start->vu_p || !eu1_start->vu_p->v_p) {
	    i++;
	    continue;
	}
	v1 = eu1_start->vu_p->v_p;

	for (j = i + 1; j < (size_t)BU_PTBL_LEN(&loops); j++) {
	    struct loopuse *lu2;
	    struct edgeuse *eu1;
	    struct edgeuse *eu2;
	    struct vertexuse *vu2;
	    struct faceuse *fu1, *fu2;
	    int anti = 1;

	    lu2 = (struct loopuse *)BU_PTBL_GET(&loops, j);
	    if (!lu2)
		continue;

	    /* look for v1 in lu2 */
	    vu2 = nmg_find_vertex_in_lu(v1, lu2);

	    if (!vu2 || !vu2->up.eu_p)
		continue;

	    /* found common vertex, now look for the rest */
	    eu2 = vu2->up.eu_p;
	    eu1 = eu1_start;
	    do {
		eu2 = BU_LIST_PNEXT_CIRC(edgeuse, &eu2->l);
		eu1 = BU_LIST_PPREV_CIRC(edgeuse, &eu1->l);

		if (!eu2->vu_p || !eu1->vu_p || eu2->vu_p->v_p != eu1->vu_p->v_p) {
		    anti = 0;
		    break;
		}
	    } while (eu1 != eu1_start);

	    if (!anti)
		continue;

	    fu1 = lu1->up.fu_p;
	    fu2 = lu2->up.fu_p;

	    if (fu1 == fu2)
		continue;

	    /* remove from loops prior to kill so we don't pass around
	     * free'd pointers.  fine for ptbl, but misleading.
	     */
	    bu_ptbl_rm(&loops, (long *)lu1);
	    bu_ptbl_rm(&loops, (long *)lu2);

	    if (nmg_klu(lu1)) {
		if (nmg_kfu(fu1))
		    goto out;
	    }
	    if (nmg_klu(lu2)) {
		if (nmg_kfu(fu2))
		    goto out;
	    }

	    killed = 1;
	    break;
	}

	if (!killed) {
	    i++;
	}
    }
out:
    bu_ptbl_free(&loops);
}


static void
nmg_kill_wire_edges(struct shell *s)
{
    struct loopuse *lu;
    struct edgeuse *eu;

    if (!s)
	return;

    while (BU_LIST_NON_EMPTY(&s->lu_hd)) {
	lu = BU_LIST_FIRST(loopuse, &s->lu_hd);
	nmg_klu(lu);
    }

    while (BU_LIST_NON_EMPTY(&s->eu_hd)) {
	eu = BU_LIST_FIRST(edgeuse, &s->eu_hd);
	nmg_keu(eu);
    }
}


/**
 * Perform boolean operations on a pair of shells.
 *
 * The return is an updated version of shell A.  Shell B is destroyed.
 *
 * XXX this probably should operate on regions, not shells.
 */
static struct shell * nmg_bool(struct shell *sA, struct shell *sB, const int oper, struct bu_list *vlfree, const struct bn_tol *tol)
{
    int i;
    long nelem;
    char *classlist[8];
    FILE *fd = NULL, *fp = NULL;
    struct model *m;
    struct nmgregion *rA;
    struct nmgregion *rB;

    if (!sA || !sB || !tol)
	return (struct shell *)NULL;

    NMG_CK_SHELL(sA);
    NMG_CK_SHELL(sB);
    rA = sA->r_p;
    rB = sB->r_p;
    if (!rA || !rB)
	return (struct shell *)NULL;
    NMG_CK_REGION(rA);
    NMG_CK_REGION(rB);
    m = rA->m_p;
    if (!m)
	return (struct shell *)NULL;
    NMG_CK_MODEL(m);

    if (sA->r_p->m_p != sB->r_p->m_p) {
	bu_bomb("nmg_bool(): internal error, both shells are not in the same nmg model\n");
    }

    if (!sA->sa_p)
	nmg_shell_a(sA, tol);
    if (!sB->sa_p)
	nmg_shell_a(sB, tol);

    /* for the simple case where shells sA and sB are disjoint by at
     * least distance tolerance, we can skip most of the steps to
     * perform the boolean operation
     */
    if (sA->sa_p && sB->sa_p &&
	V3RPP_DISJOINT_TOL(sA->sa_p->min_pt, sA->sa_p->max_pt,
			   sB->sa_p->min_pt, sB->sa_p->max_pt, tol->dist)) {
	switch (oper) {
	    case NMG_BOOL_ADD: {
		struct faceuse *fu;
		vect_t s_min_pt;
		vect_t s_max_pt;

		/* find new sA shell bounding box which combines the
		 * bounding boxes of shells sA and sB.
		 */
		VSETALL(s_min_pt, MAX_FASTF);
		VSETALL(s_max_pt, -MAX_FASTF);
		VMIN(s_min_pt, sA->sa_p->min_pt);
		VMIN(s_min_pt, sB->sa_p->min_pt);
		VMIN(s_min_pt, sB->sa_p->max_pt);
		VMAX(s_max_pt, sA->sa_p->max_pt);
		VMAX(s_max_pt, sB->sa_p->min_pt);
		VMAX(s_max_pt, sB->sa_p->max_pt);

		/* move all the faceuse from shell sB to shell sA */
		for (BU_LIST_FOR(fu, faceuse, &sB->fu_hd)) {
		    fu->s_p = sA;
		}
		BU_LIST_APPEND_LIST(&(sA->fu_hd), &(sB->fu_hd));

		/* assign new bounding box to sA */
		VMOVE(sA->sa_p->min_pt, s_min_pt);
		VMOVE(sA->sa_p->max_pt, s_max_pt);

		/* kill shell sB */
		nmg_ks(sB);
		break;
	    }
	    case NMG_BOOL_SUB:
		/* kill shell sB */
		nmg_ks(sB);
		break;
	    case NMG_BOOL_ISECT:
		/* kill the contents of shell sA */
		while (BU_LIST_NON_EMPTY(&sA->fu_hd)) {
		    (void)nmg_kfu(BU_LIST_FIRST(faceuse, &sA->fu_hd));
		}
		while (BU_LIST_NON_EMPTY(&sA->lu_hd)) {
		    (void)nmg_klu(BU_LIST_FIRST(loopuse, &sA->lu_hd));
		}
		while (BU_LIST_NON_EMPTY(&sA->eu_hd)) {
		    (void)nmg_keu(BU_LIST_FIRST(edgeuse, &sA->eu_hd));
		}
		if (sA->vu_p) {
		    nmg_kvu(sA->vu_p);
		}
		/* kill shell sB */
		nmg_ks(sB);
		break;
	    default:
		bu_bomb("nmg_bool(): unknown operation\n");
	}
	return sA;
    }

    debug_file_count++;
    if (nmg_debug & NMG_DEBUG_VERIFY) {
	/* Sometimes the tessellations of non-participating regions
	 * are damaged during a boolean operation.  Check everything.
	 */
	nmg_vmodel(m);
    }

    nmg_shell_coplanar_face_merge(sA, tol, 1, vlfree);
    nmg_shell_coplanar_face_merge(sB, tol, 1, vlfree);

    nmg_model_fuse(m, vlfree, tol);

    if (nmg_check_closed_shell(sA, tol)) {
	if (nmg_debug & NMG_DEBUG_BOOL &&
	    nmg_debug & NMG_DEBUG_PLOTEM) {
	    if ((fp=fopen("Unclosed.plot3", "wb")) != (FILE *)NULL) {
		bu_log("Plotting unclosed NMG shell\n");
		nmg_pl_s(fp, sA, vlfree);
		fclose(fp);
	    }
	}
	if (nmg_debug & NMG_DEBUG_BOOL)
	    nmg_pr_s(sA, "");

	bu_log("nmg_bool: sA is unclosed, barging ahead\n");
    }

    if (nmg_check_closed_shell(sB, tol)) {
	if (nmg_debug & NMG_DEBUG_BOOL &&
	    nmg_debug & NMG_DEBUG_PLOTEM) {
	    if ((fp=fopen("Unclosed.plot3", "wb")) != (FILE *)NULL) {
		bu_log("Plotting unclosed NMG shell\n");
		nmg_pl_s(fp, sB, vlfree);
		fclose(fp);
	    }
	}
	if (nmg_debug & NMG_DEBUG_BOOL)
	    nmg_pr_s(sB, "");
	bu_log("nmg_bool: sB is unclosed, barging ahead\n");
    }


    if (nmg_debug & NMG_DEBUG_BOOL && nmg_debug & NMG_DEBUG_PLOTEM) {
	if ((fp=fopen("shellA.plot3", "wb")) == (FILE*)NULL) {
	    (void)perror("shellA.plot3");
	    bu_bomb("unable to open shellA.plot3 for writing");
	}
	bu_log("plotting shellA.plot3\n");
	nmg_pl_s(fp, sA, vlfree);
	fclose(fp);

	if ((fp=fopen("shellB.plot3", "wb")) == (FILE*)NULL) {
	    (void)perror("shellB.plot3");
	    bu_bomb("unable to open shellB.plot3 for writing");
	}
	bu_log("plotting shellB.plot3\n");
	nmg_pl_s(fp, sB, vlfree);
	fclose(fp);
    }

    if (nmg_debug & NMG_DEBUG_VERIFY) {
	/* Sometimes the tessellations of non-participating regions
	 * are damaged during a boolean operation.  Check everything.
	 */
	nmg_vmodel(m);
    }

    /* Perform shell/shell intersections */
    nmg_crackshells(sA, sB, vlfree, tol);

    if (nmg_debug & NMG_DEBUG_BOOL) {
	bu_log("Just After Crackshells:\nShell A:\n");
	nmg_pr_s_briefly(sA, 0);
	bu_log("Just After Crackshells:\nShell B:\n");
	nmg_pr_s_briefly(sB, 0);
    }

    (void)nmg_vertex_fuse(&m->magic, vlfree, tol);

    (void)nmg_kill_anti_loops(sA);
    (void)nmg_kill_anti_loops(sB);

    /* clean things up now that the intersections have been built */
    nmg_sanitize_s_lv(sA, OT_BOOLPLACE);
    nmg_sanitize_s_lv(sB, OT_BOOLPLACE);

    /* Separate any touching loops, so classifier does not have any
     * really complex loops to do.
     * In particular, it is necessary for (r410) to make
     * interior (touching) loop segments into true interior loops
     * that are separate from the exterior loop,
     * so the classifier can assess each separately.
     */
    nmg_s_split_touchingloops(sA, tol);
    nmg_s_split_touchingloops(sB, tol);

    (void)nmg_kill_cracks(sA);
    (void)nmg_kill_cracks(sB);

    /* eliminate unnecessary breaks in edges */
    (void)nmg_simplify_shell_edges(sA, tol);
    (void)nmg_simplify_shell_edges(sB, tol);

    (void)nmg_break_e_on_v(&m->magic, vlfree, tol);

    (void)nmg_edge_fuse(&m->magic, vlfree, tol);

    if (nmg_debug & NMG_DEBUG_VERIFY) {
	/* Sometimes the tessellations of non-participating regions
	 * are damaged during a boolean operation.  Check everything.
	 */
	nmg_vmodel(m);
	if ((i = nmg_model_fuse(m, vlfree, tol)) > 0) {
	    bu_log("NOTICE: nmg_bool: fused %d entities while cracking shells\n", i);
	    bu_bomb("nmg_bool() entities unfused after nmg_crackshells()\n");
	}
    }

    if (nmg_debug & NMG_DEBUG_BOOL) {
	int dangle_error = 0;
	if (nmg_has_dangling_faces((uint32_t *)rA, (char *)NULL, vlfree)) {
	    dangle_error = 1;
	    bu_log("nmg_bool(): Dangling faces detected in rA before classification\n");
	}
	if (nmg_has_dangling_faces((uint32_t *)rB, (char *)NULL, vlfree)) {
	    dangle_error = 1;
	    bu_log("nmg_bool(): Dangling faces detected in rB before classification\n");
	}
	if (nmg_has_dangling_faces((uint32_t *)m, (char *)NULL, vlfree)) {
	    dangle_error = 1;
	    bu_log("nmg_bool(): Dangling faces detected in model before classification\n");
	}
	if (dangle_error) {
	    bu_bomb("nmg_bool(): Dangling faces detected before classification\n");
	}
    }

    if (nmg_debug & NMG_DEBUG_VERIFY) {
	/* Sometimes the tessellations of non-participating regions
	 * are damaged during a boolean operation.  Check everything.
	 */
	nmg_vmodel(m);
    }

    /* Re-build bounding boxes, edge geometry, as needed. */
    nmg_shell_a(sA, tol);
    nmg_shell_a(sB, tol);

    if (nmg_debug & NMG_DEBUG_BOOL) {
	bu_log("sA:\n");
	nmg_pr_s_briefly(sA, 0);
	bu_log("sB:\n");
	nmg_pr_s_briefly(sB, 0);
    }

    if (nmg_debug & NMG_DEBUG_BOOL) {
	if (nmg_debug & NMG_DEBUG_PLOTEM) {
	    if ((fd = fopen("Cracked_Shells.plot3", "wb")) == (FILE *)NULL) {
		(void)perror("Cracked_Shells");
		bu_bomb("unable to open Cracked_Shells.plot3 for writing");
	    }
	    bu_log("plotting Cracked_Shells.plot3\n");

	    nmg_pl_s(fd, sA, vlfree);
	    nmg_pl_s(fd, sB, vlfree);
	    (void)fclose(fd);

	    nmg_pl_isect("isectA.plot3", sA, vlfree, tol);
	    nmg_pl_isect("isectB.plot3", sB, vlfree, tol);
	}

	bu_log("check 2\n");
    }

    if (nmg_ck_closed_surf(sA, tol))
	bu_log("nmg_bool() WARNING: sA unclosed before classification.  Boldly pressing on.\n");
    if (nmg_ck_closed_surf(sB, tol))
	bu_log("nmg_bool() WARNING: sB unclosed before classification.  Boldly pressing on.\n");

    if (nmg_debug & NMG_DEBUG_VERIFY) {
	/* Sometimes the tessellations of non-participating regions
	 * are damaged during a boolean operation.  Check everything.
	 */
	nmg_vmodel(m);
    }

    nmg_m_reindex(m, 0);

    /* Allocate storage for classlist[]. Allocate each of the 8 class
     * lists one at a time. This will assist with debugging to
     * determine if each array read/write is within its allocated space.
     */
    nelem = m->maxindex > 0 ? m->maxindex : 1;
    for (i = 0; i < 8; i++) {
	classlist[i] = (char *)bu_calloc((size_t)nelem, sizeof(char), "nmg_bool classlist");
    }

    nmg_classify_shared_edges_verts(sA, sB, classlist, vlfree);

    nmg_class_nothing_broken = 1;
    if (nmg_debug & (NMG_DEBUG_GRAPHCL|NMG_DEBUG_PL_LOOP)) {
	nmg_show_broken_classifier_stuff((uint32_t *)sA, &classlist[0], nmg_class_nothing_broken, 1, "unclassed sA", vlfree);
	nmg_show_broken_classifier_stuff((uint32_t *)sB, &classlist[4], 1, 1, "unclassed sB", vlfree);
    }

    if (m->manifolds) {
	bu_free((char *)m->manifolds, "free manifolds table");
	m->manifolds = (char *)NULL;
    }
    m->manifolds = nmg_manifolds(m);

    /*
     * Classify A -vs- B, then B -vs- A.
     * Carry onAonBshared and onAonBanti classifications forward
     * from first step to second step.
     * A -vs- B live in classlist[0..3], B -vs- A live in classlist[4..7].
     */
    nmg_class_shells(sA, sB, &classlist[0], vlfree, tol);
    memcpy((char *)classlist[4+NMG_CLASS_AonBshared],
	   (char *)classlist[0+NMG_CLASS_AonBshared],
	   (size_t)nelem * sizeof(char));
    memcpy((char *)classlist[4+NMG_CLASS_AonBanti],
	   (char *)classlist[0+NMG_CLASS_AonBanti],
	   (size_t)nelem * sizeof(char));
    memcpy((char *)classlist[4+NMG_CLASS_AoutB],
	   (char *)classlist[0+NMG_CLASS_AoutB],
	   (size_t)nelem * sizeof(char));
    nmg_class_shells(sB, sA, &classlist[4], vlfree, tol);

    if (m->manifolds) {
	bu_free((char *)m->manifolds, "free manifolds table");
	m->manifolds = (char *)NULL;
    }

    if (nmg_debug & (NMG_DEBUG_GRAPHCL|NMG_DEBUG_PL_LOOP)) {
	nmg_class_nothing_broken = 1;

	/* Show each loop, one at a time, non-fancy */
	/* XXX Should have its own bit, or combination -- not always wanted */
	nmg_show_each_loop(sA, &classlist[0], 1, 0, "sA lu", vlfree);
	nmg_show_each_loop(sB, &classlist[4], 1, 0, "sB lu", vlfree);

	/* Show each shell as a whole */
	nmg_show_broken_classifier_stuff((uint32_t *)sA, &classlist[0], 1, 0, "sA classed", vlfree);
	nmg_show_broken_classifier_stuff((uint32_t *)sB, &classlist[4], 1, 0, "sB classed", vlfree);
    }

    if (nmg_debug & NMG_DEBUG_BOOL) {
	bu_log("Just before nmg_evaluate_boolean:\nShell A:\n");
	nmg_pr_s_briefly(sA, 0);
	bu_log("Shell B:\n");
	nmg_pr_s_briefly(sB, 0);
    }

    nmg_s_radial_check(sA, vlfree, tol);
    nmg_s_radial_check(sB, vlfree, tol);
    nmg_evaluate_boolean(sA, sB, oper, classlist, vlfree, tol);
    sB = NULL; /* sanity, killed during boolean eval */

    if (nmg_debug & NMG_DEBUG_BOOL) {
	bu_log("Just after nmg_evaluate_boolean:\nShell A:\n");
	nmg_pr_s_briefly(sA, 0);
	bu_log("Shell B:\nFreed.");
    }

    if (nmg_debug & NMG_DEBUG_VERIFY) {
	nmg_vmodel(m);
	if ((i = nmg_model_fuse(m, vlfree, tol)) > 0) {
	    bu_log("ERROR: nmg_bool: fused %d entities after BOOLEAN.  Isect bug.\n", i);
	    bu_bomb("nmg_bool() entities unfused after nmg_evaluate_boolean()\n");
	}
    }

    /*
     * nmg_evaluate_boolean() may return an invalid shell, i.e., one
     * that has absolutely nothing in it.  This is an indication that
     * the shell should be deleted from the region, an operation which
     * can not be accomplished this far down in the subroutine tree.
     */
    if (!nmg_shell_is_empty(sA)) {

	nmg_s_radial_check(sA, vlfree, tol);

	if (nmg_debug & NMG_DEBUG_BOOL) {
	    int dangle_error = 0;
	    if (nmg_has_dangling_faces((uint32_t *)rA, (char *)NULL, vlfree)) {
		dangle_error = 1;
		bu_log("nmg_bool(): Dangling faces detected in rA after boolean\n");
	    }
	    if (nmg_has_dangling_faces((uint32_t *)rB, (char *)NULL, vlfree)) {
		dangle_error = 1;
		bu_log("nmg_bool(): Dangling faces detected in rB after boolean\n");
	    }
	    if (nmg_has_dangling_faces((uint32_t *)m, (char *)NULL, vlfree)) {
		dangle_error = 1;
		bu_log("nmg_bool(): Dangling faces detected in m after boolean\n");
	    }
	    if (dangle_error) {
		bu_bomb("nmg_bool(): Dangling faces detected after boolean\n");
	    }
	} else {
	    if (nmg_has_dangling_faces((uint32_t *)rA, (char *)NULL, vlfree)) {
		(void)nmg_plot_open_edges((const uint32_t *)rA, "open_edges", vlfree);
		bu_bomb("nmg_bool(): Dangling faces detected in rA after boolean\n");
	    }
	}

	/* Do this before table size changes */
	if (nmg_debug & (NMG_DEBUG_GRAPHCL|NMG_DEBUG_PL_LOOP)) {
	    nmg_class_nothing_broken = 1;

	    /* Show final result of the boolean */
	    nmg_show_broken_classifier_stuff((uint32_t *)sA, &classlist[0], 1, 0, "sA result", vlfree);
	}

	/* Go back and combine loops of faces together wherever
	 * possible to reduce the loop/edge count.
	 */
	nmg_simplify_shell(sA, vlfree);
	if (nmg_debug & NMG_DEBUG_VERIFY)
	    nmg_vshell(&rA->s_hd, rA);

	(void) nmg_unbreak_region_edges(&sA->l.magic, vlfree);

	if (nmg_debug & NMG_DEBUG_BOOL) {
	    bu_log("Just after nmg_simplify_shell:\nShell A:\n");
	    nmg_pr_s_briefly(sA, 0);
	}

	/* Bounding boxes may have changed */
	nmg_shell_a(sA, tol);

	if (nmg_ck_closed_surf(sA, tol)) {
	    if (nmg_debug)
		bu_log("nmg_bool() WARNING: sA unclosed at return, barging on.\n");
	    else
		bu_bomb("nmg_bool() sA unclosed at return, aborting.\n");
	}
	nmg_s_radial_check(sA, vlfree, tol);
    }

    for (i = 0; i < 8; i++) {
	bu_free((char *)classlist[i], "nmg_bool classlist");
    }

    if (nmg_debug & NMG_DEBUG_BOOL) {
	bu_log("Returning from NMG_BOOL\n");
    }
    if (nmg_debug & NMG_DEBUG_VERIFY) {
	/* Sometimes the tessellations of non-participating regions
	 * are damaged during a boolean operation.  Check everything.
	 */
	nmg_vmodel(m);
    }

    nmg_kill_wire_edges(sA);

    return sA;
}


/**
 * BUG: we assume only one shell per region
 */
struct nmgregion *
nmg_do_bool(struct nmgregion *rA, struct nmgregion *rB, const int oper, struct bu_list *vlfree, const struct bn_tol *tol)
{
    struct shell *s;
    struct nmgregion *r;

    if (!rA || !rB || !tol)
	return (struct nmgregion *)NULL;

    NMG_CK_REGION(rA);
    NMG_CK_REGION(rB);

    if (BU_LIST_IS_EMPTY(&rA->s_hd) || BU_LIST_IS_EMPTY(&rB->s_hd)) {
	if (oper == NMG_BOOL_ADD) {
	    if (BU_LIST_IS_EMPTY(&rA->s_hd)) {
		nmg_kr(rA);
		return rB;
	    }
	    nmg_kr(rB);
	    return rA;
	} else if (oper == NMG_BOOL_SUB) {
	    nmg_kr(rB);
	    if (BU_LIST_IS_EMPTY(&rA->s_hd)) {
		nmg_kr(rA);
		return (struct nmgregion *)NULL;
	    }
	    return rA;
	} else {
	    nmg_kr(rA);
	    nmg_kr(rB);
	    return (struct nmgregion *)NULL;
	}
    }

    nmg_region_v_unique(rA, vlfree, tol);
    nmg_region_v_unique(rB, vlfree, tol);

    s = nmg_bool(BU_LIST_FIRST(shell, &rA->s_hd),
		 BU_LIST_FIRST(shell, &rB->s_hd),
		 oper, vlfree, tol);
    if (!s) {
	nmg_kr(rB);
	return (struct nmgregion *)NULL;
    }
    r = s->r_p;

    /* shell B was destroyed, need to eliminate region B */
    nmg_kr(rB);

    NMG_CK_SHELL(s);
    if (!r)
	return (struct nmgregion *)NULL;
    NMG_CK_REGION(r);

    /* If shell A became empty, eliminate it from the returned region */
    if (nmg_shell_is_empty(s)) {
	nmg_ks(s);
	if (BU_LIST_NON_EMPTY(&r->s_hd)) {
	    bu_bomb("nmg_do_bool: Result of Boolean is an empty shell, but region is not empty!!!\n");
	}
	nmg_kr(r);
	return (struct nmgregion *)NULL;
    }

    return r;
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
