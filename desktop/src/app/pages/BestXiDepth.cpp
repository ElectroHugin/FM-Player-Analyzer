// Best XI page, part 2 (2.1.0): bridge search from a place of the pitch and
// the squad depth per missing starter. Kept apart from BestXiPage.cpp, which
// holds the layout and the team calculation.
#include "BestXiPage.h"
#include "PageHelpers.h"

#include "../AppContext.h"
#include "../PlayerActions.h"
#include "../widgets/NumericTableItem.h"
#include "../widgets/TacticPitchWidget.h"
#include "core/ScoutSearch.h"
#include "core/SquadBuilder.h"
#include "core/Utils.h"

#include <QGroupBox>
#include <QHeaderView>
#include <QLabel>
#include <QLocale>
#include <QMenu>
#include <QTableWidget>

namespace fm {

void BestXiPage::clearPitchTeams()
{
    m_pitchTeams.clear();
    m_pitchTactic.clear();
    m_valueOfUid.clear();
}

void BestXiPage::addScoutActions(QMenu &menu, TacticPitchWidget *pitch, const QString &slot)
{
    const auto teamIt = m_pitchTeams.constFind(pitch);
    if (teamIt == m_pitchTeams.cend() || !teamIt->lineUp.contains(slot))
        return;
    const PitchTeam team = *teamIt;
    const QString tactic = m_pitchTactic;
    const QString version = m_context.fmVersionId();
    const Definitions &definitions = m_context.definitions();
    const auto placeText = [&](const QString &s, const QString &role) {
        return QStringLiteral("%1 · %2").arg(s, definitions.roleLabel(role));
    };
    const auto launch = [this](const ScoutSearchRequest &request) {
        m_context.setPendingScoutSearch(request);
        m_context.requestNavigation(QStringLiteral("scouting_matrix"));
    };
    const XiCell cell = team.cells.value(slot);
    const QString role = team.lineUp.value(slot);

    // One line-up (FM24): position and role of the place.
    if (!team.partner) {
        ScoutSearchRequest request;
        request.tactic = tactic;
        request.criteria = {ScoutSearch::criterion(ScoutSearch::slotPlace(version, slot, role))};
        if (cell.isFilled()) {
            request.source = tr("Aus Best XI: Ersatz für %1 (%2).")
                                 .arg(cell.name, placeText(slot, role));
            request.referenceName = getLastName(cell.name);
            request.referenceValue = m_valueOfUid.value(cell.playerUid, cell.rating);
        } else {
            request.source = tr("Aus Best XI: freier Platz %1.").arg(placeText(slot, role));
        }
        menu.addAction(tr("🔍 Im Scouting suchen (%1)").arg(placeText(slot, role)), this,
                       [launch, request] { launch(request); });
        return;
    }

    const PitchTeam other = m_pitchTeams.value(team.partner);
    // (ip slot, oop slot) from this place and one of the other line-up.
    const auto bridgeRequest = [&](const QString &otherSlot) {
        const QString ipSlot = team.ip ? slot : otherSlot;
        const QString oopSlot = team.ip ? otherSlot : slot;
        const QString ipRole = (team.ip ? team : other).lineUp.value(ipSlot);
        const QString oopRole = (team.ip ? other : team).lineUp.value(oopSlot);
        ScoutSearchRequest request;
        request.tactic = tactic;
        request.criteria = {ScoutSearch::bridge(version, ipSlot, ipRole, oopSlot, oopRole)};
        return std::make_tuple(request, placeText(ipSlot, ipRole), placeText(oopSlot, oopRole));
    };

    if (cell.isFilled()) {
        // His bridge: this place and his place in the other line-up.
        QString otherSlot;
        for (auto it = other.cells.cbegin(); it != other.cells.cend(); ++it) {
            if (it->playerUid == cell.playerUid)
                otherSlot = it.key();
        }
        if (otherSlot.isEmpty())
            return;
        auto [request, ipText, oopText] = bridgeRequest(otherSlot);
        request.source = tr("Aus Best XI: Bridge von %1 (mit Ball %2, ohne Ball %3).")
                             .arg(cell.name, ipText, oopText);
        request.referenceName = getLastName(cell.name);
        request.referenceValue = m_valueOfUid.value(cell.playerUid, cell.rating);
        menu.addAction(tr("🔍 Bridge im Scouting suchen (mit Ball %1, ohne Ball %2)")
                           .arg(ipText, oopText),
                       this, [launch, request] { launch(request); });
        return;
    }

    // Empty place: pair it with a free place of the other line-up. One free
    // place gives the bridge directly, several are offered to choose from;
    // this phase alone is always possible.
    QStringList freeSlots;
    for (const QString &s : other.order) {
        if (other.lineUp.contains(s) && !other.cells.value(s).isFilled())
            freeSlots << s;
    }
    const auto freeBridge = [&](const QString &otherSlot) {
        auto [request, ipText, oopText] = bridgeRequest(otherSlot);
        request.source = tr("Aus Best XI: freie Plätze mit Ball %1 und ohne Ball %2.")
                             .arg(ipText, oopText);
        return std::make_tuple(request, ipText, oopText);
    };
    if (freeSlots.size() == 1) {
        auto [request, ipText, oopText] = freeBridge(freeSlots.front());
        menu.addAction(tr("🔍 Bridge im Scouting suchen (mit Ball %1, ohne Ball %2)")
                           .arg(ipText, oopText),
                       this, [launch, request] { launch(request); });
    } else if (freeSlots.size() > 1) {
        QMenu *sub = menu.addMenu(team.ip ? tr("🔍 Bridge im Scouting suchen – Platz ohne Ball wählen")
                                          : tr("🔍 Bridge im Scouting suchen – Platz mit Ball wählen"));
        for (const QString &s : std::as_const(freeSlots)) {
            auto [request, ipText, oopText] = freeBridge(s);
            sub->addAction(placeText(s, other.lineUp.value(s)), this,
                           [launch, request] { launch(request); });
        }
    }
    ScoutSearchRequest single;
    single.tactic = tactic;
    single.criteria = {ScoutSearch::criterion(ScoutSearch::slotPlace(version, slot, role))};
    single.source = team.ip ? tr("Aus Best XI: freier Platz mit Ball %1.").arg(placeText(slot, role))
                            : tr("Aus Best XI: freier Platz ohne Ball %1.").arg(placeText(slot, role));
    menu.addAction(team.ip ? tr("🔍 Nur mit Ball suchen (%1)").arg(placeText(slot, role))
                           : tr("🔍 Nur ohne Ball suchen (%1)").arg(placeText(slot, role)),
                   this, [launch, single] { launch(single); });
}

void BestXiPage::showEmptySlotMenu(TacticPitchWidget *pitch, const QString &slot,
                                   const QPoint &globalPos)
{
    QMenu menu(this);
    auto *title = menu.addAction(tr("Freier Platz %1").arg(slot));
    title->setEnabled(false);
    menu.addSeparator();
    addScoutActions(menu, pitch, slot);
    if (menu.actions().size() > 2)
        menu.exec(globalPos);
}

void BestXiPage::fillOutageTable(const QList<OutageCover> &covers, bool twoPhase)
{
    m_outageBox->setVisible(!covers.isEmpty());
    if (covers.isEmpty()) {
        m_outageTable->setRowCount(0);
        return;
    }
    m_outageNote->setText(
        twoPhase ? tr("Fällt ein Spieler der Startelf aus, kommt genau ein Spieler dazu. Die "
                      "übrigen dürfen dafür ihre Plätze mit und ohne Ball tauschen. „Volle "
                      "Bridge“ heißt: Der Ersatz übernimmt beide Plätze des Ausfalls. Von Hand "
                      "gesetzte Plätze bleiben. Weitere Lösungen zeigt der Tooltip.")
                 : tr("Fällt ein Spieler der Startelf aus, kommt genau ein Spieler dazu. Die "
                      "übrigen dürfen dafür ihre Plätze tauschen. Weitere Lösungen zeigt der "
                      "Tooltip."));

    const auto nameOf = [this](const QString &uid) {
        const Player *p = m_context.store().findByUid(uid);
        return p ? p->name : uid;
    };
    const auto places = [twoPhase](const QPair<QString, QString> &pair) {
        return twoPhase ? tr("mit %1 · ohne %2").arg(pair.first, pair.second) : pair.first;
    };
    const auto movesText = [&](const ReplacementPlan &plan) {
        QStringList parts;
        for (const PlaceMove &move : plan.moves) {
            QStringList changes;
            if (move.from.first != move.to.first)
                changes << (twoPhase ? tr("mit %1 → %2").arg(move.from.first, move.to.first)
                                     : QStringLiteral("%1 → %2").arg(move.from.first, move.to.first));
            if (twoPhase && move.from.second != move.to.second)
                changes << tr("ohne %1 → %2").arg(move.from.second, move.to.second);
            parts << QStringLiteral("%1: %2").arg(getLastName(nameOf(move.uid)),
                                                  changes.join(QStringLiteral(", ")));
        }
        return parts.isEmpty() ? tr("keine") : parts.join(QStringLiteral("; "));
    };
    const auto delta = [](double value) {
        const QString text = QLocale().toString(std::abs(value), 'f', 1);
        return value <= -0.05 ? QStringLiteral("−") + text
               : value >= 0.05 ? QStringLiteral("+") + text
                               : QStringLiteral("±0");
    };

    m_outageTable->setSortingEnabled(false);
    m_outageTable->clearContents();
    m_outageTable->setRowCount(static_cast<int>(covers.size()));
    int row = 0;
    for (const OutageCover &cover : covers) {
        const ReplacementPlan &plan = cover.plans.front();
        int column = 0;
        auto *outItem = new NameItem(nameOf(cover.outUid));
        outItem->setData(Qt::UserRole, cover.outUid);
        m_outageTable->setItem(row, column++, outItem);
        m_outageTable->setItem(row, column++, new QTableWidgetItem(places(cover.outSlots)));

        auto *inItem = new NameItem(plan.inUid.isEmpty() ? tr("— niemand passt —")
                                                         : nameOf(plan.inUid));
        inItem->setData(Qt::UserRole, plan.inUid);
        // The other solutions, best first.
        QStringList alternatives;
        for (qsizetype i = 1; i < cover.plans.size(); ++i) {
            const ReplacementPlan &alt = cover.plans.at(i);
            alternatives << tr("%1: %2, Umstellungen: %3, Elf %4")
                                .arg(nameOf(alt.inUid), places(alt.inSlots), movesText(alt),
                                     delta(alt.total - cover.current));
        }
        if (!alternatives.isEmpty())
            inItem->setToolTip(tr("Weitere Lösungen:") + QLatin1Char('\n')
                               + alternatives.join(QLatin1Char('\n')));
        m_outageTable->setItem(row, column++, inItem);

        QString covers;
        if (plan.inUid.isEmpty())
            covers = tr("Platz bleibt frei");
        else if (twoPhase && plan.inSlots == cover.outSlots)
            covers = tr("%1 (volle Bridge)").arg(places(plan.inSlots));
        else
            covers = places(plan.inSlots);
        m_outageTable->setItem(row, column++, new QTableWidgetItem(covers));

        auto *valueItem = new NumericItem(
            plan.inUid.isEmpty() ? QStringLiteral("–") : QString::number(qRound(plan.inValue)),
            plan.inValue);
        valueItem->setTextAlignment(Qt::AlignCenter);
        const CellStyle style = dwrsCellStyle(plan.inValue);
        if (!plan.inUid.isEmpty() && style.isValid()) {
            valueItem->setBackground(style.background);
            valueItem->setForeground(style.text);
        }
        m_outageTable->setItem(row, column++, valueItem);

        const double change = plan.total - cover.current;
        auto *changeItem = new NumericItem(delta(change), change);
        changeItem->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
        m_outageTable->setItem(row, column++, changeItem);
        m_outageTable->setItem(row, column++, new QTableWidgetItem(movesText(plan)));
        ++row;
    }
    m_outageTable->setSortingEnabled(true);
    // Long move lists wrap instead of widening the table.
    fitTableToContents(m_outageTable, false,
                       m_outageTable->fontMetrics().averageCharWidth() * 60);
}

} // namespace fm
