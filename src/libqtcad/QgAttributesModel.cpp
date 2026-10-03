/*              Q G A T T R I B U T E S M O D E L . C P P
 * BRL-CAD
 *
 * Copyright (c) 2020-2026 United States Government as represented by
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
/** @file QgAttributesModel.cpp
 *
 */

#include "common.h"
#include <QPainter>
#include <QString>
#include <QtGlobal>

#include "bu/sort.h"
#include "bu/avs.h"
#include "bu/malloc.h"
#include "qtcad/QgAttributesModel.h"
#include "qtcad/QgModel.h"

QgAttributesModel::QgAttributesModel(QObject *parentobj, struct db_i *dbip, struct directory *dp, int show_standard, int show_user)
    : QgKeyValModel(parentobj),
      current_dbip(dbip),
      current_dp(dp),
      avs(nullptr),
      std_visible(show_standard ? 1 : 0),
      user_visible(show_user ? 1 : 0)
{
    int i = 0;
    m_root = new QgKeyValNode();
    BU_GET(avs, struct bu_attribute_value_set);
    bu_avs_init_empty(avs);
    if (std_visible) {
	while (i != ATTR_NULL) {
	    add_pair(db5_standard_attribute(i), "", m_root, i);
	    i++;
	}
    }
    if (dbip != DBI_NULL && dp != RT_DIR_NULL) {
	update(dbip, dp);
    }
}

QgAttributesModel::~QgAttributesModel()
{
    delete m_root;
    m_root = nullptr;
    if (avs) {
	bu_avs_free(avs);
	BU_PUT(avs, struct bu_attribute_value_set);
	avs = nullptr;
    }
}

static int
attr_children(const char *attr)
{
    if (attr && BU_STR_EQUAL(attr, "color")) return 3;
    return 0;
}


bool QgAttributesModel::canFetchMore(const QModelIndex &idx) const
{
    QgKeyValNode *curr_node = IndexNode(idx);
    if (!curr_node || curr_node == m_root) return false;
    if (rowCount(idx)) {
	return false;
    }
    QByteArray ba = curr_node->name.toLocal8Bit();
    int cnt = attr_children(ba.constData());
    if (cnt > 0) return true;
    return false;
}

void
QgAttributesModel::add_Children(const char *name, QgKeyValNode *curr_node)
{
    if (!name || !curr_node || !avs)
	return;
    const char *val_str = bu_avs_get(avs, name);
    if (BU_STR_EQUAL(name, "color")) {
	QString val(val_str ? val_str : "");
#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
	QStringList vals = val.split(QRegExp("/"));
#else
	QStringList vals = val.split(QRegularExpression("/"));
#endif
	if (vals.size() >= 3) {
	    (void)add_pair("r", vals.at(0).toLocal8Bit().constData(), curr_node, db5_standardize_attribute(name));
	    (void)add_pair("g", vals.at(1).toLocal8Bit().constData(), curr_node, db5_standardize_attribute(name));
	    (void)add_pair("b", vals.at(2).toLocal8Bit().constData(), curr_node, db5_standardize_attribute(name));
	}
	return;
    }
    (void)add_pair(name, val_str ? val_str : "", curr_node, db5_standardize_attribute(name));
}


void QgAttributesModel::fetchMore(const QModelIndex &idx)
{
    QgKeyValNode *curr_node = IndexNode(idx);
    if (!curr_node || curr_node == m_root) return;
    QByteArray ba = curr_node->name.toLocal8Bit();
    int cnt = attr_children(ba.constData());
    if (cnt > 0) {
	beginInsertRows(idx, 0, cnt - 1);
	add_Children(ba.constData(), curr_node);
	endInsertRows();
    }
}

bool QgAttributesModel::hasChildren(const QModelIndex &idx) const
{
    QgKeyValNode *curr_node = IndexNode(idx);
    if (!curr_node) return false;
    if (curr_node == m_root) return true;
    if (curr_node->value.isEmpty()) return false;
    QByteArray ba = curr_node->name.toLocal8Bit();
    int cnt = attr_children(ba.constData());
    if (cnt > 0) return true;
    return false;
}

int QgAttributesModel::update(struct db_i *new_dbip, struct directory *new_dp)
{
    current_dp = new_dp;
    current_dbip = new_dbip;
    beginResetModel();
    delete m_root;
    m_root = new QgKeyValNode();

    if (current_dbip != DBI_NULL && current_dp != RT_DIR_NULL && avs) {
	QMap<QString, QgKeyValNode*> standard_nodes;
	int i = 0;
	bu_avs_free(avs);
	bu_avs_init_empty(avs);
	(void)db5_get_attributes(current_dbip, avs, current_dp);

	if (std_visible) {
	    while (i != ATTR_NULL) {
		standard_nodes.insert(db5_standard_attribute(i), add_pair(db5_standard_attribute(i), "", m_root, i));
		i++;
	    }
	    struct bu_attribute_value_pair *avpp;
	    for (BU_AVS_FOR(avpp, avs)) {
		if (db5_is_standard_attribute(avpp->name)) {
		    if (standard_nodes.find(avpp->name) != standard_nodes.end()) {
			QString new_value(avpp->value);
			QgKeyValNode *snode = standard_nodes.find(avpp->name).value();
			snode->value = new_value;
		    } else {
			add_pair(avpp->name, avpp->value, m_root, db5_standardize_attribute(avpp->name));
		    }
		}
	    }
	}
	if (user_visible) {
	    struct bu_attribute_value_pair *avpp;
	    for (BU_AVS_FOR(avpp, avs)) {
		if (!db5_is_standard_attribute(avpp->name)) {
		    add_pair(avpp->name, avpp->value, m_root, ATTR_NULL);
		}
	    }
	}
    }
    endResetModel();
    return 0;
}

void
QgAttributesModel::refresh(const QModelIndex &idx)
{
    QTCAD_SLOT("QgAttributesModel::refresh", 1);
    if (!idx.isValid())
	return;
    current_dp = (struct directory *)(idx.data(QgModel::DirectoryInternalRole).value<void *>());
    update(current_dbip, current_dp);
}

void
QgAttributesModel::db_change_refresh()
{
    QTCAD_SLOT("QgAttributesModel::db_change_refresh", 1);
    update(current_dbip, current_dp);
}

void
QgAttributesModel::do_dbi_update(struct db_i *dbip)
{
    QTCAD_SLOT("QgAttributesModel::do_dbi_update", 1);
    current_dbip = dbip;
    beginResetModel();
    delete m_root;
    m_root = new QgKeyValNode();
    if (std_visible) {
	int i = 0;
	while (i != ATTR_NULL) {
	    add_pair(db5_standard_attribute(i), "", m_root, i);
	    i++;
	}
    }
    endResetModel();
}

// Local Variables:
// tab-width: 8
// mode: C++
// c-basic-offset: 4
// indent-tabs-mode: t
// c-file-style: "stroustrup"
// End:
// ex: shiftwidth=4 tabstop=8

