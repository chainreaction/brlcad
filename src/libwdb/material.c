/*                  M A T E R I A L . C
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

/** @file libwdb/material.c
 *
 * @brief 
 *
 */

#include "common.h"

#include <stdlib.h>

#include "raytrace.h"
#include "wdb.h"


int
mk_material(struct rt_wdb *wdbp,
            const char *db_name,
            const char *name,
            const char *parent,
            const char *source,
            struct bu_attribute_value_set *physicalProperties,
            struct bu_attribute_value_set *mechanicalProperties,
            struct bu_attribute_value_set *opticalProperties,
            struct bu_attribute_value_set *thermalProperties) {
    struct rt_material_internal *material_ip;
    struct rt_db_internal intern;

    if (!wdbp || !db_name)
	return -1;
    RT_CK_WDB(wdbp);

    /* Create a fresh new object for export */
    BU_ALLOC(material_ip, struct rt_material_internal);
    material_ip->magic = RT_MATERIAL_MAGIC;
    BU_VLS_INIT(&material_ip->name);
    BU_VLS_INIT(&material_ip->parent);
    BU_VLS_INIT(&material_ip->source);

    bu_avs_init_empty(&material_ip->physicalProperties);
    bu_avs_init_empty(&material_ip->mechanicalProperties);
    bu_avs_init_empty(&material_ip->opticalProperties);
    bu_avs_init_empty(&material_ip->thermalProperties);

    RT_DB_INTERNAL_INIT(&intern);
    intern.idb_major_type = DB5_MAJORTYPE_BRLCAD;
    intern.idb_type = ID_MATERIAL;
    intern.idb_ptr = (void *)material_ip;
    intern.idb_meth = &OBJ[ID_MATERIAL];

    /* Add data */
    if (name)
	bu_vls_strcpy(&material_ip->name, name);
    if (parent)
	bu_vls_strcpy(&material_ip->parent, parent);
    if (source)
	bu_vls_strcpy(&material_ip->source, source);

    if (physicalProperties)
	bu_avs_merge(&material_ip->physicalProperties, physicalProperties);
    if (mechanicalProperties)
	bu_avs_merge(&material_ip->mechanicalProperties, mechanicalProperties);
    if (opticalProperties)
	bu_avs_merge(&material_ip->opticalProperties, opticalProperties);
    if (thermalProperties)
	bu_avs_merge(&material_ip->thermalProperties, thermalProperties);

    /* The internal representation will be freed */
    return wdb_put_internal(wdbp, db_name, &intern, mk_conv2mm);
}
