#include "widgets/TimegrapherPlotWidget.hpp"

#include <QPainter>
#include <QPaintEvent>
#include <QPainterPath>

#include <algorithm>
#include <cmath>

namespace chronolab {

TimegrapherPlotWidget::TimegrapherPlotWidget(QWidget* parent)
    : QWidget(parent)
{
    setMinimumHeight(280);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
}

void TimegrapherPlotWidget::setAnalysis(const AnalysisResult& result)
{
    m_result = result;
    update();
}

void TimegrapherPlotWidget::clear()
{
    m_result = {};
    update();
}

void TimegrapherPlotWidget::paintEvent(QPaintEvent* event)
{
    Q_UNUSED(event);
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.fillRect(rect(), QColor(9, 15, 21));

    const QRectF plot = rect().adjusted(48, 28, -20, -24);
    painter.setPen(QPen(QColor(34, 48, 59), 1));
    for (int i = 0; i <= 10; ++i) {
        const qreal x = plot.left() + plot.width() * i / 10.0;
        painter.drawLine(QPointF(x, plot.top()), QPointF(x, plot.bottom()));
    }
    for (int i = 0; i <= 8; ++i) {
        const qreal y = plot.top() + plot.height() * i / 8.0;
        painter.drawLine(QPointF(plot.left(), y), QPointF(plot.right(), y));
    }

    painter.setPen(QColor(116, 133, 146));
    painter.drawText(QRectF(12, 5, width() - 24, 20),
                     Qt::AlignLeft | Qt::AlignVCenter,
                     tr("STRISCIA TEMPORALE · residuo del battito"));

    if (!m_result.valid || m_result.stripResidualMilliseconds.size() < 2) {
        painter.setPen(QColor(126, 142, 156));
        painter.drawText(plot, Qt::AlignCenter,
                         tr("Avvia l'ascolto o apri una registrazione WAV"));
        return;
    }

    double absolutePeak = 0.25;
    for (const double residual : m_result.stripResidualMilliseconds)
        absolutePeak = std::max(absolutePeak, std::abs(residual));
    const double horizontalRange = std::min(10.0, absolutePeak * 1.25);

    QPainterPath trace;
    QVector<QPointF> points;
    points.reserve(static_cast<qsizetype>(m_result.stripResidualMilliseconds.size()));
    for (std::size_t i = 0; i < m_result.stripResidualMilliseconds.size(); ++i) {
        const double normalizedX =
            std::clamp(m_result.stripResidualMilliseconds[i] / horizontalRange, -1.0, 1.0);
        const qreal x = plot.center().x() + normalizedX * plot.width() * 0.46;
        const qreal y = plot.top()
            + (m_result.stripResidualMilliseconds.size() == 1
                   ? 0.0
                   : static_cast<double>(i)
                       / (m_result.stripResidualMilliseconds.size() - 1))
                * plot.height();
        points.push_back({x, y});
        if (i == 0)
            trace.moveTo(x, y);
        else
            trace.lineTo(x, y);
    }

    painter.setPen(QPen(QColor(64, 221, 174, 90), 1));
    painter.drawPath(trace);
    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor(76, 238, 185));
    for (const QPointF& point : points)
        painter.drawEllipse(point, 2.4, 2.4);

    painter.setPen(QColor(116, 133, 146));
    painter.drawText(QRectF(plot.left(), plot.bottom() + 3, plot.width(), 18),
                     Qt::AlignCenter,
                     tr("scala automatica ±%1 ms").arg(horizontalRange, 0, 'f', 2));
}

} // namespace chronolab
