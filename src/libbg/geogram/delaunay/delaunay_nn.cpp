/*
 *  Copyright (c) 2000-2022 Inria
 *  All rights reserved.
 *
 *  Redistribution and use in source and binary forms, with or without
 *  modification, are permitted provided that the following conditions are met:
 *
 *  * Redistributions of source code must retain the above copyright notice,
 *  this list of conditions and the following disclaimer.
 *  * Redistributions in binary form must reproduce the above copyright notice,
 *  this list of conditions and the following disclaimer in the documentation
 *  and/or other materials provided with the distribution.
 *  * Neither the name of the ALICE Project-Team nor the names of its
 *  contributors may be used to endorse or promote products derived from this
 *  software without specific prior written permission.
 *
 *  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 *  AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 *  IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 *  ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
 *  LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 *  CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 *  SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 *  INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 *  CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 *  ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 *  POSSIBILITY OF SUCH DAMAGE.
 *
 *  Contact: Bruno Levy
 *
 *     https://www.inria.fr/fr/bruno-levy
 *
 *     Inria,
 *     Domaine de Voluceau,
 *     78150 Le Chesnay - Rocquencourt
 *     FRANCE
 *
 */

#include "common.h"
#include <geogram/delaunay/delaunay_nn.h>

namespace GEOBRL {

    Delaunay_NearestNeighbors::Delaunay_NearestNeighbors(
        coord_index_t dimension
    ) :
        Delaunay(dimension) {
        set_thread_safe(true);
        set_default_nb_neighbors(20);
        set_stores_neighbors(true);
        // NN_ is created in apply_options(), called by Delaunay::create().
    }

    void Delaunay_NearestNeighbors::apply_options(const GeoOptions& opts) {
        NN_ = NearestNeighborSearch::create(dimension(), opts);
    }

    Delaunay_NearestNeighbors::~Delaunay_NearestNeighbors() {
    }

    void Delaunay_NearestNeighbors::enlarge_neighborhood(
        index_t v, index_t nb
    ) {
        if(v >= nb_vertices() || v >= neighbors_.nb_arrays() || nb == 0) {
            return;
        }
        neighbors_.lock_array(v);
        if(nb > neighbors_.array_size(v)) {
            if(nb <= 512) {
                index_t* neighbors = (index_t*) BRLCAD_ALLOCA(
                    sizeof(index_t) * nb
                );
                nb = get_neighbors_internal(v, nb, neighbors);
                neighbors_.set_array(v, nb, neighbors, false);
            } else {
                vector<index_t> neighbors(nb);
                nb = get_neighbors_internal(v, nb, neighbors.data());
                neighbors_.set_array(v, nb, neighbors.data(), false);
            }
        }
        neighbors_.unlock_array(v);
    }

    void Delaunay_NearestNeighbors::store_neighbors_CB(index_t v) {
        // No need to lock/unlock array here, since we are
        // sure that each array is accessed by a single thread.
        if(v >= nb_vertices() || nb_vertices() == 0 || v >= neighbors_.nb_arrays()) {
            return;
        }
        index_t nb = neighbors_.array_size(v);
        if(nb_vertices() > 1) {
            nb = std::min(nb, nb_vertices() - 1);
        } else {
            nb = 0;
        }
        if(nb <= 512) {
            index_t* neighbors = (index_t*) BRLCAD_ALLOCA(
                sizeof(index_t) * nb
            );
            nb = get_neighbors_internal(v, nb, neighbors);
            neighbors_.set_array(v, nb, neighbors, false);
        } else {
            vector<index_t> neighbors(nb);
            nb = get_neighbors_internal(v, nb, neighbors.data());
            neighbors_.set_array(v, nb, neighbors.data(), false);
        }
    }

    void Delaunay_NearestNeighbors::get_neighbors_internal(
        index_t v, vector<index_t>& neighbors
    ) const {
        if(neighbors.empty()) {
            return;
        }
        index_t nb = get_neighbors_internal(
            v, index_t(neighbors.size()), neighbors.data()
        );
        neighbors.resize(nb);
    }

    void Delaunay_NearestNeighbors::set_vertices(
        index_t nb_vertices, const double* vertices
    ) {
        Delaunay::set_vertices(nb_vertices, vertices);
        if(NN_) {
            NN_->set_points(nb_vertices, vertices);
        }
        update_neighbors();
    }

    index_t Delaunay_NearestNeighbors::get_neighbors_internal(
        index_t i, index_t nb_neigh, index_t* neighbors
    ) const {
        if(!NN_ || nb_vertices() == 0 || nb_neigh == 0 || neighbors == nullptr || i >= nb_vertices()) {
            return 0;
        }
        nb_neigh++;
        nb_neigh = std::min(nb_neigh, nb_vertices());

        index_t* closest_pt_ix = nullptr;
        double* closest_pt_dist = nullptr;
        vector<index_t> closest_pt_ix_vec;
        vector<double> closest_pt_dist_vec;

        if(nb_neigh <= 512) {
            closest_pt_ix = (index_t*) BRLCAD_ALLOCA(sizeof(index_t) * nb_neigh);
            closest_pt_dist = (double*) BRLCAD_ALLOCA(sizeof(double) * nb_neigh);
        } else {
            closest_pt_ix_vec.resize(nb_neigh);
            closest_pt_dist_vec.resize(nb_neigh);
            closest_pt_ix = closest_pt_ix_vec.data();
            closest_pt_dist = closest_pt_dist_vec.data();
        }

        NN_->get_nearest_neighbors(
            nb_neigh, i, closest_pt_ix, closest_pt_dist
        );

        index_t nb_neigh_result = 0;
        for(index_t j = 0; j < nb_neigh; j++) {
            if(closest_pt_ix[j] != i && closest_pt_ix[j] < nb_vertices()) {
                // Check for duplicated points (distance near zero)
                if(closest_pt_dist[j] <= 0.0) {
                    // If i is not the first one (in the
                    // duplicated points), then we 'disconnect' it
                    // (no neighbor !)
                    if(closest_pt_ix[j] < i) {
                        return 0;
                    }
                    // Else, i is the first one, and we simply
                    // skip (do not store) the connection with
                    // closest_pt_ix[j].
                } else {
                    neighbors[nb_neigh_result] = closest_pt_ix[j];
                    nb_neigh_result++;
                    if(nb_neigh_result == nb_neigh - 1) {
                        break;
                    }
                }
            }
        }
        return nb_neigh_result;
    }

    index_t Delaunay_NearestNeighbors::nearest_vertex(const double* p) const {
        if(!NN_ || p == nullptr || nb_vertices() == 0) {
            return NO_INDEX;
        }
        return NN_->get_nearest_neighbor(p);
    }

    /************************************************************************/
}
