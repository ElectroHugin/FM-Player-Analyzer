#include "CommandPalette.h"

#include "core/Utils.h"

#include <QApplication>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QVBoxLayout>

namespace fm {

CommandPalette::CommandPalette(QWidget *parent)
    : QDialog(parent, Qt::Popup | Qt::FramelessWindowHint)
{
    setObjectName(QStringLiteral("commandPalette"));
    setFixedWidth(600);
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(8, 8, 8, 8);
    layout->setSpacing(6);

    m_input = new QLineEdit(this);
    m_input->setObjectName(QStringLiteral("paletteInput"));
    m_input->setPlaceholderText(tr("Spieler, Seite oder Aktion suchen …"));
    m_input->installEventFilter(this);
    layout->addWidget(m_input);

    m_list = new QListWidget(this);
    m_list->setObjectName(QStringLiteral("paletteList"));
    m_list->setIconSize(QSize(18, 18));
    m_list->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_list->setFocusPolicy(Qt::NoFocus);
    layout->addWidget(m_list);

    m_hint = new QLabel(tr("↑↓ auswählen   ·   Enter öffnen   ·   Esc schließen"), this);
    m_hint->setObjectName(QStringLiteral("kpiCaption"));
    layout->addWidget(m_hint);

    connect(m_input, &QLineEdit::textChanged, this, &CommandPalette::rebuild);
    connect(m_list, &QListWidget::itemClicked, this, [this] { runCurrent(); });
}

void CommandPalette::popup()
{
    m_input->clear();
    rebuild();
    if (QWidget *window = parentWidget() ? parentWidget()->window() : nullptr) {
        const QRect area = window->geometry();
        adjustSize();
        move(area.center().x() - width() / 2, area.top() + 90);
    }
    show();
    raise();
    activateWindow();
    m_input->setFocus();
}

void CommandPalette::rebuild()
{
    const QString query = foldForSearch(m_input->text().trimmed());
    m_list->clear();
    m_shown.clear();

    QList<Entry> matches;
    if (!query.isEmpty() && m_playerSearch)
        matches += m_playerSearch(query);
    for (const Entry &entry : std::as_const(m_entries)) {
        if (query.isEmpty() || entry.foldedKey.contains(query))
            matches.append(entry);
    }

    QString section;
    for (const Entry &entry : std::as_const(matches)) {
        if (entry.section != section) {
            section = entry.section;
            auto *header = new QListWidgetItem(section.toUpper(), m_list);
            header->setFlags(Qt::NoItemFlags);
            QFont font = header->font();
            font.setBold(true);
            font.setPointSizeF(font.pointSizeF() * 0.8);
            font.setLetterSpacing(QFont::AbsoluteSpacing, 0.8);
            header->setFont(font);
            m_shown.append(Entry{});
        }
        QString text = entry.title;
        if (!entry.detail.isEmpty())
            text += QStringLiteral("    ·    ") + entry.detail;
        new QListWidgetItem(entry.icon, text, m_list);
        m_shown.append(entry);
    }
    if (matches.isEmpty()) {
        auto *none = new QListWidgetItem(tr("Nichts gefunden"), m_list);
        none->setFlags(Qt::NoItemFlags);
        m_shown.append(Entry{});
    }
    moveSelection(0);

    // Grow with the results up to a cap, so few hits leave no empty box.
    int height = 2 * m_list->frameWidth() + 4;
    for (int row = 0; row < m_list->count(); ++row)
        height += m_list->sizeHintForRow(row) + 2 * m_list->spacing();
    m_list->setFixedHeight(qMin(height, 400));
    adjustSize();
}

void CommandPalette::moveSelection(int delta)
{
    // delta 0 selects the first runnable row (section headers are skipped).
    int row = m_list->currentRow();
    if (delta == 0) {
        row = -1;
        delta = 1;
    }
    for (int r = row + delta; r >= 0 && r < m_list->count(); r += delta) {
        if (m_shown.value(r).run) {
            m_list->setCurrentRow(r);
            m_list->scrollToItem(m_list->item(r));
            return;
        }
    }
}

void CommandPalette::runCurrent()
{
    const int row = m_list->currentRow();
    if (row < 0 || row >= m_shown.size() || !m_shown.at(row).run)
        return;
    const std::function<void()> run = m_shown.at(row).run;
    hide();
    run();
}

bool CommandPalette::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == m_input && event->type() == QEvent::KeyPress) {
        auto *key = static_cast<QKeyEvent *>(event);
        switch (key->key()) {
        case Qt::Key_Down:
            moveSelection(1);
            return true;
        case Qt::Key_Up:
            moveSelection(-1);
            return true;
        case Qt::Key_Return:
        case Qt::Key_Enter:
            runCurrent();
            return true;
        case Qt::Key_Escape:
            hide();
            return true;
        default:
            break;
        }
    }
    return QDialog::eventFilter(watched, event);
}

} // namespace fm
