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
 * loftwing skins a set of transformed NACA sections into an ARS surface.  That
 * is the right primitive to raytrace inside BRL-CAD, but an ARS is not an
 * exchangeable exact surface: g-iges(1) flattens it into planar facets.  This
 * file adds the second back end - the same sections skinned into a degree 3x3
 * NURBS surface, closed with two planar caps and written as a BREP solid (a
 * genuine NURBS surface, IGES type 128).
 *
 * The caller supplies planar section rings in a single array.  Each ring
 * starts at the trailing edge and runs around the section, e.g.
 *
 *      TE -> upper surface -> LE -> lower surface -> TE
 *
 * so the u = 0 and u = 1 edges of the skinned surface meet along the sharp TE
 * seam (the curve is closed, the trailing edge point repeated), and the two
 * v = const edges are closed planar loops that get capped.
 *
 * The skinning is a global cubic interpolation (collocation) through the
 * stations in v and through each section in u, so the surface passes exactly
 * through every point it is given; the knot vectors are the averaged-knot
 * (centripetal-free) clamped form.
 *
 * Such a closed ring is not, however, how the shell is built: a surface
 * closed in u leaves a degenerate TE seam, and the CDT mesher behind g-stl(1)
 * and the mged display does not always resolve it on a tapered planform -
 * "misoriented edges" along the seam and then "tessellation failure" (0
 * triangles written), even though mged's "brep <obj> solid" test, rt(1) and
 * g-iges(1) are all happy with the result.  Every ring is therefore cut at
 * the TE and at the LE and the shell is skinned as two open ruled faces per
 * panel, sharing the TE and LE ridges as explicit edges, with a planar cap
 * owning both runs at each end station.  That shell is a closed oriented
 * 2-manifold (ON_Brep::IsSolid()) and tessellates cleanly.  --format ars (the
 * default), which builds no BREP at all, is unaffected either way.
 */

#include "common.h"

#include "bu/app.h"
#include "bn/tol.h"
#include "rt/wdb.h"
#include "wdb.h"
#include "opennurbs.h"
#include "primitive_brep.h"

#include <vector>
#include <cmath>

using std::vector;

/* ------------------------------------------------------------------ helpers */

/* ON convention: a degree d NURBS with n control points stores d + n - 2
 * knots, clamped with multiplicity d - 1 at each end (a cubic Bezier is
 * {0,0,0,1,1,1}).  The interior knots are the averages of the collocation
 * parameters, the standard choice that keeps the system well conditioned. */
static void
clamped_knots(const vector<double>& t, vector<double>& U)
{
    int m = (int)t.size();

    U.assign(m + 2, 0.0);
    U[0] = U[1] = U[2] = t[0];
    for (int j = 3; j <= m - 2; ++j)
	U[j] = (t[j - 2] + t[j - 1] + t[j]) / 3.0;
    U[m - 1] = U[m] = U[m + 1] = t[m - 1];
}

/* Solve A x = b in place (dense, partial pivoting).  m is small (one row per
 * section or per section point), so the cubic cost is irrelevant. */
static bool
solve(vector<vector<double> > A, vector<double> b, vector<double>& x)
{
    int m = (int)b.size();

    x.assign(m, 0.0);

    for (int col = 0; col < m; ++col) {
	int piv = col;
	for (int r = col + 1; r < m; ++r)
	    if (fabs(A[r][col]) > fabs(A[piv][col])) piv = r;
	if (fabs(A[piv][col]) < 1e-12)
	    return false;
	if (piv != col) {
	    std::swap(A[piv], A[col]);
	    std::swap(b[piv], b[col]);
	}
	for (int r = col + 1; r < m; ++r) {
	    double f = A[r][col] / A[col][col];
	    if (f == 0.0) continue;
	    for (int c = col; c < m; ++c) A[r][c] -= f * A[col][c];
	    b[r] -= f * b[col];
	}
    }

    for (int r = m - 1; r >= 0; --r) {
	double s = b[r];
	for (int c = r + 1; c < m; ++c) s -= A[r][c] * x[c];
	x[r] = s / A[r][r];
    }
    return true;
}

