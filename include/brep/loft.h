/*                    L O F T . H
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
/** @addtogroup brep_loft
 * @brief
 * NURBS solids lofted through a sequence of closed planar sections.
 */
#ifndef BREP_LOFT_H
#define BREP_LOFT_H

#include "common.h"
#include "brep/defines.h"

/** @{ */
/** @file brep/loft.h */

#ifdef __cplusplus

extern "C++" {

/**
 * Build a closed, oriented NURBS solid by lofting a sequence of closed
 * planar sections.
 *
 * Each section is a ring of points in order around the section; the ring is
 * closed implicitly (the last point is joined back to the first), so it must
 * not repeat a point.  Consecutive sections are interpolated with a cubic
 * that passes exactly through the given points, both across the sections and
 * around each ring, and the two end sections are closed with planar caps.
 * Each panel between two sections is skinned as two separate ruled faces
 * instead of one surface closed in u: that keeps a degenerate seam out of the
 * shell, which is what makes the result tessellatable (see the
 * implementation for the details).
 *
 * All sections must have the same number of points, at least 8 (each of the
 * two halves of a ring is interpolated with a cubic), and at least 2 sections
 * are needed.  Sections are taken in the order given, and the ring handedness
 * is normalized, so it does not matter which way round a caller lists them.
 *
 * @param points [in] ns * np points, section by section: the point at index
 * (section * np + i) is the i'th point of that section's ring
 * @param ns [in] number of sections (at least 2)
 * @param np [in] points per section (at least 8, the same for every section)
 * @return a new ON_Brep the caller is responsible for deleting, or NULL if
 * the sections are unusable or the shell could not be built
 */
extern BREP_EXPORT ON_Brep *
brep_loft(const ON_3dPoint *points, int ns, int np);

} /* extern C++ */
#endif

#endif  /* BREP_LOFT_H */
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
