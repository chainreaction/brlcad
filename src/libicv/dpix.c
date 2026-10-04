/*                          D P I X . C
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
/** @file dpix.c
 *
 * this contains read/write routines for dpix format.
 *
 */

#include "common.h"

#include <math.h>
#include <sys/stat.h>  /* for file mode info in WRMODE */
#include <string.h>

#include "bio.h"
#include "vmath.h"
#include "bu/cv.h"
#include "bu/log.h"
#include "bu/malloc.h"
#include "icv_private.h"

#define WRMODE S_IRUSR|S_IRGRP|S_IROTH
#define DPIX_CHANNELS 3


/* DPIX files store doubles in network byte order so their representation is
 * independent of host byte order. */
static void
dpix_decode(double *out, const unsigned char *in, size_t count)
{
    bu_cv_ntohd((unsigned char *)out, in, count);
}


static void
dpix_encode(unsigned char *out, const double *in, size_t count)
{
    bu_cv_htond(out, (const unsigned char *)in, count);
}

/*
 * This function normalizes the data array of the input image.
 * This performs the normalization when the input image has data
 * entries less than 0.0 or greater than 1.0 .
 */
static icv_image_t *
icv_normalize(icv_image_t *bif)
{
    double *data;
    double max, min;
    double m, b;
    size_t size;
    size_t i;

    if (bif == NULL || bif->data == NULL) {
	bu_log("icv_normalize : trying to normalize a NULL bif or data\n");
	return bif;
    }

    if (bif->width == 0 || bif->height == 0 || bif->channels == 0)
	return bif;

    if (bif->width > SIZE_MAX / bif->height / bif->channels)
	return bif;

    size = bif->height*bif->width*bif->channels;
    data = bif->data;

    min = INFINITY;
    max = -INFINITY;

    for (i = 0; i < size; i++) {
	double val = *data++;
	if (isnan(val))
	    continue;
	V_MIN(min, val);
	V_MAX(max, val);
    }

    if (isnan(min) || isinf(min) || isnan(max) || isinf(max)) {
	icv_sanitize(bif);
	return bif;
    }

    /* strict Condition for avoiding normalization */
    if (max <= 1.0 && min >= 0.0)
	return bif;

    if (max - min < 1e-12 || ZERO(max - min)) {
	icv_sanitize(bif);
	return bif;
    }

    data = bif->data;
    m = 1.0 / (max - min);
    b = -min / (max - min);

    for (i = 0; i < size; i++) {
	double val = *data;
	if (isnan(val))
	    *data = 0.0;
	else
	    *data = m * val + b;
	data++;
    }

    return bif;
}


icv_image_t *
dpix_read(FILE *fp, size_t width, size_t height)
{
    if (UNLIKELY(!fp))
	return NULL;

    icv_image_t *bif;
    size_t size;

    if (width == 0 || height == 0) {
	bu_log("dpix_read : Using default size.\n");
	height = 512;
	width = 512;
    }

    if (width > 0 && height > (size_t)-1 / width / DPIX_CHANNELS /
	    SIZEOF_NETWORK_DOUBLE) {
	bu_log("dpix_read: dimensions excessively large, causing integer overflow\n");
	return NULL;
    }

    bif = icv_create(width, height, ICV_COLOR_SPACE_RGB);
    if (!bif || !bif->data) {
	if (bif)
	    icv_destroy(bif);
	return NULL;
    }

    /* Size in Bytes for reading. */
    size_t sample_count = width * height * DPIX_CHANNELS;
    size = sample_count * SIZEOF_NETWORK_DOUBLE;
    unsigned char *encoded = (unsigned char *)bu_malloc(size,
	"dpix encoded input");
    if (!encoded) {
	icv_destroy(bif);
	return NULL;
    }

    size_t ret = fread(encoded, 1, size, fp);
    if (ret != size) {
	bu_log("dpix_read : Error while reading\n");
	bu_free(encoded, "dpix encoded input");
	icv_destroy(bif);
	return NULL;
    }

    dpix_decode(bif->data, encoded, sample_count);
    bu_free(encoded, "dpix encoded input");

    icv_normalize(bif);

    return bif;
}


