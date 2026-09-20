/*                      G F I L E . C P P
 * BRL-ISST
 *
 * Copyright (c) 2014-2026 United States Government as represented by
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
/** @file gfile.cpp
 *
 *
 */

#include <QFileInfo>
#include <QFile>
#include <QPlainTextEdit>
#include <QTextStream>
#include "gfile.h"

#include "bu/malloc.h"
#include "bu/file.h"
#include "gcv.h"
#include "nmg.h"
#include "rt/geom.h"

/* Replace the load_g globals from adrt with a struct that is
 * passed through the callers. */
struct isst_nmg_data {
    TIE_3 **tribuf;
    struct tie_s *cur_tie;
    struct db_i *dbip;
    struct bn_tol *tol;
    struct bu_list *vlfree;
    struct adrt_mesh_s **meshes;
};

struct gcv_data {
    struct gcv_region_end_data region_end_data;
    struct adrt_mesh_s **meshes;
};

static void nmg_to_adrt_gcvwrite(struct nmgregion *r, const struct db_full_path *pathp, struct db_tree_state *tsp, void *client_data);

/* load the region into the tie image */
static void
nmg_to_adrt_internal(TIE_3 **tribuf, struct tie_s *cur_tie, struct adrt_mesh_s *mesh, struct nmgregion *r)
{
    struct model *m;
    struct shell *s;

    if (!tribuf || !tribuf[0] || !tribuf[1] || !tribuf[2] || !cur_tie || !mesh || !r)
	return;

    NMG_CK_REGION(r);

    m = r->m_p;
    if (!m)
	return;
    NMG_CK_MODEL(m);

    /* Check triangles */
    for (BU_LIST_FOR (s, shell, &r->s_hd))
    {
	struct faceuse *fu;

	NMG_CK_SHELL(s);

	for (BU_LIST_FOR (fu, faceuse, &s->fu_hd))
	{
	    struct loopuse *lu;

	    NMG_CK_FACEUSE(fu);

	    if (fu->orientation != OT_SAME)
		continue;

	    for (BU_LIST_FOR (lu, loopuse, &fu->lu_hd))
	    {
		struct edgeuse *eu;
		int vert_count = 0;

		NMG_CK_LOOPUSE(lu);

		if (BU_LIST_FIRST_MAGIC(&lu->down_hd) != NMG_EDGEUSE_MAGIC)
		    continue;

		/* check vertex numbers for each triangle */
		for (BU_LIST_FOR (eu, edgeuse, &lu->down_hd))
		{
		    struct vertex *v;

		    NMG_CK_EDGEUSE(eu);
		    if (!eu->vu_p || !eu->vu_p->v_p)
			continue;

		    v = eu->vu_p->v_p;
		    NMG_CK_VERTEX(v);
		    if (!v->vg_p)
			continue;

		    /* convert mm to m */
		    if (vert_count < 3) {
			VSCALE((*tribuf[vert_count]).v, v->vg_p->coord, 1.0/1000.0);
		    }
		    vert_count++;
		}
		if (vert_count != 3)
		{
		    if (vert_count > 3)
			bu_log("lu %p has %d vertices (not a triangle)\n", (void *)lu, vert_count);
		    continue;
		}

		TIE_VAL(tie_push)(cur_tie, tribuf, 1, mesh, 0);
	    }
	}
    }

    /* region_name must not be freed until we're done with the tie engine. */
}


