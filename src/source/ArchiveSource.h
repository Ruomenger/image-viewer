#pragma once

#include "source/ImageSource.h"

#include <QStringList>

// Browses image entries inside an archive (zip / cbz / 7z / rar / tar ...)
// via libarchive. Entry order is natural-sorted by path.
//
// Note: libarchive is a streaming reader, so reading entry N currently
// re-opens the archive and scans to it (O(n) per read). That's fine for a
// first version; a later pass can add a sequential fast-path + LRU cache.
class ArchiveSource : public ImageSource {
public:
    explicit ArchiveSource(const QString& archivePath);

    int count() const override { return static_cast<int>(m_names.size()); }
    QString entryName(int index) const override;
    QByteArray readEntry(int index) const override;

private:
    QString m_path;
    QStringList m_names;  // image entry paths, natural-sorted
};
