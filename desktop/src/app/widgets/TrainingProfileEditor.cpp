#include "TrainingProfileEditor.h"

#include "RatingProfileEditor.h"
#include "core/Attributes.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFile>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QJsonDocument>
#include <QLabel>
#include <QPushButton>
#include <QSpinBox>
#include <QTableWidget>
#include <QValidator>
#include <QVBoxLayout>

namespace fm {

using TrainingAdvice::TrainingBand;
using TrainingAdvice::TrainingProfile;
using TrainingAdvice::kLifelongAge;

namespace {
enum BandColumn { ColName, ColPeak, ColLocked, ColWeight, ColMentoring, BandColumns };

// The top age means "never locks" and reads as a word.
class AgeSpin : public QSpinBox
{
public:
    QString textFromValue(int value) const override
    {
        return value >= kLifelongAge ? TrainingProfileEditor::tr("immer") : QSpinBox::textFromValue(value);
    }
    int valueFromText(const QString &text) const override
    {
        return text == TrainingProfileEditor::tr("immer") ? kLifelongAge : QSpinBox::valueFromText(text);
    }
    QValidator::State validate(QString &text, int &pos) const override
    {
        if (TrainingProfileEditor::tr("immer").startsWith(text) && !text.isEmpty())
            return text == TrainingProfileEditor::tr("immer") ? QValidator::Acceptable
                                                              : QValidator::Intermediate;
        return QSpinBox::validate(text, pos);
    }
};

QSpinBox *ageSpin(int value, const QString &lifelongText)
{
    auto *spin = new AgeSpin;
    spin->setRange(14, kLifelongAge);
    spin->setValue(value);
    spin->setToolTip(lifelongText);
    return spin;
}
} // namespace

QString TrainingProfileEditor::bandLabel(const QString &storedName)
{
    if (storedName == QLatin1String("Explosive")) return tr("Explosiv");
    if (storedName == QLatin1String("Strength")) return tr("Kraft");
    if (storedName == QLatin1String("Technical")) return tr("Technik");
    if (storedName == QLatin1String("Mental")) return tr("Mental");
    if (storedName == QLatin1String("Character")) return tr("Charakter");
    return storedName;
}

QString TrainingProfileEditor::storedName(const QString &label) const
{
    for (const QString &key : {QStringLiteral("Explosive"), QStringLiteral("Strength"),
                               QStringLiteral("Technical"), QStringLiteral("Mental"),
                               QStringLiteral("Character")})
        if (bandLabel(key) == label)
            return key;
    return label.trimmed();
}

TrainingProfileEditor::TrainingProfileEditor(QWidget *parent)
    : QWidget(parent)
{
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(10);

    m_bands = new QTableWidget(0, BandColumns, this);
    m_bands->setHorizontalHeaderLabels({tr("Band"), tr("Voll trainierbar bis"), tr("Gesperrt ab"),
                                        tr("Gewicht"), tr("Nur Mentoring")});
    m_bands->verticalHeader()->hide();
    m_bands->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    m_bands->horizontalHeader()->setSectionResizeMode(ColName, QHeaderView::Stretch);
    m_bands->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_bands->setSelectionMode(QAbstractItemView::SingleSelection);
    m_bands->setMinimumHeight(200);
    layout->addWidget(m_bands);

    auto *bandButtons = new QHBoxLayout;
    auto *addBandButton = new QPushButton(tr("Band hinzufügen"), this);
    m_removeBand = new QPushButton(tr("Band entfernen"), this);
    bandButtons->addWidget(addBandButton);
    bandButtons->addWidget(m_removeBand);
    bandButtons->addStretch(1);
    layout->addLayout(bandButtons);
    connect(addBandButton, &QPushButton::clicked, this, &TrainingProfileEditor::addBand);
    connect(m_removeBand, &QPushButton::clicked, this, &TrainingProfileEditor::removeBand);
    connect(m_bands, &QTableWidget::itemChanged, this, [this](QTableWidgetItem *item) {
        if (item->column() == ColName)
            refreshAttributeChoices();
    });

    m_attributes = new QTableWidget(0, 2, this);
    m_attributes->setHorizontalHeaderLabels({tr("Attribut"), tr("Band")});
    m_attributes->verticalHeader()->hide();
    m_attributes->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    m_attributes->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_attributes->setMinimumHeight(360);
    layout->addWidget(m_attributes);

    auto *fileButtons = new QHBoxLayout;
    auto *importButton = new QPushButton(tr("Importieren …"), this);
    auto *exportButton = new QPushButton(tr("Exportieren …"), this);
    auto *standardButton = new QPushButton(tr("Standard wiederherstellen"), this);
    fileButtons->addWidget(importButton);
    fileButtons->addWidget(exportButton);
    fileButtons->addStretch(1);
    fileButtons->addWidget(standardButton);
    layout->addLayout(fileButtons);
    m_status = new QLabel(this);
    m_status->setObjectName(QStringLiteral("kpiCaption"));
    m_status->setWordWrap(true);
    m_status->setVisible(false);
    layout->addWidget(m_status);
    connect(importButton, &QPushButton::clicked, this, &TrainingProfileEditor::importProfile);
    connect(exportButton, &QPushButton::clicked, this, &TrainingProfileEditor::exportProfile);
    connect(standardButton, &QPushButton::clicked, this, [this] {
        fillBands(m_standard);
        fillAttributes(m_standard);
        m_status->setVisible(false);
    });
}

void TrainingProfileEditor::setProfile(const TrainingProfile &profile, const TrainingProfile &standard)
{
    m_standard = standard;
    fillBands(profile);
    fillAttributes(profile);
    m_status->setVisible(false);
}

void TrainingProfileEditor::fillBands(const TrainingProfile &profile)
{
    const QSignalBlocker blocker(m_bands);
    m_bands->setRowCount(0);
    const QString lifelong = tr("Ganz nach oben: immer trainierbar");
    for (const TrainingBand &band : profile.bands) {
        const int row = m_bands->rowCount();
        m_bands->insertRow(row);
        auto *nameItem = new QTableWidgetItem(bandLabel(band.name));
        nameItem->setTextAlignment(Qt::AlignLeft | Qt::AlignVCenter);
        m_bands->setItem(row, ColName, nameItem);
        m_bands->setCellWidget(row, ColPeak, ageSpin(band.peakAge, lifelong));
        m_bands->setCellWidget(row, ColLocked, ageSpin(band.lockedAge, lifelong));
        auto *weight = new QDoubleSpinBox;
        weight->setRange(0.0, 5.0);
        weight->setSingleStep(0.1);
        weight->setDecimals(1);
        weight->setValue(band.weight);
        m_bands->setCellWidget(row, ColWeight, weight);
        auto *holder = new QWidget;
        auto *holderLayout = new QHBoxLayout(holder);
        holderLayout->setContentsMargins(0, 0, 0, 0);
        holderLayout->setAlignment(Qt::AlignCenter);
        auto *mentoring = new QCheckBox(holder);
        mentoring->setChecked(band.mentoringOnly);
        holderLayout->addWidget(mentoring);
        m_bands->setCellWidget(row, ColMentoring, holder);
        // Mentoring-only bands ignore the age window.
        QWidget *peak = m_bands->cellWidget(row, ColPeak);
        QWidget *locked = m_bands->cellWidget(row, ColLocked);
        const auto sync = [peak, locked, weight](bool on) {
            peak->setEnabled(!on);
            locked->setEnabled(!on);
            weight->setEnabled(!on);
        };
        sync(band.mentoringOnly);
        connect(mentoring, &QCheckBox::toggled, this, sync);
    }
    m_removeBand->setEnabled(m_bands->rowCount() > 1);
}

void TrainingProfileEditor::fillAttributes(const TrainingProfile &profile)
{
    m_attributes->setRowCount(0);
    QList<int> order;
    for (int i = 0; i < int(attrNames().size()); ++i)
        order << i;
    std::sort(order.begin(), order.end(), [](int a, int b) {
        return RatingProfileEditor::attributeLabel(a).localeAwareCompare(
                   RatingProfileEditor::attributeLabel(b)) < 0;
    });
    for (int index : order) {
        const QString name = attrNames()[size_t(index)];
        const int row = m_attributes->rowCount();
        m_attributes->insertRow(row);
        auto *label = new QTableWidgetItem(RatingProfileEditor::attributeLabel(index));
        label->setTextAlignment(Qt::AlignLeft | Qt::AlignVCenter);
        label->setData(Qt::UserRole, name);
        m_attributes->setItem(row, 0, label);
        auto *combo = new QComboBox;
        combo->setProperty("band", profile.bandOf(name));
        m_attributes->setCellWidget(row, 1, combo);
    }
    refreshAttributeChoices();
}

void TrainingProfileEditor::refreshAttributeChoices()
{
    QStringList names;
    for (int row = 0; row < m_bands->rowCount(); ++row)
        names << m_bands->item(row, ColName)->text();
    for (int row = 0; row < m_attributes->rowCount(); ++row) {
        auto *combo = qobject_cast<QComboBox *>(m_attributes->cellWidget(row, 1));
        const int band = combo->count() > 0 ? combo->currentData().toInt() : combo->property("band").toInt();
        const QSignalBlocker blocker(combo);
        combo->clear();
        combo->addItem(tr("nicht trainieren"), -1);
        for (int i = 0; i < names.size(); ++i)
            combo->addItem(names.at(i), i);
        combo->setCurrentIndex(std::max(0, combo->findData(band)));
    }
}

void TrainingProfileEditor::addBand()
{
    TrainingProfile current = profile();
    TrainingBand band;
    band.name = tr("Neues Band %1").arg(current.bands.size() + 1);
    current.bands.append(band);
    fillBands(current);
    refreshAttributeChoices();
    m_bands->selectRow(m_bands->rowCount() - 1);
    m_bands->editItem(m_bands->item(m_bands->rowCount() - 1, ColName));
}

void TrainingProfileEditor::removeBand()
{
    const int removed = m_bands->currentRow();
    if (removed < 0 || m_bands->rowCount() <= 1)
        return;
    // Attributes of the removed band are no longer trained; later bands move up.
    for (int row = 0; row < m_attributes->rowCount(); ++row) {
        auto *combo = qobject_cast<QComboBox *>(m_attributes->cellWidget(row, 1));
        const int band = combo->currentData().toInt();
        const int shifted = band == removed ? -1 : band > removed ? band - 1 : band;
        const QSignalBlocker blocker(combo);
        combo->setCurrentIndex(std::max(0, combo->findData(shifted)));
    }
    m_bands->removeRow(removed);
    m_removeBand->setEnabled(m_bands->rowCount() > 1);
    refreshAttributeChoices();
}

TrainingProfile TrainingProfileEditor::profile() const
{
    TrainingProfile out;
    for (int row = 0; row < m_bands->rowCount(); ++row) {
        TrainingBand band;
        band.name = storedName(m_bands->item(row, ColName)->text());
        if (band.name.isEmpty())
            band.name = tr("Band %1").arg(row + 1);
        band.peakAge = qobject_cast<QSpinBox *>(m_bands->cellWidget(row, ColPeak))->value();
        band.lockedAge = std::max(band.peakAge,
                                  qobject_cast<QSpinBox *>(m_bands->cellWidget(row, ColLocked))->value());
        band.weight = qobject_cast<QDoubleSpinBox *>(m_bands->cellWidget(row, ColWeight))->value();
        band.mentoringOnly =
            m_bands->cellWidget(row, ColMentoring)->findChild<QCheckBox *>()->isChecked();
        out.bands.append(band);
    }
    // Attributes in the app's order, so an unchanged profile compares equal.
    QHash<QString, int> bandOf;
    for (int row = 0; row < m_attributes->rowCount(); ++row) {
        auto *combo = qobject_cast<QComboBox *>(m_attributes->cellWidget(row, 1));
        bandOf.insert(m_attributes->item(row, 0)->data(Qt::UserRole).toString(),
                      combo->currentData().toInt());
    }
    for (const QString &name : attrNames()) {
        const int band = bandOf.value(name, -1);
        if (band >= 0 && band < out.bands.size())
            out.bands[band].attributes << name;
    }
    return out;
}

void TrainingProfileEditor::importProfile()
{
    const QString path = QFileDialog::getOpenFileName(this, tr("Trainingsprofil importieren"), QString(),
                                                      tr("Trainingsprofil (*.json)"));
    if (path.isEmpty())
        return;
    QFile file(path);
    TrainingProfile imported;
    const bool ok = file.open(QIODevice::ReadOnly)
                    && TrainingProfile::fromJson(QJsonDocument::fromJson(file.readAll()).object(), &imported);
    m_status->setVisible(true);
    if (!ok) {
        m_status->setText(tr("Die Datei ist kein gültiges Trainingsprofil."));
        return;
    }
    fillBands(imported);
    fillAttributes(imported);
    m_status->setText(tr("Importiert. Mit „Speichern“ übernehmen."));
}

void TrainingProfileEditor::exportProfile()
{
    const QString path = QFileDialog::getSaveFileName(this, tr("Trainingsprofil exportieren"),
                                                      QStringLiteral("training-profile.json"),
                                                      tr("Trainingsprofil (*.json)"));
    if (path.isEmpty())
        return;
    QFile file(path);
    const bool ok = file.open(QIODevice::WriteOnly | QIODevice::Truncate)
                    && file.write(QJsonDocument(profile().toJson()).toJson()) > 0;
    m_status->setVisible(true);
    m_status->setText(ok ? tr("Exportiert.") : tr("Die Datei konnte nicht geschrieben werden."));
}

} // namespace fm
