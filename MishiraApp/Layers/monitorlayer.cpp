//*****************************************************************************
// Mishira: An audiovisual production tool for broadcasting live video
//
// Copyright (C) 2014 Lucas Murray <lucas@polyflare.com>
// All rights reserved.
//
// This program is free software; you can redistribute it and/or modify it
// under the terms of the GNU General Public License as published by the Free
// Software Foundation; either version 2 of the License, or (at your option)
// any later version.
//
// This program is distributed in the hope that it will be useful, but WITHOUT
// ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
// FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
// more details.
//*****************************************************************************

#include "monitorlayer.h"
#include "application.h"
#include "layergroup.h"
#include "monitorlayerdialog.h"
#ifdef Q_OS_WIN
#include "winapplication.h"
#endif
#include <Libdeskcap/capturemanager.h>
#include <Libdeskcap/captureobject.h>
#include <Libvidgfx/graphicscontext.h>

const QString LOG_CAT = QStringLiteral("Scene");

MonitorLayer::MonitorLayer(LayerGroup *parent)
	: Layer(parent)
	, m_captureObj(NULL)
	, m_curSize()
	, m_curFlipped(false)
	, m_vertBuf(NULL)
	, m_vertBufRect()
	, m_vertBufTlUv(0.0f, 0.0f)
	, m_vertBufBrUv(0.0f, 0.0f)
	, m_vertBufFlipped(false)
	, m_cursorVertBuf(NULL)
	, m_cursorVertBufRect()
	, m_cursorVertBufBrUv(0.0f, 0.0f)
	, m_monitorChanged(false)
	, m_aeroDisableReffed(false)

	// Settings
	, m_monitor(1)
	, m_captureMouse(true)
	, m_disableAero(false)
	, m_captureMethod(CptrAutoMethod)
	, m_cropInfo()
	, m_gamma(1.0f)
	, m_brightness(0)
	, m_contrast(0)
	, m_saturation(0)
{
	// Watch for monitor creation and deletion
	CaptureManager *mgr = App->getCaptureManager();
	connect(mgr, &CaptureManager::monitorInfoChanged,
		this, &MonitorLayer::monitorInfoChanged);
}

MonitorLayer::~MonitorLayer()
{
	if(m_captureObj != NULL)
		m_captureObj->release();

	// Make sure we dereference disabling Windows Aero
	m_disableAero = false;
	updateDisableAero();
}

void MonitorLayer::setMonitor(int monitor)
{
	if(m_monitor == monitor)
		return; // Nothing to do
	m_monitor = monitor;
	m_monitorChanged = true;
	updateResourcesIfLoaded();
	m_parent->layerChanged(this); // Remote emit
}

void MonitorLayer::setCaptureMouse(bool capture)
{
	if(m_captureMouse == capture)
		return; // Nothing to do
	m_captureMouse = capture;
	//updateResourcesIfLoaded(); // Not needed
	m_parent->layerChanged(this); // Remote emit
}

void MonitorLayer::setDisableAero(bool disableAero)
{
	if(m_disableAero == disableAero)
		return; // Nothing to do
	m_disableAero = disableAero;
	//updateResourcesIfLoaded(); // Not needed
	updateDisableAero();
	m_parent->layerChanged(this); // Remote emit
}

void MonitorLayer::setCaptureMethod(CptrMethod method)
{
	if(m_captureMethod == method)
		return; // Nothing to do
	m_captureMethod = method;
	if(m_captureObj != NULL)
		m_captureObj->setMethod(m_captureMethod);
	//updateResourcesIfLoaded(); // Not needed
	m_parent->layerChanged(this); // Remote emit
}

void MonitorLayer::setCropInfo(const CropInfo &info)
{
	if(m_cropInfo == info)
		return; // Nothing changed
	m_cropInfo = info;
	updateResourcesIfLoaded();
	m_parent->layerChanged(this); // Remote emit
}

void MonitorLayer::setGamma(float gamma)
{
	if(m_gamma == gamma)
		return; // Nothing to do
	m_gamma = gamma;
	updateResourcesIfLoaded();
	m_parent->layerChanged(this); // Remote emit
}

void MonitorLayer::setBrightness(int brightness)
{
	if(m_brightness == brightness)
		return; // Nothing to do
	m_brightness = brightness;
	updateResourcesIfLoaded();
	m_parent->layerChanged(this); // Remote emit
}

