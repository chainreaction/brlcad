/*                         M A T E R I A L . C
 * BRL-CAD
 *
 * Copyright (c) 2021-2026 United States Government as represented by
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
/** @file libged/material.c
 *
 * The material command.
 *
 */

#include "common.h"

#include <string.h>

#include "bu/cmd.h"
#include "bu/getopt.h"
#include "rt/geom.h"
#include "raytrace.h"

#include "../ged_private.h"
#include "wdb.h"

typedef enum {
    MATERIAL_ASSIGN,
    MATERIAL_CREATE,
    MATERIAL_DESTROY,
    MATERIAL_GET,
    MATERIAL_HELP,
    MATERIAL_IMPORT,
    MATERIAL_REMOVE,
    MATERIAL_SET,
    ATTR_UNKNOWN
} material_cmd_t;

static const char *material_usage = " help \n\n"
    "material create {objectName} {materialName}\n\n"
    "material destroy {object}\n\n"
    "material assign {object} {materialName}\n\n"
    "material get {object} [propertyGroupName] {propertyName}\n\n"
    "material set {object} [propertyGroupName] {propertyName} [newPropertyValue]\n\n"
    "material remove {object} [propertyGroupName] {propertyName}\n\n"
    "material import [--id | --name] {fileName}\n\n"
    "  --id       - Specifies the id the material will be imported with\n\n"
    "  --name     - Specifies the name the material will be imported with\n\n"
    "Note: Object, property, and group names are case sensitive.";

static const char *possibleProperties = "The following are properties of material objects that can be set/modified: \n"
    "- name\n"
    "- source\n"
    "- parent\n\n"
    "The following are property groups (utilizable in [propertyGroupName] for materials): \n"
    "- physical\n"
    "- mechanical\n"
    "- optical\n"
    "- thermal\n";


static material_cmd_t
get_material_cmd(const char* arg)
{
    /* sub-commands */
    if (BU_STR_EQUIV("assign", arg))
	return MATERIAL_ASSIGN;
    else if (BU_STR_EQUIV("create", arg))
	return MATERIAL_CREATE;
    else if (BU_STR_EQUIV("destroy", arg))
	return MATERIAL_DESTROY;
    else if (BU_STR_EQUIV("set", arg))
	return MATERIAL_SET;
    else if (BU_STR_EQUIV("get", arg))
	return MATERIAL_GET;
    else if (BU_STR_EQUIV("help", arg))
	return MATERIAL_HELP;
    else if (BU_STR_EQUIV("import", arg))
	return MATERIAL_IMPORT;
    else if (BU_STR_EQUIV("remove", arg))
	return MATERIAL_REMOVE;
    else
	return ATTR_UNKNOWN;
}


static int
assign_material(struct ged *gedp, int argc, const char *argv[])
{
    struct directory *dp;
    struct bu_attribute_value_set avs;

    GED_CHECK_DATABASE_OPEN(gedp, BRLCAD_ERROR);
    GED_CHECK_DRAWABLE(gedp, BRLCAD_ERROR);
    GED_CHECK_READ_ONLY(gedp, BRLCAD_ERROR);
    GED_CHECK_ARGC_GT_0(gedp, argc, BRLCAD_ERROR);

    if (argc < 4 || !argv || !argv[2] || !argv[3]) {
        bu_vls_printf(gedp->ged_result_str, "you must provide at least four arguments.\n");
        return BRLCAD_ERROR;
    }

    if ((dp = db_lookup(gedp->dbip, argv[2], 0)) != RT_DIR_NULL) {
        bu_avs_init_empty(&avs);

        if (db5_get_attributes(gedp->dbip, &avs, dp)) {
            bu_vls_printf(gedp->ged_result_str, "Cannot get attributes for object %s\n", dp->d_namep);
            bu_avs_free(&avs);
            return BRLCAD_ERROR;
        } else {
            bu_avs_add(&avs, "material_name", argv[3]);
            bu_avs_add(&avs, "material_id", "1");
        }

        if (db5_update_attributes(dp, &avs, gedp->dbip)) {
            bu_vls_printf(gedp->ged_result_str, "Error: failed to update attributes\n");
            bu_avs_free(&avs);
            return BRLCAD_ERROR;
        }
        bu_avs_free(&avs);
    } else {
        bu_vls_printf(gedp->ged_result_str, "Cannot get object %s\n", argv[2]);
        return BRLCAD_ERROR;
    }

    return BRLCAD_OK;
}


