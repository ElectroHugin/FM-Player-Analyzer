#pragma once

#include <QHash>
#include <QString>
#include <QStringList>

#include <cstdint>
#include <vector>

namespace fm {

// Index of a Football Manager "graphics" folder (face packs, logo packs).
//
// FM packs map their files with a config.xml per folder:
//   <record from="face_123" to="graphics/pictures/person/123/portrait"/>
//   <record from="123_club" to="graphics/pictures/club/123/logo"/>
// The index reads every config.xml below the root and keeps, per kind, the
// sorted ids with a compact reference to the file (folder + name pattern), so
// 560,000 portraits cost about 4.5 MB instead of one string each.
//
// The index never writes into the graphics folder. A cache file (anywhere
// else) skips the scan on the next start as long as every config.xml is
// unchanged (size + modification time).
class GraphicsIndex
{
public:
    enum class Kind : uint8_t { Portrait = 0, ClubLogo = 1, NationLogo = 2 };

    struct Stats {
        int configFiles = 0;
        int portraits = 0;
        int clubLogos = 0;
        int nationLogos = 0;
        qint64 milliseconds = 0;
        bool fromCache = false;
    };

    GraphicsIndex() = default;

    // Full scan of root (recursive). Clears the index first. Returns false if
    // root does not exist. Cancels early (returning false) when *cancel turns true.
    bool scan(const QString &root, const bool *cancel = nullptr);

    // Cache: the file holds the root, every config.xml with size and mtime
    // and the compact tables. load() fails (index stays empty) when the file
    // is missing, from another root or any config.xml changed.
    bool saveCache(const QString &cacheFile) const;
    bool loadCache(const QString &cacheFile, const QString &root);

    bool isEmpty() const;
    QString root() const { return m_root; }
    Stats stats() const { return m_stats; }

    // Absolute path of the image for that FM id ("" = none). The file is not
    // checked for existence here (the caller loads it and falls back).
    QString path(Kind kind, quint32 id) const;
    bool contains(Kind kind, quint32 id) const;

    // FM ids in exports: "12345" or, for newgens in FM24 HTML, "r-12345".
    // Returns 0 if no id can be read.
    static quint32 parseId(const QString &uid);

    // Parses the records of one config.xml (exposed for tests): person
    // portraits, club and nation logos. Icons, kits, "none" defaults etc. are
    // skipped.
    struct Record {
        Kind kind;
        quint32 id;
        QString from;
    };
    static std::vector<Record> parseConfig(const QByteArray &xml);

private:
    struct Entry {
        quint32 id;
        quint32 folder;  // index into m_folders
        quint32 pattern; // index into m_patterns
    };
    struct Pattern {
        QString prefix; // file name = prefix + id + suffix (+ extension)
        QString suffix;
        bool withId = true; // false: prefix is the whole file name
    };
    struct ConfigStamp {
        QString relPath;
        qint64 size = 0;
        qint64 mtime = 0;
    };

    void clear();
    void addRecord(quint32 folder, const Record &record);
    void finish();
    quint32 patternIndex(const QString &prefix, const QString &suffix, bool withId);
    QString fileFor(const Entry &entry) const;
    const Entry *find(Kind kind, quint32 id) const;

    QString m_root;
    QStringList m_folders;            // relative to root, "" = root itself
    std::vector<Pattern> m_patterns;
    std::vector<Entry> m_entries[3];
    std::vector<ConfigStamp> m_configs;
    QHash<QString, quint32> m_patternLookup; // only while scanning
    Stats m_stats;
};

} // namespace fm