void MonitorLayer::setContrast(int contrast)
{
	if(m_contrast == contrast)
		return; // Nothing to do
	m_contrast = contrast;
	updateResourcesIfLoaded();
	m_parent->layerChanged(this); // Remote emit
}

void MonitorLayer::setSaturation(int saturation)
{
	if(m_saturation == saturation)
		return; // Nothing to do
	m_saturation = saturation;
	updateResourcesIfLoaded();
	m_parent->layerChanged(this); // Remote emit
}

void MonitorLayer::updateVertBuf(
	GraphicsContext *gfx, const QPointF &tlUv, const QPointF &brUv)
{
	if(m_vertBuf == NULL)
		return;
	if(m_captureObj == NULL)
		return; // Nothing visible

	// Has anything changed that would result in us having to recreate the
	// vertex buffer?
	QRect cropRect = m_cropInfo.calcCroppedRectForSize(m_curSize);
	const QRectF rect = scaledRectFromActualSize(cropRect.size());
	setVisibleRect(rect.toAlignedRect());
	if(m_vertBufTlUv == tlUv && m_vertBufBrUv == brUv &&
		m_vertBufRect == rect && m_vertBufFlipped == m_curFlipped)
	{
		return;
	}

	m_vertBufRect = rect;
	m_vertBufTlUv = tlUv;
	m_vertBufBrUv = brUv;
	m_vertBufFlipped = m_curFlipped;
	if(m_vertBufFlipped) {
		// Texture is flipped vertically
		gfx->createTexDecalRect(m_vertBuf, m_vertBufRect,
			QPointF(tlUv.x(), brUv.y()), brUv,
			tlUv, QPointF(brUv.x(), tlUv.y()));
	} else {
		gfx->createTexDecalRect(m_vertBuf, m_vertBufRect,
			tlUv, QPointF(brUv.x(), tlUv.y()),
			QPointF(tlUv.x(), brUv.y()), brUv);
	}
}

void MonitorLayer::updateCursorVertBuf(
	GraphicsContext *gfx, const QPointF &brUv, const QRect &relRect)
{
	if(m_cursorVertBuf == NULL)
		return;

	// Scale cursor rectangle nicely to match the scaled monitor
	QRect cropRect = m_cropInfo.calcCroppedRectForSize(m_curSize);
	const QPointF scale(
		m_vertBufRect.width() / (float)cropRect.width(),
		m_vertBufRect.height() / (float)cropRect.height());
	QRectF rect(
		m_vertBufRect.x() + qRound((float)(relRect.x() - cropRect.x()) * scale.x()),
		m_vertBufRect.y() + qRound((float)(relRect.y() - cropRect.y()) * scale.y()),
		qRound((float)relRect.width() * scale.x()),
		qRound((float)relRect.height() * scale.y()));

	if(m_cursorVertBufBrUv == brUv && m_cursorVertBufRect == rect)
		return; // Buffer hasn't changed

	m_cursorVertBufRect = rect;
	m_cursorVertBufBrUv = brUv;
	gfx->createTexDecalRect(
		m_cursorVertBuf, m_cursorVertBufRect, m_cursorVertBufBrUv);
}

void MonitorLayer::updateDisableAero()
{
#ifdef Q_OS_WIN
	WinApplication *app = static_cast<WinApplication *>(App);
	if(m_disableAero && !m_aeroDisableReffed) {
		app->refDisableAero();
		m_aeroDisableReffed = true;
	} else if(!m_disableAero && m_aeroDisableReffed) {
		app->derefDisableAero();
		m_aeroDisableReffed = false;
	}
#endif
}

void MonitorLayer::initializeResources(GraphicsContext *gfx)
{
	appLog(LOG_CAT)
		<< "Creating hardware resources for layer " << getIdString();

	// Allocate resources
	m_vertBuf = gfx->createVertexBuffer(GraphicsContext::TexDecalRectBufSize);
	m_cursorVertBuf =
		gfx->createVertexBuffer(GraphicsContext::TexDecalRectBufSize);

	// Reset caching and update all resources
	m_monitorChanged = true;
	m_vertBufRect = QRectF();
	m_vertBufTlUv = QPointF(0.0f, 0.0f);
	m_vertBufBrUv = QPointF(0.0f, 0.0f);
	m_cursorVertBufRect = QRectF();
	m_cursorVertBufBrUv = QPointF(0.0f, 0.0f);
	updateResources(gfx);
}