/* Routine handles the import of a density table.
 *
 * FIXME: this routine is derived from libanalyze, but it would be
 * better to call analyze_densities_load() to ensure consistent
 * density file parsing and reduced code.
 */
static int
import_materials(struct ged *gedp, int argc, const char *argv[])
{
    const char* fileName;
    const char* flag;
    char buffer[BUFSIZ] = {0};

    if (argc < 4 || !argv || !argv[2] || !argv[3]) {
        bu_vls_printf(gedp->ged_result_str, "ERROR, not enough arguments!\n");
        return BRLCAD_ERROR;
    }

    flag = argv[2];
    fileName = argv[3];

    FILE *densityTable = fopen(fileName, "r");
    if (!densityTable) {
        bu_vls_printf(gedp->ged_result_str, "ERROR: File does not exist.\n");
        return BRLCAD_ERROR;
    }

    struct rt_wdb *wdbp = wdb_dbopen(gedp->dbip, RT_WDB_TYPE_DB_DEFAULT);
    if (!wdbp) {
        fclose(densityTable);
        bu_vls_printf(gedp->ged_result_str, "ERROR: Unable to open database for writing.\n");
        return BRLCAD_ERROR;
    }

    while (bu_fgets(buffer, BUFSIZ, densityTable)) {
	char *p;
	double density = -1;
	int have_density = 0;
	int idx = 0;
	int aborted;
	struct bu_vls name = BU_VLS_INIT_ZERO;

	/* reset every pass */
	p = buffer;
	aborted = 0;

	/* Skip initial whitespace */
	while (*p && (*p == '\t' || *p == ' ' || *p == '\n' || *p == '\r'))
	    p++;

	/* Skip initial comments */
	while (*p == '#') {
	    /* Skip comment */
	    while (*p && *p != '\n') {
		p++;
	    }
	}

	/* Skip whitespace */
	while (*p && (*p == '\t' || *p == ' ' || *p == '\n' || *p == '\r'))
	    p++;

	while (*p) {
	    int len = 0;
	    char *q = NULL;

	    if (*p == '#') {
		while (*p && *p != '\n')
		    p++;

		/* Skip whitespace */
		while (*p && (*p == '\t' || *p == ' ' || *p == '\n' || *p == '\r'))
		    p++;
		continue;
	    }

	    if (have_density) {
		aborted = 1;
		bu_vls_printf(gedp->ged_result_str, "ERROR: Extra content after density entry\n");
		break;
	    }
	    idx = strtol(p, &q, 10);
	    if (idx < 0) {
		aborted = 1;
		bu_vls_printf(gedp->ged_result_str, "ERROR: Bad density index\n");
		break;
	    }
	    density = strtod(q, &p);
	    if (q == p) {
		aborted = 1;
		bu_vls_printf(gedp->ged_result_str, "ERROR: Could not convert density\n");
		break;
	    }

	    if (density < 0.0) {
		aborted = 1;
		bu_vls_printf(gedp->ged_result_str, "ERROR: Bad Density\n");
		break;
	    }
	    while (*p && (*p == '\t' || *p == ' ')) p++;
	    if (!*p) {
		aborted = 1;
		bu_vls_printf(gedp->ged_result_str, "ERROR: Missing name\n");
		break;
	    }

	    while (*(p + len) && !(*(p + len) == '\n' || *(p+len) == '#')) {
		len++;
	    }

	    while (len >= 0 && !((*(p + len) >= 'A' && *(p + len) <= 'Z') ||  (*(p + len) >= 'a' && *(p + len) <= 'z') || (*(p + len) >= '0' && *(p + len) <= '9'))) {
		len--;
	    }

	    if (len < 0) {
		aborted = 1;
		bu_vls_printf(gedp->ged_result_str, "ERROR: Missing name\n");
		break;
	    }

	    bu_vls_strncpy(&name, p, len+1);
	    break;
	}

	if (aborted) {
	    bu_vls_free(&name);
	    fclose(densityTable);
	    wdb_close(wdbp);
	    return BRLCAD_ERROR;
	}

	if (idx == 0) {
	    bu_vls_free(&name);
	    memset(buffer, 0, BUFSIZ);
	    continue;
	}

	struct bu_attribute_value_set physicalProperties;
	struct bu_attribute_value_set mechanicalProperties;
	struct bu_attribute_value_set opticalProperties;
	struct bu_attribute_value_set thermalProperties;

	bu_avs_init_empty(&physicalProperties);
	bu_avs_init_empty(&mechanicalProperties);
	bu_avs_init_empty(&opticalProperties);
	bu_avs_init_empty(&thermalProperties);

	struct bu_vls idxChar = BU_VLS_INIT_ZERO;
	bu_vls_sprintf(&idxChar, "%d", idx);

	struct bu_vls densityChar = BU_VLS_INIT_ZERO;
	bu_vls_sprintf(&densityChar, "%.3f", density);

	bu_avs_add(&physicalProperties, "density", bu_vls_cstr(&densityChar));
	bu_avs_add(&physicalProperties, "id", bu_vls_cstr(&idxChar));

	if (BU_STR_EQUAL("--id", flag)) {
	    struct bu_vls mat_with_id = BU_VLS_INIT_ZERO;

	    bu_vls_strcat(&mat_with_id, "matl");
	    bu_vls_vlscat(&mat_with_id, &idxChar);

	    mk_material(wdbp,
			bu_vls_cstr(&mat_with_id),
			bu_vls_cstr(&name),
			"",
			"",
			&physicalProperties,
			&mechanicalProperties,
			&opticalProperties,
			&thermalProperties);
	    bu_vls_free(&mat_with_id);
	} else {
	    mk_material(wdbp,
			bu_vls_cstr(&name),
			bu_vls_cstr(&name),
			"",
			"",
			&physicalProperties,
			&mechanicalProperties,
			&opticalProperties,
			&thermalProperties);
	}
	bu_vls_free(&idxChar);
	bu_vls_free(&densityChar);
	bu_vls_free(&name);

	bu_avs_free(&physicalProperties);
	bu_avs_free(&mechanicalProperties);
	bu_avs_free(&opticalProperties);
	bu_avs_free(&thermalProperties);

	memset(buffer, 0, BUFSIZ);
    }

    fclose(densityTable);
    wdb_close(wdbp);

    return BRLCAD_OK;
}


