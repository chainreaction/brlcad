/*                        D M _ U T I L . C
 * BRL-CAD
 *
 * Copyright (c) 1988-2026 United States Government as represented by
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
/** @file libdm/dm_util.c
 */

#include "common.h"
#include <string.h>
#include "bu/malloc.h"
#include "bn.h"
#include "bg.h"
#include "dm.h"

#include "./include/private.h"

int
draw_Line3D(struct dm *dmp, point_t pt1, point_t pt2)
{
    if (!dmp)
	return BRLCAD_ERROR;

    if (bg_pnt3_pnt3_equal(pt1, pt2, NULL)) {
	/* nothing to do for a singular point */
	return BRLCAD_OK;
    }

    return BRLCAD_OK;
}


void
flip_display_image_vertically(unsigned char *image, size_t width, size_t height, int alpha)
{
    size_t i, j;
    int psize;
    size_t row_bytes;
    size_t img_bytes;
    unsigned char *inv_img;

    if (!image || width == 0 || height == 0)
	return;

    psize = (!alpha) ? 3 : 4;
    if (width > SIZE_MAX / (size_t)psize)
	return;
    row_bytes = width * psize * sizeof(unsigned char);
    if (height > SIZE_MAX / row_bytes)
	return;
    img_bytes = row_bytes * height;

    inv_img = (unsigned char *)bu_malloc(img_bytes,
	    "inverted image");
    if (!inv_img)
	return;

    for (i = 0, j = height - 1; i < height; ++i, --j) {
	memcpy(inv_img + i * row_bytes, image + j * row_bytes, row_bytes);
    }
    memcpy(image, inv_img, img_bytes);
    bu_free(inv_img, "inverted image");
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
