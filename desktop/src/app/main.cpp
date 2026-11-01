#include <QApplication>
#include <QFont>
#include <QGuiApplication>
#include <QIcon>
#include <QMessageBox>
#include <QTimer>
#include <QTranslator>

#include "AppContext.h"
#include "FirstRunDialog.h"
#include "MainWindow.h"
#include "ScreenshotTour.h"
#include "StartScreen.h"
#include "theming/ThemeManager.h"
#include "core/Version.h"

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    QApplication::setApplicationName(fm::appName());
    QApplication::setApplicationVersion(fm::appVersion());
    // Stable internal identifier — do NOT rename, or existing users lose their
    // data folder (%LOCALAPPDATA%\FM24PlayerAnalyzer). The visible app name
    // lives in Version::appName().
    QApplication::setOrganizationName(QStringLiteral("FM24PlayerAnalyzer"));
    QApplication::setWindowIcon(QIcon(QStringLiteral(":/app/favicon.ico")));

    QFont appFont(QStringLiteral("Segoe UI"), 10);
    appFont.setHintingPreference(QFont::PreferFullHinting);
    app.setFont(appFont);

    fm::AppContext context;

    // Language must be installed before any widgets are built (including the
    // first-run dialog and the statically initialized navigation labels).
    // Default English via the bundled .qm; "de" keeps the German source text.
    QTranslator appTranslator;
    if (context.paths().language() != QLatin1String("de")
        && appTranslator.load(QStringLiteral(":/i18n/fmplayeranalyzer_en.qm"))) {
        QApplication::installTranslator(&appTranslator);
    }

    if (context.paths().isFirstRun()) {
        fm::FirstRunDialog dialog(fm::AppPaths::defaultDataDir());
        if (dialog.exec() != QDialog::Accepted)
            return 0;
        context.paths().setDataDir(dialog.chosenDataDir());
    }

    // --smoke (build verification) must never block on a dialog: it opens
    // the last save directly, as the app did before the start screen.
    const bool smoke = app.arguments().contains(QStringLiteral("--smoke"));

    QString error;
    if (!context.initialize(&error, /*openActiveDatabase*/ smoke)) {
        QMessageBox::critical(nullptr, fm::appName(),
                              QObject::tr("Start fehlgeschlagen:\n%1").arg(error));
        return 1;
    }

    fm::ThemeManager theme(context.config());
    theme.apply();

    if (!smoke) {
        // "Always open the last save" skips the start screen; holding Shift
        // at launch shows it anyway (e.g. when that save no longer opens).
        const bool forceStartScreen =
            QGuiApplication::queryKeyboardModifiers().testFlag(Qt::ShiftModifier);
        const QString last = context.currentDbName();
        bool opened = false;
        if (context.config().autoOpenLastDatabase() && !forceStartScreen
            && context.availableDatabases().contains(last)) {
            opened = context.openDatabase(last, &error);
            if (!opened) {
                QMessageBox::warning(
                    nullptr, fm::appName(),
                    QObject::tr("Der zuletzt verwendete Spielstand „%1“ konnte nicht geöffnet "
                                "werden:\n%2")
                        .arg(last, error));
            }
        }
        if (!opened) {
            fm::StartScreen start(context, theme, fm::StartScreen::Mode::Startup);
            if (start.exec() != QDialog::Accepted || !context.hasDatabase())
                return 0;
        }
    }

    fm::MainWindow window(context, theme);
    window.show();
    if (fm::ScreenshotTour::requested())
        fm::ScreenshotTour::start(context, theme, &window);

    // --smoke: exit immediately after the event loop starts (build verification)
    if (smoke) {
        QTimer::singleShot(0, &app, &QCoreApplication::quit);
    }

    return app.exec();
}