static void
print_avs_value(struct ged *gedp, const struct bu_attribute_value_set * avp, const char * name, const char * avsName)
{
    const char * val = bu_avs_get(avp, name);

    if (val != NULL) {
        bu_vls_printf(gedp->ged_result_str, "%s", val);
    } else {
        bu_vls_printf(gedp->ged_result_str, "Error: unable to find the %s property %s.", avsName, name);
    }
}


// Routine handles the creation of a material
static int
create_material(struct ged *gedp, int argc, const char *argv[])
{
    const char* db_name;
    const char* name;
    const char* parent;
    const char* source;
    struct bu_attribute_value_set physicalProperties;
    struct bu_attribute_value_set mechanicalProperties;
    struct bu_attribute_value_set opticalProperties;
    struct bu_attribute_value_set thermalProperties;

    if (argc < 4 || !argv || !argv[2] || !argv[3]) {
        bu_vls_printf(gedp->ged_result_str, "ERROR, not enough arguments!\n");
        return BRLCAD_ERROR;
    }

    // Initialize AVS stores
    bu_avs_init_empty(&physicalProperties);
    bu_avs_init_empty(&mechanicalProperties);
    bu_avs_init_empty(&opticalProperties);
    bu_avs_init_empty(&thermalProperties);

    db_name = argv[2];
    name = argv[3];
    parent = NULL;
    source = NULL;

    struct rt_wdb *wdbp = wdb_dbopen(gedp->dbip, RT_WDB_TYPE_DB_DEFAULT);
    if (!wdbp) {
        bu_vls_printf(gedp->ged_result_str, "ERROR: unable to open database for writing\n");
        return BRLCAD_ERROR;
    }
    mk_material(wdbp,
		db_name,
		name,
		parent,
		source,
		&physicalProperties,
		&mechanicalProperties,
		&opticalProperties,
		&thermalProperties);
    wdb_close(wdbp);

    bu_avs_free(&physicalProperties);
    bu_avs_free(&mechanicalProperties);
    bu_avs_free(&opticalProperties);
    bu_avs_free(&thermalProperties);

    return BRLCAD_OK;
}