/* Interpolate pts -> control points + knots (exact interpolation). */
static bool
interp(const vector<ON_3dPoint>& pts, vector<ON_3dPoint>& cvs, vector<double>& U)
{
    int m = (int)pts.size();

    if (m < 4)
	return false;

    vector<double> t(m);
    for (int i = 0; i < m; ++i)
	t[i] = (double)i / (m - 1.0);
    clamped_knots(t, U);

    vector<vector<double> > A(m, vector<double>(m, 0.0));
    for (int i = 0; i < m; ++i) {
	if (i == 0) { A[0][0] = 1.0; continue; }
	if (i == m - 1) { A[m - 1][m - 1] = 1.0; continue; }

	/* Span k with U[k] <= t[i] < U[k+1]; the non-zero basis functions are
	 * k-2 .. k+1.  The basis is evaluated with ON's own evaluator: the
	 * knot window it wants is the 2*d knots active for the span, and the
	 * hand-rolled Cox-de Boor recursion reads past the end of the knot
	 * vector for the last spans under ON's knot convention. */
	int k = 2;
	while (k < m - 1 && !(U[k] <= t[i] && t[i] < U[k + 1])) ++k;
	if (k > m - 2) k = m - 2;

	double kw[6];
	for (int q = 0; q < 6; ++q) kw[q] = U[k - 2 + q];
	double B[16] = { 0.0 };
	ON_EvaluateNurbsBasis(4, kw, t[i], B);
	for (int jj = 0; jj < 4; ++jj) A[i][k - 2 + jj] = B[jj];
    }

    vector<double> b(m), x;
    cvs.assign(m, ON_3dPoint::Origin);
    for (int c = 0; c < 3; ++c) {
	for (int i = 0; i < m; ++i) b[i] = pts[i][c];
	if (!solve(A, b, x))
	    return false;
	for (int j = 0; j < m; ++j) cvs[j][c] = x[j];
    }
    return true;
}

static ON_NurbsCurve *
make_nurbs_curve(const vector<ON_3dPoint>& cvs, const vector<double>& U)
{
    int m = (int)cvs.size();
    ON_NurbsCurve *c = new ON_NurbsCurve(3, false, 4, m);

    for (int j = 0; j < m; ++j)
	c->SetCV(j, ON::not_rational, cvs[j]);
    for (int k = 0; k < (int)U.size(); ++k)
	c->SetKnot(k, U[k]);
    return c;
}

static ON_3dPoint
ring_centroid(const vector<ON_3dPoint>& ring)
{
    ON_3dPoint c = ON_3dPoint::Origin;
    for (size_t i = 0; i < ring.size(); ++i)
	c += ring[i];
    return c / (double)ring.size();
}

/* Plane of a closed planar ring: centroid + area-weighted normal.  Used for
 * the cap planes, so the cap matches the ring exactly regardless of how the
 * caller oriented the section.  'outward' points away from the solid; the
 * normal is forced to agree with it, because a cap whose normal faces into
 * the wing tessellates with its triangles wound the wrong way. */
static ON_Plane
ring_plane(const vector<ON_3dPoint>& ring, const ON_3dVector& outward)
{
    ON_3dPoint c = ring_centroid(ring);

    ON_3dVector n = ON_3dVector::ZeroVector;
    for (size_t i = 0; i < ring.size(); ++i)
	n += ON_CrossProduct(ring[i] - c, ring[(i + 1) % ring.size()] - c);
    if (!n.Unitize()) {
	for (size_t i = 2; i < ring.size() && !n.Unitize(); ++i)
	    n = ON_CrossProduct(ring[1] - ring[0], ring[i] - ring[0]);
    }
    if (n * outward < 0.0)
	n = -n;

    ON_3dVector xa = ring[0] - c;
    xa -= (xa * n) * n;
    if (!xa.Unitize())
	xa = ON_3dVector(1.0, 0.0, 0.0) - n.x * n;
    return ON_Plane(c, xa, ON_CrossProduct(n, xa));
}

