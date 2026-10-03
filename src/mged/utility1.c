/*                      U T I L I T Y 1 . C
 * BRL-CAD
 *
 * Copyright (c) 1990-2026 United States Government as represented by
 * the U.S. Army Research Laboratory.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public License
 * version 2.1 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this file; see the file named COPYING for more
 * information.
 */

#include "common.h"

#include <stdlib.h>
#include <string.h>

#include "tcl.h"
#include "bu/vls.h"
#include "ged.h"

#include "./mged.h"
#include "./cmd.h"


/*
 * Control routine for editing color
 */
int
f_edcolor(ClientData clientData, Tcl_Interp *UNUSED(interpreter), int argc, const char *argv[])
{
    struct cmdtab *ctp = (struct cmdtab *)clientData;
    MGED_CK_CMD(ctp);
    struct mged_state *s = ctp->s;

    CHECK_DBI_NULL;

    if (!s || !s->gedp)
	return TCL_ERROR;

    if (!ged_set_editor(s->gedp, s->classic_mged))
	return TCL_ERROR;

    ged_exec(s->gedp, argc, argv);

    ged_clear_editor(s->gedp);
    return TCL_OK;
}


/*
 * Control routine for editing region ident codes
 */
int
f_edcodes(ClientData clientData, Tcl_Interp *interpreter, int argc, const char *argv[])
{
    struct cmdtab *ctp = (struct cmdtab *)clientData;
    MGED_CK_CMD(ctp);
    struct mged_state *s = ctp->s;

    CHECK_DBI_NULL;

    if (!s || !s->gedp)
	return TCL_ERROR;

    if (argc < 2) {
	if (interpreter)
	    Tcl_Eval(interpreter, "help edcodes");
	return TCL_ERROR;
    }

    if (!ged_set_editor(s->gedp, s->classic_mged))
	return TCL_ERROR;

    ged_exec(s->gedp, argc, argv);

    ged_clear_editor(s->gedp);
    return TCL_OK;
}


/*
 * Control routine for editing mater information
 */
int
f_edmater(ClientData clientData, Tcl_Interp *interpreter, int argc, const char *argv[])
{
    struct cmdtab *ctp = (struct cmdtab *)clientData;
    MGED_CK_CMD(ctp);
    struct mged_state *s = ctp->s;

    CHECK_DBI_NULL;

    if (!s || !s->gedp)
	return TCL_ERROR;

    if (argc < 2) {
	if (interpreter)
	    Tcl_Eval(interpreter, "help edmater");
	return TCL_ERROR;
    }

    if (!ged_set_editor(s->gedp, s->classic_mged))
	return TCL_ERROR;

    ged_exec(s->gedp, argc, argv);

    ged_clear_editor(s->gedp);
    return TCL_OK;
}


/*
 * Get editing string and call ged_red
 */
int
f_red(ClientData clientData, Tcl_Interp *interpreter, int argc, const char *argv[])
{
    struct cmdtab *ctp = (struct cmdtab *)clientData;
    MGED_CK_CMD(ctp);
    struct mged_state *s = ctp->s;

    CHECK_DBI_NULL;

    if (!s || !s->gedp)
	return TCL_ERROR;

    if (argc != 2) {
	if (interpreter)
	    Tcl_Eval(interpreter, "help red");
	return TCL_ERROR;
    }

    ged_set_editor(s->gedp, s->classic_mged);

    if (ged_exec(s->gedp, argc, argv) & BRLCAD_ERROR) {
	mged_pr_output(interpreter);
	if (interpreter && s->gedp->ged_result_str)
	    Tcl_AppendResult(interpreter, "Error: ", bu_vls_cstr(s->gedp->ged_result_str), (char *)NULL);
    } else {
	mged_pr_output(interpreter);
	if (interpreter && s->gedp->ged_result_str)
	    Tcl_AppendResult(interpreter, bu_vls_cstr(s->gedp->ged_result_str), (char *)NULL);
    }

    ged_clear_editor(s->gedp);
    return TCL_OK;
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