// Routine handles the deletion of a material
static int
destroy_material(struct ged *gedp, int argc, const char *argv[])
{
    struct directory *dp;
    GED_CHECK_DATABASE_OPEN(gedp, BRLCAD_ERROR);
    GED_CHECK_DRAWABLE(gedp, BRLCAD_ERROR);
    GED_CHECK_READ_ONLY(gedp, BRLCAD_ERROR);
    GED_CHECK_ARGC_GT_0(gedp, argc, BRLCAD_ERROR);

    if (argc != 3) {
        bu_vls_printf(gedp->ged_result_str, "ERROR, incorrect number of arguments.");
        return BRLCAD_ERROR;
    }

    /* initialize result */
    bu_vls_trunc(gedp->ged_result_str, 0);

    _dl_eraseAllNamesFromDisplay(gedp, argv[2], 0);

    if ((dp = db_lookup(gedp->dbip,  argv[2], 0)) != RT_DIR_NULL) {
	if (dp->d_major_type == DB5_MAJORTYPE_ATTRIBUTE_ONLY && dp->d_minor_type == 0) {
            bu_vls_printf(gedp->ged_result_str, "an error occurred while deleting %s", argv[2]);
	    return BRLCAD_ERROR;
	}

        if (db_delete(gedp->dbip, dp) != 0 || db_dirdelete(gedp->dbip, dp) != 0) {
	    /* Abort kill processing on first error */
            bu_vls_printf(gedp->ged_result_str, "an error occurred while deleting %s", argv[2]);
            return BRLCAD_ERROR;
	}
    }

    /* Update references. */
    db_update_nref(gedp->dbip);

    return BRLCAD_OK;
}


