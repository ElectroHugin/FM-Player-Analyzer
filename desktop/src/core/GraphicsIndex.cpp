#include "GraphicsIndex.h"

#include <QDataStream>
#include <QDateTime>
#include <QDir>
#include <QDirIterator>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>

#include <algorithm>

namespace fm {

namespace {

constexpr quint32 kCacheMagic = 0x464d4758; // "FMGX"
constexpr quint32 kCacheVersion = 1;

const QString kConfigName = QStringLiteral("config.xml");

// Reads the value of attribute name="..." starting at pos inside one record.
QByteArray attribute(const QByteArray &record, const char *name)
{
    const QByteArray key = QByteArray(name) + "=\"";
    const int start = record.indexOf(key);
    if (start < 0)
        return {};
    const int valueStart = start + key.size();
    const int end = record.indexOf('"', valueStart);
    if (end < 0)
        return {};
    return record.mid(valueStart, end - valueStart);
}

bool parseUInt(const QByteArray &text, quint32 &out)
{
    if (text.isEmpty() || text.size() > 10)
        return false;
    quint64 value = 0;
    for (char c : text) {
        if (c < '0' || c > '9')
            return false;
        value = value * 10 + quint64(c - '0');
    }
    if (value == 0 || value > 0xffffffffull)
        return false;
    out = quint32(value);
    return true;
}

qint64 mtimeOf(const QFileInfo &info)
{
    return info.lastModified().toMSecsSinceEpoch();
}

} // namespace

quint32 GraphicsIndex::parseId(const QString &uid)
{
    QString text = uid.trimmed();
    if (text.startsWith(QLatin1String("r-"), Qt::CaseInsensitive))
        text = text.mid(2);
    bool ok = false;
    const qulonglong value = text.toULongLong(&ok);
    if (!ok || value == 0 || value > 0xffffffffull)
        return 0;
    return quint32(value);
}

std::vector<GraphicsIndex::Record> GraphicsIndex::parseConfig(const QByteArray &xml)
{
    std::vector<Record> records;
    int pos = 0;
    while (true) {
        const int start = xml.indexOf("<record", pos);
        if (start < 0)
            break;
        const int end = xml.indexOf('>', start);
        if (end < 0)
            break;
        pos = end + 1;
        const QByteArray record = xml.mid(start, end - start);
        const QByteArray from = attribute(record, "from");
        const QByteArray to = attribute(record, "to");
        if (from.isEmpty() || !to.startsWith("graphics/pictures/"))
            continue;
        // graphics/pictures/<kind>/<id>/<what>
        const QList<QByteArray> parts = to.mid(18).split('/');
        if (parts.size() != 3)
            continue;
        Kind kind;
        if (parts[0] == "person" && parts[2] == "portrait")
            kind = Kind::Portrait;
        else if (parts[0] == "club" && parts[2] == "logo")
            kind = Kind::ClubLogo;
        else if (parts[0] == "nation" && parts[2] == "logo")
            kind = Kind::NationLogo;
        else
            continue;
        quint32 id = 0;
        if (!parseUInt(parts[1], id))
            continue;
        records.push_back({kind, id, QString::fromUtf8(from)});
    }
    return records;
}

void GraphicsIndex::clear()
{
    m_root.clear();
    m_folders.clear();
    m_patterns.clear();
    for (auto &entries : m_entries)
        entries.clear();
    m_configs.clear();
    m_patternLookup.clear();
    m_stats = {};
}

bool GraphicsIndex::isEmpty() const
{
    return m_entries[0].empty() && m_entries[1].empty() && m_entries[2].empty();
}

quint32 GraphicsIndex::patternIndex(const QString &prefix, const QString &suffix, bool withId)
{
    const QString key = prefix + QChar(0x1f) + suffix + (withId ? QChar('1') : QChar('0'));
    const auto it = m_patternLookup.constFind(key);
    if (it != m_patternLookup.constEnd())
        return it.value();
    const quint32 index = quint32(m_patterns.size());
    m_patterns.push_back({prefix, suffix, withId});
    m_patternLookup.insert(key, index);
    return index;
}

void GraphicsIndex::addRecord(quint32 folder, const Record &record)
{
    const QString idText = QString::number(record.id);
    const int at = record.from.indexOf(idText);
    quint32 pattern;
    if (at >= 0)
        pattern = patternIndex(record.from.left(at), record.from.mid(at + idText.size()), true);
    else
        pattern = patternIndex(record.from, QString(), false);
    m_entries[int(record.kind)].push_back({record.id, folder, pattern});
}

void GraphicsIndex::finish()
{
    for (auto &entries : m_entries) {
        // Stable: on duplicates the first config.xml (sorted path) wins.
        std::stable_sort(entries.begin(), entries.end(),
                         [](const Entry &a, const Entry &b) { return a.id < b.id; });
        entries.erase(std::unique(entries.begin(), entries.end(),
                                  [](const Entry &a, const Entry &b) { return a.id == b.id; }),
                      entries.end());
        entries.shrink_to_fit();
    }
    m_patternLookup.clear();
    m_stats.portraits = int(m_entries[0].size());
    m_stats.clubLogos = int(m_entries[1].size());
    m_stats.nationLogos = int(m_entries[2].size());
    m_stats.configFiles = int(m_configs.size());
}

bool GraphicsIndex::scan(const QString &root, const bool *cancel)
{
    clear();
    QElapsedTimer timer;
    timer.start();
    const QDir rootDir(root);
    if (root.isEmpty() || !rootDir.exists())
        return false;
    m_root = rootDir.absolutePath();

    QStringList configs;
    QDirIterator it(m_root, {kConfigName}, QDir::Files | QDir::NoDotAndDotDot,
                    QDirIterator::Subdirectories);
    while (it.hasNext()) {
        configs.append(it.next());
        if (cancel && *cancel)
            return false;
    }
    configs.sort();

    for (const QString &configPath : std::as_const(configs)) {
        if (cancel && *cancel) {
            clear();
            return false;
        }
        QFile file(configPath);
        if (!file.open(QIODevice::ReadOnly))
            continue;
        const QByteArray xml = file.readAll();
        const QFileInfo info(configPath);
        const QString relFolder = rootDir.relativeFilePath(info.absolutePath());
        m_configs.push_back({rootDir.relativeFilePath(configPath), info.size(), mtimeOf(info)});

        const std::vector<Record> records = parseConfig(xml);
        if (records.empty())
            continue;
        const quint32 folder = quint32(m_folders.size());
        m_folders.append(relFolder == QLatin1String(".") ? QString() : relFolder);
        for (const Record &record : records)
            addRecord(folder, record);
    }
    finish();
    m_stats.milliseconds = timer.elapsed();
    return true;
}

const GraphicsIndex::Entry *GraphicsIndex::find(Kind kind, quint32 id) const
{
    if (id == 0)
        return nullptr;
    const auto &entries = m_entries[int(kind)];
    const auto it = std::lower_bound(entries.begin(), entries.end(), id,
                                     [](const Entry &e, quint32 value) { return e.id < value; });
    if (it == entries.end() || it->id != id)
        return nullptr;
    return &*it;
}

bool GraphicsIndex::contains(Kind kind, quint32 id) const
{
    return find(kind, id) != nullptr;
}

QString GraphicsIndex::fileFor(const Entry &entry) const
{
    const Pattern &pattern = m_patterns[entry.pattern];
    const QString name = pattern.withId
                             ? pattern.prefix + QString::number(entry.id) + pattern.suffix
                             : pattern.prefix;
    const QString &folder = m_folders[int(entry.folder)];
    const QString base = folder.isEmpty() ? m_root + QLatin1Char('/') + name
                                          : m_root + QLatin1Char('/') + folder
                                                + QLatin1Char('/') + name;
    // FM looks the file up without extension; packs ship PNG (rarely JPG).
    const QString png = base + QStringLiteral(".png");
    if (QFileInfo::exists(png))
        return png;
    const QString jpg = base + QStringLiteral(".jpg");
    if (QFileInfo::exists(jpg))
        return jpg;
    return QFileInfo::exists(base) ? base : png;
}

QString GraphicsIndex::path(Kind kind, quint32 id) const
{
    const Entry *entry = find(kind, id);
    return entry ? fileFor(*entry) : QString();
}

bool GraphicsIndex::saveCache(const QString &cacheFile) const
{
    if (m_root.isEmpty())
        return false;
    QDir().mkpath(QFileInfo(cacheFile).absolutePath());
    QSaveFile file(cacheFile);
    if (!file.open(QIODevice::WriteOnly))
        return false;
    QDataStream out(&file);
    out.setVersion(QDataStream::Qt_6_0);
    out << kCacheMagic << kCacheVersion << m_root;
    out << quint32(m_configs.size());
    for (const ConfigStamp &stamp : m_configs)
        out << stamp.relPath << stamp.size << stamp.mtime;
    out << m_folders;
    out << quint32(m_patterns.size());
    for (const Pattern &pattern : m_patterns)
        out << pattern.prefix << pattern.suffix << pattern.withId;
    for (const auto &entries : m_entries) {
        out << quint32(entries.size());
        out.writeRawData(reinterpret_cast<const char *>(entries.data()),
                         int(entries.size() * sizeof(Entry)));
    }
    if (out.status() != QDataStream::Ok)
        return false;
    return file.commit();
}

bool GraphicsIndex::loadCache(const QString &cacheFile, const QString &root)
{
    clear();
    QElapsedTimer timer;
    timer.start();
    QFile file(cacheFile);
    if (!file.open(QIODevice::ReadOnly))
        return false;
    QDataStream in(&file);
    in.setVersion(QDataStream::Qt_6_0);
    quint32 magic = 0, version = 0;
    QString cachedRoot;
    in >> magic >> version >> cachedRoot;
    if (magic != kCacheMagic || version != kCacheVersion
        || cachedRoot != QDir(root).absolutePath())
        return false;

    const auto fail = [this] {
        clear();
        return false;
    };
    quint32 configCount = 0;
    in >> configCount;
    if (in.status() != QDataStream::Ok || configCount > 100000)
        return fail();
    const QDir rootDir(cachedRoot);
    for (quint32 i = 0; i < configCount; ++i) {
        ConfigStamp stamp;
        in >> stamp.relPath >> stamp.size >> stamp.mtime;
        const QFileInfo info(rootDir.filePath(stamp.relPath));
        if (!info.exists() || info.size() != stamp.size || mtimeOf(info) != stamp.mtime)
            return fail(); // a pack changed: rescan
        m_configs.push_back(stamp);
    }
    in >> m_folders;
    quint32 patternCount = 0;
    in >> patternCount;
    if (in.status() != QDataStream::Ok || patternCount > 10000000)
        return fail();
    m_patterns.resize(patternCount);
    for (Pattern &pattern : m_patterns)
        in >> pattern.prefix >> pattern.suffix >> pattern.withId;
    for (auto &entries : m_entries) {
        quint32 count = 0;
        in >> count;
        if (in.status() != QDataStream::Ok || count > 50000000)
            return fail();
        entries.resize(count);
        const int bytes = int(count * sizeof(Entry));
        if (in.readRawData(reinterpret_cast<char *>(entries.data()), bytes) != bytes)
            return fail();
        for (const Entry &entry : entries) {
            if (entry.folder >= quint32(m_folders.size()) || entry.pattern >= patternCount)
                return fail();
        }
    }
    if (in.status() != QDataStream::Ok)
        return fail();
    m_root = cachedRoot;
    finish();
    m_stats.fromCache = true;
    m_stats.milliseconds = timer.elapsed();
    return true;
}

} // namespace fm
