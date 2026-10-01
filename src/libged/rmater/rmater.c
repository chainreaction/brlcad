/*                        R M A T E R . C
 * BRL-CAD
 *
 * Copyright (c) 2008-2026 United States Government as represented by
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
/** @file libged/rmater.c
 *
 * The rmater command.
 *
 */

#include "ged.h"


static int
extract_mater_from_line(char *line,
			char *name,
			size_t name_len,
			char *shader,
			size_t shader_len,
			int *r,
			int *g,
			int *b,
			int *override,
			int *inherit)
{
    int i, j;
    size_t k;
    char *str[2];
    size_t maxlen[2];

    if (!line || !name || name_len == 0 || !shader || shader_len == 0 ||
	!r || !g || !b || !override || !inherit)
	return BRLCAD_ERROR;

    str[0] = name;
    maxlen[0] = name_len - 1;
    str[1] = shader;
    maxlen[1] = shader_len - 1;

    /* Extract first 2 strings. */
    for (i = j = 0; i < 2; ++i) {

	/* skip white space */
	while (line[j] == ' ' || line[j] == '\t')
	    ++j;

	if (line[j] == '\0')
	    return BRLCAD_ERROR;

	/* We found a double quote, so use everything between the quotes */
	if (line[j] == '"') {
	    for (k = 0, ++j; line[j] != '"' && line[j] != '\0'; ++j) {
		if (k < maxlen[i])
		    str[i][k++] = line[j];
	    }
	} else {
	    for (k = 0; line[j] != ' ' && line[j] != '\t' && line[j] != '\0'; ++j) {
		if (k < maxlen[i])
		    str[i][k++] = line[j];
	    }
	}

	if (line[j] == '\0')
	    return BRLCAD_ERROR;

	str[i][k] = '\0';
	++j;
    }

    /* character and/or whitespace delimited numbers */
    if ((bu_sscanf(line + j, "%d%*c%d%*c%d%*c%d%*c%d", r, g, b, override, inherit)) != 5)
	return BRLCAD_ERROR;

    return BRLCAD_OK;
}


int
ged_rmater_core(struct ged *gedp, int argc, const char *argv[])
{
#ifndef LINELEN
#define LINELEN 256
#endif
    int status = BRLCAD_OK;
    FILE *fp;
    struct directory *dp;
    struct rt_db_internal intern;
    struct rt_comb_internal *comb;
    char line[LINELEN];
    char name[128];
    char shader[256];
    int r, g, b;
    int override;
    int inherit;
    static const char *usage = "filename";

    GED_CHECK_DATABASE_OPEN(gedp, BRLCAD_ERROR);
    GED_CHECK_READ_ONLY(gedp, BRLCAD_ERROR);
    GED_CHECK_ARGC_GT_0(gedp, argc, BRLCAD_ERROR);

    /* initialize result */
    bu_vls_trunc(gedp->ged_result_str, 0);

    /* must be wanting help */
    if (argc == 1) {
	bu_vls_printf(gedp->ged_result_str, "Usage: %s %s\n", argv[0], usage);
	return GED_HELP;
    }

    if (argc != 2) {
	bu_vls_printf(gedp->ged_result_str, "Usage: %s %s\n", argv[0], usage);
	return BRLCAD_ERROR;
    }

    if (!argv[1] || argv[1][0] == '\0') {
	bu_vls_printf(gedp->ged_result_str, "ged_rmater: missing filename\n");
	return BRLCAD_ERROR;
    }

    fp = fopen(argv[1], "r");
    if (fp == NULL) {
	bu_vls_printf(gedp->ged_result_str, "ged_rmater: Failed to read file - %s\n", argv[1]);
	return BRLCAD_ERROR;
    }

    while (bu_fgets(line, LINELEN, fp) != NULL) {
	if ((extract_mater_from_line(line, name, sizeof(name), shader, sizeof(shader),
				     &r, &g, &b, &override, &inherit)) & BRLCAD_ERROR)
	    continue;

	if ((dp = db_lookup(gedp->dbip, name, LOOKUP_NOISY)) == RT_DIR_NULL) {
	    bu_vls_printf(gedp->ged_result_str, "ged_rmater: Failed to find %s\n", name);
	    status = BRLCAD_ERROR;
	    continue;
	}

	if (rt_db_get_internal(&intern, dp, gedp->dbip, (fastf_t *)NULL) < 0) {
	    bu_vls_printf(gedp->ged_result_str, "Database read error, aborting\n");
	    status = BRLCAD_ERROR;
	    continue;
	}

	if (intern.idb_type != ID_COMBINATION || !intern.idb_ptr) {
	    bu_vls_printf(gedp->ged_result_str, "ged_rmater: %s is not a combination\n", name);
	    rt_db_free_internal(&intern);
	    status = BRLCAD_ERROR;
	    continue;
	}

	comb = (struct rt_comb_internal *)intern.idb_ptr;
	RT_CK_COMB(comb);

	/* Assign new values */
	if (shader[0] == '-')
	    bu_vls_trunc(&comb->shader, 0);
	else
	    bu_vls_strcpy(&comb->shader, shader);

	comb->rgb[0] = (unsigned char)r;
	comb->rgb[1] = (unsigned char)g;
	comb->rgb[2] = (unsigned char)b;
	comb->rgb_valid = override;
	comb->inherit = inherit;

	/* Write new values to database */
	if (rt_db_put_internal(dp, gedp->dbip, &intern) < 0) {
	    bu_vls_printf(gedp->ged_result_str, "Database write error, aborting\n");
	    status = BRLCAD_ERROR;
	}
    }

    (void)fclose(fp);
    return status;
}


#include "../include/plugin.h"

#define GED_RMATER_COMMANDS(X, XID) \
    X(rmater, ged_rmater_core, GED_CMD_DEFAULT) \

GED_DECLARE_COMMAND_SET(GED_RMATER_COMMANDS)
GED_DECLARE_PLUGIN_MANIFEST("libged_rmater", 1, GED_RMATER_COMMANDS)

/*
 * Local Variables:
 * mode: C
 * tab-width: 8
 * indent-tabs-mode: t
 * c-file-style: "stroustrup"
 * End:
 * ex: shiftwidth=4 tabstop=8
 */