// routine handles getting individual properties of the material
static int
get_material(struct ged *gedp, int argc, const char *argv[])
{
    struct directory *dp;
    struct rt_db_internal intern;

    GED_CHECK_DATABASE_OPEN(gedp, BRLCAD_ERROR);
    GED_CHECK_DRAWABLE(gedp, BRLCAD_ERROR);
    GED_CHECK_READ_ONLY(gedp, BRLCAD_ERROR);
    GED_CHECK_ARGC_GT_0(gedp, argc, BRLCAD_ERROR);

    if (argc < 4 || !argv || !argv[2] || !argv[3]) {
        bu_vls_printf(gedp->ged_result_str, "you must provide at least four arguments.\n");
        return BRLCAD_ERROR;
    }

    if ((dp = db_lookup(gedp->dbip, argv[2], 0)) == RT_DIR_NULL) {
        bu_vls_printf(gedp->ged_result_str, "an error occurred finding the material: %s\n", argv[2]);
        return BRLCAD_ERROR;
    }

    GED_DB_GET_INTERN(gedp, &intern, dp, (matp_t)NULL, BRLCAD_ERROR);

    if (intern.idb_major_type != DB5_MAJORTYPE_BRLCAD || intern.idb_type != ID_MATERIAL) {
        bu_vls_printf(gedp->ged_result_str, "%s is not a material object\n", argv[2]);
        rt_db_free_internal(&intern);
        return BRLCAD_ERROR;
    }

    struct rt_material_internal *material = (struct rt_material_internal *)intern.idb_ptr;

    if (BU_STR_EQUAL(argv[3], "name")) {
        bu_vls_printf(gedp->ged_result_str, "%s", bu_vls_cstr(&material->name));
    } else if (BU_STR_EQUAL(argv[3], "parent")) {
        bu_vls_printf(gedp->ged_result_str, "%s", bu_vls_cstr(&material->parent));
    } else if (BU_STR_EQUAL(argv[3], "source")) {
        bu_vls_printf(gedp->ged_result_str, "%s", bu_vls_cstr(&material->source));
    } else {
        if (argc < 5 || !argv[4]) {
            bu_vls_printf(gedp->ged_result_str, "the property you requested: %s, could not be found.\n", argv[3]);
            rt_db_free_internal(&intern);
            return BRLCAD_ERROR;
        } else if (BU_STR_EQUAL(argv[3], "physical")) {
            print_avs_value(gedp, &material->physicalProperties, argv[4], argv[3]);
        } else if (BU_STR_EQUAL(argv[3], "mechanical")) {
            print_avs_value(gedp, &material->mechanicalProperties, argv[4], argv[3]);
        } else if (BU_STR_EQUAL(argv[3], "optical")) {
            print_avs_value(gedp, &material->opticalProperties, argv[4], argv[3]);
        } else if (BU_STR_EQUAL(argv[3], "thermal")) {
            print_avs_value(gedp, &material->thermalProperties, argv[4], argv[3]);
        } else {
            bu_vls_printf(gedp->ged_result_str, "an error occurred finding the material property group: %s\n", argv[3]);
            rt_db_free_internal(&intern);
            return BRLCAD_ERROR;
        }
    }

    rt_db_free_internal(&intern);
    return BRLCAD_OK;
}


// Routine handles the setting of a material property to a value
static int
set_material(struct ged *gedp, int argc, const char *argv[])
{
    struct directory *dp;
    struct rt_db_internal intern;

    GED_CHECK_DATABASE_OPEN(gedp, BRLCAD_ERROR);
    GED_CHECK_DRAWABLE(gedp, BRLCAD_ERROR);
    GED_CHECK_READ_ONLY(gedp, BRLCAD_ERROR);
    GED_CHECK_ARGC_GT_0(gedp, argc, BRLCAD_ERROR);

    if (argc < 5 || !argv || !argv[2] || !argv[3] || !argv[4]) {
        bu_vls_printf(gedp->ged_result_str, "you must provide at least five arguments.\n");
        return BRLCAD_ERROR;
    }

    if ((dp = db_lookup(gedp->dbip, argv[2], 0)) == RT_DIR_NULL) {
        bu_vls_printf(gedp->ged_result_str, "an error occurred finding the material: %s\n", argv[2]);
        return BRLCAD_ERROR;
    }

    GED_DB_GET_INTERN(gedp, &intern, dp, (matp_t)NULL, BRLCAD_ERROR);

    if (intern.idb_major_type != DB5_MAJORTYPE_BRLCAD || intern.idb_type != ID_MATERIAL) {
        bu_vls_printf(gedp->ged_result_str, "%s is not a material object\n", argv[2]);
        rt_db_free_internal(&intern);
        return BRLCAD_ERROR;
    }

    struct rt_material_internal *material = (struct rt_material_internal *)intern.idb_ptr;

    if (BU_STR_EQUAL(argv[3], "name")) {
        bu_vls_strcpy(&material->name, argv[4]);
    } else if (BU_STR_EQUAL(argv[3], "parent")) {
        bu_vls_strcpy(&material->parent, argv[4]);
    } else if (BU_STR_EQUAL(argv[3], "source")) {
        bu_vls_strcpy(&material->source, argv[4]);
    } else {
        if (argc < 6 || !argv[5]) {
            bu_vls_printf(gedp->ged_result_str, "property name and new value required for group %s\n", argv[3]);
            rt_db_free_internal(&intern);
            return BRLCAD_ERROR;
        }
        if (BU_STR_EQUAL(argv[3], "physical")) {
            bu_avs_remove(&material->physicalProperties, argv[4]);
            bu_avs_add(&material->physicalProperties, argv[4], argv[5]);
        } else if (BU_STR_EQUAL(argv[3], "mechanical")) {
            bu_avs_remove(&material->mechanicalProperties, argv[4]);
            bu_avs_add(&material->mechanicalProperties, argv[4], argv[5]);
        } else if (BU_STR_EQUAL(argv[3], "optical")) {
            bu_avs_remove(&material->opticalProperties, argv[4]);
            bu_avs_add(&material->opticalProperties, argv[4], argv[5]);
        } else if (BU_STR_EQUAL(argv[3], "thermal")) {
            bu_avs_remove(&material->thermalProperties, argv[4]);
            bu_avs_add(&material->thermalProperties, argv[4], argv[5]);
        } else {
            bu_vls_printf(gedp->ged_result_str, "an error occurred finding the material property group: %s\n", argv[3]);
            rt_db_free_internal(&intern);
            return BRLCAD_ERROR;
        }
    }

    struct rt_wdb *wdbp = wdb_dbopen(gedp->dbip, RT_WDB_TYPE_DB_DEFAULT);
    if (!wdbp) {
        bu_vls_printf(gedp->ged_result_str, "ERROR: unable to open database for writing\n");
        rt_db_free_internal(&intern);
        return BRLCAD_ERROR;
    }
    int ret = wdb_put_internal(wdbp, argv[2], &intern, mk_conv2mm);
    wdb_close(wdbp);
    rt_db_free_internal(&intern);
    return ret;
}


