#include "ScreenshotTour.h"

#include "AppContext.h"
#include "PlayerActions.h"
#include "theming/ThemeManager.h"

#include <QCoreApplication>
#include <QDir>
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
    auto steps = std::make_shared<QList<Step>>(QList<Step>{
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

    auto index = std::make_shared<int>(0);
    auto next = std::make_shared<std::function<void()>>();
    *next = [steps, index, next, window, dir] {
        if (*index >= steps->size()) {
            QCoreApplication::quit();
            return;
        }
        const Step step = steps->at(*index);
        step.prepare();
        QTimer::singleShot(2500, window, [steps, index, next, window, dir, step] {
            if (!step.file.isEmpty())
                window->grab().save(QDir(dir).filePath(step.file));
            ++*index;
            (*next)();
        });
    };
    QTimer::singleShot(1500, window, [next] { (*next)(); });
}

} // namespace fm::ScreenshotTour
