#include "source/ArchiveSource.h"

#include <QCollator>

#include <algorithm>

#include <archive.h>
#include <archive_entry.h>

namespace {

// libarchive returns UTF-8 entry names when it can; fall back to local 8-bit.
QString entryPath(struct archive_entry* entry)
{
    if (const char* utf8 = archive_entry_pathname_utf8(entry))
        return QString::fromUtf8(utf8);
    if (const char* raw = archive_entry_pathname(entry))
        return QString::fromLocal8Bit(raw);
    return {};
}

struct archive* openForRead(const QString& path)
{
    struct archive* a = archive_read_new();
    archive_read_support_filter_all(a);
    archive_read_support_format_all(a);
    if (archive_read_open_filename(a, path.toUtf8().constData(), 64 * 1024) != ARCHIVE_OK) {
        archive_read_free(a);
        return nullptr;
    }
    return a;
}

} // namespace

ArchiveSource::ArchiveSource(const QString& archivePath)
    : m_path(archivePath)
{
    struct archive* a = openForRead(m_path);
    if (!a)
        return;

    struct archive_entry* entry = nullptr;
    while (archive_read_next_header(a, &entry) == ARCHIVE_OK) {
        const QString name = entryPath(entry);
        if (archive_entry_filetype(entry) == AE_IFREG && ImageSource::isSupportedImage(name))
            m_names.push_back(name);
        archive_read_data_skip(a);
    }
    archive_read_free(a);

    QCollator collator;
    collator.setNumericMode(true);
    collator.setCaseSensitivity(Qt::CaseInsensitive);
    std::sort(m_names.begin(), m_names.end(),
              [&collator](const QString& x, const QString& y) {
                  return collator.compare(x, y) < 0;
              });
}

QString ArchiveSource::entryName(int index) const
{
    if (index < 0 || index >= m_names.size())
        return {};
    return m_names.at(index);
}

QByteArray ArchiveSource::readEntry(int index) const
{
    if (index < 0 || index >= m_names.size())
        return {};
    const QString target = m_names.at(index);

    struct archive* a = openForRead(m_path);
    if (!a)
        return {};

    QByteArray data;
    struct archive_entry* entry = nullptr;
    while (archive_read_next_header(a, &entry) == ARCHIVE_OK) {
        if (entryPath(entry) != target) {
            archive_read_data_skip(a);
            continue;
        }

        const la_int64_t declaredSize = archive_entry_size(entry);
        if (declaredSize > 0) {
            data.resize(static_cast<qsizetype>(declaredSize));
            const la_ssize_t read = archive_read_data(a, data.data(),
                                                      static_cast<size_t>(declaredSize));
            if (read < 0)
                data.clear();
            else if (read < declaredSize)
                data.resize(static_cast<qsizetype>(read));
        } else {
            // Size unknown ahead of time: pull block by block.
            const void* buffer = nullptr;
            size_t length = 0;
            la_int64_t offset = 0;
            while (archive_read_data_block(a, &buffer, &length, &offset) == ARCHIVE_OK)
                data.append(static_cast<const char*>(buffer), static_cast<qsizetype>(length));
        }
        break;
    }

    archive_read_free(a);
    return data;
}