// Routine handles the removal of a material property
static int
remove_material(struct ged *gedp, int argc, const char *argv[])
{
    struct directory *dp;
    struct rt_db_internal intern;

    GED_CHECK_DATABASE_OPEN(gedp, BRLCAD_ERROR);
    GED_CHECK_DRAWABLE(gedp, BRLCAD_ERROR);
    GED_CHECK_READ_ONLY(gedp, BRLCAD_ERROR);
    GED_CHECK_ARGC_GT_0(gedp, argc, BRLCAD_ERROR);

    if (argc < 4 || !argv || !argv[2] || !argv[3]) {
        bu_vls_printf(gedp->ged_result_str, "you must provide at least four arguments.\n");
        return BRLCAD_ERROR;
    }

    if ((dp = db_lookup(gedp->dbip, argv[2], 0)) == RT_DIR_NULL) {
        bu_vls_printf(gedp->ged_result_str, "an error occurred finding the material: %s\n", argv[2]);
        return BRLCAD_ERROR;
    }

    GED_DB_GET_INTERN(gedp, &intern, dp, (matp_t)NULL, BRLCAD_ERROR);

    if (intern.idb_major_type != DB5_MAJORTYPE_BRLCAD || intern.idb_type != ID_MATERIAL) {
        bu_vls_printf(gedp->ged_result_str, "%s is not a material object\n", argv[2]);
        rt_db_free_internal(&intern);
        return BRLCAD_ERROR;
    }

    struct rt_material_internal *material = (struct rt_material_internal *)intern.idb_ptr;

    if (BU_STR_EQUAL(argv[3], "name")) {
        bu_vls_trunc(&material->name, 0);
    } else if (BU_STR_EQUAL(argv[3], "parent")) {
        bu_vls_trunc(&material->parent, 0);
    } else if (BU_STR_EQUAL(argv[3], "source")) {
        bu_vls_trunc(&material->source, 0);
    } else {
        if (argc < 5 || !argv[4]) {
            bu_vls_printf(gedp->ged_result_str, "property name required for group %s\n", argv[3]);
            rt_db_free_internal(&intern);
            return BRLCAD_ERROR;
        }
        if (BU_STR_EQUAL(argv[3], "physical")) {
            bu_avs_remove(&material->physicalProperties, argv[4]);
        } else if (BU_STR_EQUAL(argv[3], "mechanical")) {
            bu_avs_remove(&material->mechanicalProperties, argv[4]);
        } else if (BU_STR_EQUAL(argv[3], "optical")) {
            bu_avs_remove(&material->opticalProperties, argv[4]);
        } else if (BU_STR_EQUAL(argv[3], "thermal")) {
            bu_avs_remove(&material->thermalProperties, argv[4]);
        } else {
            bu_vls_printf(gedp->ged_result_str, "an error occurred finding the material property group: %s\n", argv[3]);
            rt_db_free_internal(&intern);
            return BRLCAD_ERROR;
        }
    }

    struct rt_wdb *wdbp = wdb_dbopen(gedp->dbip, RT_WDB_TYPE_DB_DEFAULT);
    if (!wdbp) {
        bu_vls_printf(gedp->ged_result_str, "ERROR: unable to open database for writing\n");
        rt_db_free_internal(&intern);
        return BRLCAD_ERROR;
    }
    int ret = wdb_put_internal(wdbp, argv[2], &intern, mk_conv2mm);
    wdb_close(wdbp);
    rt_db_free_internal(&intern);
    return ret;
}


