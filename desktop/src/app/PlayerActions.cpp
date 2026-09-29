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

    sub->addSeparator();
    // Show what "automatic" currently resolves to for this player.
    const Registration::U21Status autoStatus = Registration::u21StatusByAge(player.age);
    const QString autoText = autoStatus == Registration::U21Status::Yes
                                 ? ActionText::tr("ja")
                             : autoStatus == Registration::U21Status::No
                                 ? ActionText::tr("nein")
                                 : ActionText::tr("unklar");
    auto *u21Group = new QActionGroup(sub);
    const auto addU21 = [&](const QString &text, U21 value) {
        auto *action = sub->addAction(text);
        action->setCheckable(true);
        action->setChecked(current.u21 == value);
        u21Group->addAction(action);
        QObject::connect(action, &QAction::triggered, parent, [current, store, value] {
            PlayerRegistration r = current;
            r.u21 = value;
            store(r);
        });
    };
    addU21(ActionText::tr("U21 automatisch nach Alter (%1)").arg(autoText), U21::Auto);
    addU21(ActionText::tr("U21: ja"), U21::Yes);
    addU21(ActionText::tr("U21: nein"), U21::No);
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
                     const QPoint &globalPos)
{
    const Player *player = context.store().findByUid(uid);
    if (!player)
        return;

    QMenu menu(parent);
    auto *title = menu.addAction(QStringLiteral("%1 · %2").arg(player->name, player->club));
    title->setEnabled(false);
    menu.addSeparator();

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

    if (context.registrationSettings().active())
        addRegistrationMenu(context, parent, menu, *player);

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
                         bool doubleClickOpensProfile)
{
    CellStyleDelegate::install(table); // keep model DWRS/attribute colors visible
    const auto uidForRow = [table, uidColumn](int row) {
        const QTableWidgetItem *item = table->item(row, uidColumn);
        return item ? item->data(Qt::UserRole).toString() : QString();
    };
    table->setContextMenuPolicy(Qt::CustomContextMenu);
    QObject::connect(table, &QTableWidget::customContextMenuRequested, table,
                     [&context, table, uidForRow](const QPoint &pos) {
                         const QModelIndex index = table->indexAt(pos);
                         if (!index.isValid())
                             return;
                         const QString uid = uidForRow(index.row());
                         if (!uid.isEmpty()) {
                             showContextMenu(context, table, uid,
                                             table->viewport()->mapToGlobal(pos));
                         }
                     });
    if (doubleClickOpensProfile) {
        QObject::connect(table, &QTableWidget::cellDoubleClicked, table,
                         [&context, uidForRow](int row, int) {
                             const QString uid = uidForRow(row);
                             if (!uid.isEmpty())
                                 openProfile(context, uid);
                         });
    }
}

} // namespace PlayerActions

} // namespace fm