void MonitorLayer::updateResources(GraphicsContext *gfx)
{
	// Layer is invisible by default
	setVisibleRect(QRect());

	if(m_monitorChanged) {
		m_monitorChanged = false;

		// Release the existing object if it exists
		if(m_captureObj != NULL) {
			m_captureObj->release();
			m_captureObj = NULL;
		}

		// Create the monitor capture object if it exists
		CaptureManager *mgr = App->getCaptureManager();
		MonitorId monId = NULL;
		const MonitorInfoList &monitors = mgr->getMonitorInfo();
		for(int i = 0; i < monitors.size(); i++) {
			if(monitors.at(i).friendlyId == m_monitor) {
				monId = monitors.at(i).handle;
				break;
			}
		}
		if(monId != NULL)
			m_captureObj = mgr->captureMonitor(monId, m_captureMethod);
	}

	m_curSize = QSize();
	if(m_captureObj != NULL) {
		m_curSize = m_captureObj->getSize();
		m_curFlipped = m_captureObj->isFlipped();
	}

	// Assume that the left-left UV is at (0, 0) and bottom-right UV is at
	// (1, 1) for now
	updateVertBuf(gfx, QPointF(0.0f, 0.0f), QPointF(1.0f, 1.0f));
}

void MonitorLayer::destroyResources(GraphicsContext *gfx)
{
	appLog(LOG_CAT)
		<< "Destroying hardware resources for layer " << getIdString();

	gfx->deleteVertexBuffer(m_vertBuf);
	gfx->deleteVertexBuffer(m_cursorVertBuf);
	m_vertBuf = NULL;
	m_cursorVertBuf = NULL;

	if(m_captureObj != NULL) {
		m_captureObj->release();
		m_captureObj = NULL;
	}

	m_curSize = QSize();
}

void MonitorLayer::render(
	GraphicsContext *gfx, Scene *scene, uint frameNum, int numDropped)
{
	if(m_captureObj == NULL || !m_captureObj->isTextureValid())
		return; // Nothing to render

	// Has the capture object changed size since last frame?
	if(m_captureObj->getSize() != m_curSize ||
		m_captureObj->isFlipped() != m_curFlipped)
	{
		updateResources(gfx);
	}

	// Prepare texture for render
	// TODO: Filter mode selection
	QPointF pxSize, topLeft, botRight;
	QRect cropRect = m_cropInfo.calcCroppedRectForSize(m_curSize);
	Texture *tex = gfx->prepareTexture(
		m_captureObj->getTexture(), cropRect,
		m_vertBufRect.toAlignedRect().size(), GfxBilinearFilter, true, pxSize,
		topLeft, botRight);
	if(m_vertBufTlUv != topLeft || m_vertBufBrUv != botRight)
		updateVertBuf(gfx, topLeft, botRight);
	gfx->setTexture(tex);

	// sRGB back buffer correction HACK
	// TODO: 2.233333 gamma isn't really accurate, see:
	// http://chilliant.blogspot.com.au/2012/08/srgb-approximations-for-hlsl.html
	float gamma = m_gamma;
	if(tex->isSrgbHack())
		gamma *= 2.233333f;

	// Do the actual render
	if(gfx->setTexDecalEffectsHelper(
		gamma, m_brightness, m_contrast, m_saturation))
	{
		gfx->setShader(GfxTexDecalGbcsShader);
	} else
		gfx->setShader(GfxTexDecalRgbShader);
	gfx->setTopology(GfxTriangleStripTopology);
	QColor prevCol = gfx->getTexDecalModColor();
	gfx->setTexDecalModColor(
		QColor(255, 255, 255, (int)(getOpacity() * 255.0f)));
	if(getOpacity() != 1.0f)
		gfx->setBlending(GfxAlphaBlending);
	else
		gfx->setBlending(GfxNoBlending);
	gfx->drawBuffer(m_vertBuf);
	gfx->setTexDecalModColor(prevCol);

	//-------------------------------------------------------------------------
	// Render the mouse cursor if it's enabled

	// Get cursor information and return if we have nothing to do
	if(!m_captureMouse)
		return;
	QPoint globalPos;
	QPoint offset;
	bool isVisible;
	Texture *cursorTex =
		App->getSystemCursorInfo(&globalPos, &offset, &isVisible);
	if(cursorTex == NULL || !isVisible)
		return;

	// Is the cursor above our window?
	QPoint localPos = m_captureObj->mapScreenPosToLocal(globalPos);
	if(localPos.x() < cropRect.left() || localPos.x() >= cropRect.right() ||
		localPos.y() < cropRect.top() || localPos.y() >= cropRect.bottom())
	{
		// Cursor is outside our window
		return;
	}

	// Prepare texture for render
	// TODO: Filter mode selection?
	tex = gfx->prepareTexture(
		cursorTex, m_cursorVertBufRect.toAlignedRect().size(),
		GfxBilinearFilter, true, pxSize, botRight);
	updateCursorVertBuf( // Always update
		gfx, botRight, QRect(localPos + offset, cursorTex->getSize()));
	gfx->setTexture(tex);

	// Do the actual render
	if(gfx->setTexDecalEffectsHelper(
		m_gamma, m_brightness, m_contrast, m_saturation))
	{
		gfx->setShader(GfxTexDecalGbcsShader);
	} else
		gfx->setShader(GfxTexDecalShader);
	gfx->setTopology(GfxTriangleStripTopology);
	gfx->setTexDecalModColor( // Incorrect blending but we don't care
		QColor(255, 255, 255, (int)(getOpacity() * 255.0f)));
	gfx->setBlending(GfxAlphaBlending);
	gfx->drawBuffer(m_cursorVertBuf);
	gfx->setTexDecalModColor(prevCol);
}

