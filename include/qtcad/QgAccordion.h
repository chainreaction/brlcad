/*                  Q G A C C O R D I O N . H
 * BRL-CAD
 *
 * Copyright (c) 2014-2026 United States Government as represented by
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
/** @file QgAccordion.h
 *
 * Show one of a stack of widgets
 */

#ifndef QGACCORDIANWIDGET_H
#define QGACCORDIANWIDGET_H

#include "common.h"


#include <QWidget>
#include <QVBoxLayout>
#include <QPushButton>
#include <QScrollArea>
#include <QSet>

#include "qtcad/defines.h"

class QTCAD_EXPORT QgAccordionObject : public QWidget
{
    Q_OBJECT

    public:
	explicit QgAccordionObject(QWidget *pparent = nullptr, QWidget *object = nullptr, const QString &header_title = QString());
	~QgAccordionObject();
	QPushButton *toggle = nullptr;
	QScrollArea *objscrollarea = nullptr;

    signals:
	void select(QgAccordionObject *);

    public slots:
	void toggleVisibility();

    private:
	QVBoxLayout *objlayout = nullptr;
	QString title;
};

class QTCAD_EXPORT QgAccordion : public QWidget
{
    Q_OBJECT

    public:
	explicit QgAccordion(QWidget *pparent = nullptr);
	~QgAccordion();
	void addObject(QgAccordionObject *object);

    public slots:
	void open(QgAccordionObject *);

    private:
        QSet<QgAccordionObject *> objs;
        QgAccordionObject *selected = nullptr;
        QVBoxLayout *mlayout = nullptr;
};

#endif /* QGACCORDIANWIDGET_H */

/*
 * Local Variables:
 * mode: C
 * tab-width: 8
 * indent-tabs-mode: t
 * c-file-style: "stroustrup"
 * End:
 * ex: shiftwidth=4 tabstop=8
 */

