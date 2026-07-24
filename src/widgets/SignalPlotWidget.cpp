#include "widgets/SignalPlotWidget.hpp"

#include <QPainter>
#include <QPainterPath>
#include <QPaintEvent>

#include <algorithm>
#include <cmath>

namespace chronolab {

SignalPlotWidget::SignalPlotWidget(QWidget* parent)
    : QWidget(parent)
{
    setMinimumHeight(150);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
}

void SignalPlotWidget::setSamples(const QVector<float>& samples)
{
    m_samples = samples;
    update();
}

void SignalPlotWidget::setAccentColor(const QColor& color)
{
    m_accent = color;
    update();
}

void SignalPlotWidget::paintEvent(QPaintEvent* event)
{
    Q_UNUSED(event);
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.fillRect(rect(), QColor(12, 18, 25));

    painter.setPen(QPen(QColor(38, 50, 61), 1));
    for (int i = 1; i < 4; ++i) {
        const qreal y = height() * i / 4.0;
        painter.drawLine(QPointF(0, y), QPointF(width(), y));
    }
    for (int i = 1; i < 8; ++i) {
        const qreal x = width() * i / 8.0;
        painter.drawLine(QPointF(x, 0), QPointF(x, height()));
    }

    if (m_samples.size() < 2) {
        painter.setPen(QColor(126, 142, 156));
        painter.drawText(rect(), Qt::AlignCenter, tr("In attesa del segnale audio"));
        return;
    }

    float peak = 0.0f;
    for (const float sample : m_samples)
        peak = std::max(peak, std::abs(sample));
    peak = std::max(peak, 0.02f);

    QPainterPath path;
    const qreal center = height() / 2.0;
    const qreal scale = height() * 0.43 / peak;
    path.moveTo(0, center - m_samples.front() * scale);
    for (int pixel = 1; pixel < width(); ++pixel) {
        const qsizetype index = static_cast<qsizetype>(
            static_cast<double>(pixel) / std::max(1, width() - 1)
            * (m_samples.size() - 1));
        path.lineTo(pixel, center - m_samples[index] * scale);
    }

    painter.setPen(QPen(QColor(m_accent.red(), m_accent.green(), m_accent.blue(), 45), 5));
    painter.drawPath(path);
    painter.setPen(QPen(m_accent, 1.5));
    painter.drawPath(path);

    painter.setPen(QColor(126, 142, 156));
    painter.drawText(QRect(12, 8, width() - 24, 20),
                     Qt::AlignLeft | Qt::AlignVCenter,
                     tr("Forma d'onda · guadagno visuale automatico"));
}

} // namespace chronolab
