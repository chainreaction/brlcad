/*                         S P S R . C P P
 * BRL-CAD
 *
 * Copyright (c) 2015-2026 United States Government as represented by
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
/** @file spsr.cpp
 *
 * Interface to Adaptive Multigrid Solvers for Screened Poisson Surface Reconstruction
 * Compatible with mkazhdan/PoissonRecon API (v18.x+).
 *
 */

#include "common.h"
#include "vmath.h"
#include "bu/log.h"
#include "bu/malloc.h"
#include "bg/spsr.h"
#if defined(__GNUC__) && !defined(__clang__)
#  pragma GCC diagnostic push /* start new diagnostic pragma */
#  pragma GCC diagnostic ignored "-Wall"
#elif defined(__clang__)
#  pragma clang diagnostic push /* start new diagnostic pragma */
#  pragma clang diagnostic ignored "-Weverything"
#endif
#include "SPSR/Reconstructors.h"
#if defined(__GNUC__) && !defined(__clang__)
#  pragma GCC diagnostic pop /* end ignoring warnings */
#elif defined(__clang__)
#  pragma clang diagnostic pop /* end ignoring warnings */
#endif

using namespace PoissonRecon;

// Input stream for oriented samples: implements `read` for coordinates and normals
template<typename Real, unsigned int Dim>
struct PtStream : public Reconstructor::InputOrientedSampleStream<Real, Dim>
{
    PtStream(const point_t *ipnts, const vect_t *inorms, int cnt)
	: _size((cnt > 0) ? (unsigned int)cnt : 0), _current(0), _input_pnts(ipnts), _input_nrmls(inorms) {}
    void reset(void) { _current = 0; }
    bool read(Point<Real, Dim> &p, Point<Real, Dim> &n)
    {
	if (_current < _size) {
	    for (size_t d = 0; d < Dim; d++)
		p[d] = _input_pnts[_current][d];
	    for (size_t d = 0; d < Dim; d++)
		n[d] = _input_nrmls[_current][d];
	    _current++;
	    return true;
	}
	return false;
    }
    protected:
    unsigned int _size, _current;
    const point_t *_input_pnts;
    const vect_t *_input_nrmls;
};

// Output face stream: must implement write() and size()
template <typename Index>
struct PolygonStream : public Reconstructor::OutputFaceStream<2>
{
    PolygonStream(std::vector<std::vector<Index>> &polygonStream) : _polygons(polygonStream) {}
    size_t size(void) const { return _polygons.size(); }
    size_t write(const std::vector<node_index_type> &polygon)
    {
	std::vector<Index> poly(polygon.size());
	for (size_t i = 0; i < polygon.size(); i++) poly[i] = (Index)polygon[i];
	_polygons.push_back(poly);
	return _polygons.size() - 1;
    }
    protected:
    std::vector<std::vector<Index>> &_polygons;
};

// Output vertex stream: must implement write() and size()
template <typename Real, unsigned int Dim>
struct VertexStream : public Reconstructor::OutputLevelSetVertexStream<Real, Dim>
{
    VertexStream(std::vector<Real> &vCoordinates) : _vCoordinates(vCoordinates) {}
    size_t size(void) const { return _vCoordinates.size() / Dim; }
    size_t write(const Point<Real, Dim> &p, const Point<Real, Dim> &, const Real &)
    {
	for (unsigned int d = 0; d < Dim; d++) _vCoordinates.push_back(p[d]);
	return _vCoordinates.size() / Dim - 1;
    }
    protected:
    std::vector<Real> &_vCoordinates;
};

