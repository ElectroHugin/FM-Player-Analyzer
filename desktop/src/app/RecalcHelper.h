#pragma once

#include "core/Database.h"

#include <QString>
#include <QStringList>

#include <functional>

class QWidget;

namespace fm {

class AppContext;

// Recalculates DWRS for exactly the given players in a background thread with
// a modal progress dialog. The caller must have PERSISTED its player changes
// (and refreshed the in-memory store) beforehand — the worker snapshots the
// store. On success just these players' ratings are refreshed in the context
// (AppContext::refreshRatings; dataChanged() fires);
// onDone(errorMessage) runs afterwards with an empty string on success.
// origin: Import when the players carry freshly imported data (single-player
// update), Recalc for edits and role changes.
void recalcDwrsFor(AppContext &context, QWidget *parent, const QStringList &affectedUids,
                   std::function<void(QString)> onDone = {},
                   RatingsOrigin origin = RatingsOrigin::Recalc);

} // namespace fm
