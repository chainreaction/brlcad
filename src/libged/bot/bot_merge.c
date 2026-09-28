/*                         B O T _ M E R G E . C
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
/** @file libged/bot_merge.c
 *
 * The bot_merge command.
 *
 */

#include "common.h"

#include <stdlib.h>
#include <ctype.h>
#include <string.h>

#include "rt/geom.h"

#include "../ged_private.h"


int
ged_bot_merge_core(struct ged *gedp, int argc, const char *argv[])
{
    struct directory *dp, *new_dp;
    struct rt_db_internal intern;
    struct rt_bot_internal **bots;
    int i, idx;
    static const char *usage = "bot_dest bot1_src [botn_src]";

    GED_CHECK_DATABASE_OPEN(gedp, BRLCAD_ERROR);
    GED_CHECK_READ_ONLY(gedp, BRLCAD_ERROR);
    GED_CHECK_ARGC_GT_0(gedp, argc, BRLCAD_ERROR);

    /* initialize result */
    bu_vls_trunc(gedp->ged_result_str, 0);

    /* must be wanting help */
    if (argc < 3) {
	bu_vls_printf(gedp->ged_result_str, "Usage: %s %s", argv[0], usage);
	return (argc == 1) ? GED_HELP : BRLCAD_ERROR;
    }

    bots = (struct rt_bot_internal **)bu_calloc(argc - 1, sizeof(struct rt_bot_internal *), "bot internal");

    /* read in all the bots */
    for (idx = 0, i = 2; i < argc; ++i) {
	if ((dp = db_lookup(gedp->dbip, argv[i], LOOKUP_NOISY)) == RT_DIR_NULL) {
	    continue;
	}

	RT_DB_INTERNAL_INIT(&intern);
	if (rt_db_get_internal(&intern, dp, gedp->dbip, bn_mat_identity) < 0) {
	    bu_vls_printf(gedp->ged_result_str, "Database read failure.");
	    goto fail_cleanup;
	}

	if (intern.idb_major_type != DB5_MAJORTYPE_BRLCAD || intern.idb_minor_type != DB5_MINORTYPE_BRLCAD_BOT) {
	    bu_vls_printf(gedp->ged_result_str, "%s: %s is not a BOT solid!  Skipping.\n", argv[0], argv[i]);
	    rt_db_free_internal(&intern);
	    continue;
	}

	bots[idx] = (struct rt_bot_internal *)intern.idb_ptr;

	intern.idb_ptr = (void *)0;
	rt_db_free_internal(&intern);

	RT_BOT_CK_MAGIC(bots[idx]);
	idx++;
    }

    if (idx == 0) {
	bu_vls_printf(gedp->ged_result_str, "%s: No BOT solids given.\n", argv[0]);
	bu_free(bots, "bots");
	return BRLCAD_ERROR;
    }

    RT_DB_INTERNAL_INIT(&intern);
    intern.idb_type = ID_BOT;
    intern.idb_major_type = DB5_MAJORTYPE_BRLCAD;
    intern.idb_minor_type = DB5_MINORTYPE_BRLCAD_BOT;
    intern.idb_meth = &OBJ[ID_BOT];
    intern.idb_ptr = rt_bot_merge(idx, (const struct rt_bot_internal * const *)(bots));
    if (!intern.idb_ptr) {
	bu_vls_printf(gedp->ged_result_str, "%s: rt_bot_merge failed\n", argv[0]);
	goto fail_cleanup;
    }

    new_dp = db_diradd(gedp->dbip, argv[1], RT_DIR_PHONY_ADDR, 0, RT_DIR_SOLID, (void *)&intern.idb_type);
    if (new_dp == RT_DIR_NULL) {
	bu_vls_printf(gedp->ged_result_str, "Unable to add %s to database\n", argv[1]);
	rt_db_free_internal(&intern);
	goto fail_cleanup;
    }
    GED_DB_PUT_INTERN(gedp, new_dp, &intern, BRLCAD_ERROR);

    for (i = 0; i < idx; ++i) {
	/* fill in an rt_db_internal so we can free it */
	struct rt_db_internal internal;
	RT_DB_INTERNAL_INIT(&internal);
	internal.idb_major_type = DB5_MAJORTYPE_BRLCAD;
	internal.idb_minor_type = ID_BOT;
	internal.idb_meth = &OBJ[ID_BOT];
	internal.idb_ptr = bots[i];

	rt_db_free_internal(&internal);
    }

    bu_free(bots, "bots");

    return BRLCAD_OK;

fail_cleanup:
    for (i = 0; i < idx; ++i) {
	struct rt_db_internal internal;
	RT_DB_INTERNAL_INIT(&internal);
	internal.idb_major_type = DB5_MAJORTYPE_BRLCAD;
	internal.idb_minor_type = ID_BOT;
	internal.idb_meth = &OBJ[ID_BOT];
	internal.idb_ptr = bots[i];

	rt_db_free_internal(&internal);
    }
    bu_free(bots, "bots");
    return BRLCAD_ERROR;
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