int
dpix_write(icv_image_t *bif, FILE *fp)
{
    icv_image_t *wimg;

    if (UNLIKELY(!bif))
	return BRLCAD_ERROR;
    if (UNLIKELY(!fp))
	return BRLCAD_ERROR;

    wimg = icv_image_for_write(bif, ICV_COLOR_SPACE_RGB, DPIX_CHANNELS);
    if (!wimg) {
	bu_log("dpix_write : Color Space conflict\n");
	return BRLCAD_ERROR;
    }

    if (wimg->channels != DPIX_CHANNELS) {
	bu_log("dpix_write : Channel count conflict (expected 3, got %d)\n", (int)wimg->channels);
	icv_destroy(wimg);
	return BRLCAD_ERROR;
    }

    if (!wimg->data || wimg->width == 0 || wimg->height == 0) {
	icv_destroy(wimg);
	return BRLCAD_ERROR;
    }

    if (wimg->width > SIZE_MAX / wimg->height / DPIX_CHANNELS) {
	bu_log("dpix_write : Dimensions overflow size_t\n");
	icv_destroy(wimg);
	return BRLCAD_ERROR;
    }

    size_t sample_count = wimg->width * wimg->height * DPIX_CHANNELS;
    if (sample_count > SIZE_MAX / SIZEOF_NETWORK_DOUBLE) {
	bu_log("dpix_write : Byte size overflows size_t\n");
	icv_destroy(wimg);
	return BRLCAD_ERROR;
    }

    size_t size = sample_count * SIZEOF_NETWORK_DOUBLE;
    unsigned char *encoded = (unsigned char *)bu_malloc(size,
	"dpix encoded output");
    if (!encoded) {
	icv_destroy(wimg);
	return BRLCAD_ERROR;
    }

    dpix_encode(encoded, wimg->data, sample_count);

    size_t ret = fwrite(encoded, 1, size, fp);
    bu_free(encoded, "dpix encoded output");
    icv_destroy(wimg);

    if (ret != size) {
	bu_log("dpix_write : Short Write\n");
	return BRLCAD_ERROR;
    }

    return BRLCAD_OK;
}

int
dpix_write_mem(icv_image_t *bif, unsigned char **outbuffer, size_t *outsize)
{
    icv_image_t *wimg;

    if (UNLIKELY(!bif))
	return BRLCAD_ERROR;
    if (UNLIKELY(!outbuffer || !outsize))
	return BRLCAD_ERROR;

    *outbuffer = NULL;
    *outsize = 0;

    wimg = icv_image_for_write(bif, ICV_COLOR_SPACE_RGB, DPIX_CHANNELS);
    if (!wimg) {
	bu_log("dpix_write_mem : Color Space conflict\n");
	return BRLCAD_ERROR;
    }

    if (wimg->channels != DPIX_CHANNELS) {
	bu_log("dpix_write_mem : Channel count conflict (expected 3, got %d)\n", (int)wimg->channels);
	icv_destroy(wimg);
	return BRLCAD_ERROR;
    }

    if (!wimg->data || wimg->width == 0 || wimg->height == 0) {
	icv_destroy(wimg);
	return BRLCAD_ERROR;
    }

    if (wimg->width > SIZE_MAX / wimg->height / DPIX_CHANNELS) {
	bu_log("dpix_write_mem : Dimensions overflow size_t\n");
	icv_destroy(wimg);
	return BRLCAD_ERROR;
    }

    size_t sample_count = (size_t)wimg->width * wimg->height * DPIX_CHANNELS;
    if (sample_count > SIZE_MAX / SIZEOF_NETWORK_DOUBLE) {
	bu_log("dpix_write_mem : Byte size overflows size_t\n");
	icv_destroy(wimg);
	return BRLCAD_ERROR;
    }

    *outsize = sample_count * SIZEOF_NETWORK_DOUBLE;
    *outbuffer = (unsigned char *)bu_malloc(*outsize, "dpix_write_mem buffer");
    if (!*outbuffer) {
	*outsize = 0;
	icv_destroy(wimg);
	return BRLCAD_ERROR;
    }

    dpix_encode(*outbuffer, wimg->data, sample_count);
    icv_destroy(wimg);

    return BRLCAD_OK;
}

icv_image_t *
dpix_read_mem(const unsigned char *buffer, size_t size, size_t width, size_t height)
{
    if (UNLIKELY(!buffer || size == 0))
	return NULL;

    icv_image_t *bif;

    if (width == 0 || height == 0) {
	bu_log("dpix_read_mem: Using default size.\n");
	height = 512;
	width = 512;
    }

    if (width > 0 && height > (size_t)-1 / width / DPIX_CHANNELS /
	    SIZEOF_NETWORK_DOUBLE) {
	bu_log("dpix_read_mem: dimensions excessively large, causing integer overflow\n");
	return NULL;
    }

    size_t sample_count = width * height * DPIX_CHANNELS;
    if (sample_count > SIZE_MAX / SIZEOF_NETWORK_DOUBLE) {
	bu_log("dpix_read_mem: dimensions overflow size_t\n");
	return NULL;
    }

    size_t expected_size = sample_count * SIZEOF_NETWORK_DOUBLE;
    if (size < expected_size) {
	bu_log("dpix_read_mem: Buffer size too small for dimensions\n");
	return NULL;
    }

    bif = icv_create(width, height, ICV_COLOR_SPACE_RGB);
    if (!bif || !bif->data) {
	if (bif)
	    icv_destroy(bif);
	return NULL;
    }

    dpix_decode(bif->data, buffer, sample_count);

    icv_normalize(bif);

    return bif;
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
