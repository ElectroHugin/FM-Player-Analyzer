#include "PlayerActions.h"

#include "AppContext.h"
#include "widgets/CellStyleDelegate.h"
#include "widgets/PlayerTableModel.h"

#include <QActionGroup>
#include <QCoreApplication>
#include <QMenu>
#include <QMessageBox>
#include <QTableView>
#include <QTableWidget>

namespace fm {

namespace PlayerActions {

namespace {

// Translation context "PlayerActions" (see ImportRunner.cpp: a const char*
// helper hid the strings from lupdate).
struct ActionText {
    Q_DECLARE_TR_FUNCTIONS(PlayerActions)
};

// Persists a single mutated store player; rolls back and reports on failure.
template <typename Mutator>
void togglePlayerFlag(AppContext &context, QWidget *parent, const QString &uid,
                      Mutator &&mutate)
{
    const int row = context.store().rowByUid(uid);
    if (row < 0)
        return;
    Player &player = context.store().at(row);
    mutate(player);
    std::vector<Player> batch{player};
    if (!context.database().upsertPlayers(batch)) {
        mutate(player); // toggle back
        QMessageBox::critical(parent, ActionText::tr("Spieler"),
                              context.database().errorString());
        return;
    }
    context.notifySettingsChanged(); // dataChanged -> pages refresh
}

void toggleShortlist(AppContext &context, QWidget *parent, const QString &uid)
{
    const int row = context.store().rowByUid(uid);
    if (row < 0)
        return;
    Player &player = context.store().at(row);
    player.onShortlist = !player.onShortlist;
    QList<int> ids;
    for (const Player &p : context.store().players()) {
        if (p.onShortlist)
            ids << p.id;
    }
    if (!context.database().setShortlistIds(ids)) {
        player.onShortlist = !player.onShortlist;
        QMessageBox::critical(parent, ActionText::tr("Shortlist"),
                              context.database().errorString());
        return;
    }
    context.notifySettingsChanged();
}

// "Registrierung" submenu: quick edit of the hand-maintained home-grown /
// club-trained / U21 status, so the squad-registration assistant can be fed
// from any player list.
void addRegistrationMenu(AppContext &context, QWidget *parent, QMenu &menu,
                         const Player &player)
{
    using U21 = PlayerRegistration::U21;
    const QString uid = player.uid;
    const PlayerRegistration current = player.registration;
    const auto store = [&context, parent, uid](const PlayerRegistration &r) {
        if (!context.setPlayerRegistration(uid, r))
            QMessageBox::critical(parent, ActionText::tr("Registrierung"),
                                  context.database().errorString());
    };

    QMenu *sub = menu.addMenu(ActionText::tr("Registrierung"));
    sub->setToolTipsVisible(true);
    auto *homeGrown = sub->addAction(ActionText::tr("Home-Grown (im Verband ausgebildet)"));
    homeGrown->setCheckable(true);
    homeGrown->setChecked(Registration::isHomeGrown(player));
    QObject::connect(homeGrown, &QAction::triggered, parent, [current, store](bool on) {
        PlayerRegistration r = current;
        r.homeGrown = on;
        if (!on)
            r.clubTrained = false; // club-trained implies home-grown
        store(r);
    });
    auto *clubTrained = sub->addAction(ActionText::tr("Club-Grown (im Verein ausgebildet)"));
    clubTrained->setCheckable(true);
    clubTrained->setChecked(current.clubTrained);
    QObject::connect(clubTrained, &QAction::triggered, parent, [current, store](bool on) {
        PlayerRegistration r = current;
        r.clubTrained = on;
        store(r);
    });

    const Registration::Settings settings = context.registrationSettings();
    const Registration::YouthRule youthRule = Registration::youthRule(settings.league);
    const auto statusText = [](Registration::U21Status status) {
        return status == Registration::U21Status::Yes  ? ActionText::tr("ja")
               : status == Registration::U21Status::No ? ActionText::tr("nein")
                                                       : ActionText::tr("unklar");
    };
    // One group of three choices (automatic / yes / no) for an override field.
    const auto addOverride = [&](const QStringList &texts, U21 PlayerRegistration::*field) {
        sub->addSeparator();
        auto *group = new QActionGroup(sub);
        const U21 values[] = {U21::Auto, U21::Yes, U21::No};
        for (int i = 0; i < 3; ++i) {
            auto *action = sub->addAction(texts.at(i));
            action->setCheckable(true);
            action->setChecked(current.*field == values[i]);
            group->addAction(action);
            const U21 value = values[i];
            QObject::connect(action, &QAction::triggered, parent, [current, store, field, value] {
                PlayerRegistration r = current;
                r.*field = value;
                store(r);
            });
        }
    };

    // LaLiga's own youth rule: EU players aged 18 or younger on 1 January.
    if (youthRule == Registration::YouthRule::U18Eu) {
        Player autoPlayer = player;
        autoPlayer.registration.youth = U21::Auto;
        const QString autoText =
            statusText(Registration::u18EuStatus(autoPlayer, settings.gameDate));
        addOverride({ActionText::tr("U18 EU automatisch nach Alter (%1)").arg(autoText),
                     ActionText::tr("U18 EU: ja (braucht keinen Platz)"),
                     ActionText::tr("U18 EU: nein")},
                    &PlayerRegistration::youth);
    }
    // U21: where the league exempts U21 players, and for UEFA list B.
    if (youthRule != Registration::YouthRule::U18Eu || settings.uefa) {
        const QString autoText = statusText(Registration::u21StatusByAge(player.age));
        addOverride({ActionText::tr("U21 automatisch nach Alter (%1)").arg(autoText),
                     ActionText::tr("U21: ja"), ActionText::tr("U21: nein")},
                    &PlayerRegistration::u21);
    }

    // Only where the chosen league limits non-EU players.
    const int nonEuLimit = Registration::restrictsSquad(settings.league)
                               ? Registration::nonEuLimit(settings.league)
                               : 0;
    if (nonEuLimit > 0) {
        sub->addSeparator();
        auto *nonEu = sub->addAction(ActionText::tr("Nicht-EU-Spieler"));
        nonEu->setCheckable(true);
        nonEu->setChecked(current.nonEu);
        nonEu->setToolTip(ActionText::tr("Zählt laut Spiel als Nicht-EU-Spieler. %1 erlaubt "
                                         "höchstens %2 auf der Meldeliste.")
                              .arg(Registration::leagueDisplayName(settings.league))
                              .arg(nonEuLimit));
        QObject::connect(nonEu, &QAction::triggered, parent, [current, store](bool on) {
            PlayerRegistration r = current;
            r.nonEu = on;
            store(r);
        });
    }

    // Exceptions the assistant cannot know, e.g. signed after the deadline.
    sub->addSeparator();
    if (Registration::hasWageRules(settings.league)) {
        auto *wageExempt = sub->addAction(ActionText::tr("Mindestgehalt gilt nicht (z. B. Leihe)"));
        wageExempt->setCheckable(true);
        wageExempt->setChecked(current.minWageExempt);
        wageExempt->setToolTip(ActionText::tr(
            "Im Spiel registrierbar, obwohl das Gehalt unter dem Mindestgehalt liegt, z. B. bei "
            "einer Leihe mit Gehaltsanteil. Fürs Gehaltsbudget zählt weiter das Gehalt aus dem "
            "Export."));
        QObject::connect(wageExempt, &QAction::triggered, parent, [current, store](bool on) {
            PlayerRegistration r = current;
            r.minWageExempt = on;
            store(r);
        });
    }
    const auto addBlocked = [&](const QString &text, bool PlayerRegistration::*flag) {
        auto *action = sub->addAction(text);
        action->setCheckable(true);
        action->setChecked(current.*flag);
        QObject::connect(action, &QAction::triggered, parent, [current, store, flag](bool on) {
            PlayerRegistration r = current;
            r.*flag = on;
            store(r);
        });
    };
    if (Registration::restrictsSquad(settings.league))
        addBlocked(ActionText::tr("Nicht registrierbar: %1")
                       .arg(Registration::leagueDisplayName(settings.league)),
                   &PlayerRegistration::leagueBlocked);
    else // no squad list (e.g. Bundesliga): a player can still be left unregistered
        addBlocked(ActionText::tr("Nicht registriert: %1")
                       .arg(Registration::leagueDisplayName(settings.league).isEmpty()
                                ? ActionText::tr("Liga")
                                : Registration::leagueDisplayName(settings.league)),
                   &PlayerRegistration::leagueBlocked);
    if (settings.uefa)
        addBlocked(ActionText::tr("Nicht registrierbar: Europapokal"),
                   &PlayerRegistration::uefaBlocked);
}

} // namespace

void openProfile(AppContext &context, const QString &uid)
{
    if (uid.isEmpty())
        return;
    context.setPendingProfileUid(uid);
    context.requestNavigation(QStringLiteral("player_profile"));
}

void showContextMenu(AppContext &context, QWidget *parent, const QString &uid,
                     const QPoint &globalPos, const std::function<void(QMenu &)> &extra)
{
    const Player *player = context.store().findByUid(uid);
    if (!player)
        return;

    QMenu menu(parent);
    auto *title = menu.addAction(QStringLiteral("%1 · %2").arg(player->name, player->club));
    title->setEnabled(false);
    menu.addSeparator();
    if (extra) {
        extra(menu);
        menu.addSeparator();
    }

    menu.addAction(ActionText::tr("👤 Profil öffnen"), [&context, uid] {
        openProfile(context, uid);
    });
    menu.addAction(ActionText::tr("⚖️ Zum Vergleich hinzufügen"), [&context, uid] {
        context.addPendingComparisonUid(uid);
        context.requestNavigation(QStringLiteral("player_comparison"));
    });
    menu.addAction(ActionText::tr("✏️ Bearbeiten"), [&context, uid] {
        context.setPendingEditUid(uid);
        context.requestNavigation(QStringLiteral("edit_player"));
    });
    menu.addSeparator();

    auto *transferAction = menu.addAction(ActionText::tr("Zum Verkauf anbieten"));
    transferAction->setCheckable(true);
    transferAction->setChecked(player->transferStatus);
    QObject::connect(transferAction, &QAction::triggered, parent,
                     [&context, parent, uid] {
                         togglePlayerFlag(context, parent, uid, [](Player &p) {
                             p.transferStatus = !p.transferStatus;
                         });
                     });

    auto *loanAction = menu.addAction(ActionText::tr("Zum Verleih anbieten"));
    loanAction->setCheckable(true);
    loanAction->setChecked(player->loanStatus);
    QObject::connect(loanAction, &QAction::triggered, parent, [&context, parent, uid] {
        togglePlayerFlag(context, parent, uid,
                         [](Player &p) { p.loanStatus = !p.loanStatus; });
    });

    auto *shortlistAction = menu.addAction(ActionText::tr("★ Auf der Shortlist"));
    shortlistAction->setCheckable(true);
    shortlistAction->setChecked(player->onShortlist);
    QObject::connect(shortlistAction, &QAction::triggered, parent,
                     [&context, parent, uid] { toggleShortlist(context, parent, uid); });

    // Only meaningful for the own squads; a stale mark stays removable.
    const QString userClub = context.userClub();
    const QString secondClub = context.secondTeamClub();
    const bool ownPlayer = (!userClub.isEmpty() && player->club == userClub)
                           || (!secondClub.isEmpty() && player->club == secondClub);
    if (ownPlayer || player->longTermInjured) {
        auto *injuryAction = menu.addAction(ActionText::tr("🚑 Langzeitverletzt"));
        injuryAction->setCheckable(true);
        injuryAction->setChecked(player->longTermInjured);
        QObject::connect(injuryAction, &QAction::triggered, parent,
                         [&context, parent, uid, injured = !player->longTermInjured] {
                             if (!context.setLongTermInjured(uid, injured))
                                 QMessageBox::critical(parent, ActionText::tr("Spieler"),
                                                       context.database().errorString());
                         });
    }

    if (context.registrationSettings().active()) {
        addRegistrationMenu(context, parent, menu, *player);
    } else if (ownPlayer || player->registration.leagueBlocked) {
        // No squad list to keep (e.g. Bundesliga): a player can still be left
        // unregistered by mistake; Best XI then leaves him out.
        auto *unregistered = menu.addAction(ActionText::tr("Nicht registriert"));
        unregistered->setCheckable(true);
        unregistered->setChecked(player->registration.leagueBlocked);
        unregistered->setToolTip(ActionText::tr("Im Spiel nicht für die Liga registriert. Best XI "
                                                "und Übersicht stellen ihn nicht auf."));
        PlayerRegistration r = player->registration;
        r.leagueBlocked = !r.leagueBlocked;
        QObject::connect(unregistered, &QAction::triggered, parent, [&context, parent, uid, r] {
            if (!context.setPlayerRegistration(uid, r))
                QMessageBox::critical(parent, ActionText::tr("Registrierung"),
                                      context.database().errorString());
        });
        menu.setToolTipsVisible(true);
    }

    menu.exec(globalPos);
}

void attachToView(AppContext &context, QTableView *view, PlayerFilterProxy *proxy,
                  int ignoreDoubleClickColumn)
{
    CellStyleDelegate::install(view); // keep model DWRS/attribute colors visible
    view->setContextMenuPolicy(Qt::CustomContextMenu);
    QObject::connect(view, &QTableView::customContextMenuRequested, view,
                     [&context, view, proxy](const QPoint &pos) {
                         const QModelIndex index = view->indexAt(pos);
                         if (!index.isValid())
                             return;
                         if (const Player *player = proxy->playerAt(index.row())) {
                             showContextMenu(context, view, player->uid,
                                             view->viewport()->mapToGlobal(pos));
                         }
                     });
    QObject::connect(view, &QTableView::doubleClicked, view,
                     [&context, proxy, ignoreDoubleClickColumn](const QModelIndex &index) {
                         if (!index.isValid()
                             || index.column() == ignoreDoubleClickColumn)
                             return;
                         if (const Player *player = proxy->playerAt(index.row()))
                             openProfile(context, player->uid);
                     });
}

void attachToTableWidget(AppContext &context, QTableWidget *table, int uidColumn,
                         bool doubleClickOpensProfile, int ignoreDoubleClickColumn)
{
    CellStyleDelegate::install(table); // keep model DWRS/attribute colors visible
    const auto uidForCell = [table, uidColumn](int row, int column) {
        const QTableWidgetItem *item = table->item(row, uidColumn >= 0 ? uidColumn : column);
        return item ? item->data(Qt::UserRole).toString() : QString();
    };
    table->setContextMenuPolicy(Qt::CustomContextMenu);
    QObject::connect(table, &QTableWidget::customContextMenuRequested, table,
                     [&context, table, uidForCell](const QPoint &pos) {
                         const QModelIndex index = table->indexAt(pos);
                         if (!index.isValid())
                             return;
                         const QString uid = uidForCell(index.row(), index.column());
                         if (!uid.isEmpty()) {
                             showContextMenu(context, table, uid,
                                             table->viewport()->mapToGlobal(pos));
                         }
                     });
    if (doubleClickOpensProfile) {
        QObject::connect(table, &QTableWidget::cellDoubleClicked, table,
                         [&context, uidForCell, ignoreDoubleClickColumn](int row, int column) {
                             if (column == ignoreDoubleClickColumn)
                                 return;
                             const QString uid = uidForCell(row, column);
                             if (!uid.isEmpty())
                                 openProfile(context, uid);
                         });
    }
}

} // namespace PlayerActions

} // namespace fm
