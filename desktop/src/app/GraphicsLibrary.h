#pragma once

#include "core/GraphicsIndex.h"

#include <QByteArray>
#include <QFutureWatcher>
#include <QObject>
#include <QPixmap>
#include <QString>

#include <memory>

namespace fm {

// Optional portraits and club logos from the FM graphics folder.
//
// Off unless the open save enables it (Settings → Graphics). While off,
// every lookup returns nothing and pages look exactly as before. When turned
// on, the folder is indexed on a worker thread (first time: a few seconds for
// a big face pack; afterwards from a cache file in the data folder), and
// ready() fires; pages then call portrait()/clubLogo().
class GraphicsLibrary : public QObject
{
    Q_OBJECT

public:
    enum class State { Off, Missing, Indexing, Ready };

    explicit GraphicsLibrary(QObject *parent = nullptr);
    ~GraphicsLibrary() override;

    // Applies the save's choice. Re-indexes only when the folder changed
    // (or forceRescan). cacheFile is where the index is kept between runs.
    void configure(bool enabled, const QString &folder, const QString &cacheFile,
                   bool forceRescan = false);

    State state() const { return m_state; }
    bool isReady() const { return m_state == State::Ready; }
    QString folder() const { return m_folder; }
    GraphicsIndex::Stats stats() const;

    // Scaled to fit size×size (keeps aspect ratio); null pixmap = none.
    // uid as stored for players ("12345", "r-12345").
    QPixmap portrait(const QString &uid, int size) const;
    QPixmap clubLogo(quint32 clubId, int size) const;
    bool hasClubLogo(quint32 clubId) const;
    // Raw file bytes of a club logo (to store it in the profile); empty = none.
    QByteArray clubLogoFile(quint32 clubId) const;

signals:
    // State changed (indexing started/finished, turned off, folder missing).
    void stateChanged();

private:
    QPixmap load(GraphicsIndex::Kind kind, quint32 id, int size) const;
    void startIndexing(bool forceRescan);

    State m_state = State::Off;
    QString m_folder;
    QString m_cacheFile;
    std::shared_ptr<GraphicsIndex> m_index;
    QFutureWatcher<std::shared_ptr<GraphicsIndex>> *m_watcher = nullptr;
    bool m_pendingRestart = false;
    bool m_pendingForce = false;
    std::shared_ptr<bool> m_cancel;
};

} // namespace fm
