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

#ifndef WATERFALLITEM_H
#define WATERFALLITEM_H

#include <qqmlintegration.h>

#include <QColor>
#include <QImage>
#include <QPointF>
#include <QQuickItem>
#include <QVector>
#include <vector>

class QSGTexture;

/**
 * \class WaterfallItem
 * \brief Qt Quick scene graph item rendering spectrum history as a scrolling waterfall.
 *
 * Displays a 2D time-frequency-intensity plot where:
 *   - X axis = frequency (MHz), same range as the spectrum line chart
 *   - Y axis = time (newest row at top, scrolls downward)
 *   - Color = signal intensity mapped via rainbow color LUT (blue=weak, red=strong)
 *
 * The item is designed to be placed below the spectrum LineChartItem with its
 * plotLeftMargin/plotRightMargin bound to the spectrum chart so the frequency
 * columns align exactly.
 */
class WaterfallItem : public QQuickItem
{
    Q_OBJECT
    QML_ELEMENT

    Q_PROPERTY(double xMin READ xMin WRITE setXMin NOTIFY xRangeChanged)
    Q_PROPERTY(double xMax READ xMax WRITE setXMax NOTIFY xRangeChanged)
    Q_PROPERTY(double yMin READ yMin WRITE setYMin NOTIFY yRangeChanged)
    Q_PROPERTY(double yMax READ yMax WRITE setYMax NOTIFY yRangeChanged)
    Q_PROPERTY(int numRows READ numRows WRITE setNumRows NOTIFY numRowsChanged)
    Q_PROPERTY(int rowHeight READ rowHeight WRITE setRowHeight NOTIFY rowHeightChanged)
    Q_PROPERTY(QColor backgroundColor READ backgroundColor WRITE setBackgroundColor NOTIFY appearanceChanged)
    Q_PROPERTY(int plotLeftMargin READ plotLeftMargin WRITE setPlotLeftMargin NOTIFY appearanceChanged)
    Q_PROPERTY(int plotRightMargin READ plotRightMargin WRITE setPlotRightMargin NOTIFY appearanceChanged)
    Q_PROPERTY(int plotTopMargin READ plotTopMargin WRITE setPlotTopMargin NOTIFY appearanceChanged)
    Q_PROPERTY(int plotBottomMargin READ plotBottomMargin WRITE setPlotBottomMargin NOTIFY appearanceChanged)
    Q_PROPERTY(double markerLeft READ markerLeft WRITE setMarkerLeft NOTIFY markersChanged)
    Q_PROPERTY(double markerRight READ markerRight WRITE setMarkerRight NOTIFY markersChanged)
    Q_PROPERTY(double markerCenter READ markerCenter WRITE setMarkerCenter NOTIFY markersChanged)
    Q_PROPERTY(QColor markerBandColor READ markerBandColor WRITE setMarkerBandColor NOTIFY appearanceChanged)
    Q_PROPERTY(QColor markerCenterColor READ markerCenterColor WRITE setMarkerCenterColor NOTIFY appearanceChanged)

public:
    explicit WaterfallItem(QQuickItem *parent = nullptr);
    ~WaterfallItem() override;

    // Visible frequency range (bound from spectrum chart)
    double xMin() const { return m_xMin; }
    double xMax() const { return m_xMax; }
    void setXMin(double v);
    void setXMax(double v);

    // dB color scale range (bound from spectrum chart yMin/yMax)
    double yMin() const { return m_yMin; }
    double yMax() const { return m_yMax; }
    void setYMin(double v);
    void setYMax(double v);

    int numRows() const { return m_numRows; }
    void setNumRows(int rows);

    int rowHeight() const { return m_rowHeight; }
    void setRowHeight(int h);

    QColor backgroundColor() const { return m_backgroundColor; }
    void setBackgroundColor(const QColor &c);

    int plotLeftMargin() const { return m_plotLeftMargin; }
    int plotRightMargin() const { return m_plotRightMargin; }
    int plotTopMargin() const { return m_plotTopMargin; }
    int plotBottomMargin() const { return m_plotBottomMargin; }
    void setPlotLeftMargin(int v);
    void setPlotRightMargin(int v);
    void setPlotTopMargin(int v);
    void setPlotBottomMargin(int v);

    double markerLeft() const { return m_markerLeft; }
    double markerRight() const { return m_markerRight; }
    double markerCenter() const { return m_markerCenter; }
    void setMarkerLeft(double v);
    void setMarkerRight(double v);
    void setMarkerCenter(double v);

    QColor markerBandColor() const { return m_markerBandColor; }
    void setMarkerBandColor(const QColor &c);
    QColor markerCenterColor() const { return m_markerCenterColor; }
    void setMarkerCenterColor(const QColor &c);

    /**
     * Add a new spectrum row to the waterfall (called from SignalBackend).
     * Each QPointF is (frequencyMHz, dBvalue). The image scrolls: newest row at top.
     */
    Q_INVOKABLE void addRow(const QVector<QPointF> &bins);

    /**
     * Update the full data frequency range and clear the image (called on tune/frequency change).
     */
    Q_INVOKABLE void setDataRange(double xDataMin, double xDataMax);

signals:
    void xRangeChanged();
    void yRangeChanged();
    void numRowsChanged();
    void rowHeightChanged();
    void appearanceChanged();
    void markersChanged();

protected:
    QSGNode *updatePaintNode(QSGNode *old, UpdatePaintNodeData *) override;
    void geometryChange(const QRectF &newGeometry, const QRectF &oldGeometry) override;

private:
    void buildColorLut();
    void allocateImage();
    void clearImage();
    QRgb dbToColor(float dB) const;
    QRectF plotRect() const;
    void recolorImage();         // Re-color entire m_image from m_rawData using current yMin/yMax
    void rebuildColorBarImage(); // Render gradient strip + dB labels into m_colorBarImage

    // Visible range (from spectrum chart)
    double m_xMin = -1.0;
    double m_xMax = 1.0;
    double m_yMin = -140.0;
    double m_yMax = 0.0;

    // Full data range (set by setDataRange)
    double m_xDataMin = -1.024;
    double m_xDataMax = 1.024;

    int m_numRows = 200;
    int m_rowHeight = 2;   // pixel rows written per spectrum update
    int m_numCols = 2048;  // matches FFT size

    QColor m_backgroundColor{0x10, 0x10, 0x14};
    int m_plotLeftMargin = 64;
    int m_plotRightMargin = 24;
    int m_plotTopMargin = 4;
    int m_plotBottomMargin = 4;

    double m_markerLeft = -0.768;
    double m_markerRight = 0.768;
    double m_markerCenter = 0.0;
    QColor m_markerBandColor{Qt::white};
    QColor m_markerCenterColor{255, 84, 84};

    QImage m_image;           // ARGB32, m_numCols × m_numRows
    bool m_imageDirty = false;
    bool m_geometryDirty = true;

    QRgb m_colorLut[256];
    QSGTexture *m_texture = nullptr;

    // Raw dB history — lets us re-color when yMin/yMax changes
    std::vector<float> m_rawData;       // [row * m_numCols + col], row 0 = newest
    static constexpr float kNoData = -1e30f;

    // Color-scale bar drawn via QPainter (gradient strip + dB labels)
    QImage m_colorBarImage;
    bool m_colorBarDirty = true;
    QSGTexture *m_colorBarTexture = nullptr;
};

#endif  // WATERFALLITEM_H
