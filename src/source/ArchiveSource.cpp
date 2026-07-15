#include "source/ArchiveSource.h"

#include <QCollator>
#include <QMutexLocker>

#include <algorithm>
#include <utility>

#include <archive.h>
#include <archive_entry.h>

namespace {

// libarchive 读取缓冲块大小(用 size_t 字面量避免 int 乘法溢出)。
constexpr size_t kReadBlockSize = size_t{64} * 1024;

// libarchive returns UTF-8 entry names when it can; fall back to local 8-bit.
QString entryPath(struct archive_entry* entry) {
    if (const char* utf8 = archive_entry_pathname_utf8(entry))
        return QString::fromUtf8(utf8);
    if (const char* raw = archive_entry_pathname(entry))
        return QString::fromLocal8Bit(raw);
    return {};
}

// 失败时返回 nullptr,并把 libarchive 的报错写入 *error(可传 nullptr)。
struct archive* openForRead(const QString& path, QString* error = nullptr) {
    struct archive* a = archive_read_new();
    archive_read_support_filter_all(a);
    archive_read_support_format_all(a);
    if (archive_read_open_filename(a, path.toUtf8().constData(), kReadBlockSize) != ARCHIVE_OK) {
        if (error) {
            const char* msg = archive_error_string(a);
            *error = msg ? QString::fromUtf8(msg) : QStringLiteral("无法打开压缩包");
        }
        archive_read_free(a);
        return nullptr;
    }
    return a;
}

// 读取 reader 当前 entry 的数据(header 刚被 next_header 消费)。
// 读数据出错时 *fatal 置 true:流状态已不可靠,调用方须重开 reader。
QByteArray readCurrentEntry(struct archive* a, struct archive_entry* entry, bool* fatal) {
    *fatal = false;
    QByteArray data;

    const la_int64_t declaredSize = archive_entry_size(entry);
    if (declaredSize > 0) {
        data.resize(static_cast<qsizetype>(declaredSize));
        qsizetype total = 0;
        while (total < data.size()) {  // archive_read_data 可能一次只返回部分数据
            const la_ssize_t read =
                archive_read_data(a, data.data() + total, static_cast<size_t>(data.size() - total));
            if (read < 0) {
                *fatal = true;
                return {};
            }
            if (read == 0)
                break;  // 实际数据比声明尺寸短:截断为已读部分
            total += static_cast<qsizetype>(read);
        }
        data.resize(total);
    } else {
        // Size unknown ahead of time: pull block by block.
        const void* buffer = nullptr;
        size_t length = 0;
        la_int64_t offset = 0;
        while (archive_read_data_block(a, &buffer, &length, &offset) == ARCHIVE_OK)
            data.append(static_cast<const char*>(buffer), static_cast<qsizetype>(length));
    }
    return data;
}

}  // namespace

ArchiveSource::ArchiveSource(QString archivePath) : m_path(std::move(archivePath)) {
    struct archive* a = openForRead(m_path, &m_openError);
    if (!a)
        return;

    struct archive_entry* entry = nullptr;
    int ordinal = 0;
    int result = ARCHIVE_OK;
    while ((result = archive_read_next_header(a, &entry)) == ARCHIVE_OK) {
        const QString name = entryPath(entry);
        if (archive_entry_filetype(entry) == AE_IFREG && ImageSource::isSupportedImage(name))
            m_entries.push_back({.name = name, .ordinal = ordinal});
        archive_read_data_skip(a);
        ++ordinal;
    }
    // 一个 entry 都没读到就中断(如非压缩包文件):按打开失败上报;
    // 已读到部分 entry 的截断包保持可浏览。
    if (result != ARCHIVE_EOF && m_entries.isEmpty()) {
        const char* msg = archive_error_string(a);
        m_openError = msg ? QString::fromUtf8(msg) : QStringLiteral("读取压缩包失败");
    }
    archive_read_free(a);

    QCollator collator;
    collator.setNumericMode(true);
    collator.setCaseSensitivity(Qt::CaseInsensitive);
    std::ranges::sort(m_entries, [&collator](const Entry& x, const Entry& y) {
        return collator.compare(x.name, y.name) < 0;
    });
}

ArchiveSource::~ArchiveSource() {
    const QMutexLocker locker(&m_readMutex);
    closeReaderLocked();
}

QString ArchiveSource::entryName(int index) const {
    if (index < 0 || index >= m_entries.size())
        return {};
    return m_entries.at(index).name;
}

QByteArray ArchiveSource::readEntry(int index) const {
    if (index < 0 || index >= m_entries.size())
        return {};
    const int target = m_entries.at(index).ordinal;

    const QMutexLocker locker(&m_readMutex);
    // 目标在当前扫描位置之前(或 reader 未开)才需要重开;否则接着扫。
    if ((m_reader == nullptr || target < m_nextOrdinal) && !reopenReaderLocked())
        return {};

    struct archive_entry* entry = nullptr;
    while (m_nextOrdinal <= target) {
        if (archive_read_next_header(m_reader, &entry) != ARCHIVE_OK) {
            closeReaderLocked();
            return {};
        }
        ++m_nextOrdinal;
        if (m_nextOrdinal <= target)
            archive_read_data_skip(m_reader);
    }
    if (entry == nullptr)
        return {};

    bool fatal = false;
    QByteArray data = readCurrentEntry(m_reader, entry, &fatal);
    if (fatal)
        closeReaderLocked();  // 流状态不可靠,下次读取时重开
    return data;
}

bool ArchiveSource::reopenReaderLocked() const {
    closeReaderLocked();
    m_reader = openForRead(m_path);
    return m_reader != nullptr;
}

void ArchiveSource::closeReaderLocked() const {
    if (m_reader != nullptr) {
        archive_read_free(m_reader);
        m_reader = nullptr;
    }
    m_nextOrdinal = 0;
}
