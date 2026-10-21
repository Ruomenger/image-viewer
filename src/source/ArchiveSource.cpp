#include "source/ArchiveSource.h"

#include "source/ZipArchiveSource.h"

#include <QMutexLocker>

#include <utility>

std::unique_ptr<ImageSource> openArchive(const QString& path) {
    if (probeArchiveAccess(path) == ArchiveAccess::Random)
        return std::make_unique<ZipArchiveSource>(path);
    return std::make_unique<ArchiveSource>(path);  // 打不开的文件也走这里,由它报 openError
}

ArchiveSource::ArchiveSource(QString archivePath) : m_path(std::move(archivePath)) {
    ArchiveReader reader(m_path);
    if (!reader.isOpen()) {
        m_openError = reader.error();
        return;
    }
    m_entries = listArchiveImages(reader, &m_openError);
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
        entry = m_reader->nextEntry();
        if (entry == nullptr) {
            closeReaderLocked();
            return {};
        }
        ++m_nextOrdinal;
    }

    QByteArray data = m_reader->readData(entry);
    if (!m_reader->isOpen())
        closeReaderLocked();  // 读数据出错,句柄已作废:下次读取时重开
    return data;
}

bool ArchiveSource::reopenReaderLocked() const {
    closeReaderLocked();
    auto reader = std::make_unique<ArchiveReader>(m_path);
    if (!reader->isOpen())
        return false;
    m_reader = std::move(reader);
    return true;
}

void ArchiveSource::closeReaderLocked() const {
    m_reader.reset();
    m_nextOrdinal = 0;
}