static int
ged_material_core(struct ged *gedp, int argc, const char *argv[])
{
    material_cmd_t scmd;
    int ret = BRLCAD_OK;

    GED_CHECK_DATABASE_OPEN(gedp, BRLCAD_ERROR);
    GED_CHECK_ARGC_GT_0(gedp, argc, BRLCAD_ERROR);

    /* initialization */
    bu_vls_trunc(gedp->ged_result_str, 0);

    /* incorrect arguments */
    if (argc < 2 || !argv || !argv[1]) {
        bu_vls_printf(gedp->ged_result_str, "Usage: %s %s\n", argv ? argv[0] : "material", material_usage);
        return GED_HELP;
    }

    scmd = get_material_cmd(argv[1]);

    if (scmd == MATERIAL_ASSIGN) {
        ret = assign_material(gedp, argc, argv);
    } else if (scmd == MATERIAL_CREATE) {
        ret = create_material(gedp, argc, argv);
    } else if (scmd == MATERIAL_DESTROY) {
        ret = destroy_material(gedp, argc, argv);
    } else if (scmd == MATERIAL_IMPORT) {
        ret = import_materials(gedp, argc, argv);
    } else if (scmd == MATERIAL_GET) {
        ret = get_material(gedp, argc, argv);
    } else if (scmd == MATERIAL_HELP) {
        bu_vls_printf(gedp->ged_result_str, "Usage: %s %s\n\n\n", argv[0], material_usage);
        bu_vls_printf(gedp->ged_result_str, "%s", possibleProperties);
        return GED_HELP;
    } else if (scmd == MATERIAL_REMOVE) {
        ret = remove_material(gedp, argc, argv);
    } else if (scmd == MATERIAL_SET) {
        ret = set_material(gedp, argc, argv);
    } else {
        bu_vls_printf(gedp->ged_result_str, "Error: %s is not a valid subcommand.\n", argv[1]);
        bu_vls_printf(gedp->ged_result_str, "Usage: %s %s\n", argv[0], material_usage);
        return BRLCAD_ERROR;
    }

    return ret;
}

#include "../include/plugin.h"

#define GED_MATERIAL_COMMANDS(X, XID) \
    X(material, ged_material_core, GED_CMD_DEFAULT) \

GED_DECLARE_COMMAND_SET(GED_MATERIAL_COMMANDS)
GED_DECLARE_PLUGIN_MANIFEST("libged_material", 1, GED_MATERIAL_COMMANDS)

/*
 * Local Variables:
 * tab-width: 8
 * mode: C
 * indent-tabs-mode: t
 * c-file-style: "stroustrup"
 * End:
 * ex: shiftwidth=4 tabstop=8
 */