static int
nmg_to_adrt_regstart(struct db_tree_state *ts, const struct db_full_path *path, const struct rt_comb_internal *rci, void *client_data)
{
    /*
     * if it's a simple single bot region, just eat the bots and return -1.
     * Omnomnom. Return 0 to do nmg eval.
     */
    struct gcv_data *gd = (struct gcv_data *)client_data;
    if (!gd || !gd->region_end_data.client_data || !gd->meshes || !*gd->meshes)
	return 0;

    struct isst_nmg_data *d = (struct isst_nmg_data *)gd->region_end_data.client_data;
    struct directory *dir;
    struct rt_db_internal intern;
    struct adrt_mesh_s *mesh;
    unsigned char rgb[3] = { 0xc0, 0xc0, 0xc0 };
    char *path_str;
    int ret = 0;

    if (!ts || !path || !rci || !d->dbip || !d->cur_tie || !d->tribuf)
	return 0;

    RT_CHECK_COMB(rci);

    /* abort cases, no fast loading. */
    if (rci->tree == NULL)
	return 0;
    RT_CK_TREE(rci->tree);
    if (rci->tree->tr_op != OP_DB_LEAF)
	return 0;
    if ((dir = db_lookup(d->dbip, rci->tree->tr_l.tl_name, 1)) == NULL) {
	bu_log("Lookup failed: %s\n", rci->tree->tr_l.tl_name);
	return 0;
    }
    if (dir->d_minor_type != ID_BOT && dir->d_minor_type != ID_NMG)
	return 0;
    if (rt_db_get_internal(&intern, dir, d->dbip, (fastf_t *)NULL) < 0) {
	bu_log("Failed to load: %s\n", rci->tree->tr_l.tl_name);
	return 0;
    }

    if (dir->d_minor_type == ID_NMG) {
	rt_db_free_internal(&intern);
	return 0;
    }

    BU_ALLOC(mesh, struct adrt_mesh_s);

    BU_LIST_PUSH(&((*gd->meshes)->l), &(mesh->l));

    mesh->texture = NULL;
    mesh->flags = 0;

    BU_ALLOC(mesh->attributes, struct adrt_mesh_attributes_s);
    mesh->matid = ts->ts_gmater;

    rt_comb_get_color(d->dbip, rgb, rci);
    VSCALE(mesh->attributes->color.v, rgb, 1.0/256.0);

    path_str = db_path_to_string(path);
    if (path_str) {
	bu_strlcpy(mesh->name, path_str, sizeof(mesh->name));
	bu_free(path_str, "db_path_to_string");
    } else {
	mesh->name[0] = '\0';
    }

    if (intern.idb_minor_type == ID_NMG) {
	nmg_to_adrt_internal(d->tribuf, d->cur_tie, mesh, (struct nmgregion *)intern.idb_ptr);
	ret = -1;
    } else if (intern.idb_minor_type == ID_BOT) {
	size_t i;
	struct rt_bot_internal *bot = (struct rt_bot_internal *)intern.idb_ptr;

	RT_BOT_CK_MAGIC(bot);

	if (bot->vertices && bot->faces) {
	    for (i = 0; i < bot->num_faces; i++)
	    {
		size_t v0 = (size_t)bot->faces[3*i+0];
		size_t v1 = (size_t)bot->faces[3*i+1];
		size_t v2 = (size_t)bot->faces[3*i+2];

		if (v0 >= bot->num_vertices || v1 >= bot->num_vertices || v2 >= bot->num_vertices)
		    continue;

		VSCALE((*d->tribuf[0]).v, (bot->vertices+3*v0), 1.0/1000.0);
		VSCALE((*d->tribuf[1]).v, (bot->vertices+3*v1), 1.0/1000.0);
		VSCALE((*d->tribuf[2]).v, (bot->vertices+3*v2), 1.0/1000.0);

		TIE_VAL(tie_push)(d->cur_tie, d->tribuf, 1, mesh, 0);
	    }
	}
	ret = -1;
    } else {
	bu_log("Strange, %d is not %d or %d\n", intern.idb_minor_type, ID_BOT, ID_NMG);
	ret = 0;
    }

    rt_db_free_internal(&intern);
    return ret;
}


static void
nmg_to_adrt_gcvwrite(struct nmgregion *r, const struct db_full_path *pathp, struct db_tree_state *tsp, void *client_data)
{
    struct isst_nmg_data *d = (struct isst_nmg_data *)client_data;
    struct model *m;
    struct adrt_mesh_s *mesh;
    char *path_str;

    if (!r || !pathp || !tsp || !d || !d->meshes || !*d->meshes)
	return;

    NMG_CK_REGION(r);
    RT_CK_FULL_PATH(pathp);

    m = r->m_p;
    if (!m)
	return;
    NMG_CK_MODEL(m);

    /* triangulate model */
    nmg_triangulate_model(m, d->vlfree, d->tol);

    BU_ALLOC(mesh, struct adrt_mesh_s);

    BU_LIST_PUSH(&((*d->meshes)->l), &(mesh->l));

    mesh->texture = NULL;
    mesh->flags = 0;

    BU_ALLOC(mesh->attributes, struct adrt_mesh_attributes_s);
    mesh->matid = tsp->ts_gmater;

    VMOVE(mesh->attributes->color.v, tsp->ts_mater.ma_color);
    path_str = db_path_to_string(pathp);
    if (path_str) {
	bu_strlcpy(mesh->name, path_str, sizeof(mesh->name));
	bu_free(path_str, "db_path_to_string");
    } else {
	mesh->name[0] = '\0';
    }

    nmg_to_adrt_internal(d->tribuf, d->cur_tie, mesh, r);
}

GFile::GFile()
    : QObject(), tie(NULL), meshes(NULL), dbip(NULL), tribuf(NULL), current_file()
{
}

GFile::~GFile()
{
    closedb();
}

