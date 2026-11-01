#pragma once

#include <QString>

class QWidget;

namespace fm {

class AppContext;
class ThemeManager;

// README screenshots (no user feature): with FMPA_SCREENSHOTS=<folder> set,
// the app walks through the main pages, saves one PNG per page and quits.
// Run it on a demo save with invented players only.
namespace ScreenshotTour {

bool requested();
void start(AppContext &context, ThemeManager &theme, QWidget *window);

} // namespace ScreenshotTour

} // namespace fm
