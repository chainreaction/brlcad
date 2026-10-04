/*                          S T A T . C
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
/** @file libicv/stat.c
 *
 * This file contains image statistics and histogram routines.
 *
 */

#include "common.h"
#include <math.h>
#include <stdint.h>
#include "bu/magic.h"
#include "bu/malloc.h"
#include "icv.h"

static size_t **
icv_init_bins(icv_image_t* img, size_t n_bins)
{
    size_t c;
    size_t **bins;

    if (!img || img->channels == 0 || n_bins == 0)
	return NULL;

    bins = (size_t**) bu_malloc(sizeof(size_t*)*img->channels, "icv_init_bins : Histogram Bins");
    if (!bins)
	return NULL;

    for (c = 0; c < img->channels; c++) {
	bins[c] = (size_t*) bu_calloc(n_bins, sizeof(size_t), "icv_init_bins : Histogram Array for Channels");
	if (!bins[c]) {
	    while (c > 0) {
		c--;
		bu_free(bins[c], "icv_init_bins free");
	    }
	    bu_free(bins, "icv_init_bins free");
	    return NULL;
	}
    }
    return bins;
}


size_t **
icv_hist(icv_image_t* img, size_t n_bins)
{
    size_t i;
    size_t j;
    double *data;
    size_t temp;
    size_t size;
    size_t **bins;

    ICV_IMAGE_VAL_PTR(img);

    if (!img->data || img->width == 0 || img->height == 0 || img->channels == 0 || n_bins == 0)
	return NULL;

    if (img->width > SIZE_MAX / img->height)
	return NULL;

    size = img->width*img->height;
    data = img->data;

    bins = icv_init_bins(img, n_bins);
    if (!bins)
	return NULL;

    for (i = 0; i < size; i++) {
	for (j = 0; j < img->channels; j++) {
	    double val = (*data++) * n_bins;
	    if (isnan(val) || !(val >= 0.0)) val = 0.0;
	    temp = (size_t)val;
	    if (temp >= n_bins) temp = n_bins - 1; /* clamp max values to the final bin */
	    bins[j][temp]++;
	}
    }
    return bins;
}


double *
icv_max(icv_image_t* img)
{
    double *data = NULL;
    size_t size;
    double *max; /**< An array of size channels. */
    size_t i;

    ICV_IMAGE_VAL_PTR(img);

    if (!img->data || img->channels == 0 || img->width == 0 || img->height == 0)
	return NULL;

    if (img->width > SIZE_MAX / img->height)
	return NULL;

    max = (double *)bu_malloc(sizeof(double)*img->channels, "max values");
    if (!max)
	return NULL;

    data = img->data;
    for (i = 0; i < img->channels; i++)
	max[i] = data[i];

    size = img->width*img->height;
    while (size-- > 0) {
	for (i = 0; i < img->channels; i++) {
	    double val = *data++;
	    if (!isnan(val) && (isnan(max[i]) || max[i] < val))
		max[i] = val;
	}
    }

    return max;
}


double *
icv_sum(icv_image_t* img)
{
    double *data = NULL;
    double *sum; /**< An array of size channels. */
    size_t i;
    size_t size, j;

    ICV_IMAGE_VAL_PTR(img);

    if (!img->data || img->channels == 0 || img->width == 0 || img->height == 0)
	return NULL;

    if (img->width > SIZE_MAX / img->height)
	return NULL;

    sum = (double *)bu_malloc(sizeof(double)*img->channels, "sum values");
    if (!sum)
	return NULL;

    for (i = 0; i < img->channels; i++)
	sum[i] = 0.0;

    data = img->data;
    size = (size_t)img->width*img->height;

    for (j = 0; j < size; j++) {
	for (i = 0; i < img->channels; i++) {
	    double val = *data++;
	    if (!isnan(val))
		sum[i] += val;
	}
    }

    return sum;
}


double *
icv_mean(icv_image_t* img)
{
    double *mean;
    size_t size;
    size_t i;

    ICV_IMAGE_VAL_PTR(img);

    if (!img->data || img->channels == 0 || img->width == 0 || img->height == 0)
	return NULL;

    if (img->width > SIZE_MAX / img->height)
	return NULL;

    size = (size_t)img->width*img->height;
    if (size == 0)
	return NULL;

    mean = icv_sum(img); /**< receives sum from icv_image_sum*/
    if (!mean)
	return NULL;

    for (i = 0; i < img->channels; i++)
	mean[i] /= size;

    return mean;
}


