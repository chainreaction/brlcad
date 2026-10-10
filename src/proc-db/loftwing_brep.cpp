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
 * starts and ends at the trailing edge:
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
 * Known limitation: the skin is closed in u, and the CDT tessellator behind
 * g-stl(1) and the mged display does not always mesh such a surface when the
 * planform tapers - it reports "misoriented edges" along the TE seam and then
 * "tessellation failure" (0 triangles written).  The solid itself is valid:
 * mged's "brep <obj> solid" confirms it and rt(1) and g-iges(1) handle it
 * without complaint.  Reproduction: --span 1000 --root-chord 800 --tip-chord
 * 400 --sweep 0 --twist 0 --dihedral 0 --naca 0012 --stations 4 --points 12.
 * Rectangular planforms (root chord == tip chord), circular and polygonal test
 * sections, and rcc/trc BREP conversions all tessellate, and no tessellation
 * tolerance (g-stl -a/-r/-n) changes the outcome, so the failure sits in the
 * mesher rather than in this construction.  --format ars (the default) is the
 * back end that works everywhere.
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
section_curve(const vector<ON_3dPoint>& ring)
{
    vector<ON_3dPoint> cvs;
    vector<double> U;

    if (!interp(ring, cvs, U))
	return NULL;

    ON_NurbsCurve *c = make_nurbs_curve(cvs, U);
    c->SetDomain(0.0, 1.0);
    return c;
}

/* ------------------------------------------------------------------ brep */

/* Cap one section: a planar surface whose domain is tightened to the loop's
 * bounding box, trimmed by the section curve itself.  ON creates the 3D edge
 * of that loop, and that edge is what the skin is ruled to - which is why the
 * caps must be added before the ruled faces.
 *
 * 'outward' points away from the solid and decides the plane normal.  This is
 * where we differ from librt: tgc_brep's cap planes take the normal of the
 * cone's own axes, which on the bottom cap points along the axis *into* the
 * solid, so it has to FlipFace afterwards.  Ours already faces out, and
 * flipping it on top of that is what left the bottom cap wound against the
 * skin and made the mesher report misoriented edges. */
static int
add_cap(ON_Brep *b, const vector<ON_3dPoint>& ring, const ON_3dVector& outward,
	ON_NurbsCurve *curve)
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
    boundary.Append(ON_Curve::Cast(curve));
    if (!b->NewPlanarFaceLoop(face.m_face_index, ON_BrepLoop::outer, boundary, true))
	return -1;

    const ON_BrepLoop *loop = b->m_L.Last();
    p->SetDomain(0, loop->m_pbox.m_min.x, loop->m_pbox.m_max.x);
    p->SetDomain(1, loop->m_pbox.m_min.y, loop->m_pbox.m_max.y);
    p->SetExtents(0, p->Domain(0));
    p->SetExtents(1, p->Domain(1));

    b->SetTrimIsoFlags(face);

    return b->m_E.Count() - 1;
}

/* A section that has no cap of its own still needs a 3D edge for the skin to
 * be ruled to.  The ring is closed, so both ends of the edge are one vertex. */
static int
add_section_edge(ON_Brep *b, ON_NurbsCurve *curve, double tol)
{
    const int c3i = b->AddEdgeCurve(ON_Curve::Cast(curve));
    ON_BrepVertex& v = b->NewVertex(curve->PointAtStart(), tol);
    ON_BrepEdge& e = b->NewEdge(v, v, c3i);
    e.m_tolerance = tol;
    return e.m_edge_index;
}

/* Skin the sections: a planar cap on the first and the last station, and one
 * ruled face between every pair of neighbouring section edges.
 *
 * This is the construction librt uses for rcc/tgc
 * (src/librt/primitives/tgc/tgc_brep.cpp), and the only one the CDT mesher
 * accepts: ON keeps the trim, vertex and edge indices consistent as the faces
 * are added, so the shell comes out closed and correctly wound.  Building the
 * trims by hand instead - one tensor-product surface plus a manually welded
 * seam and two caps - produced a b-rep that IsValid() called a solid and that
 * rt and g-iges converted, but whose triangles the mesher could not repair. */
static ON_Brep *
build_solid(vector<vector<ON_3dPoint> > secs)
{
    const int ns = (int)secs.size();

    /* The skin and the caps have to agree on which way is out.  Ruling two
     * sections puts the surface normal along ring_tangent x span, so the ring
     * must wind counterclockwise as seen from the station after it - the way
     * a circle sampled by increasing angle does.  Every station comes in the
     * same order, so if the first one is wound the other way, they all are. */
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

    ON_Brep *b = ON_Brep::New();

    ON_3dPoint lo = secs[0][0], hi = secs[0][0];
    for (int j = 0; j < ns; ++j) {
	for (size_t i = 0; i < secs[j].size(); ++i) {
	    const ON_3dPoint& p = secs[j][i];
	    for (int k = 0; k < 3; ++k) {
		if (p[k] < lo[k]) lo[k] = p[k];
		if (p[k] > hi[k]) hi[k] = p[k];
	    }
	}
    }
    const double tol = lo.DistanceTo(hi) * 1e-9;

    ON_3dPoint mid = ON_3dPoint::Origin;
    for (int j = 0; j < ns; ++j)
	mid += ring_centroid(secs[j]);
    mid /= (double)ns;

    vector<int> eidx(ns, -1);
    for (int j = 0; j < ns; ++j) {
	ON_NurbsCurve *c = section_curve(secs[j]);
	if (!c) {
	    delete b;
	    return NULL;
	}
	if (j == 0 || j == ns - 1) {
	    eidx[j] = add_cap(b, secs[j], ring_centroid(secs[j]) - mid, c);
	} else {
	    eidx[j] = add_section_edge(b, c, tol);
	}
	if (eidx[j] < 0) {
	    delete b;
	    return NULL;
	}
    }

    for (int j = 0; j + 1 < ns; ++j) {
	if (!b->NewRuledFace(b->m_E[eidx[j]], false, b->m_E[eidx[j + 1]], false)) {
	    delete b;
	    return NULL;
	}
    }

    b->Compact();

    {
	ON_wString w;
	ON_TextLog tl(w);
	if (!b->IsValid(&tl)) {
	    ON_String s(w);
	    bu_log("loftwing: invalid brep:\n%s", s.Array());
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

    ON_Brep *brep = build_solid(secs);
    if (!brep) {
	bu_log("loftwing: solid construction failed for %s\n", name);
	return -1;
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
