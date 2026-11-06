#include "ScreenshotTour.h"

#include "AppContext.h"
#include "PlayerActions.h"
#include "theming/ThemeManager.h"

#include <QCoreApplication>
#include <QDir>
#include <QTabWidget>
#include <QTimer>
#include <QWidget>

#include <algorithm>
#include <functional>
#include <memory>

namespace fm::ScreenshotTour {

namespace {

struct Step {
    std::function<void()> prepare;
    QString file; // empty: no picture for this step
};

} // namespace

bool requested()
{
    return qEnvironmentVariableIsSet("FMPA_SCREENSHOTS");
}

void start(AppContext &context, ThemeManager &theme, QWidget *window)
{
    const QString dir = qEnvironmentVariable("FMPA_SCREENSHOTS");
    QDir().mkpath(dir);
    window->resize(1600, 1180);

    // The two best-known players of the own club for profile and comparison.
    QStringList own;
    std::vector<const Player *> club;
    for (const Player &p : context.store().players()) {
        if (p.club == context.userClub())
            club.push_back(&p);
    }
    std::sort(club.begin(), club.end(),
              [](const Player *a, const Player *b) { return a->age > b->age; });
    QStringList centreBacks; // comparison: players sharing their roles
    for (const Player *p : club) {
        own << p->uid;
        if (p->positionRaw == QLatin1String("D (C)") && centreBacks.size() < 3)
            centreBacks << p->uid;
    }

    const auto page = [&context](const QString &id) {
        return [&context, id] { context.requestNavigation(id); };
    };
    // FMPA_SCREENSHOT_SET=ui: the views of the UI review (club header, colors)
    // in both modes, for checking club color profiles.
    const bool uiSet = qEnvironmentVariable("FMPA_SCREENSHOT_SET") == QLatin1String("ui");
    const auto mode = [&theme](const QString &m) { return [&theme, m] { theme.setMode(m); }; };
    auto uiSteps = QList<Step>{
        {mode(QStringLiteral("night")), {}},
        {page(QStringLiteral("dashboard")), QStringLiteral("night-dashboard.png")},
        {page(QStringLiteral("best_xi")), QStringLiteral("night-best-xi.png")},
        {page(QStringLiteral("scouting_matrix")), QStringLiteral("night-scouting.png")},
        {page(QStringLiteral("settings")), QStringLiteral("night-settings.png")},
        {mode(QStringLiteral("day")), {}},
        {page(QStringLiteral("dashboard")), QStringLiteral("day-dashboard.png")},
        {page(QStringLiteral("best_xi")), QStringLiteral("day-best-xi.png")},
        {page(QStringLiteral("scouting_matrix")), QStringLiteral("day-scouting.png")},
        {page(QStringLiteral("settings")), QStringLiteral("day-settings.png")},
    };
    // FMPA_SCREENSHOT_SET=history: profile header, history and the in-game
    // date on the dashboard and in the settings, on a tall window.
    const bool historySet = qEnvironmentVariable("FMPA_SCREENSHOT_SET") == QLatin1String("history");
    const QString profileUid = qEnvironmentVariable("FMPA_PROFILE_UID", own.value(0));
    const QString otherUid = qEnvironmentVariable("FMPA_PROFILE_UID2");
    const auto tall = [window](int height) { return [window, height] { window->resize(1600, height); }; };
    const auto profile = [&context](const QString &uid) {
        return [&context, uid] { PlayerActions::openProfile(context, uid); };
    };
    auto historySteps = QList<Step>{
        {mode(QStringLiteral("night")), {}},
        {tall(1100), {}},
        {page(QStringLiteral("dashboard")), QStringLiteral("dashboard.png")},
        {tall(2300), {}},
        {profile(profileUid), QStringLiteral("profile.png")},
        {profile(otherUid), QStringLiteral("profile-other.png")},
        {page(QStringLiteral("settings")), QStringLiteral("settings.png")},
        {mode(QStringLiteral("day")), {}},
        {profile(profileUid), QStringLiteral("profile-day.png")},
    };
    if (historySet)
        uiSteps = historySteps;
    auto steps = std::make_shared<QList<Step>>(uiSet || historySet ? uiSteps : QList<Step>{
        {[&theme] { theme.setMode(QStringLiteral("night")); }, {}},
        {page(QStringLiteral("dashboard")), QStringLiteral("dashboard.png")},
        {page(QStringLiteral("best_xi")), QStringLiteral("best-xi.png")},
        {page(QStringLiteral("role_analysis")), QStringLiteral("role-analysis.png")},
        {[&context, own] { PlayerActions::openProfile(context, own.value(own.size() / 2)); },
         QStringLiteral("profile.png")},
        {[&context, centreBacks] {
             for (const QString &uid : centreBacks)
                 context.addPendingComparisonUid(uid);
             context.requestNavigation(QStringLiteral("player_comparison"));
         },
         QStringLiteral("comparison.png")},
        {page(QStringLiteral("tactic_explorer")), QStringLiteral("tactic-explorer.png")},
        {page(QStringLiteral("training_plan")), QStringLiteral("training.png")},
        {page(QStringLiteral("registration")), QStringLiteral("registration.png")},
        {page(QStringLiteral("dwrs_development")), QStringLiteral("development.png")},
        {page(QStringLiteral("new_tactic")), QStringLiteral("tactic-editor.png")},
        {[&context, &theme] {
             theme.setMode(QStringLiteral("day"));
             context.requestNavigation(QStringLiteral("dashboard"));
         },
         QStringLiteral("dashboard-light.png")},
    });
    // Settings: open the design tab (or FMPA_SETTINGS_TAB, German tab name).
    const QByteArray settingsTab = qEnvironmentVariableIsSet("FMPA_SETTINGS_TAB")
                                       ? qgetenv("FMPA_SETTINGS_TAB")
                                       : QByteArray("Design");
    for (Step &step : *steps) {
        if (step.file.endsWith(QLatin1String("settings.png"))) {
            const auto open = step.prepare;
            step.prepare = [open, window, settingsTab] {
                open();
                for (QTabWidget *tabs : window->findChildren<QTabWidget *>()) {
                    for (int i = 0; i < tabs->count(); ++i) {
                        if (tabs->tabText(i) == QCoreApplication::translate("fm::SettingsPage",
                                                                            settingsTab.constData()))
                            tabs->setCurrentIndex(i);
                    }
                }
            };
        }
    }

    auto index = std::make_shared<int>(0);
    auto next = std::make_shared<std::function<void()>>();
    *next = [steps, index, next, window, dir, uiSet] {
        if (*index >= steps->size()) {
            QCoreApplication::quit();
            return;
        }
        const Step step = steps->at(*index);
        step.prepare();
        QTimer::singleShot(uiSet ? 1500 : 2500, window, [steps, index, next, window, dir, step] {
            if (!step.file.isEmpty())
                window->grab().save(QDir(dir).filePath(step.file));
            ++*index;
            (*next)();
        });
    };
    QTimer::singleShot(1500, window, [next] { (*next)(); });
}

} // namespace fm::ScreenshotTour