extern "C" int
bg_3d_spsr(int **faces, int *num_faces, point_t **points, int *num_pnts,
	const point_t *input_points_3d, const vect_t *input_normals_3d,
	int num_input_pnts, struct bg_3d_spsr_opts *spsr_opts)
{
    if (faces) *faces = NULL;
    if (num_faces) *num_faces = 0;
    if (points) *points = NULL;
    if (num_pnts) *num_pnts = 0;

    if (!faces || !num_faces || !points || !num_pnts ||
	!input_points_3d || !input_normals_3d || num_input_pnts < 3) {
	return -1;
    }

    using Real = fastf_t;
    static const unsigned int FEMSig = FEMDegreeAndBType<Reconstructor::Poisson::DefaultFEMDegree, Reconstructor::Poisson::DefaultFEMBoundary>::Signature;
    using FEMSigs = IsotropicUIntPack<3, FEMSig>;
    using Implicit = Reconstructor::Implicit<Real, 3, FEMSigs>;
    using Solver = Reconstructor::Poisson::Solver<Real, 3, FEMSigs>;

    Implicit *implicit = NULL;

    try {
	// Set up multithreading
	PoissonRecon::ThreadPool::ParallelizationType = PoissonRecon::ThreadPool::ASYNC;

	// Solver and extraction parameters
	Reconstructor::Poisson::SolutionParameters<Real> solverParams;
	solverParams.verbose = false;
	solverParams.depth = (spsr_opts) ? spsr_opts->depth : 8;
	solverParams.fullDepth = (spsr_opts) ? spsr_opts->full_depth : 11;
	solverParams.samplesPerNode = (spsr_opts) ? spsr_opts->samples_per_node : 1.5;
	solverParams.exactInterpolation = true;

	Reconstructor::LevelSetExtractionParameters extractionParams;
	extractionParams.forceManifold = true;
	extractionParams.linearFit = false;
	extractionParams.polygonMesh = false;
	extractionParams.verbose = false;

	PtStream<Real, 3> vstream(input_points_3d, input_normals_3d, num_input_pnts);

	implicit = Solver::Solve(vstream, solverParams);

	if (!implicit) {
	    bu_log("PoissonRecon: Solver::Solve failed\n");
	    return -1;
	}

	std::vector<std::vector<int>> polygons;
	std::vector<Real> vCoordinates;
	PolygonStream<int> pStream(polygons);
	VertexStream<Real, 3> vStream(vCoordinates);

	implicit->extractLevelSet(vStream, pStream, extractionParams);

	delete implicit;
	implicit = NULL;

	int nf = (int)(polygons.size());
	int np = (int)(vCoordinates.size() / 3);
	if (nf <= 0 || np <= 0 || (size_t)nf > SIZE_MAX / (3 * sizeof(int)) || (size_t)np > SIZE_MAX / sizeof(point_t)) {
	    return -1;
	}

	bu_log("Point cnt: %d\n", np);
	bu_log("Face cnt: %d\n", nf);

	// Allocate output arrays
	int *out_faces = (int *)bu_calloc((size_t)nf * 3, sizeof(int), "faces array");
	point_t *out_points = (point_t *)bu_calloc((size_t)np, sizeof(point_t), "points array");

	// Copy faces (triangulated output expected)
	for (int i = 0; i < nf; i++) {
	    if (polygons[i].size() != 3) {
		bu_log("Warning: polygon %d is not a triangle (has %zu vertices)\n", i, polygons[i].size());
		// TODO - triangulate if we don't have a triangle.
		continue;
	    }
	    out_faces[3*i+0] = polygons[i][0];
	    out_faces[3*i+1] = polygons[i][1];
	    out_faces[3*i+2] = polygons[i][2];
	}
	// Copy points
	for (int i = 0; i < np; i++) {
	    out_points[i][X] = vCoordinates[3*i+0];
	    out_points[i][Y] = vCoordinates[3*i+1];
	    out_points[i][Z] = vCoordinates[3*i+2];
	}

	*faces = out_faces;
	*points = out_points;
	*num_faces = nf;
	*num_pnts = np;

	return 0;
    } catch (...) {
	if (implicit) {
	    delete implicit;
	}
	bu_log("PoissonRecon: exception caught during reconstruction\n");
	return -1;
    }
}


// Local Variables:
// tab-width: 8
// mode: C++
// c-basic-offset: 4
// indent-tabs-mode: t
// c-file-style: "stroustrup"
// End:
// ex: shiftwidth=4 tabstop=8 cino=N-s