LyrType MonitorLayer::getType() const
{
	return LyrMonitorLayerType;
}

bool MonitorLayer::hasSettingsDialog()
{
	return true;
}

LayerDialog *MonitorLayer::createSettingsDialog(QWidget *parent)
{
	return new MonitorLayerDialog(this, parent);
}

void MonitorLayer::serialize(QDataStream *stream) const
{
	Layer::serialize(stream);

	// Write data version number
	*stream << (quint32)1;

	// Save our data
	*stream << (quint32)m_monitor;
	*stream << m_captureMouse;
	*stream << m_disableAero;
	*stream << (quint32)m_captureMethod;
	*stream << m_cropInfo.getMargins();
	CropInfo::Anchor leftAnchor, rightAnchor, topAnchor, botAnchor;
	m_cropInfo.getAnchors(&leftAnchor, &rightAnchor, &topAnchor, &botAnchor);
	*stream << (quint32)leftAnchor;
	*stream << (quint32)rightAnchor;
	*stream << (quint32)topAnchor;
	*stream << (quint32)botAnchor;
	*stream << m_gamma;
	*stream << (qint32)m_brightness;
	*stream << (qint32)m_contrast;
	*stream << (qint32)m_saturation;
}

bool MonitorLayer::unserialize(QDataStream *stream)
{
	if(!Layer::unserialize(stream))
		return false;

	// Read data version number
	quint32 version;
	*stream >> version;

	// Read our data
	if(version >= 0 && version <= 1) {
		qint32 int32Data;
		quint32 uint32Data;

		*stream >> uint32Data;
		m_monitor = uint32Data;
		*stream >> m_captureMouse;
		if(version >= 1)
			*stream >> m_disableAero;
		else
			m_disableAero = false;
		updateDisableAero();
		*stream >> uint32Data;
		m_captureMethod = (CptrMethod)uint32Data;

		*stream >> m_cropInfo;

		*stream >> m_gamma;
		*stream >> int32Data;
		m_brightness = int32Data;
		*stream >> int32Data;
		m_contrast = int32Data;
		*stream >> int32Data;
		m_saturation = int32Data;
	} else {
		appLog(LOG_CAT, Log::Warning)
			<< "Unknown version number in monitor capture layer serialized "
			<< "data, cannot load settings";
		return false;
	}

	return true;
}

void MonitorLayer::monitorInfoChanged()
{
	if(m_captureObj == NULL)
		return;
	// A monitor was added or removed
	m_monitorChanged = true;
	updateResourcesIfLoaded();
}
