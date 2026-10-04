/*                      E N C O D I N G . C
 * BRL-CAD
 *
 * Copyright (c) 2013-2026 United States Government as represented by
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
/** @file encoding.c
 *
 * Contains encoding conversion supports for various file formats.
 *
 */

#include "common.h"
#include <math.h>
#include <stdint.h>
#include "icv.h"
#include "vmath.h"
#include "bu/magic.h"
#include "bu/malloc.h"
#include "bn.h"

double *
icv_uchar2double(unsigned char *data, size_t size)
{
    double *double_data, *double_p;
    unsigned char *char_p;

    if (!data || size == 0 || size > SIZE_MAX / sizeof(double))
	return NULL;

    char_p = data;
    double_p = double_data = (double *) bu_malloc(size*sizeof(double), "uchar2data : double data");
    if (!double_data)
	return NULL;

    while (size--) {
	*double_p = ICV_CONV_8BIT(*char_p);
	double_p++;
	char_p++;
    }

    return double_data;
}


unsigned char *
icv_data2uchar(const icv_image_t *bif)
{
    size_t size;
    size_t pixels;
    unsigned char *uchar_data, *char_p;
    double *double_p;

    if (!bif) {
	return NULL;
    }

    ICV_IMAGE_VAL_PTR(bif);

    if (!bif->data || bif->width == 0 || bif->height == 0 || bif->channels == 0) {
	return NULL;
    }

    if (bif->width > SIZE_MAX / bif->height)
	return NULL;
    pixels = bif->width * bif->height;
    if (bif->channels > SIZE_MAX / pixels)
	return NULL;
    size = pixels * bif->channels;

    char_p = uchar_data = (unsigned char *) bu_malloc(size, "data2uchar : unsigned char data");
    if (!char_p) {
	return NULL;
    }

    double_p = bif->data;

    if (ZERO(bif->gamma_corr) || bif->gamma_corr <= 0.0) {
	while (size--) {
	    double val = *double_p;
	    if (isnan(val) || val <= 0.0) {
		*char_p = 0;
	    } else if (val >= 1.0) {
		*char_p = 255;
	    } else {
		long longval = lrint(val * 255.0);
		*char_p = (longval > 255) ? 255 : (longval < 0 ? 0 : (unsigned char)longval);
	    }

	    char_p++;
	    double_p++;
	}

    } else {
	float *rand_p = NULL;
	double ex = 1.0 / bif->gamma_corr;
	bn_rand_init(rand_p, 0);

	while (size--) {
	    double val = *double_p;
	    if (isnan(val) || val <= 0.0) {
		*char_p = 0;
	    } else if (val >= 1.0) {
		*char_p = 255;
	    } else {
		long longval = lrint(pow(val, ex) * 255.0 + (double) bn_rand0to1(rand_p));
		*char_p = (longval > 255) ? 255 : (longval < 0 ? 0 : (unsigned char)longval);
	    }

	    char_p++;
	    double_p++;
	}
    }

    return uchar_data;
}


/*
 * Local Variables:
 * tab-width: 8
 * mode: C
 * indent-tabs-mode: t
 * c-file-style: "stroustrup"
 * End:
 * ex: shiftwidth=4 tabstop=8
 */
