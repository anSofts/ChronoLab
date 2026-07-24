#pragma once

#include "core/TimegrapherAnalyzer.hpp"

#include <QWidget>

namespace chronolab {

class TimegrapherPlotWidget final : public QWidget {
    Q_OBJECT

public:
    explicit TimegrapherPlotWidget(QWidget* parent = nullptr);

    void setAnalysis(const AnalysisResult& result);
    void clear();

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    AnalysisResult m_result;
};

} // namespace chronolab
