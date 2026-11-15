#include "DwrsColorSchemeEditor.h"
#include "core/DwrsDisplay.h"

#include <QCheckBox>
#include <QColorDialog>
#include <QDoubleSpinBox>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPainter>
#include <QPushButton>
#include <QVBoxLayout>

namespace fm {

namespace {
// Two bars (outfield, goalkeeper) from 30 to 100 with a label every 10 points.
class SchemePreview : public QWidget {
public:
    explicit SchemePreview(const DwrsColorScheme *scheme, QWidget *parent = nullptr)
        : QWidget(parent), m_scheme(scheme)
    {
        setMinimumHeight(96);
        setMinimumWidth(320);
    }
protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter painter(this);
        const int labelWidth = 96;
        const QRect area = rect().adjusted(labelWidth, 4, -8, -20);
        const double from = 30.0, to = 100.0;
        const QStringList names = {DwrsColorSchemeEditor::tr("Feldspieler"),
                                   DwrsColorSchemeEditor::tr("Torhüter")};
        const int barHeight = (area.height() - 8) / 2;
        for (int row = 0; row < 2; ++row) {
            const QRect bar(area.left(), area.top() + row * (barHeight + 8), area.width(), barHeight);
            painter.setPen(palette().color(QPalette::WindowText));
            painter.drawText(QRect(0, bar.top(), labelWidth - 8, bar.height()),
                             Qt::AlignRight | Qt::AlignVCenter, names[row]);
            for (int x = 0; x < bar.width(); ++x) {
                const double value = from + (to - from) * x / std::max(1, bar.width() - 1);
                const CellStyle style = m_scheme->style(value, row == 1);
                painter.setPen(style.isValid() ? style.background : palette().color(QPalette::Base));
                painter.drawLine(bar.left() + x, bar.top(), bar.left() + x, bar.bottom());
            }
            painter.setPen(QPen(palette().color(QPalette::Mid), 1));
            painter.setBrush(Qt::NoBrush);
            painter.drawRect(bar.adjusted(0, 0, -1, -1));
        }
        painter.setPen(palette().color(QPalette::WindowText));
        for (int value = 30; value <= 100; value += 10) {
            const int x = area.left() + int((value - from) / (to - from) * (area.width() - 1));
            painter.drawText(QRect(x - 20, area.bottom() + 2, 40, 16), Qt::AlignCenter, QString::number(value));
        }
    }
private:
    const DwrsColorScheme *m_scheme;
};
} // namespace

DwrsColorSchemeEditor::DwrsColorSchemeEditor(QWidget *parent) : QWidget(parent)
{
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(10);
    m_rows = new QWidget;
    m_grid = new QGridLayout(m_rows);
    m_grid->setContentsMargins(0, 0, 0, 0);
    m_grid->setHorizontalSpacing(12);
    m_grid->setVerticalSpacing(6);
    layout->addWidget(m_rows);

    m_add = new QPushButton(tr("Stufe hinzufügen"));
    m_remove = new QPushButton(tr("Oberste Stufe entfernen"));
    auto *buttons = new QHBoxLayout;
    buttons->addWidget(m_add);
    buttons->addWidget(m_remove);
    buttons->addStretch(1);
    layout->addLayout(buttons);
    connect(m_add, &QPushButton::clicked, this, [this] {
        beginEdit();
        const DwrsColorStop last = m_scheme.stops.last();
        m_scheme.stops.append({std::min(100.0, last.field + 5.0), std::min(100.0, last.goalkeeper + 5.0),
                               last.color.isValid() ? last.color.darker(120) : QColor(0x2F, 0x7F, 0xD8)});
        finishEdit();
    });
    connect(m_remove, &QPushButton::clicked, this, [this] {
        beginEdit();
        m_scheme.stops.removeLast();
        finishEdit();
    });

    auto *blendRow = new QHBoxLayout;
    blendRow->addWidget(new QLabel(tr("Weicher Übergang")));
    m_blend = new QDoubleSpinBox;
    m_blend->setRange(0.0, DwrsColorScheme::kMaxBlend);
    m_blend->setDecimals(1);
    m_blend->setSingleStep(0.5);
    m_blend->setSuffix(tr(" Punkte"));
    m_blend->setSpecialValueText(tr("aus (harte Grenzen)"));
    m_blend->setMaximumWidth(200);
    blendRow->addWidget(m_blend);
    blendRow->addStretch(1);
    layout->addLayout(blendRow);
    connect(m_blend, &QDoubleSpinBox::valueChanged, this, [this](double value) {
        if (m_loading) return;
        beginEdit();
        m_scheme.blend = value;
        m_preview->update();
        emit edited();
    });

    m_preview = new SchemePreview(&m_scheme);
    layout->addWidget(m_preview);
}

void DwrsColorSchemeEditor::setScheme(const DwrsColorScheme &scheme)
{
    m_scheme = scheme;
    rebuild();
}

