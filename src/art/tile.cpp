/*                        T I L E . C P P
 * BRL-CAD
 *
 * Copyright (c) 2022-2026 United States Government as represented by
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

/* interface header */
#include "tile.h"

#if defined(__GNUC__) && !defined(__clang__)
#  pragma GCC diagnostic push
#endif
#if defined(__clang__)
#  pragma clang diagnostic push
#endif
#if defined(__GNUC__) && !defined(__clang__)
#  pragma GCC diagnostic ignored "-Wall"
#  pragma GCC diagnostic ignored "-Wextra"
#endif
#if defined(__clang__)
#  pragma clang diagnostic ignored "-Wdocumentation"
#  pragma clang diagnostic ignored "-Wfloat-equal"
#  pragma clang diagnostic ignored "-Wunused-parameter"
#  pragma clang diagnostic ignored "-Wpedantic"
#  pragma clang diagnostic ignored "-Wignored-qualifiers"
#endif

#include "renderer/modeling/frame/frame.h"
#include "foundation/image/pixel.h"
#include "foundation/image/image.h"
#include "foundation/image/tile.h"

#if defined(__GNUC__) && !defined(__clang__)
#  pragma GCC diagnostic pop
#endif
#if defined(__clang__)
#  pragma clang diagnostic pop
#endif

#include "dm.h"


using namespace foundation;

extern struct fb *fbp;	/* Framebuffer handle */


void
ArtTileCallback::on_tile_end(const renderer::Frame* frame, const size_t tile_x, const size_t tile_y)
{
    if (!frame || !fbp)
	return;

    const foundation::Image& img = frame->image();
    const auto& props = img.properties();
    if (tile_x >= props.m_tile_count_x || tile_y >= props.m_tile_count_y)
	return;

    const foundation::Tile& t = img.tile(tile_x, tile_y);
    const foundation::Tile rgb(t, PixelFormatUInt8);

    size_t img_w = props.m_canvas_width;
    size_t img_h = props.m_canvas_height;
    size_t tile_w = rgb.get_width();
    size_t tile_h = rgb.get_height();
    const uint8_t *storage = reinterpret_cast<const uint8_t *>(rgb.get_storage());
    size_t storage_size = rgb.get_size();

    if (!storage || img_h == 0)
	return;

    for (size_t y = 0; y < tile_h; y++) {
	size_t y_coord = tile_y * tile_h + y;
	if (y_coord >= img_h)
	    continue;
	int fb_y = (int)(img_h - 1 - y_coord);
	for (size_t x = 0; x < tile_w; x++) {
	    size_t x_coord = tile_x * tile_w + x;
	    if (x_coord >= img_w)
		continue;
	    size_t pixel_offset = (y * tile_w + x) * 4;
	    if (pixel_offset + 4 <= storage_size) {
		fb_write(fbp, (int)x_coord, fb_y, storage + pixel_offset, 1);
	    }
	}
    }
}


// Local Variables:
// tab-width: 8
// mode: C++
// c-basic-offset: 4
// indent-tabs-mode: t
// c-file-style: "stroustrup"
// End:
// ex: shiftwidth=4 tabstop=8
