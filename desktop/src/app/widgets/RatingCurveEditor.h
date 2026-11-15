#pragma once
#include "core/RatingCurve.h"
#include <QWidget>
#include <vector>

class QDoubleSpinBox;
class QComboBox;
class QLabel;
class QSpinBox;
class QPushButton;
namespace fm {
class CurveCanvas;
class RatingCurveEditor : public QWidget {
    Q_OBJECT
public:
    explicit RatingCurveEditor(QWidget *parent = nullptr);
    void setChartPalette(const QPalette &palette);
    void setCurve(const RatingCurve &curve);
    RatingCurve curve() const { return m_curve; }
    bool setContribution(int level, double contribution);
    void undo();
    void redo();
signals:
    void curveChanged();
private:
    void apply(const RatingCurve &curve);
    void refresh();
    void showInvalid();
    RatingCurve m_curve = RatingCurve::linear(1);
    RatingCurve m_original = m_curve;
    std::vector<RatingCurve> m_undo, m_redo;
    CurveCanvas *m_canvas;
    QDoubleSpinBox *m_weight;
    QComboBox *m_mode;
    QSpinBox *m_plateau;
    QLabel *m_status;
    QPushButton *m_undoButton;
    QPushButton *m_redoButton;
    std::array<QDoubleSpinBox *, 20> m_points{};
};
} // namespace fm