/* --------------------------------------------------------------- sections */

/* Interpolate one closed section ring into a NURBS curve.  The ring repeats
 * its first point at the end, so the curve closes on the trailing edge. */
static ON_NurbsCurve *
section_run(const vector<ON_3dPoint>& ring, size_t i0, size_t i1)
{
    vector<ON_3dPoint> pts(ring.begin() + (long)i0, ring.begin() + (long)i1 + 1);
    vector<ON_3dPoint> cvs;
    vector<double> U;

    if (!interp(pts, cvs, U))
	return NULL;

    ON_NurbsCurve *c = make_nurbs_curve(cvs, U);
    if (c)
	c->SetDomain(0.0, 1.0);
    return c;
}

/* Index of the ring point farthest from the first one: the leading edge, the
 * corner opposite the trailing-edge seam.  Picking it from the geometry keeps
 * this independent of the handedness the caller generated the ring with. */
static int
ring_far_point(const vector<ON_3dPoint>& ring)
{
    int best = 1;
    double dmax = -1.0;

    for (size_t i = 1; i + 1 < ring.size(); ++i) {
	const double d = ring[i].DistanceTo(ring[0]);
	if (d > dmax) {
	    dmax = d;
	    best = (int)i;
	}
    }
    return best;
}

/* The vertex of edge ei that sits at the start of the edge's 3D curve. */
static int
edge_start_vertex(const ON_Brep *b, int ei)
{
    const ON_BrepEdge& e = b->m_E[ei];

    if (e.m_c3i >= 0) {
	const ON_3dPoint p = b->m_C3[e.m_c3i]->PointAtStart();
	if (b->m_V[e.m_vi[1]].point.DistanceTo(p) <
	    b->m_V[e.m_vi[0]].point.DistanceTo(p))
	    return e.m_vi[1];
    }
    return e.m_vi[0];
}

/* A straight edge between two existing vertices: the TE and LE ridges. */
static int
add_line_edge(ON_Brep *b, int v0, int v1, const ON_3dPoint& p0,
	      const ON_3dPoint& p1, double tol)
{
    /* A degree-1 NURBS line rather than ON_LineCurve: its parameter is
     * normalized to 0..1 like the section curves.  ON_LineCurve would be
     * parameterized by length in model units (0..333 here), leaving the ruled
     * surface with a wildly anisotropic (u,v) domain. */
    ON_NurbsCurve *line = new ON_NurbsCurve(3, false, 2, 2);
    line->SetCV(0, p0);
    line->SetCV(1, p1);
    /* ON stores only cv+order-2 knots: for a degree-1 curve with 2 CVs the
     * vector is exactly {0,1} and the domain follows. */
    line->SetKnot(0, 0.0);
    line->SetKnot(1, 1.0);
    const int c3i = b->AddEdgeCurve(ON_Curve::Cast(line));
    ON_BrepEdge& e = b->NewEdge(b->m_V[v0], b->m_V[v1], c3i);
    e.m_tolerance = tol;
    return e.m_edge_index;
}

/* Cap a split section: one planar face whose outer loop holds both halves, so
 * the vertices and the two edges it creates are the ones the skin rules to.
 * ON decides the direction of those edges from the loop orientation, so they
 * are identified by the corner their curve starts at. */
