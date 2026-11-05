#include "GraphicsLibrary.h"

#include <QDir>
#include <QFile>
#include <QImageReader>
#include <QPixmapCache>
#include <QtConcurrent>

namespace fm {

GraphicsLibrary::GraphicsLibrary(QObject *parent)
    : QObject(parent)
{
    m_watcher = new QFutureWatcher<std::shared_ptr<GraphicsIndex>>(this);
    connect(m_watcher, &QFutureWatcherBase::finished, this, [this] {
        std::shared_ptr<GraphicsIndex> result = m_watcher->result();
        if (m_pendingRestart) {
            m_pendingRestart = false;
            startIndexing(m_pendingForce);
            return;
        }
        if (m_state != State::Indexing)
            return; // turned off meanwhile
        if (result && result->root() == QDir(m_folder).absolutePath()) {
            m_index = std::move(result);
            m_state = State::Ready;
        } else {
            m_index.reset();
            m_state = State::Missing;
        }
        QPixmapCache::clear();
        emit stateChanged();
    });
}

GraphicsLibrary::~GraphicsLibrary()
{
    if (m_cancel)
        *m_cancel = true;
    m_watcher->waitForFinished();
}

GraphicsIndex::Stats GraphicsLibrary::stats() const
{
    return m_index ? m_index->stats() : GraphicsIndex::Stats{};
}

void GraphicsLibrary::configure(bool enabled, const QString &folder, const QString &cacheFile,
                                bool forceRescan)
{
    m_cacheFile = cacheFile;
    if (!enabled) {
        if (m_cancel)
            *m_cancel = true;
        const bool changed = m_state != State::Off;
        m_state = State::Off;
        m_index.reset();
        if (changed) {
            QPixmapCache::clear();
            emit stateChanged();
        }
        return;
    }
    const bool sameFolder = QDir(folder).absolutePath() == QDir(m_folder).absolutePath();
    m_folder = folder;
    if (!forceRescan && sameFolder && (m_state == State::Ready || m_state == State::Indexing))
        return;
    if (!QDir(folder).exists()) {
        m_index.reset();
        m_state = State::Missing;
        emit stateChanged();
        return;
    }
    startIndexing(forceRescan);
}

void GraphicsLibrary::startIndexing(bool forceRescan)
{
    m_state = State::Indexing;
    m_index.reset();
    emit stateChanged();
    if (m_watcher->isRunning()) {
        // One job at a time: cancel the running one and start again afterwards.
        if (m_cancel)
            *m_cancel = true;
        m_pendingRestart = true;
        m_pendingForce = m_pendingForce || forceRescan;
        return;
    }
    m_pendingForce = false;
    m_cancel = std::make_shared<bool>(false);
    const std::shared_ptr<bool> cancel = m_cancel;
    const QString folder = m_folder;
    const QString cacheFile = m_cacheFile;
    m_watcher->setFuture(QtConcurrent::run([folder, cacheFile, forceRescan, cancel] {
        auto index = std::make_shared<GraphicsIndex>();
        if (!forceRescan && !cacheFile.isEmpty() && index->loadCache(cacheFile, folder))
            return index;
        if (!index->scan(folder, cancel.get()))
            return std::shared_ptr<GraphicsIndex>();
        if (!cacheFile.isEmpty())
            index->saveCache(cacheFile);
        return index;
    }));
}

QPixmap GraphicsLibrary::load(GraphicsIndex::Kind kind, quint32 id, int size) const
{
    if (m_state != State::Ready || !m_index || id == 0 || size <= 0)
        return {};
    const QString key = QStringLiteral("fmgfx/%1/%2/%3").arg(int(kind)).arg(id).arg(size);
    QPixmap pixmap;
    if (QPixmapCache::find(key, &pixmap))
        return pixmap;
    const QString path = m_index->path(kind, id);
    if (path.isEmpty())
        return {};
    QImageReader reader(path);
    reader.setAutoTransform(true);
    const QSize original = reader.size();
    if (original.isValid())
        reader.setScaledSize(original.scaled(size, size, Qt::KeepAspectRatio));
    const QImage image = reader.read();
    if (image.isNull())
        return {};
    pixmap = QPixmap::fromImage(image);
    QPixmapCache::insert(key, pixmap);
    return pixmap;
}

QPixmap GraphicsLibrary::portrait(const QString &uid, int size) const
{
    return load(GraphicsIndex::Kind::Portrait, GraphicsIndex::parseId(uid), size);
}

QPixmap GraphicsLibrary::clubLogo(quint32 clubId, int size) const
{
    return load(GraphicsIndex::Kind::ClubLogo, clubId, size);
}

bool GraphicsLibrary::hasClubLogo(quint32 clubId) const
{
    return m_state == State::Ready && m_index
           && m_index->contains(GraphicsIndex::Kind::ClubLogo, clubId);
}

QByteArray GraphicsLibrary::clubLogoFile(quint32 clubId) const
{
    if (!hasClubLogo(clubId))
        return {};
    QFile file(m_index->path(GraphicsIndex::Kind::ClubLogo, clubId));
    if (!file.open(QIODevice::ReadOnly))
        return {};
    return file.readAll();
}

} // namespace fm
