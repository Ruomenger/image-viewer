#include "source/StreamArchiveSource.h"

#include <QMutexLocker>

#include <utility>

namespace {
// waitForListing 即使没有新进度也定期醒来,让调用方有机会放弃等待(如用户已打开别的来源)。
constexpr unsigned long kProgressPollMs = 200;
}  // namespace

StreamArchiveSource::StreamArchiveSource(QString archivePath, qint64 memoryBytes)
    : m_path(std::move(archivePath)), m_store(memoryBytes) {
    m_reader = std::make_unique<ArchiveReader>(m_path);
    if (!m_reader->isOpen()) {
        m_openError = m_reader->error();
        m_listed = true;  // 没有东西可抽:列表(空)即刻完成
        return;
    }
    m_worker.reset(QThread::create([this] { extract(); }));
    m_worker->start();
}

StreamArchiveSource::~StreamArchiveSource() {
    m_cancel = true;
    m_changed.wakeAll();
    if (m_worker)
        m_worker->wait();
}

void StreamArchiveSource::extract() {
    int ordinal = 0;
    while (!m_cancel) {
        struct archive_entry* entry = m_reader->nextEntry();
        if (entry == nullptr)
            break;
        if (isArchiveImageEntry(entry)) {
            const QString name = archiveEntryPath(entry);
            const QByteArray bytes = m_reader->readData(entry);
            if (!m_reader->isOpen())
                break;  // 读数据出错(如截断):流已不可靠,保留已抽出的部分
            if (m_store.put(ordinal, bytes)) {
                const QMutexLocker locker(&m_mutex);
                m_entries.push_back({.name = name, .ordinal = ordinal});
            }
        }
        ++ordinal;
        {
            const QMutexLocker locker(&m_mutex);
            m_scanned = ordinal;
        }
        m_changed.wakeAll();
    }

    const QMutexLocker locker(&m_mutex);
    // 一个条目都没抽到就中断:按打开失败上报(openError 在列表完成后才会被读)。
    if (!m_cancel && !m_reader->atEnd() && m_entries.isEmpty())
        m_openError = m_reader->error();
    sortArchiveEntries(m_entries);
    m_listed = true;
    m_changed.wakeAll();
}

int StreamArchiveSource::count() const {
    const QMutexLocker locker(&m_mutex);
    return static_cast<int>(m_entries.size());
}

QString StreamArchiveSource::entryName(int index) const {
    const QMutexLocker locker(&m_mutex);
    if (index < 0 || index >= m_entries.size())
        return {};
    return m_entries.at(index).name;
}

QByteArray StreamArchiveSource::readEntry(int index) const {
    int ordinal = -1;
    {
        QMutexLocker locker(&m_mutex);
        while (!m_listed && !m_cancel)  // 下标指向排好序的列表,须等列表完成
            m_changed.wait(&m_mutex);
        if (index < 0 || index >= m_entries.size())
            return {};
        ordinal = m_entries.at(index).ordinal;
    }
    return m_store.get(ordinal);
}

bool StreamArchiveSource::waitForListing(const std::function<bool(int)>& progress) {
    QMutexLocker locker(&m_mutex);
    for (;;) {
        const int scanned = m_scanned;
        const bool listed = m_listed;
        locker.unlock();
        if (!progress(scanned))  // 至少调用一次;完成后再报一次最终计数
            return false;
        if (listed)
            return true;
        locker.relock();
        if (!m_listed && m_scanned == scanned)
            m_changed.wait(&m_mutex, kProgressPollMs);
    }
}

bool StreamArchiveSource::isListed() const {
    const QMutexLocker locker(&m_mutex);
    return m_listed;
}