static int
add_cap2(ON_Brep *b, const vector<ON_3dPoint>& ring, const ON_3dVector& outward,
	 ON_NurbsCurve *clo, ON_NurbsCurve *cup, int *elo, int *eup)
{
    ON_PlaneSurface *p = new ON_PlaneSurface();
    p->m_plane = ring_plane(ring, outward);
    p->SetDomain(0, -100.0, 100.0);
    p->SetDomain(1, -100.0, 100.0);
    p->SetExtents(0, p->Domain(0));
    p->SetExtents(1, p->Domain(1));
    const int si = b->AddSurface(p);

    ON_BrepFace& face = b->NewFace(si);
    ON_SimpleArray<ON_Curve*> boundary;
    boundary.Append(ON_Curve::Cast(clo));
    boundary.Append(ON_Curve::Cast(cup));
    if (!b->NewPlanarFaceLoop(face.m_face_index, ON_BrepLoop::outer, boundary, true))
	return -1;

    const ON_BrepLoop *loop = b->m_L.Last();
    p->SetDomain(0, loop->m_pbox.m_min.x, loop->m_pbox.m_max.x);
    p->SetDomain(1, loop->m_pbox.m_min.y, loop->m_pbox.m_max.y);
    p->SetExtents(0, p->Domain(0));
    p->SetExtents(1, p->Domain(1));

    b->SetTrimIsoFlags(face);

    const int e0 = b->m_E.Count() - 2;
    const int e1 = b->m_E.Count() - 1;
    const double d0 = b->m_V[edge_start_vertex(b, e0)].point.DistanceTo(ring[0]);
    const double d1 = b->m_V[edge_start_vertex(b, e1)].point.DistanceTo(ring[0]);
    *elo = (d0 < d1) ? e0 : e1;
    *eup = (d0 < d1) ? e1 : e0;
    return 0;
}

/* Skin the stations as two open ruled faces per panel.
 *
 * Every ring is cut at the TE and at the LE, the two halves are skinned as
 * separate faces and the TE and LE ridges become ordinary edges shared by the
 * lower and the upper face; the caps get a two-trim outer loop.  Keeping the
 * ring closed in one surface instead (the original approach) leaves a
 * degenerate TE seam that the CDT mesher behind g-stl(1) and the mged display
 * does not always resolve - "misoriented edges" along the seam and then
 * "tessellation failure" - although mged's own solid test, rt(1) and
 * g-iges(1) accept it.
 *
 * The two runs are always skinned from the TE towards the LE below and from
 * the LE towards the TE above, whatever direction ON gave the cap edges, so
 * the two skins come out with opposite, outward normals without any face
 * flipping, and the shell is a closed oriented solid. */
