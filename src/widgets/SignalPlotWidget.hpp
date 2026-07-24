#pragma once

#include <QColor>
#include <QVector>
#include <QWidget>

namespace chronolab {

class SignalPlotWidget final : public QWidget {
    Q_OBJECT

public:
    explicit SignalPlotWidget(QWidget* parent = nullptr);

    void setSamples(const QVector<float>& samples);
    void setAccentColor(const QColor& color);

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    QVector<float> m_samples;
    QColor m_accent {67, 230, 178};
};

} // namespace chronolab
