#include "source/ArchiveReader.h"

#include "source/ImageSource.h"

#include <QCollator>

#include <algorithm>

#include <archive.h>
#include <archive_entry.h>

namespace {

// libarchive 读取缓冲块大小(用 size_t 字面量避免 int 乘法溢出)。
constexpr size_t kReadBlockSize = size_t{64} * 1024;

QString errorString(struct archive* a, const QString& fallback) {
    const char* msg = archive_error_string(a);
    return msg ? QString::fromUtf8(msg) : fallback;
}

}  // namespace

ArchiveReader::ArchiveReader(const QString& path) {
    m_archive = archive_read_new();
    archive_read_support_filter_all(m_archive);
    archive_read_support_format_all(m_archive);
    if (archive_read_open_filename(m_archive, path.toUtf8().constData(), kReadBlockSize) !=
        ARCHIVE_OK)
        fail(QStringLiteral("无法打开压缩包"));
}

ArchiveReader::~ArchiveReader() {
    if (m_archive != nullptr)
        archive_read_free(m_archive);
}

struct archive_entry* ArchiveReader::nextEntry() {
    if (m_archive == nullptr)
        return nullptr;
    struct archive_entry* entry = nullptr;
    const int result = archive_read_next_header(m_archive, &entry);
    if (result == ARCHIVE_OK || result == ARCHIVE_WARN)
        return entry;
    if (result == ARCHIVE_EOF)
        m_atEnd = true;
    else
        fail(QStringLiteral("读取压缩包失败"));
    return nullptr;
}

QByteArray ArchiveReader::readData(struct archive_entry* entry) {
    if (m_archive == nullptr || entry == nullptr)
        return {};
    QByteArray data;

    const la_int64_t declaredSize = archive_entry_size(entry);
    if (declaredSize > 0) {
        data.resize(static_cast<qsizetype>(declaredSize));
        qsizetype total = 0;
        while (total < data.size()) {  // archive_read_data 可能一次只返回部分数据
            const la_ssize_t read = archive_read_data(m_archive, data.data() + total,
                                                      static_cast<size_t>(data.size() - total));
            if (read < 0) {
                fail(QStringLiteral("读取条目数据失败"));
                return {};
            }
            if (read == 0)
                break;  // 实际数据比声明尺寸短:截断为已读部分
            total += static_cast<qsizetype>(read);
        }
        data.resize(total);
    } else {
        // 尺寸未知:逐块拉取。
        const void* buffer = nullptr;
        size_t length = 0;
        la_int64_t offset = 0;
        while (archive_read_data_block(m_archive, &buffer, &length, &offset) == ARCHIVE_OK)
            data.append(static_cast<const char*>(buffer), static_cast<qsizetype>(length));
    }
    return data;
}

bool ArchiveReader::isRandomAccess() const {
    if (m_archive == nullptr || archive_filter_code(m_archive, 0) != ARCHIVE_FILTER_NONE)
        return false;  // 外层有压缩过滤器(gzip / xz …):跳过条目也要解压
    // 位运算用无符号:libarchive 的格式常量与返回值都是 int。
    const auto base = static_cast<unsigned>(archive_format(m_archive)) &
                      static_cast<unsigned>(ARCHIVE_FORMAT_BASE_MASK);
    return base == static_cast<unsigned>(ARCHIVE_FORMAT_ZIP) ||
           base == static_cast<unsigned>(ARCHIVE_FORMAT_TAR);
}

void ArchiveReader::fail(const QString& fallback) {
    m_error = errorString(m_archive, fallback);
    archive_read_free(m_archive);
    m_archive = nullptr;
}

QString archiveEntryPath(struct archive_entry* entry) {
    if (const char* utf8 = archive_entry_pathname_utf8(entry))
        return QString::fromUtf8(utf8);
    if (const char* raw = archive_entry_pathname(entry))
        return QString::fromLocal8Bit(raw);
    return {};
}

QList<ArchiveEntryRef> listArchiveImages(ArchiveReader& reader, QString* error) {
    QList<ArchiveEntryRef> entries;
    int ordinal = 0;
    while (struct archive_entry* entry = reader.nextEntry()) {
        const QString name = archiveEntryPath(entry);
        if (archive_entry_filetype(entry) == AE_IFREG && ImageSource::isSupportedImage(name))
            entries.push_back({.name = name, .ordinal = ordinal});
        ++ordinal;
    }
    if (!reader.atEnd() && entries.isEmpty() && error != nullptr)
        *error = reader.error();

    QCollator collator;
    collator.setNumericMode(true);
    collator.setCaseSensitivity(Qt::CaseInsensitive);
    std::ranges::sort(entries, [&collator](const ArchiveEntryRef& x, const ArchiveEntryRef& y) {
        return collator.compare(x.name, y.name) < 0;
    });
    return entries;
}

ArchiveAccess probeArchiveAccess(const QString& path) {
    ArchiveReader reader(path);
    if (!reader.isOpen())
        return ArchiveAccess::Unreadable;
    // 格式要读过第一个 header 才确定;空包(直接到末尾)也能判定。
    if (reader.nextEntry() == nullptr && !reader.atEnd())
        return ArchiveAccess::Unreadable;
    return reader.isRandomAccess() ? ArchiveAccess::Random : ArchiveAccess::Streaming;
}