static ON_Brep *
build_solid_split(vector<vector<ON_3dPoint> > secs)
{
    const int ns = (int)secs.size();

    {
	ON_3dPoint c0 = ring_centroid(secs[0]);
	ON_3dVector span = ring_centroid(secs[ns - 1]) - c0;
	ON_3dVector n = ON_3dVector::ZeroVector;
	for (size_t i = 0; i < secs[0].size(); ++i)
	    n += ON_CrossProduct(secs[0][i] - c0,
		secs[0][(i + 1) % secs[0].size()] - c0);
	if (n * span < 0.0) {
	    for (int j = 0; j < ns; ++j)
		reverse(secs[j].begin(), secs[j].end());
	}
    }

    const int k = ring_far_point(secs[0]);

    ON_3dPoint lo = secs[0][0], hi = secs[0][0];
    for (int j = 0; j < ns; ++j) {
	for (size_t i = 0; i < secs[j].size(); ++i) {
	    const ON_3dPoint& p = secs[j][i];
	    for (int m = 0; m < 3; ++m) {
		if (p[m] < lo[m]) lo[m] = p[m];
		if (p[m] > hi[m]) hi[m] = p[m];
	    }
	}
    }
    const double tol = lo.DistanceTo(hi) * 1e-9;

    ON_3dPoint mid = ON_3dPoint::Origin;
    for (int j = 0; j < ns; ++j)
	mid += ring_centroid(secs[j]);
    mid /= (double)ns;

    ON_Brep *b = ON_Brep::New();
    vector<int> vte(ns, -1), vle(ns, -1), elo(ns, -1), eup(ns, -1);

    for (int j = 0; j < ns; ++j) {
	const vector<ON_3dPoint>& r = secs[j];
	ON_NurbsCurve *clo = section_run(r, 0, (size_t)k);
	ON_NurbsCurve *cup = section_run(r, (size_t)k, r.size() - 1);

	if (!clo || !cup) {
	    delete b;
	    return NULL;
	}

	if (j == 0 || j == ns - 1) {
	    if (add_cap2(b, r, ring_centroid(r) - mid, clo, cup,
			 &elo[j], &eup[j]) < 0) {
		delete b;
		return NULL;
	    }
	    const int vs = edge_start_vertex(b, elo[j]);
	    vte[j] = vs;
	    vle[j] = (b->m_E[elo[j]].m_vi[0] == vs) ? b->m_E[elo[j]].m_vi[1]
						   : b->m_E[elo[j]].m_vi[0];
	} else {
	    /* Indices only: NewVertex and NewEdge append to m_V/m_E, so a
	     * reference taken across another such call can dangle. */
	    vte[j] = b->NewVertex(r[0], tol).m_vertex_index;
	    vle[j] = b->NewVertex(r[k], tol).m_vertex_index;

	    const int c3i_lo = b->AddEdgeCurve(ON_Curve::Cast(clo));
	    elo[j] = b->NewEdge(b->m_V[vte[j]], b->m_V[vle[j]], c3i_lo).m_edge_index;
	    b->m_E[elo[j]].m_tolerance = tol;

	    const int c3i_up = b->AddEdgeCurve(ON_Curve::Cast(cup));
	    eup[j] = b->NewEdge(b->m_V[vle[j]], b->m_V[vte[j]], c3i_up).m_edge_index;
	    b->m_E[eup[j]].m_tolerance = tol;
	}
    }

    /* The ridges first: NewRuledFace looks for an existing straight edge
     * between the same two vertices and reuses it, and that reuse is what
     * welds the panels into one shell. */
    for (int j = 0; j + 1 < ns; ++j) {
	if (add_line_edge(b, vte[j], vte[j + 1], secs[j][0],
			  secs[j + 1][0], tol) < 0 ||
	    add_line_edge(b, vle[j], vle[j + 1], secs[j][k],
			  secs[j + 1][k], tol) < 0) {
	    delete b;
	    return NULL;
	}
    }

    for (int j = 0; j + 1 < ns; ++j) {
	const bool la = (edge_start_vertex(b, elo[j]) != vte[j]);
	const bool lb = (edge_start_vertex(b, elo[j + 1]) != vte[j + 1]);
	const bool ua = (edge_start_vertex(b, eup[j]) != vle[j]);
	const bool ub = (edge_start_vertex(b, eup[j + 1]) != vle[j + 1]);

	if (!b->NewRuledFace(b->m_E[elo[j]], la, b->m_E[elo[j + 1]], lb)) {
	    delete b;
	    return NULL;
	}
	if (!b->NewRuledFace(b->m_E[eup[j]], ua, b->m_E[eup[j + 1]], ub)) {
	    delete b;
	    return NULL;
	}
	/* la/lb/ua/ub put every section run in the direction ON expects, so the
	 * two skins of a panel come out with opposite, outward normals; no face
	 * has to be reversed after the fact. */
    }

    b->Compact();

    {
	ON_wString w;
	ON_TextLog tl(w);
	if (!b->IsValid(&tl)) {
	    ON_String s(w);
	    bu_log("loftwing: split brep invalid:\n%s", s.Array());
	}
    }

    return b;
}

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
 *          Each station must be a planar ring in TE -> upper -> LE -> lower
 *          -> TE order, closed on the trailing edge (the TE point appears
 *          both first and last, so the section curve is closed).
 */
extern "C" int
loftwing_brep(struct rt_wdb *fp, const char *name, const double *sec, int ns, int np)
{
    if (ns < 2 || np < 4) {
	bu_log("loftwing: need at least 2 stations and 4 points per section "
	       "(got %d and %d)\n", ns, np);
	return -1;
    }

    vector<vector<ON_3dPoint> > secs(ns, vector<ON_3dPoint>(np));
    for (int j = 0; j < ns; ++j)
	for (int i = 0; i < np; ++i) {
	    const double *p = sec + ((size_t)j * np + i) * 3;
	    secs[j][i] = ON_3dPoint(p[0], p[1], p[2]);
	}

    ON_Brep *brep = build_solid_split(secs);

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
