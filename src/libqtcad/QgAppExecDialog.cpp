/*                  Q G A P P E X E C D I A L O G . C P P
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
/** @file QgAppExecDialog.cpp
 *
 * Support for running external applications and viewing the output
 * stream in a console widget embedded in a dialog window.
 *
 */

#include <QFileInfo>
#include <QFile>
#include <QPlainTextEdit>
#include <QTextStream>
#include "qtcad/QgAppExecDialog.h"

QgAppExecDialog::QgAppExecDialog(QWidget *pparent, const QString &executable, const QStringList &args, const QString &lfile)
    : QDialog(pparent), logfile(nullptr), console(nullptr), proc(nullptr), buttonBox(nullptr)
{
    QVBoxLayout *dlayout = new QVBoxLayout(this);
    buttonBox = new QDialogButtonBox(QDialogButtonBox::Cancel, this);
    QObject::connect(buttonBox, &QDialogButtonBox::rejected, this, &QgAppExecDialog::process_abort);
    console = new QgConsole(this);
    console->prompt("");
    setLayout(dlayout);
    dlayout->addWidget(console);
    dlayout->addWidget(buttonBox);
    if (!lfile.isEmpty()) {
	logfile = new QFile(lfile, this);
	if (!logfile->open(QIODevice::Append | QIODevice::Text)) {
	    delete logfile;
	    logfile = nullptr;
	} else {
	    QTextStream log_stream(logfile);
	    log_stream << executable << " " << args.join(" ") << "\n";
	}
    }
}

QgAppExecDialog::~QgAppExecDialog()
{
    if (logfile) {
	if (logfile->isOpen()) {
	    logfile->close();
	}
	delete logfile;
	logfile = nullptr;
    }
}

void QgAppExecDialog::read_stdout()
{
    QTCAD_SLOT("QgAppExecDialog::read_stdout", 1);
    if (!proc)
	return;
    QString std_output = proc->readAllStandardOutput();
    if (console)
	console->printString(std_output);
    if (logfile && logfile->isOpen()) {
	QTextStream log_stream(logfile);
	log_stream << std_output;
	logfile->flush();
    }
}

void QgAppExecDialog::read_stderr()
{
    QTCAD_SLOT("QgAppExecDialog::read_stderr", 1);
    if (!proc)
	return;
    QString err_output = proc->readAllStandardError();
    if (console)
	console->printString(err_output);
    if (logfile && logfile->isOpen()) {
	QTextStream log_stream(logfile);
	log_stream << err_output;
	logfile->flush();
    }
}

void QgAppExecDialog::process_abort()
{
    QTCAD_SLOT("QgAppExecDialog::process_abort", 1);
    if (proc)
	proc->kill();
    if (console)
	console->printString("\nAborted!\n");
    if (logfile && logfile->isOpen()) {
	QTextStream log_stream(logfile);
	log_stream << "\nAborted!\n";
	logfile->flush();
    }
    process_done(0, QProcess::NormalExit);
}

void QgAppExecDialog::process_done(int , QProcess::ExitStatus)
{
    QTCAD_SLOT("QgAppExecDialog::process_done", 1);
    if (logfile && logfile->isOpen())
	logfile->close();
    if (buttonBox) {
	buttonBox->clear();
	buttonBox->addButton(QDialogButtonBox::Ok);
	QObject::connect(buttonBox, &QDialogButtonBox::accepted, this, &QgAppExecDialog::accept);
    }
    setWindowTitle("Process Finished");
}

/*
 * Local Variables:
 * mode: C++
 * tab-width: 8
 * c-basic-offset: 4
 * indent-tabs-mode: t
 * c-file-style: "stroustrup"
 * End:
 * ex: shiftwidth=4 tabstop=8
 */