double *
icv_min(icv_image_t* img)
{
    double *data = NULL;
    size_t size;
    double *min; /**< An array of size channels. */
    size_t i;

    ICV_IMAGE_VAL_PTR(img);

    if (!img->data || img->channels == 0 || img->width == 0 || img->height == 0)
	return NULL;

    if (img->width > SIZE_MAX / img->height)
	return NULL;

    min = (double *)bu_malloc(sizeof(double)*img->channels, "min values");
    if (!min)
	return NULL;

    data = img->data;
    for (i = 0; i < img->channels; i++)
	min[i] = data[i];

    size = (size_t)img->width*img->height;
    while (size-- > 0) {
	for (i = 0; i < img->channels; i++) {
	    double val = *data++;
	    if (!isnan(val) && (isnan(min[i]) || min[i] > val))
		min[i] = val;
	}
    }

    return min;
}


double *
icv_var(icv_image_t* img, size_t** bins, size_t n_bins)
{
    size_t i, c;
    double *var;
    double *mean;
    size_t size;
    double d;

    ICV_IMAGE_VAL_PTR(img);

    if (!bins || n_bins == 0 || !img->data || img->channels == 0 || img->width == 0 || img->height == 0)
	return NULL;

    if (img->width > SIZE_MAX / img->height)
	return NULL;

    size = (size_t) img->height*img->width;
    if (size == 0)
	return NULL;

    mean = icv_mean(img);
    if (!mean)
	return NULL;

    var = (double *) bu_calloc(img->channels, sizeof(double), "variance values");
    if (!var) {
	bu_free(mean, "mean values");
	return NULL;
    }

    for (i = 0; i < n_bins; i++) {
	for (c = 0; c < img->channels; c++) {
	    if (!bins[c])
		continue;
	    d = (double)i - n_bins*mean[c];
	    var[c] += bins[c][i] * d * d;
	}
    }

    for (c = 0; c < img->channels; c++) {
	var[c] /= size;
    }

    bu_free(mean, "mean values");

    return var;
}


double *
icv_skew(icv_image_t* img, size_t** bins, size_t n_bins)
{
    size_t i, c;
    double *skew;
    double *mean;
    size_t size;
    double d;

    ICV_IMAGE_VAL_PTR(img);

    if (!bins || n_bins == 0 || !img->data || img->channels == 0 || img->width == 0 || img->height == 0)
	return NULL;

    if (img->width > SIZE_MAX / img->height)
	return NULL;

    size = (size_t) img->height*img->width;
    if (size == 0)
	return NULL;

    mean = icv_mean(img);
    if (!mean)
	return NULL;

    skew = (double *)bu_calloc(img->channels, sizeof(double), "skewness values");
    if (!skew) {
	bu_free(mean, "mean values");
	return NULL;
    }

    for (i = 0; i < n_bins; i++) {
	for (c = 0; c < img->channels; c++) {
	    if (!bins[c])
		continue;
	    d = (double)i - n_bins*mean[c];
	    skew[c] += bins[c][i] * d * d * d;
	}
    }

    for (c = 0; c < img->channels; c++) {
	skew[c] /= size;
    }

    bu_free(mean, "mean values");

    return skew;
}


int *
icv_median(icv_image_t* img, size_t** bins, size_t n_bins)
{
    size_t i, c;
    int *median;
    double *partial_sum;

    ICV_IMAGE_VAL_PTR(img);

    if (!bins || n_bins == 0 || img->channels == 0 || img->width == 0 || img->height == 0)
	return NULL;

    median = (int *)bu_malloc(sizeof(int)*img->channels, "median values");
    if (!median)
	return NULL;
    partial_sum = (double *)bu_malloc(sizeof(double)*img->channels, "partial sum values");
    if (!partial_sum) {
	bu_free(median, "median values");
	return NULL;
    }

    size_t num_pixels = (size_t)img->width * img->height;

    for (c = 0; c < img->channels; c++) {
	median[c] = 0;
	partial_sum[c] = 0.0;
    }

    for (c = 0; c < img->channels; c++) {
	if (!bins[c])
	    continue;
	for (i = 0; i < n_bins; i++) {
	    partial_sum[c] += bins[c][i];
	    if (partial_sum[c] >= num_pixels / 2.0) {
		median[c] = (int)i;
		break;
	    }
	}
    }

    bu_free(partial_sum, "icv_median : partial sum values\n");

    return median;
}


int *
icv_mode(icv_image_t* img, size_t** bins, size_t n_bins)
{
    size_t i, c;
    int *mode;

    ICV_IMAGE_VAL_PTR(img);

    if (!bins || n_bins == 0 || img->channels == 0)
	return NULL;

    mode = (int *) bu_malloc(sizeof(int)*img->channels, "mode values");
    if (!mode)
	return NULL;

    for (c = 0; c < img->channels; c++) {
	mode[c] = 0;
	if (!bins[c])
	    continue;
	for (i = 0; i < n_bins; i++)
	    if (bins[c][mode[c]] < bins[c][i])
		mode[c] = (int)i;
    }
    return mode;
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