int
GFile::load_g(const char *filename, int argc, const char *argv[])
{
    struct model *the_model;
    struct bn_tol tol;
    struct bg_tess_tol ttol;		/* tessellation tolerance in mm */
    struct db_tree_state tree_state;	/* includes tol & model */
    struct isst_nmg_data d;
    struct gcv_data gcvwriter;

    if (!filename)
	return -1;

    closedb();

    RT_DBTS_INIT(&tree_state);
    tree_state.ts_tol = &tol;
    tree_state.ts_ttol = &ttol;
    tree_state.ts_m = &the_model;

    /* Set up tessellation tolerance defaults */
    ttol.magic = BG_TESS_TOL_MAGIC;
    /* Defaults, updated by command line options. */
    ttol.abs = 0.0;
    ttol.rel = 0.01;
    ttol.norm = 0.0;

    /* Set up calculation tolerance defaults */
    tol.magic = BN_TOL_MAGIC;
    tol.dist = 0.0005;
    tol.dist_sq = tol.dist * tol.dist;
    tol.perp = 1e-6;
    tol.para = 1 - tol.perp;
    d.tol = &tol;

    tie_check_degenerate = 0;

    /* make empty NMG model */
    the_model = nmg_mm();
    d.vlfree = &rt_vlfree;

    /*
     * these should probably encode so the result can be passed back to client
     */
    if ((dbip = db_open(filename, DB_OPEN_READONLY)) == DBI_NULL) {
	bu_log("Unable to open geometry database file (%s)\n", filename);
	nmg_km(the_model);
	return -1;
    }
    if (db_dirbuild(dbip)) {
	bu_log("ERROR: db_dirbuild failed\n");
	db_close(dbip);
	dbip = NULL;
	nmg_km(the_model);
	return -1;
    }
    d.dbip = dbip;

    BU_ALLOC(tie, struct tie_s);
    d.cur_tie = this->tie;

    BN_CK_TOL(tree_state.ts_tol);
    BG_CK_TESS_TOL(tree_state.ts_ttol);

    TIE_VAL(tie_init)(d.cur_tie, BU_PAGE_SIZE, TIE_KDTREE_FAST);

    BU_ALLOC(this->meshes, struct adrt_mesh_s);
    BU_LIST_INIT(&((this->meshes)->l));

    d.meshes = &this->meshes;

    memset(&gcvwriter, 0, sizeof(gcvwriter));
    gcvwriter.region_end_data.write_region = nmg_to_adrt_gcvwrite;
    gcvwriter.region_end_data.vlfree = &rt_vlfree;
    gcvwriter.region_end_data.client_data = &d;
    gcvwriter.meshes = &this->meshes;

    tribuf = (TIE_3 **)bu_malloc(sizeof(TIE_3 *) * 3, "triangle tribuffer tribuffer");
    tribuf[0] = (TIE_3 *)bu_malloc(sizeof(TIE_3) * 3, "triangle tribuffer");
    tribuf[1] = (TIE_3 *)bu_malloc(sizeof(TIE_3) * 3, "triangle tribuffer");
    tribuf[2] = (TIE_3 *)bu_malloc(sizeof(TIE_3) * 3, "triangle tribuffer");
    d.tribuf = this->tribuf;

    (void) db_walk_tree(dbip,
			argc,			/* number of toplevel regions */
			argv,			/* region names */
			1,			/* ncpu */
			&tree_state,		/* initial tree state */
			nmg_to_adrt_regstart,	/* region start function */
			gcv_region_end,		/* region end function */
			rt_booltree_leaf_tess,	/* leaf func */
			(void *)&gcvwriter);	/* client data */

    /* Release dynamic storage */
    nmg_km(the_model);
    rt_vlist_cleanup();
    db_close(dbip);
    dbip = NULL;

    if (tribuf) {
	bu_free(tribuf[0], "vert");
	bu_free(tribuf[1], "vert");
	bu_free(tribuf[2], "vert");
	bu_free(tribuf, "tri");
	tribuf = NULL;
    }

    TIE_VAL(tie_prep)(d.cur_tie);

    current_file = QString::fromLocal8Bit(filename);

    return 0;
}

void
GFile::closedb()
{
    if (this->tie) {
	TIE_VAL(tie_free)(this->tie);
	bu_free(this->tie, "free tie");
	this->tie = NULL;
    }

    if (this->meshes) {
	struct adrt_mesh_s *mesh;
	while (BU_LIST_WHILE(mesh, adrt_mesh_s, &this->meshes->l)) {
	    BU_LIST_DEQUEUE(&mesh->l);
	    if (mesh->attributes) {
		bu_free(mesh->attributes, "mesh attributes");
		mesh->attributes = NULL;
	    }
	    bu_free(mesh, "mesh");
	}
	bu_free(this->meshes, "mesh head");
	this->meshes = NULL;
    }

    if (this->tribuf) {
	if (this->tribuf[0]) bu_free(this->tribuf[0], "vert");
	if (this->tribuf[1]) bu_free(this->tribuf[1], "vert");
	if (this->tribuf[2]) bu_free(this->tribuf[2], "vert");
	bu_free(this->tribuf, "tri");
	this->tribuf = NULL;
    }

    if (this->dbip) {
	db_close(this->dbip);
	this->dbip = NULL;
    }

    current_file.clear();
}

/*
 * Local Variables:
 * mode: C++
 * tab-width: 8
 * c-basic-offset: 4
 * indent-tabs-mode: t
 * c-file-style: "stroustrup"
 * End:
 * ex: shiftwidth=4 tabstop=8
 */

