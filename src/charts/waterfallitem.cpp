/*
 * This file is part of the AbracaDABra project
 *
 * MIT License
 *
 * Copyright (c) 2019-2026 Petr Kopecký <xkejpi (at) gmail (dot) com>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include "waterfallitem.h"

#include <QQuickWindow>
#include <QSGClipNode>
#include <QSGFlatColorMaterial>
#include <QSGGeometry>
#include <QSGGeometryNode>
#include <QSGSimpleTextureNode>
#include <QPainter>
#include <algorithm>
#include <cmath>
#include <cstring>

// ============================================================================
// Construction
// ============================================================================

WaterfallItem::WaterfallItem(QQuickItem *parent) : QQuickItem(parent)
{
    setFlag(ItemHasContents, true);
    buildColorLut();
    allocateImage();
    clearImage();
}

WaterfallItem::~WaterfallItem()
{
    delete m_texture;
    delete m_colorBarTexture;
}

// ============================================================================
// Color LUT — classic rainbow: blue → cyan → green → yellow → red
// index 0 = weakest signal (yMin), index 255 = strongest (yMax)
// ============================================================================

void WaterfallItem::buildColorLut()
{
    for (int i = 0; i < 256; ++i)
    {
        float t = i / 255.0f;  // 0..1
        int r, g, b;

        if (t < 0.25f)
        {  // blue → cyan
            float s = t / 0.25f;
            r = 0;
            g = static_cast<int>(255 * s);
            b = 255;
        }
        else if (t < 0.5f)
        {  // cyan → green
            float s = (t - 0.25f) / 0.25f;
            r = 0;
            g = 255;
            b = static_cast<int>(255 * (1.0f - s));
        }
        else if (t < 0.75f)
        {  // green → yellow
            float s = (t - 0.5f) / 0.25f;
            r = static_cast<int>(255 * s);
            g = 255;
            b = 0;
        }
        else
        {  // yellow → red
            float s = (t - 0.75f) / 0.25f;
            r = 255;
            g = static_cast<int>(255 * (1.0f - s));
            b = 0;
        }
        m_colorLut[i] = qRgb(r, g, b);
    }
}

// ============================================================================
// Image management
// ============================================================================

void WaterfallItem::allocateImage()
{
    m_image = QImage(m_numCols, m_numRows, QImage::Format_ARGB32);
    m_rawData.assign(static_cast<size_t>(m_numCols) * m_numRows, kNoData);
}

void WaterfallItem::clearImage()
{
    m_image.fill(m_backgroundColor);
    std::fill(m_rawData.begin(), m_rawData.end(), kNoData);
    m_imageDirty = true;
    m_colorBarDirty = true;
}

QRgb WaterfallItem::dbToColor(float dB) const
{
    float range = static_cast<float>(m_yMax - m_yMin);
    if (range <= 0.0f)
    {
        return m_colorLut[0];
    }
    float norm = (dB - static_cast<float>(m_yMin)) / range;
    int idx = static_cast<int>(norm * 255.0f);
    idx = std::clamp(idx, 0, 255);
    return m_colorLut[idx];
}

// ============================================================================
// Public API
// ============================================================================

void WaterfallItem::addRow(const QVector<QPointF> &bins)
{
    if (bins.isEmpty() || m_numCols <= 0 || m_numRows <= 0)
    {
        return;
    }

    // Scroll: move rows 0..n-rowHeight-1 down by rowHeight (newest rows = rows 0..rowHeight-1)
    const int scrollRows = std::min(m_rowHeight, m_numRows);
    if (m_numRows > scrollRows)
    {
        const int rowBytes = m_image.bytesPerLine();
        uchar *data = m_image.bits();
        std::memmove(data + rowBytes * scrollRows, data, rowBytes * (m_numRows - scrollRows));
        // Also scroll the raw dB history
        std::memmove(&m_rawData[static_cast<size_t>(scrollRows) * m_numCols], &m_rawData[0],
                     sizeof(float) * (m_numRows - scrollRows) * m_numCols);
    }

    // Fill rows 0..scrollRows-1 with the new spectrum data
    // Build the pixel row once, then copy it for each repeated row.
    QRgb *row0 = reinterpret_cast<QRgb *>(m_image.scanLine(0));
    float *rawRow0 = m_rawData.data();

    // Background fill first
    QRgb bg = m_backgroundColor.rgba();
    std::fill(row0, row0 + m_numCols, bg);
    std::fill(rawRow0, rawRow0 + m_numCols, kNoData);

    double dataSpan = m_xDataMax - m_xDataMin;
    if (dataSpan <= 0.0)
    {
        m_imageDirty = true;
        update();
        return;
    }
    double colScale = (m_numCols - 1) / dataSpan;

    for (const QPointF &pt : bins)
    {
        double freqMHz = pt.x();
        double col = (freqMHz - m_xDataMin) * colScale;
        int c = static_cast<int>(std::round(col));
        if (c >= 0 && c < m_numCols)
        {
            const float dB = static_cast<float>(pt.y());
            row0[c] = dbToColor(dB);
            rawRow0[c] = dB;
        }
    }

    // Duplicate row 0 into rows 1..scrollRows-1
    const int rowBytes = m_image.bytesPerLine();
    for (int r = 1; r < scrollRows; ++r)
    {
        uchar *dst = m_image.scanLine(r);
        std::memcpy(dst, reinterpret_cast<const uchar *>(row0), rowBytes);
        std::memcpy(&m_rawData[static_cast<size_t>(r) * m_numCols], rawRow0, sizeof(float) * m_numCols);
    }

    m_imageDirty = true;
    update();
}

void WaterfallItem::setDataRange(double xDataMin, double xDataMax)
{
    m_xDataMin = xDataMin;
    m_xDataMax = xDataMax;
    clearImage();
    update();
}

// ============================================================================
// Property setters
// ============================================================================

void WaterfallItem::setXMin(double v)
{
    if (qFuzzyCompare(m_xMin, v))
        return;
    m_xMin = v;
    emit xRangeChanged();
    update();
}

void WaterfallItem::setXMax(double v)
{
    if (qFuzzyCompare(m_xMax, v))
        return;
    m_xMax = v;
    emit xRangeChanged();
    update();
}

void WaterfallItem::setYMin(double v)
{
    if (qFuzzyCompare(m_yMin, v))
        return;
    m_yMin = v;
    recolorImage();
    m_colorBarDirty = true;
    emit yRangeChanged();
    update();
}

void WaterfallItem::setYMax(double v)
{
    if (qFuzzyCompare(m_yMax, v))
        return;
    m_yMax = v;
    recolorImage();
    m_colorBarDirty = true;
    emit yRangeChanged();
    update();
}

void WaterfallItem::setNumRows(int rows)
{
    if (rows == m_numRows || rows <= 0)
        return;
    m_numRows = rows;
    allocateImage();
    clearImage();
    emit numRowsChanged();
    update();
}

void WaterfallItem::setRowHeight(int h)
{
    if (h == m_rowHeight || h <= 0)
        return;
    m_rowHeight = h;
    emit rowHeightChanged();
}

void WaterfallItem::setBackgroundColor(const QColor &c)
{
    if (m_backgroundColor == c)
        return;
    m_backgroundColor = c;
    clearImage();
    emit appearanceChanged();
    update();
}

void WaterfallItem::setPlotLeftMargin(int v)
{
    if (m_plotLeftMargin == v)
        return;
    m_plotLeftMargin = v;
    m_geometryDirty = true;
    m_colorBarDirty = true;
    emit appearanceChanged();
    update();
}

void WaterfallItem::setPlotRightMargin(int v)
{
    if (m_plotRightMargin == v)
        return;
    m_plotRightMargin = v;
    m_geometryDirty = true;
    emit appearanceChanged();
    update();
}

void WaterfallItem::setPlotTopMargin(int v)
{
    if (m_plotTopMargin == v)
        return;
    m_plotTopMargin = v;
    m_geometryDirty = true;
    m_colorBarDirty = true;
    emit appearanceChanged();
    update();
}

void WaterfallItem::setPlotBottomMargin(int v)
{
    if (m_plotBottomMargin == v)
        return;
    m_plotBottomMargin = v;
    m_geometryDirty = true;
    m_colorBarDirty = true;
    emit appearanceChanged();
    update();
}

void WaterfallItem::setMarkerLeft(double v)
{
    if (qFuzzyCompare(m_markerLeft, v))
        return;
    m_markerLeft = v;
    emit markersChanged();
    update();
}

void WaterfallItem::setMarkerRight(double v)
{
    if (qFuzzyCompare(m_markerRight, v))
        return;
    m_markerRight = v;
    emit markersChanged();
    update();
}

void WaterfallItem::setMarkerCenter(double v)
{
    if (qFuzzyCompare(m_markerCenter, v))
        return;
    m_markerCenter = v;
    emit markersChanged();
    update();
}

void WaterfallItem::setMarkerBandColor(const QColor &c)
{
    if (m_markerBandColor == c)
        return;
    m_markerBandColor = c;
    emit appearanceChanged();
    update();
}

void WaterfallItem::setMarkerCenterColor(const QColor &c)
{
    if (m_markerCenterColor == c)
        return;
    m_markerCenterColor = c;
    emit appearanceChanged();
    update();
}

void WaterfallItem::geometryChange(const QRectF &newGeometry, const QRectF &oldGeometry)
{
    QQuickItem::geometryChange(newGeometry, oldGeometry);
    if (newGeometry.size() != oldGeometry.size())
    {
        m_geometryDirty = true;
        m_colorBarDirty = true;
        update();
    }
}

// ============================================================================
// Helper: plot area rectangle
// ============================================================================

QRectF WaterfallItem::plotRect() const
{
    return QRectF(m_plotLeftMargin, m_plotTopMargin, width() - m_plotLeftMargin - m_plotRightMargin,
                  height() - m_plotTopMargin - m_plotBottomMargin);
}

// ============================================================================
// Helper: compute x pixel for a frequency value within the plot area
// ============================================================================

static float freqToPlotX(double freqMHz, double xMin, double xMax, const QRectF &pr)
{
    if (xMax <= xMin)
        return static_cast<float>(pr.left());
    double norm = (freqMHz - xMin) / (xMax - xMin);
    return static_cast<float>(pr.left() + norm * pr.width());
}

// ============================================================================
// Re-color entire history after Y range change
// ============================================================================

void WaterfallItem::recolorImage()
{
    if (m_rawData.empty())
        return;
    const QRgb bg = m_backgroundColor.rgba();
    for (int r = 0; r < m_numRows; ++r)
    {
        QRgb *rowPtr = reinterpret_cast<QRgb *>(m_image.scanLine(r));
        const float *rawRow = &m_rawData[static_cast<size_t>(r) * m_numCols];
        for (int c = 0; c < m_numCols; ++c)
        {
            rowPtr[c] = (rawRow[c] > kNoData + 1.0f) ? dbToColor(rawRow[c]) : bg;
        }
    }
    m_imageDirty = true;
}

// ============================================================================
// Rebuild color-scale bar (gradient strip + dB labels drawn via QPainter)
// ============================================================================

void WaterfallItem::rebuildColorBarImage()
{
    const qreal dpr = window() ? window()->devicePixelRatio() : 1.0;
    const int logH = std::max(static_cast<int>(height()) - m_plotTopMargin - m_plotBottomMargin, 4);
    const int logW = std::max(m_plotLeftMargin, 4);
    // Render at full physical resolution so it is crisp on HiDPI/Retina
    const int physW = static_cast<int>(std::ceil(logW * dpr));
    const int physH = static_cast<int>(std::ceil(logH * dpr));

    m_colorBarImage = QImage(physW, physH, QImage::Format_ARGB32_Premultiplied);
    m_colorBarImage.fill(Qt::transparent);

    const int fontPx = std::max(1, static_cast<int>(std::round(12.0 * dpr)));
    // halfH ≈ half a line height — strip is inset by this amount top & bottom so
    // the yMax/yMin labels sit centred exactly at the strip endpoints, no clamping needed.
    const int halfH = std::max(1, static_cast<int>(std::round(7.0 * dpr)));

    // Strip: 10 logical px wide, 2 logical px from the RIGHT edge (nearest the plot)
    const int stripW = static_cast<int>(std::round(10.0 * dpr));
    const int stripX = physW - static_cast<int>(std::round(8.0 * dpr)) - stripW;

    // Shorter strip — inset by halfH top & bottom so end labels fit without clipping
    const int stripTop    = halfH;
    const int stripBottom = physH - halfH;  // exclusive

    for (int y = stripTop; y < stripBottom; ++y)
    {
        // y = stripTop -> yMax (red);  y = stripBottom-1 -> yMin (blue)
        const float norm = 1.0f - static_cast<float>(y - stripTop)
                                  / static_cast<float>(stripBottom - stripTop - 1);
        const int idx = std::clamp(static_cast<int>(norm * 255.0f), 0, 255);
        const QRgb color = m_colorLut[idx] | 0xFF000000u;
        QRgb *line = reinterpret_cast<QRgb *>(m_colorBarImage.scanLine(y));
        for (int x = stripX; x < stripX + stripW; ++x)
            line[x] = color;
    }

    const double range = m_yMax - m_yMin;
    if (range > 0.0)
    {
        QPainter p(&m_colorBarImage);
        p.setRenderHint(QPainter::TextAntialiasing);
        QFont f;
        f.setPixelSize(fontPx);
        p.setFont(f);
        p.setPen(QColor(0xe8, 0xe8, 0xf0));

        const int stripSpan = stripBottom - stripTop - 1;

        // Choose smallest multiple-of-10 step so labels (height ≈ 2*halfH px each)
        // never overlap. Derive from how many pixels one 10 dB step occupies.
        const double pixPer10dB = (stripSpan > 0) ? (10.0 / range) * stripSpan : 0.0;
        const double minStep    = (pixPer10dB > 0) ? std::ceil((halfH * 2.0) / pixPer10dB) * 10.0 : range;
        const double tickStep   = std::max(10.0, minStep);

        // Ticks protrude LEFT from the strip's left edge
        const int tickStartX = stripX - static_cast<int>(std::round(3.0 * dpr));
        // Label area: from x=0, right-aligned, up to just left of the tick
        const int labelW = tickStartX - static_cast<int>(std::round(2.0 * dpr));

        for (double tick = std::ceil(m_yMin / tickStep) * tickStep; tick <= m_yMax + 0.01; tick += tickStep)
        {
            // Map tick value into the shorter strip coordinate range
            const int y = static_cast<int>(std::round(
                stripTop + (1.0 - (tick - m_yMin) / range) * (stripBottom - stripTop - 1)));
            if (y < 0 || y >= physH)
                continue;
            p.drawLine(tickStartX, y, stripX, y);
            if (labelW > 0)
            {
                // End labels: y == stripTop or stripBottom-1, so y ± halfH stays in 0..physH
                p.drawText(QRect(0, y - halfH, labelW, halfH * 2),
                           Qt::AlignRight | Qt::AlignVCenter,
                           QString::number(static_cast<int>(std::round(tick))));
            }
        }
    }

    m_colorBarDirty = false;
}

// ============================================================================
// Scene graph rendering
// ============================================================================

QSGNode *WaterfallItem::updatePaintNode(QSGNode *old, UpdatePaintNodeData *)
{
    if (width() <= 0 || height() <= 0)
    {
        delete old;
        return nullptr;
    }

    // Root: clip node restricting rendering to the item bounds
    QSGClipNode *root = static_cast<QSGClipNode *>(old);
    if (!root)
    {
        root = new QSGClipNode();
        root->setIsRectangular(true);
    }
    root->setClipRect(QRectF(0, 0, width(), height()));

    // ---- Node 0: background rectangle ----
    QSGGeometryNode *bgNode = nullptr;
    QSGSimpleTextureNode *texNode = nullptr;
    QSGGeometryNode *markerLeftNode = nullptr;
    QSGGeometryNode *markerRightNode = nullptr;
    QSGGeometryNode *markerCenterNode = nullptr;
    QSGSimpleTextureNode *colorBarNode = nullptr;

    if (root->childCount() == 0)
    {
        // Build tree for the first time

        // Background
        bgNode = new QSGGeometryNode();
        auto *bgGeom = new QSGGeometry(QSGGeometry::defaultAttributes_Point2D(), 4);
        bgGeom->setDrawingMode(QSGGeometry::DrawTriangleStrip);
        bgNode->setGeometry(bgGeom);
        bgNode->setFlag(QSGNode::OwnsGeometry);
        auto *bgMat = new QSGFlatColorMaterial();
        bgNode->setMaterial(bgMat);
        bgNode->setFlag(QSGNode::OwnsMaterial);
        root->appendChildNode(bgNode);

        // Texture node for waterfall image
        texNode = new QSGSimpleTextureNode();
        texNode->setFiltering(QSGTexture::Nearest);
        root->appendChildNode(texNode);

        // Marker: left band edge
        markerLeftNode = new QSGGeometryNode();
        auto *lGeom = new QSGGeometry(QSGGeometry::defaultAttributes_Point2D(), 2);
        lGeom->setDrawingMode(QSGGeometry::DrawLines);
        lGeom->setLineWidth(1.0f);
        markerLeftNode->setGeometry(lGeom);
        markerLeftNode->setFlag(QSGNode::OwnsGeometry);
        auto *lMat = new QSGFlatColorMaterial();
        markerLeftNode->setMaterial(lMat);
        markerLeftNode->setFlag(QSGNode::OwnsMaterial);
        root->appendChildNode(markerLeftNode);

        // Marker: right band edge
        markerRightNode = new QSGGeometryNode();
        auto *rGeom = new QSGGeometry(QSGGeometry::defaultAttributes_Point2D(), 2);
        rGeom->setDrawingMode(QSGGeometry::DrawLines);
        rGeom->setLineWidth(1.0f);
        markerRightNode->setGeometry(rGeom);
        markerRightNode->setFlag(QSGNode::OwnsGeometry);
        auto *rMat = new QSGFlatColorMaterial();
        markerRightNode->setMaterial(rMat);
        markerRightNode->setFlag(QSGNode::OwnsMaterial);
        root->appendChildNode(markerRightNode);

        // Marker: center
        markerCenterNode = new QSGGeometryNode();
        auto *cGeom = new QSGGeometry(QSGGeometry::defaultAttributes_Point2D(), 2);
        cGeom->setDrawingMode(QSGGeometry::DrawLines);
        cGeom->setLineWidth(1.0f);
        markerCenterNode->setGeometry(cGeom);
        markerCenterNode->setFlag(QSGNode::OwnsGeometry);
        auto *cMat = new QSGFlatColorMaterial();
        markerCenterNode->setMaterial(cMat);
        markerCenterNode->setFlag(QSGNode::OwnsMaterial);
        root->appendChildNode(markerCenterNode);

        // Color scale bar (gradient strip + dB labels)
        colorBarNode = new QSGSimpleTextureNode();
        colorBarNode->setFiltering(QSGTexture::Nearest);
        root->appendChildNode(colorBarNode);
    }
    else
    {
        bgNode = static_cast<QSGGeometryNode *>(root->childAtIndex(0));
        texNode = static_cast<QSGSimpleTextureNode *>(root->childAtIndex(1));
        markerLeftNode = static_cast<QSGGeometryNode *>(root->childAtIndex(2));
        markerRightNode = static_cast<QSGGeometryNode *>(root->childAtIndex(3));
        markerCenterNode = static_cast<QSGGeometryNode *>(root->childAtIndex(4));
        colorBarNode = static_cast<QSGSimpleTextureNode *>(root->childAtIndex(5));
    }

    const QRectF pr = plotRect();

    // ---- Update background ----
    {
        auto *mat = static_cast<QSGFlatColorMaterial *>(bgNode->material());
        mat->setColor(m_backgroundColor);
        bgNode->markDirty(QSGNode::DirtyMaterial);

        float x0 = 0, y0 = 0, x1 = static_cast<float>(width()), y1 = static_cast<float>(height());
        auto *geom = bgNode->geometry();
        auto *v = geom->vertexDataAsPoint2D();
        v[0].set(x0, y0);
        v[1].set(x1, y0);
        v[2].set(x0, y1);
        v[3].set(x1, y1);
        bgNode->markDirty(QSGNode::DirtyGeometry);
    }

    // ---- Update waterfall texture ----
    if (m_imageDirty || m_geometryDirty)
    {
        // Recreate texture from image
        delete m_texture;
        m_texture = window()->createTextureFromImage(m_image);
        texNode->setTexture(m_texture);
        m_imageDirty = false;
        m_geometryDirty = false;
    }

    // Set the destination rect (plot area in item coordinates)
    texNode->setRect(pr);

    // Compute source rect in PIXEL coordinates (setSourceRect takes pixel coords,
    // not normalized UVs) to show only the visible [xMin..xMax] sub-range.
    {
        double dataSpan = m_xDataMax - m_xDataMin;
        if (dataSpan > 0.0)
        {
            float u0 = static_cast<float>((m_xMin - m_xDataMin) / dataSpan);
            float u1 = static_cast<float>((m_xMax - m_xDataMin) / dataSpan);
            u0 = std::clamp(u0, 0.0f, 1.0f);
            u1 = std::clamp(u1, 0.0f, 1.0f);
            texNode->setSourceRect(QRectF(u0 * m_numCols, 0.0, (u1 - u0) * m_numCols, m_numRows));
        }
        else
        {
            texNode->setSourceRect(QRectF(0.0, 0.0, m_numCols, m_numRows));
        }
    }

    // ---- Update marker lines ----
    auto updateMarker = [&](QSGGeometryNode *node, double freqMHz, const QColor &color)
    {
        float x = freqToPlotX(freqMHz, m_xMin, m_xMax, pr);
        float y0 = static_cast<float>(pr.top());
        float y1 = static_cast<float>(pr.bottom());

        auto *geom = node->geometry();
        auto *v = geom->vertexDataAsPoint2D();
        v[0].set(x, y0);
        v[1].set(x, y1);
        node->markDirty(QSGNode::DirtyGeometry);

        auto *mat = static_cast<QSGFlatColorMaterial *>(node->material());
        mat->setColor(color);
        node->markDirty(QSGNode::DirtyMaterial);
    };

    updateMarker(markerLeftNode, m_markerLeft, m_markerBandColor);
    updateMarker(markerRightNode, m_markerRight, m_markerBandColor);
    updateMarker(markerCenterNode, m_markerCenter, m_markerCenterColor);

    // ---- Update color scale bar ----
    if (m_colorBarDirty || !m_colorBarTexture)
    {
        rebuildColorBarImage();
        delete m_colorBarTexture;
        m_colorBarTexture = window()->createTextureFromImage(m_colorBarImage);
    }
    colorBarNode->setTexture(m_colorBarTexture);
    colorBarNode->setRect(QRectF(0, m_plotTopMargin, m_plotLeftMargin,
                                  height() - m_plotTopMargin - m_plotBottomMargin));

    return root;
}
