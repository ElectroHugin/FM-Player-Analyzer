#include "RatingCurveEditor.h"
#include "FlowLayout.h"
#include <QComboBox>
#include <QCoreApplication>
#include <QDoubleSpinBox>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QVBoxLayout>
#include <algorithm>
#include <cmath>
#include <functional>
#include <limits>

namespace fm {
namespace {
class ContributionSpinBox : public QDoubleSpinBox {
protected:
    QString textFromValue(double value) const override {
        auto text = locale().toString(value, 'f', decimals());
        if (!isGroupSeparatorShown()) text.remove(locale().groupSeparator());
        if (decimals() > 0) {
            const auto zero = locale().zeroDigit();
            while (text.endsWith(zero)) text.chop(zero.size());
            if (text.endsWith(locale().decimalPoint())) text.chop(locale().decimalPoint().size());
        }
        return text;
    }
};
std::optional<RatingCurve> fromPoints(const std::array<double, 20> &points, const RatingCurve &previous)
{
    for (int i = 0; i < 20; ++i)
        if (!std::isfinite(points[i]) || (i && points[i] < points[i - 1])) return {};
    RatingCurve result = previous;
    result.baseline = points.front();
    result.weight = points.back() - points.front();
    if (result.weight > 0)
        for (int i = 0; i < 20; ++i) result.shape[i] = (points[i] - points.front()) / result.weight;
    return result.isValid() ? std::optional(result) : std::nullopt;
}
std::array<double, 20> pointsOf(const RatingCurve &curve)
{
    std::array<double, 20> result;
    for (int i = 0; i < 20; ++i) result[i] = *curve.contribution(i + 1);
    return result;
}
}

class CurveCanvas : public QWidget {
public:
    RatingCurve curve = RatingCurve::linear(1);
    std::function<void(const RatingCurve &)> changed;
    std::function<void()> invalid;
    int selected = 0;
    bool increments = false;
    explicit CurveCanvas(QWidget *parent) : QWidget(parent) {
        setFocusPolicy(Qt::StrongFocus);
        setMinimumSize(280, 240);
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
        setAccessibleName(QCoreApplication::translate("fm::RatingCurveEditor", "Beitragskurve"));
    }
protected:
    QRectF plot() const { return QRectF(48, 20, width() - 68, height() - 64); }
    double bottom() const { return increments ? 0 : std::min(0.0, curve.baseline); }
    double top() const { return std::max(bottom() + 1.0, increments ? curve.weight : curve.baseline + curve.weight); }
    QPointF point(int i) const {
        const auto area = plot();
        const double value = increments ? (i ? curve.weight * (curve.shape[i] - curve.shape[i-1]) : 0)
                                        : *curve.contribution(i + 1);
        return {area.left() + area.width() * i / 19, area.bottom() - (value - bottom()) / (top() - bottom()) * area.height()};
    }
    void paintEvent(QPaintEvent *) override {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        const auto area = plot();
        painter.fillRect(rect(), palette().base());
        painter.setPen(palette().mid().color());
        for (int i = 0; i <= 4; ++i) {
            painter.setPen(palette().mid().color());
            const double y = area.bottom() - i * area.height() / 4;
            painter.drawLine(QPointF(area.left(), y), QPointF(area.right(), y));
            painter.setPen(palette().text().color());
            painter.drawText(QRectF(0, y - 10, 42, 20), Qt::AlignRight | Qt::AlignVCenter,
                             locale().toString(bottom() + (top() - bottom()) * i / 4, 'g', 4));
        }
        QPainterPath path(point(0));
        for (int i = 1; i < 20; ++i) path.lineTo(point(i));
        painter.setPen(QPen(palette().highlight().color(), 2));
        painter.drawPath(path);
        for (int i = 0; i < 20; ++i) {
            painter.setBrush(i == selected && hasFocus() ? palette().text() : palette().highlight());
            painter.drawEllipse(point(i), i == selected ? 5 : 3, i == selected ? 5 : 3);
            if (i == 0 || i == 5 || i == 11 || i == 17 || i == 19) {
                painter.setPen(palette().text().color());
                painter.drawText(QRectF(point(i).x() - 14, area.bottom() + 8, 28, 24), Qt::AlignCenter, QString::number(i + 1));
                painter.setPen(QPen(palette().highlight().color(), 2));
            }
        }
        if (hasFocus()) {
            painter.setPen(QPen(palette().highlight().color(), 1, Qt::DashLine));
            painter.setBrush(Qt::NoBrush);
            painter.drawRect(rect().adjusted(2, 2, -3, -3));
        }
    }
    void mousePressEvent(QMouseEvent *event) override {
        if (event->button() != Qt::LeftButton || increments) return;
        setFocus();
        selected = std::clamp(int(std::lround((event->position().x() - plot().left()) / plot().width() * 19)), 0, 19);
        dragging = true;
        dragStart = curve;
        dragBottom = bottom(); dragTop = top();
        update();
    }
    void mouseMoveEvent(QMouseEvent *event) override {
        if (!dragging) return;
        auto points = pointsOf(curve);
        points[selected] = dragBottom + (plot().bottom() - event->position().y()) / plot().height() * (dragTop - dragBottom);
        const auto candidate = fromPoints(points, curve);
        if (candidate) { curve = *candidate; update(); }
        else if (invalid) invalid();
    }
    void mouseReleaseEvent(QMouseEvent *event) override {
        if (event->button() != Qt::LeftButton || !dragging) return;
        dragging = false;
        if (changed) changed(curve);
    }
    void keyPressEvent(QKeyEvent *event) override {
        if (event->key() == Qt::Key_Escape && dragging) { curve = dragStart; dragging = false; update(); return; }
        if (event->key() == Qt::Key_Left || event->key() == Qt::Key_Right) {
            selected = std::clamp(selected + (event->key() == Qt::Key_Left ? -1 : 1), 0, 19); update(); return;
        }
        if (!increments && (event->key() == Qt::Key_Up || event->key() == Qt::Key_Down)) {
            auto points = pointsOf(curve);
            points[selected] += (event->key() == Qt::Key_Up ? 1 : -1) * std::max(curve.weight / 100, 0.01);
            const auto candidate = fromPoints(points, curve);
            if (candidate && changed) changed(*candidate);
            else if (invalid) invalid();
            return;
        }
        QWidget::keyPressEvent(event);
    }
private:
    bool dragging = false;
    RatingCurve dragStart;
    double dragBottom = 0, dragTop = 1;
};

RatingCurveEditor::RatingCurveEditor(QWidget *parent) : QWidget(parent)
{
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0); layout->setSpacing(12);
    auto *toolbar = new QHBoxLayout;
    auto *weightLabel = new QLabel(tr("Gesamtgewicht"));
    m_weight = new ContributionSpinBox;
    m_weight->setFixedWidth(120); m_weight->setObjectName(QStringLiteral("curveWeight"));
    m_weight->setRange(0, std::numeric_limits<double>::max()); m_weight->setDecimals(2); m_weight->setKeyboardTracking(false);
    weightLabel->setBuddy(m_weight);
    toolbar->addWidget(weightLabel); toolbar->addWidget(m_weight);
    m_mode = new QComboBox;
    m_mode->addItems({tr("Gesamtbeitrag"), tr("Zuwachs je Attributpunkt")});
    m_mode->setAccessibleName(tr("Kurvendarstellung"));
    toolbar->addWidget(m_mode); toolbar->addStretch(); layout->addLayout(toolbar);
    auto *body = new QHBoxLayout; body->setSpacing(16);
    m_canvas = new CurveCanvas(this);
    m_canvas->setToolTip(tr("Links/Rechts wählt einen Punkt. Oben/Unten ändert seinen Beitrag."));
    body->addWidget(m_canvas, 3);
    auto *scroll = new QScrollArea;
    scroll->setWidgetResizable(true); scroll->setMinimumWidth(180); scroll->setMaximumWidth(240);
    scroll->setMinimumHeight(280);
    auto *numbers = new QWidget;
    auto *grid = new QGridLayout(numbers); grid->setSpacing(8);
    for (int i = 0; i < 20; ++i) {
        auto *label = new QLabel(QString::number(i + 1));
        auto *spin = new ContributionSpinBox;
        spin->setFixedWidth(120); spin->setObjectName(QStringLiteral("curvePoint%1").arg(i + 1));
        spin->setRange(-std::numeric_limits<double>::max(), std::numeric_limits<double>::max()); spin->setDecimals(2); spin->setKeyboardTracking(false);
        spin->setAccessibleName(tr("Beitrag bei Attributwert %1").arg(i + 1));
        label->setBuddy(spin); m_points[i] = spin;
        grid->addWidget(label, i, 0); grid->addWidget(spin, i, 1);
        connect(spin, &QDoubleSpinBox::valueChanged, this, [spin] { spin->setProperty("userEdited", true); });
        connect(spin, &QDoubleSpinBox::editingFinished, this, [this, i] {
            if (!m_points[i]->property("userEdited").toBool()) return;
            double value = m_points[i]->value();
            if (m_mode->currentIndex() == 1) {
                if (!i) { refresh(); return; }
                value += *m_curve.contribution(i);
            }
            setContribution(i + 1, value);
        });
    }
    scroll->setWidget(numbers); body->addWidget(scroll, 1); layout->addLayout(body, 1);
    auto *actions = new FlowLayout;
    auto *linear = new QPushButton(tr("Linear"));
    auto *plateau = new QPushButton(tr("Plateau ab"));
    m_plateau = new QSpinBox; m_plateau->setRange(2, 20); m_plateau->setValue(18);
    m_plateau->setAccessibleName(tr("Beginn des Plateaus"));
    auto *reset = new QPushButton(tr("Zurücksetzen"));
    m_undoButton = new QPushButton(tr("Rückgängig")); m_undoButton->setShortcut(QKeySequence::Undo);
    m_redoButton = new QPushButton(tr("Wiederholen")); m_redoButton->setShortcut(QKeySequence::Redo);
    for (auto *button : {linear, plateau}) actions->addWidget(button);
    actions->addWidget(m_plateau); actions->addWidget(reset);
    actions->addWidget(m_undoButton); actions->addWidget(m_redoButton); layout->addLayout(actions);
    m_status = new QLabel; m_status->setWordWrap(true); m_status->setObjectName(QStringLiteral("kpiCaption"));
    layout->addWidget(m_status);
    m_canvas->changed = [this](const RatingCurve &curve) { apply(curve); };
    m_canvas->invalid = [this] { showInvalid(); };
    connect(m_weight, &QDoubleSpinBox::valueChanged, this, [this] { m_weight->setProperty("userEdited", true); });
    connect(m_weight, &QDoubleSpinBox::editingFinished, this, [this] { if (!m_weight->property("userEdited").toBool()) return; auto c = m_curve; c.weight = m_weight->value(); apply(c); });
    connect(m_mode, &QComboBox::currentIndexChanged, this, [this] { refresh(); });
    connect(linear, &QPushButton::clicked, this, [this] { auto c = RatingCurve::linear(m_curve.weight); c.baseline = m_curve.baseline; apply(c); });
    connect(plateau, &QPushButton::clicked, this, [this] {
        auto c = m_curve;
        for (int i = 0; i < 20; ++i) c.shape[i] = std::min(1.0, double(i) / (m_plateau->value() - 1));
        apply(c);
    });
    connect(reset, &QPushButton::clicked, this, [this] { apply(m_original); });
    connect(m_undoButton, &QPushButton::clicked, this, &RatingCurveEditor::undo);
    connect(m_redoButton, &QPushButton::clicked, this, &RatingCurveEditor::redo);
    refresh();
}
void RatingCurveEditor::setChartPalette(const QPalette &palette) { m_canvas->setPalette(palette); m_canvas->update(); }
void RatingCurveEditor::setCurve(const RatingCurve &curve)
{
    if (!curve.isValid()) { showInvalid(); return; }
    m_curve = m_original = curve; m_undo.clear(); m_redo.clear(); refresh();
}
void RatingCurveEditor::apply(const RatingCurve &curve)
{
    if (!curve.isValid()) { refresh(); showInvalid(); return; }
    if (curve.weight == m_curve.weight && curve.shape == m_curve.shape && curve.baseline == m_curve.baseline) { refresh(); return; }
    m_undo.push_back(m_curve); m_redo.clear(); m_curve = curve; refresh(); emit curveChanged();
}
bool RatingCurveEditor::setContribution(int level, double value)
{
    if (level < 1 || level > 20) return false;
    auto points = pointsOf(m_curve); points[level - 1] = value;
    const auto candidate = fromPoints(points, m_curve);
    if (!candidate) { refresh(); showInvalid(); return false; }
    apply(*candidate); return true;
}
void RatingCurveEditor::undo() {
    if (m_undo.empty()) return;
    m_redo.push_back(m_curve); m_curve = m_undo.back(); m_undo.pop_back(); refresh(); emit curveChanged();
}
void RatingCurveEditor::redo() {
    if (m_redo.empty()) return;
    m_undo.push_back(m_curve); m_curve = m_redo.back(); m_redo.pop_back(); refresh(); emit curveChanged();
}
void RatingCurveEditor::showInvalid() { m_status->setText(tr("Beiträge müssen endlich sein und dürfen nicht fallen. Die Änderung wurde nicht übernommen.")); }
void RatingCurveEditor::refresh()
{
    const QSignalBlocker block(m_weight); m_weight->setValue(m_curve.weight); m_weight->setProperty("userEdited", false);
    const bool increments = m_mode->currentIndex() == 1;
    for (int i = 0; i < 20; ++i) {
        const QSignalBlocker pointBlock(m_points[i]);
        m_points[i]->setValue(increments ? (i ? m_curve.weight * (m_curve.shape[i] - m_curve.shape[i-1]) : 0) : *m_curve.contribution(i+1));
        m_points[i]->setProperty("userEdited", false);
        m_points[i]->setEnabled(!increments || i > 0);
    }
    m_canvas->curve = m_curve; m_canvas->increments = increments; m_canvas->update();
    m_undoButton->setEnabled(!m_undo.empty()); m_redoButton->setEnabled(!m_redo.empty());
    m_status->setText(tr("Gewicht bestimmt die gesamte Bedeutung. Die Kurve verteilt den Beitrag auf die Attributstufen."));
}
} // namespace fm
