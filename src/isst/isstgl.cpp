/*                      I S S T G L . C P P
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

#include "common.h"

// Mac has deprecated OpenGL since 10.14
#define GL_SILENCE_DEPRECATION 1

#include <QKeyEvent>
#include <QGuiApplication> // for qGuiApp
#include <QtGlobal>

#include "bu/parallel.h"
#include "bu/log.h"
#include "bu/malloc.h"
#include "isstgl.h"

#include <chrono>
#include <thread>


TIERenderer::TIERenderer(isstGL *w)
    : m_w(w)
{
    memset(&camera, 0, sizeof(camera));
    memset(&tile, 0, sizeof(tile));
    camera_pos_init[0] = camera_pos_init[1] = camera_pos_init[2] = 0.0;
    camera_focus_init[0] = camera_focus_init[1] = camera_focus_init[2] = 0.0;

    // Initialize TIE camera
    camera.type = RENDER_CAMERA_PERSPECTIVE;
    camera.fov = 25;
    camera.w = 512;
    camera.h = 512;
    render_camera_init(&camera, bu_avail_cpus());
    render_phong_init(&camera.render, NULL);

    // Initialize texture buffer
    TIENET_BUFFER_INIT(buffer_image);
    texdata_size = (long)camera.w * (long)camera.h;
    texdata = malloc((size_t)texdata_size * 3);

    // Initialize TIE tile
    tile.orig_x = 0;
    tile.orig_y = 0;
    tile.format = RENDER_CAMERA_BIT_DEPTH_24;
}

TIERenderer::~TIERenderer()
{
    TIENET_BUFFER_FREE(buffer_image);
    if (texdata) {
	free(texdata);
	texdata = NULL;
    }
    if (camera.view_list) {
	bu_free(camera.view_list, "camera view_list");
	camera.view_list = NULL;
    }
}

void TIERenderer::resize()
{
    // If something changed, we need to re-render - otherwise, no-op
    if (!changed || !m_w)
	return;

    int w = m_w->width();
    int h = m_w->height();

    if (w <= 0 || h <= 0)
	return;

    // Translated from Tcl/Tk ISST logic for resolution adjustment
    if (resolution_factor == 0) {
	camera.w = w;
	camera.h = h;
    } else {
	camera.w = (resolution_factor > 0) ? resolution_factor : 1;
	camera.h = (int)((long long)camera.w * h / w);
	if (camera.h <= 0)
	    camera.h = 1;
    }

    // Set tile size
    tile.size_x = camera.w;
    tile.size_y = camera.h;

    // Set up the raytracing image buffer
    TIENET_BUFFER_SIZE(buffer_image, (uint32_t)(3 * (size_t)camera.w * (size_t)camera.h));

    size_t new_size = (size_t)camera.w * (size_t)camera.h;
    if (texdata_size < (long)new_size) {
	void *new_texdata = realloc(texdata, new_size * 3);
	if (new_texdata) {
	    texdata = new_texdata;
	    texdata_size = (long)new_size;
	}
    }

    if (texid == 0) {
	glGenTextures(1, &texid);
    }
    glBindTexture(GL_TEXTURE_2D, texid);

    // Set up the TeXImage2D buffer that will hold the results of the raytrace
    // for OpenGL
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexEnvf(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, camera.w, camera.h, 0, GL_RGB, GL_UNSIGNED_BYTE, texdata);
}

void TIERenderer::res_incr()
{
    if (!m_w)
	return;
    resolution++;
    CLAMP(resolution, 1, 20);
    int win_w = m_w->width();
    if (win_w <= 0)
	win_w = 512;
    resolution_factor = (resolution == 20) ? 0 : lrint(floor(win_w * .05 * resolution));
    if (resolution < 20 && resolution_factor < 1)
	resolution_factor = 1;
    scaled = true;
}

void TIERenderer::res_decr()
{
    if (!m_w)
	return;
    resolution--;
    CLAMP(resolution, 1, 20);
    int win_w = m_w->width();
    if (win_w <= 0)
	win_w = 512;
    resolution_factor = (resolution == 20) ? 0 : lrint(floor(win_w * .05 * resolution));
    if (resolution < 20 && resolution_factor < 1)
	resolution_factor = 1;
    scaled = true;
}

void TIERenderer::render()
{
    if (m_exiting || !m_w)
	return;

    int w = m_w->width();
    int h = m_w->height();
    // Zero or negative size == nothing to do
    if (w <= 0 || h <= 0)
	return;

    if (!tie) {
	// No scene loaded yet
	std::this_thread::sleep_for(std::chrono::milliseconds(10));
	return;
    }

    if (scaled) {
	changed = true;
	scaled = false;
    }

    if (!changed) {
	// Avoid a hot spin
	std::this_thread::sleep_for(std::chrono::milliseconds(10));
	return;
    }

    // Since we're in a separate rendering thread, there is
    // some preliminary work we need to do before proceeding
    // with OpenGL calls
    QOpenGLContext *ctx = m_w->context();
    if (!ctx) // QOpenGLWidget not yet initialized
	return;
    // Grab the context.
    m_grabMutex.lock();
    emit contextWanted();
    m_grabCond.wait(&m_grabMutex);
    QMutexLocker lock(&m_renderMutex);
    m_grabMutex.unlock();
    if (m_exiting)
	return;
    Q_ASSERT(ctx->thread() == QThread::currentThread());

    // Have context, initialize if necessary
    m_w->makeCurrent();
    if (!m_init) {
	initializeOpenGLFunctions();
	m_init = true;
    }

    // Ready for actual OpenGL calls.
    resize();

    changed = false;

    // IMPORTANT - this reset is necessary or the resultant image will
    // not display correctly in the buffer.
    buffer_image.ind = 0;

    // Core TIE render
    render_camera_prep(&camera);
    render_camera_render(&camera, tie, &tile, &buffer_image);

    glDisable(GL_LIGHTING);

    glViewport(0, 0, m_w->width(), m_w->height());
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glOrtho(0, m_w->width(), m_w->height(), 0, -1, 1);
    glMatrixMode(GL_MODELVIEW);

    glClear(GL_COLOR_BUFFER_BIT);

    glClear(GL_DEPTH_BUFFER_BIT);
    glLoadIdentity();
    glColor3f(1, 1, 1);
    glEnable(GL_TEXTURE_2D);
    glBindTexture(GL_TEXTURE_2D, texid);

    size_t req_bytes = sizeof(camera_tile_t) + 3 * (size_t)camera.w * (size_t)camera.h;
    if (buffer_image.data && buffer_image.size >= req_bytes) {
	glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, camera.w, camera.h, GL_RGB, GL_UNSIGNED_BYTE, buffer_image.data + sizeof(camera_tile_t));
    }

    glBegin(GL_TRIANGLE_STRIP);

    glTexCoord2d(0, 0); glVertex3f(0, 0, 0);
    glTexCoord2d(0, 1); glVertex3f(0, m_w->height(), 0);
    glTexCoord2d(1, 0); glVertex3f(m_w->width(), 0, 0);
    glTexCoord2d(1, 1); glVertex3f(m_w->width(), m_w->height(), 0);

    glEnd();

    // Make no context current on this thread and move the QOpenGLWidget's
    // context back to the gui thread.
    m_w->doneCurrent();
    ctx->moveToThread(qGuiApp->thread());

    // Schedule composition. Note that this will use QueuedConnection, meaning
    // that update() will be invoked on the gui thread.
    QMetaObject::invokeMethod(m_w, "update");

    // Avoid a hot spin
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
}

isstGL::isstGL(QWidget *parent)
    : QOpenGLWidget(parent)
{
    connect(this, &QOpenGLWidget::aboutToCompose, this, &isstGL::onAboutToCompose);
    connect(this, &QOpenGLWidget::frameSwapped, this, &isstGL::onFrameSwapped);
    connect(this, &QOpenGLWidget::aboutToResize, this, &isstGL::onAboutToResize);
    connect(this, &QOpenGLWidget::resized, this, &isstGL::onResized);

    m_thread = new QThread;

    // Create the renderer
    m_renderer = new TIERenderer(this);
    m_renderer->moveToThread(m_thread);
    connect(m_thread, &QThread::finished, m_renderer, &QObject::deleteLater);

    // Let isstGL know to trigger the renderer
    connect(this, &isstGL::renderRequested, m_renderer, &TIERenderer::render);
    connect(m_renderer, &TIERenderer::contextWanted, this, &isstGL::grabContext);

    m_thread->start();

    // This is an important Qt setting for interactivity - it allowing key
    // bindings to propagate to this widget and trigger actions such as
    // resolution scaling, rotation, etc.
    setFocusPolicy(Qt::WheelFocus);
}

isstGL::~isstGL()
{
    if (m_renderer) {
	m_renderer->prepareExit();
    }
    if (m_thread) {
	m_thread->quit();
	m_thread->wait();
	delete m_thread;
	m_thread = nullptr;
    }
}

void isstGL::onAboutToCompose()
{
    // We are on the gui thread here. Composition is about to
    // begin. Wait until the render thread finishes.
    if (m_renderer)
	m_renderer->lockRenderer();
}

void isstGL::onFrameSwapped()
{
    if (m_renderer)
	m_renderer->unlockRenderer();
    // Assuming a blocking swap, our animation is driven purely by the
    // vsync in this example.
    emit renderRequested();
}

void isstGL::onAboutToResize()
{
    if (m_renderer)
	m_renderer->lockRenderer();
}

void isstGL::onResized()
{
    if (m_renderer) {
	m_renderer->changed = true;
	m_renderer->unlockRenderer();
    }
}

void isstGL::grabContext()
{
    if (!m_renderer || m_renderer->m_exiting)
	return;
    m_renderer->lockRenderer();
    QMutexLocker lock(m_renderer->grabMutex());
    context()->moveToThread(m_thread);
    m_renderer->grabCond()->wakeAll();
    m_renderer->unlockRenderer();
}

void
isstGL::set_tie(struct tie_s *in_tie)
{
    if (!m_renderer)
	return;

    m_renderer->lockRenderer();
    m_renderer->tie = in_tie;
    if (in_tie) {
	// Initialize the camera position
	VSETALL(m_renderer->camera.pos, in_tie->radius);
	VMOVE(m_renderer->camera.focus, in_tie->mid);

	// Record the initial settings for use in subsequent calculations
	VSETALL(m_renderer->camera_pos_init, in_tie->radius);
	VMOVE(m_renderer->camera_focus_init, in_tie->mid);
	m_renderer->changed = true;
    }
    m_renderer->unlockRenderer();

    // Having just loaded a new TIE scene,
    // we need a new image
    emit renderRequested();
}

void isstGL::keyPressEvent(QKeyEvent *k) {
    if (!k || !m_renderer)
	return;
    //QString kstr = QKeySequence(k->key()).toString();
    //bu_log("%s\n", kstr.toStdString().c_str());
    switch (k->key()) {
	case '=':
	    m_renderer->res_incr();
	    emit renderRequested();
	    update();
	    return;
	    break;
	case '-':
	    m_renderer->res_decr();
	    emit renderRequested();
	    update();
	    return;
	    break;
    }
    QOpenGLWidget::keyPressEvent(k);
}


void isstGL::mouseMoveEvent(QMouseEvent *e)
{
    if (!e)
	return;
#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
    bu_log("(%d,%d)\n", e->x(), e->y());
    if (x_prev > -INT_MAX && y_prev > -INT_MAX) {
	bu_log("Delta: (%d,%d)\n", e->x() - x_prev, e->y() - y_prev);
    }
    x_prev = e->x();
    y_prev = e->y();
#else
    bu_log("(%f,%f)\n", e->position().x(), e->position().y());
    if (x_prev > -INT_MAX && y_prev > -INT_MAX) {
	bu_log("Delta: (%f,%f)\n", e->position().x() - x_prev, e->position().y() - y_prev);
    }
    x_prev = (int)e->position().x();
    y_prev = (int)e->position().y();
#endif

    QOpenGLWidget::mouseMoveEvent(e);
}

void isstGL::save_image() {
    QImage image = this->grabFramebuffer();
    if (!image.isNull()) {
	image.save("file.png");
    }
}

// Local Variables:
// tab-width: 8
// mode: C++
// c-basic-offset: 4
// indent-tabs-mode: t
// c-file-style: "stroustrup"
// End:
// ex: shiftwidth=4 tabstop=8