void DwrsColorSchemeEditor::beginEdit()
{
    if (m_scheme.rainbow)
        m_scheme = m_scheme.editableCopy(QString(), QString());
    m_scheme.id = QStringLiteral("custom");
    m_scheme.name = tr("Benutzerdefiniert");
    m_scheme.builtin = false;
}

void DwrsColorSchemeEditor::finishEdit()
{
    rebuild();
    emit edited();
}

void DwrsColorSchemeEditor::rebuild()
{
    m_loading = true;
    while (QLayoutItem *item = m_grid->takeAt(0)) {
        delete item->widget();
        delete item;
    }
    m_grid->addWidget(new QLabel(tr("Farbe")), 0, 1);
    m_grid->addWidget(new QLabel(tr("Feldspieler ab")), 0, 3);
    m_grid->addWidget(new QLabel(tr("Torhüter ab")), 0, 4);
    for (int i = 0; i < m_scheme.stops.size(); ++i) {
        const int row = i + 1;
        const DwrsColorStop stop = m_scheme.stops[i];
        m_grid->addWidget(new QLabel(tr("Stufe %1").arg(i + 1)), row, 0);

        auto *swatch = new QPushButton;
        swatch->setFixedSize(132, 30);
        swatch->setCursor(Qt::PointingHandCursor);
        swatch->setAccessibleName(tr("Farbe Stufe %1").arg(i + 1));
        if (stop.color.isValid()) {
            const QColor text = dwrsReadableStyle(stop.color).text;
            swatch->setText(stop.color.name().toUpper());
            swatch->setStyleSheet(QStringLiteral("QPushButton { background: %1; color: %2; border-radius: 6px; }")
                                      .arg(stop.color.name(), text.name()));
        } else {
            swatch->setText(tr("keine Füllung"));
        }
        connect(swatch, &QPushButton::clicked, this, [this, i] {
            const QColor initial = m_scheme.stops[i].color.isValid() ? m_scheme.stops[i].color : QColor(Qt::white);
            const QColor picked = QColorDialog::getColor(initial, this, tr("Farbe wählen"));
            if (!picked.isValid()) return;
            beginEdit();
            m_scheme.stops[i].color = picked;
            finishEdit();
        });
        m_grid->addWidget(swatch, row, 1);

        auto *noFill = new QCheckBox(tr("keine Füllung"));
        noFill->setChecked(!stop.color.isValid());
        connect(noFill, &QCheckBox::toggled, this, [this, i](bool on) {
            if (m_loading) return;
            beginEdit();
            m_scheme.stops[i].color = on ? QColor() : QColor(0x9A, 0xA0, 0xA6);
            finishEdit();
        });
        m_grid->addWidget(noFill, row, 2);

        for (const bool keeper : {false, true}) {
            auto *spin = new QDoubleSpinBox;
            spin->setRange(0.0, 100.0);
            spin->setDecimals(1);
            spin->setSingleStep(0.5);
            spin->setMaximumWidth(110);
            spin->setValue(keeper ? stop.goalkeeper : stop.field);
            spin->setEnabled(i > 0); // the first band starts at 0
            spin->setAccessibleName((keeper ? tr("Torhüter ab") : tr("Feldspieler ab"))
                                    + QLatin1Char(' ') + tr("Stufe %1").arg(i + 1));
            // Applied when editing ends, so typing "7" on the way to "72"
            // never reorders the bands.
            connect(spin, &QDoubleSpinBox::editingFinished, this, [this, i, keeper, spin] {
                if (m_loading) return;
                const double old = keeper ? m_scheme.stops[i].goalkeeper : m_scheme.stops[i].field;
                if (qFuzzyCompare(old + 1.0, spin->value() + 1.0)) return;
                beginEdit();
                (keeper ? m_scheme.stops[i].goalkeeper : m_scheme.stops[i].field) = spin->value();
                // Keep the bands ascending.
                for (int k = 1; k < m_scheme.stops.size(); ++k) {
                    double &here = keeper ? m_scheme.stops[k].goalkeeper : m_scheme.stops[k].field;
                    const double before = keeper ? m_scheme.stops[k - 1].goalkeeper : m_scheme.stops[k - 1].field;
                    if (k > 1 && here <= before) here = std::min(100.0, before + 0.5);
                }
                finishEdit();
            });
            m_grid->addWidget(spin, row, keeper ? 4 : 3);
        }
    }
    m_grid->setColumnStretch(5, 1);
    m_blend->setValue(m_scheme.blend);
    m_blend->setEnabled(true);
    m_add->setEnabled(m_scheme.stops.size() < DwrsColorScheme::kMaxStops && !m_scheme.stops.isEmpty());
    m_remove->setEnabled(m_scheme.stops.size() > 1);
    m_preview->update();
    m_loading = false;
}

} // namespace fm
